/* BOT (Bulk-Only Transport) test — end-to-end USB Mass Storage.
 *
 * Workflow:
 *   1. Open xHCI controller (VFIO)
 *   2. Full controller init (reset, DCBAA, rings, RUN)
 *   3. Scan ports for connected device
 *   4. Warm reset, enable slot, address device
 *   5. Read config descriptor, find bulk endpoints
 *   6. Configure bulk transfer rings (Configure Endpoint command)
 *   7. BOT: INQUIRY → TEST UNIT READY → READ CAPACITY → READ(10)
 *
 * Usage:
 *   make build/test_bot
 *   LD_LIBRARY_PATH=build ./build/test_bot
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "xhci.h"
#include "xhci_internal.h"
#include "xhci_bot.h"

static void
msleep(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/****************************************************************
 * Config descriptor parsing
 ****************************************************************/

/*
 * xHCI endpoint context index mapping:
 *   ctx[0]  = Slot Context
 *   ctx[1]  = EP0
 *   ctx[2]  = EP1 OUT, ctx[3] = EP1 IN
 *   ctx[4]  = EP2 OUT, ctx[5] = EP2 IN
 *   ...
 *   ctx[n]  = EPk OUT/IN where n = ep_num * 2 + dir
 */
#define EP_CTX_INDEX(ep_num, dir) ((ep_num) * 2 + ((dir) ? 1 : 0))

static int
read_device_descriptor(void *handle, int slot_id, void *ep0_ring,
                       struct usb_device_descriptor *desc)
{
    u8 setup[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x18, 0x00 };
    memset(desc, 0, sizeof(*desc));
    return xhci_control_transfer(handle, slot_id, ep0_ring, setup,
                                 desc, 18, 1);
}

static u8 *
read_config_descriptor(void *handle, int slot_id, void *ep0_ring,
                       u16 *total_len_out)
{
    u8 hdr_buf[9];
    u8 setup[8] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x09, 0x00 };

    int ret = xhci_control_transfer(handle, slot_id, ep0_ring, setup,
                                    hdr_buf, 9, 1);
    if (ret < 0) return NULL;

    u16 total_len = hdr_buf[2] | (hdr_buf[3] << 8);
    if (total_len < 9 || total_len > 4096) return NULL;

    u8 *full_buf = malloc(total_len);
    if (!full_buf) return NULL;
    memset(full_buf, 0, total_len);

    setup[6] = total_len & 0xff;
    setup[7] = (total_len >> 8) & 0xff;

    ret = xhci_control_transfer(handle, slot_id, ep0_ring, setup,
                                full_buf, total_len, 1);
    if (ret < 0) { free(full_buf); return NULL; }

    *total_len_out = total_len;
    return full_buf;
}

static int
parse_endpoints(const u8 *cfg, u16 total_len,
                int *bulk_in_ep, int *bulk_out_ep, u32 *max_packet)
{
    u16 pos = 9;  /* skip config descriptor header */
    *bulk_in_ep = 0;
    *bulk_out_ep = 0;
    *max_packet = 0;

    while (pos < total_len) {
        if (pos + 2 > total_len) break;
        u8 bLength = cfg[pos];
        u8 bType = cfg[pos + 1];
        if (bLength == 0 || bLength > total_len - pos) break;

        if (bType == 0x05 && bLength >= 7) {
            /* Endpoint descriptor */
            u8 ep_addr = cfg[pos + 2];
            u8 bmAttr = cfg[pos + 3];
            u16 wMaxPkt = cfg[pos + 4] | (cfg[pos + 5] << 8);
            int ep_num = ep_addr & 0x0f;
            int dir = (ep_addr >> 7) & 1;  /* 1=IN, 0=OUT */

            if ((bmAttr & 0x03) == 0x02) {
                /* Bulk endpoint */
                u32 mps = wMaxPkt & 0x07ff;
                if (dir && *bulk_in_ep == 0) {
                    *bulk_in_ep = ep_num;
                    if (mps > *max_packet) *max_packet = mps;
                } else if (!dir && *bulk_out_ep == 0) {
                    *bulk_out_ep = ep_num;
                    if (mps > *max_packet) *max_packet = mps;
                }
            }
        }
        pos += bLength;
    }

    return (*bulk_in_ep && *bulk_out_ep) ? 0 : -1;
}

static int
configure_bulk_endpoints(void *handle, int slot_id,
                         int bulk_in_ep, int bulk_out_ep, u32 max_packet,
                         void *bulk_in_ring, void *bulk_out_ring)
{
    u64 input_ctx_phys;
    void *ctx_virt = xhci_dma_alloc(handle, 4096, &input_ctx_phys);
    if (!ctx_virt) {
        fprintf(stderr, "  input context alloc failed\n");
        return -1;
    }
    memset(ctx_virt, 0, 4096);

    u32 *ctx = ctx_virt;
    int in_ctx  = EP_CTX_INDEX(bulk_in_ep, 1);
    int out_ctx = EP_CTX_INDEX(bulk_out_ep, 0);

    /* A0C: add bulk endpoints only (EP0 already configured by Address Device) */
    ctx[0] = (1 << in_ctx) | (1 << out_ctx);

    /* Follow SeaBIOS xhci_alloc_inctx() endpoint context layout.
     * Each context entry = 8 dwords (32 bytes).
     *   dword[0]: EP State + CErr
     *   dword[1]: Max Packet Size << 16 | EP Type << 3
     *   dword[2]: Dequeue Pointer Low | DCS
     *   dword[3]: Dequeue Pointer High
     * EP Type: 3 = Bulk OUT, 4 = Bulk IN */

    /* Bulk IN endpoint context */
    u32 *ep_in_ctx = ctx + in_ctx * 8;
    memset(ep_in_ctx, 0, 8 * sizeof(u32));
    u64 ep_in_phys = xhci_transfer_ring_phys(handle, bulk_in_ring);
    int dcs = xhci_transfer_ring_cycle_state(bulk_in_ring);
    ep_in_ctx[0] = 1 | (3 << 5);  /* Running, CErr=3 */
    ep_in_ctx[1] = (max_packet << 16) | (4 << 3);  /* MaxPkt, EP Type=Bulk IN */
    ep_in_ctx[2] = (u32)ep_in_phys | (dcs & 1);    /* Dequeue Low | DCS */
    ep_in_ctx[3] = (u32)(ep_in_phys >> 32);         /* Dequeue High */

    /* Bulk OUT endpoint context */
    u32 *ep_out_ctx = ctx + out_ctx * 8;
    memset(ep_out_ctx, 0, 8 * sizeof(u32));
    u64 ep_out_phys = xhci_transfer_ring_phys(handle, bulk_out_ring);
    dcs = xhci_transfer_ring_cycle_state(bulk_out_ring);
    ep_out_ctx[0] = 1 | (3 << 5);  /* Running, CErr=3 */
    ep_out_ctx[1] = (max_packet << 16) | (3 << 3);  /* MaxPkt, EP Type=Bulk OUT */
    ep_out_ctx[2] = (u32)ep_out_phys | (dcs & 1);
    ep_out_ctx[3] = (u32)(ep_out_phys >> 32);

    fprintf(stderr, "  configure: A0C=0x%x IN_ctx=%d OUT_ctx=%d max_pkt=%u\n",
            ctx[0], in_ctx, out_ctx, max_packet);

    int ret = xhci_configure_endpoint(handle, slot_id, input_ctx_phys);
    xhci_dma_free(handle, ctx_virt, 4096);
    return ret;
}

/****************************************************************
 * Main
 ****************************************************************/

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *pci_bdf = getenv("XHCI_PCI");
    if (!pci_bdf)
        pci_bdf = "0000:05:00.1";

    printf("========================================\n");
    printf("BOT Test — USB Mass Storage on %s\n", pci_bdf);
    printf("========================================\n\n");

    /* -------------------------------------------------------
     * Step 1: Open controller
     * ------------------------------------------------------- */
    void *handle = xhci_open(pci_bdf);
    if (!handle) {
        fprintf(stderr, "Failed to open %s\n", pci_bdf);
        return 1;
    }

    /* -------------------------------------------------------
     * Step 2: Full controller init
     * ------------------------------------------------------- */
    if (xhci_full_init(handle) < 0) {
        fprintf(stderr, "Full init failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("Controller running\n\n");

    /* -------------------------------------------------------
     * Step 3: Wait for ports to settle, then scan for device
     * ------------------------------------------------------- */

    /* After HCRST, ports need time to re-detect. USB3 takes longer (up to 5s).
     * Wait and dump all port states. */
    int n_ports = xhci_port_count(handle);
    printf("Waiting for ports to settle (%d ports)...\n", n_ports);

    /* Wait for USB3 link training */
    for (int retry = 0; retry < 50; retry++) {
        msleep(100);
        int usb3_count = 0;
        for (int port = 0; port < n_ports; port++) {
            u32 p = xhci_port_read(handle, port);
            if (p & XHCI_PORTSC_CCS) {
                u32 speed = (p >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
                u32 ped = (p >> 1) & 1;
                if (speed == 4 && ped) usb3_count++;
            }
        }
        if (usb3_count > 0) {
            printf("  USB3 devices detected after %d ms\n", retry * 100);
            break;
        }
    }

    /* Print ALL port states */
    printf("  All port states:\n");
    for (int port = 0; port < n_ports; port++) {
        u32 p = xhci_port_read(handle, port);
        u32 speed = (p >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
        const char *speed_name[] = { "Full", "Low", "High", "Super", "SS+", " - ", " - ", " - ", " - " };
        printf("    Port %2d: 0x%08x CCS=%d SPD=%d[%s] PED=%d PLS=%d\n",
               port, p, !!(p&XHCI_PORTSC_CCS), speed,
               speed_name[speed < 9 ? speed : 9],
               (p>>1)&1, (p>>10)&0xf);
    }

    /* Find first port with a properly connected device.
     * A port is only truly connected when CCS=1 AND PED=1 (Port Enabled).
     * Prefer ports in U0 (PLS=0) over suspended ports (PLS>0). */
    int found_port = -1;
    u32 portsc = 0;

    /* First pass: USB 2.0 devices in U0 (PLS=0) with CCS=1 and PED=1 */
    for (int port = 0; port < n_ports; port++) {
        portsc = xhci_port_read(handle, port);
        u32 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
        u32 ped = (portsc >> 1) & 1;
        u32 pls = (portsc >> 5) & 0xf;
        if ((portsc & XHCI_PORTSC_CCS) && speed < 4 && ped && pls == 0) {
            found_port = port;
            break;
        }
    }

    /* Second pass: USB 3.0 devices in U0 (PLS=0) with CCS=1 and PED=1 */
    if (found_port < 0) {
        for (int port = 0; port < n_ports; port++) {
            portsc = xhci_port_read(handle, port);
            u32 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
            u32 ped = (portsc >> 1) & 1;
            u32 pls = (portsc >> 5) & 0xf;
            if ((portsc & XHCI_PORTSC_CCS) && speed == 4 && ped && pls == 0) {
                found_port = port;
                break;
            }
        }
    }

    /* Third pass: Any USB device with CCS=1 and PED=1 (fallback) */
    if (found_port < 0) {
        for (int port = 0; port < n_ports; port++) {
            portsc = xhci_port_read(handle, port);
            u32 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
            u32 ped = (portsc >> 1) & 1;
            if ((portsc & XHCI_PORTSC_CCS) && ped) {
                found_port = port;
                break;
            }
        }
    }

    if (found_port < 0) {
        printf("  No devices found\n");
        xhci_close(handle);
        return 1;
    }

    /* -------------------------------------------------------
     * Step 4: Warm reset if needed, enable slot, address device
     * ------------------------------------------------------- */

    /* USB 3.0 ports in U0 with PED=1: skip warm reset (device is ready)
     * Other ports: perform warm reset to ensure clean state */
    u8 pls = (portsc >> XHCI_PORTSC_PLS_SHIFT) & 0xf;
    u8 ped = (portsc >> 1) & 1;
    u8 port_speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;

    if (port_speed >= 4 && pls == 0 && ped) {
        /* USB 3.x in U0 with PED=1: device is ready, skip reset */
        printf("  Port %d: USB3 in U0, skipping warm reset...\n", found_port);
    } else {
        /* Other states: perform warm reset */
        printf("  Port %d: performing warm reset (speed=%d PLS=%d PED=%d)...\n",
               found_port, port_speed, pls, ped);

        /* Set Port Reset bit */
        u32 new_portsc = portsc | XHCI_PORTSC_PR;
        xhci_port_write(handle, found_port, new_portsc);

        /* Wait for reset to complete */
        msleep(100);
        u32 retries = 0;
        while (retries < 10) {
            portsc = xhci_port_read(handle, found_port);
            if (!(portsc & XHCI_PORTSC_PR)) {
                break;
            }
            msleep(20);
            retries++;
        }

        if (portsc & XHCI_PORTSC_PR) {
            fprintf(stderr, "  Port reset timeout\n");
        }

        printf("  After reset: PORTSC=0x%08x\n", portsc);

        /* Check if device is still connected */
        if (!(portsc & XHCI_PORTSC_CCS)) {
            fprintf(stderr, "  Device disconnected after reset\n");
            xhci_close(handle);
            return 1;
        }
    }

    /* Re-read speed after potential reset */
    portsc = xhci_port_read(handle, found_port);
    u8 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
    printf("  Final: port=%d speed=%d PED=%d PLS=%d\n", found_port, speed,
           (portsc>>1)&1, (portsc>>5)&0xf);

    /* Enable slot */
    int slot_id = xhci_enable_slot(handle);
    if (slot_id < 0) {
        fprintf(stderr, "  Enable slot failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("  Slot %d enabled\n", slot_id);

    /* Wait for controller to fully commit the slot enable state */
    msleep(1000);

    /* Create EP0 transfer ring */
    void *ep0_ring = xhci_transfer_ring_create(handle, 32);
    if (!ep0_ring) {
        fprintf(stderr, "  EP0 ring alloc failed\n");
        xhci_close(handle);
        return 1;
    }

    /* For ports already in U0 (device was enumerated by kernel before VFIO
     * takeover), the device already has a USB address. Set up the device
     * context with address=1 and try control transfers directly. */
    struct usb_device_descriptor dev_desc;

    /* Allocate device context and set up slot context */
    u64 dev_ctx_phys;
    void *dev_ctx = xhci_dma_alloc(handle, 1024, &dev_ctx_phys);
    if (!dev_ctx) {
        fprintf(stderr, "  Device context alloc failed\n");
        xhci_close(handle);
        return 1;
    }
    memset(dev_ctx, 0, 1024);

    /* Device context (output context): slot at offset 0x00, EP0 at 0x20 (32B) or 0x40 (64B).
     * Set device address = 0 (Default state). Let ADDRESS_DEVICE assign. */
    u32 *dev_slot_ctx = (u32 *)dev_ctx;
    dev_slot_ctx[0] = 0;  /* device address = 0 */
    dev_slot_ctx[1] = (0 << 8) | (speed & 0xf);  /* route|speed */
    dev_slot_ctx[2] = 2;  /* context entries: slot + EP0 */
    dev_slot_ctx[4] = found_port + 1;  /* root hub port */

    /* Also set up EP0 context in device context so control transfers work */
    u32 *dev_ep0_ctx = (u32 *)((u8 *)dev_ctx + 64);  /* EP0 at offset 64 */
    dev_ep0_ctx[0] = (1 << 29);  /* EP State = Running */
    u64 ep0_ring_phys = xhci_transfer_ring_phys(handle, ep0_ring);
    int ep0_dcs = xhci_transfer_ring_cycle_state(ep0_ring);
    dev_ep0_ctx[2] = (u32)ep0_ring_phys | (ep0_dcs & 1);  /* dequeue low + DCS */
    dev_ep0_ctx[3] = (u32)(ep0_ring_phys >> 32);  /* dequeue high */
    dev_ep0_ctx[1] = (1 << 3) | (1 << 6) | (64 << 16);  /* EP Type=1, CErr=1, MPS=64 */

    /* Set DCBAA[slot_id] to point to device context */
    struct xhci_devlist *dcbaa = xhci_get_dcbaa(handle, NULL);
    if (dcbaa && slot_id < 64) {
        dcbaa[slot_id].ptr_low = (u32)dev_ctx_phys;
        dcbaa[slot_id].ptr_high = (u32)(dev_ctx_phys >> 32);
    }

    fprintf(stderr, "  DCBAA[%d]=0x%llx, trying descriptor read (addr=0)...\n",
            slot_id, (unsigned long long)dev_ctx_phys);
    if (read_device_descriptor(handle, slot_id, ep0_ring, &dev_desc) < 0) {
        fprintf(stderr, "  Descriptor read failed, trying ADDRESS_DEVICE...\n");

        /* Try ADDRESS_DEVICE with minimal input context (slot only, addr=0) */
        u64 input_ctx_phys;
        void *ctx_virt = xhci_dma_alloc(handle, 4096, &input_ctx_phys);
        if (!ctx_virt) {
            fprintf(stderr, "  Input context alloc failed\n");
            xhci_close(handle);
            return 1;
        }
        memset(ctx_virt, 0, 4096);
        u32 *ctx = ctx_virt;

        ctx[0] = 0;        /* Drop flags: none */
        ctx[1] = (1 << 0); /* Add flags: slot context only */

        /* Input Control Context is always 32 bytes (8 DWORDs)
         * Slot Context starts at DWORD 8 (32B offset)
         * EP0 Context starts at DWORD 16 (64B offset) */
        u32 *slot_ctx = ctx + 8;
        /* Slot Context per xHCI spec:
         * DWORD 0: ContextEntries[31:27] | Speed[26:20]
         * DWORD 1: RootHubPortNumber[31:16]
         * DWORD 2: Speed[31:28] for xHCI 1.1+ */
        slot_ctx[0] = (1 << 27) | ((speed & 0xf) << 20);  /* ContextEntries=1, Speed */
        slot_ctx[1] = (found_port + 1) << 16;              /* Root Hub Port Number */

        /* Check if xHCI 1.1+ and set additional speed field */
        u32 hciversion = xhci_read32(handle, 0x02) >> 16;  /* HCIVersion from capbase + 0x02 */
        if (hciversion >= 0x0110) {
            fprintf(stderr, "  xHCI 1.1+ detected (0x%x), setting slot->ctx[2] speed bits\n", hciversion);
            slot_ctx[2] = ((u32)speed << 28);  /* xHCI 1.1+ Speed field */
        }

        /* Debug: print Input Context content */
        fprintf(stderr, "  Input Context dump (minimal):\n");
        for (int i = 0; i < 20; i++) {
            if (i % 4 == 0) fprintf(stderr, "    ");
            fprintf(stderr, "[%2d]=0x%08x ", i, ctx[i]);
            if (i % 4 == 3) fprintf(stderr, "\n");
        }

        fprintf(stderr, "  ADDRESS_DEVICE (minimal, ctx_entries=1, addr=0)...\n");
        if (xhci_address_device(handle, slot_id, input_ctx_phys) == 0) {
            printf("  Device addressed (minimal)\n");
        } else {
            /* Try full input context with EP0 */
            fprintf(stderr, "  ADDRESS_DEVICE (full, ctx_entries=2)...\n");
            memset(ctx_virt, 0, 4096);
            ctx[0] = 0;                    /* Drop flags: none */
            ctx[1] = (1 << 0) | (1 << 1); /* Add flags: slot(bit0) + EP0(bit1) */

            /* Check context size (32B or 64B) from HCCPARAMS1 */
            u32 hcc = xhci_read32(handle, 0x10);  /* HCCPARAMS1 at capbase + 0x10 */
            int context64 = (hcc >> 2) & 1;        /* CSZ bit */

            /* Input Control Context is always 32 bytes (8 DWORDs)
             * Slot Context starts at DWORD 8 (32B offset) - always 32B
             * EP0 Context offset = InputControl(32B) + Slot(context_size)
             * context64=0: 32 + 32 = 64 bytes → DWORD 16
             * context64=1: 32 + 64 = 96 bytes → DWORD 24 */
            u32 *slot_ctx = ctx + 8;
            u32 *ep0_ctx = ctx + (context64 ? 24 : 16);
            /* Slot Context per xHCI spec:
             * DWORD 0: ContextEntries[31:27] | Speed[26:20]
             * DWORD 1: RootHubPortNumber[31:16]
             * DWORD 2: Speed[31:28] for xHCI 1.1+ */
            slot_ctx[0] = (2 << 27) | ((speed & 0xf) << 20);  /* ContextEntries=2, Speed */
            slot_ctx[1] = (found_port + 1) << 16;              /* Root Hub Port Number */

            /* Check if xHCI 1.1+ and set additional speed field */
            u32 hciversion = xhci_read32(handle, 0x02) >> 16;  /* HCIVersion from capbase + 0x02 */
            if (hciversion >= 0x0110) {
                fprintf(stderr, "  xHCI 1.1+ detected (0x%x), setting slot->ctx[2] speed bits\n", hciversion);
                slot_ctx[2] = ((u32)speed << 28);  /* xHCI 1.1+ Speed field */
            }

            /* EP0 Context settings per xHCI spec */
            u16 mps0 = (speed == 4) ? 512 : 64;
            u64 ep0_phys = xhci_transfer_ring_phys(handle, ep0_ring);
            int dcs = xhci_transfer_ring_cycle_state(ep0_ring);

            /* EP Context DWORD 0: EP State[7:5] = Running (1) */
            ep0_ctx[0] = (1 << 29);  /* EP State = Running */

            /* EP Context DWORD 1: CErr[1:0] | EP Type[5:3] | MaxPacketSize[31:16]
             * EP Type 4 = Control, CErr 3 = 3 errors allowed */
            ep0_ctx[1] = (4 << 3) | (3 << 0) | ((u32)mps0 << 16);

            /* EP Context DWORD 2-3: Dequeue Pointer */
            ep0_ctx[2] = (u32)ep0_phys | (dcs & 1);  /* dequeue low */
            ep0_ctx[3] = (u32)(ep0_phys >> 32);       /* dequeue high */

            /* Debug: print Input Context content after EP0 setup */
            fprintf(stderr, "  Input Context dump (full):\n");
            for (int i = 0; i < 28; i++) {
                if (i % 4 == 0) fprintf(stderr, "    ");
                fprintf(stderr, "[%2d]=0x%08x ", i, ctx[i]);
                if (i % 4 == 3) fprintf(stderr, "\n");
            }

            /* EP Context DWORD 0: EP State[7:5] = Running (1) */
            ep0_ctx[0] = (1 << 29);  /* EP State = Running */

            /* EP Context DWORD 1: CErr[1:0] | EP Type[5:3] | MaxPacketSize[31:16]
             * EP Type 4 = Control, CErr 3 = 3 errors allowed */
            ep0_ctx[1] = (4 << 3) | (3 << 0) | ((u32)mps0 << 16);

            /* EP Context DWORD 2-3: Dequeue Pointer */
            ep0_ctx[2] = (u32)ep0_phys | (dcs & 1);  /* dequeue low */
            ep0_ctx[3] = (u32)(ep0_phys >> 32);       /* dequeue high */

            if (xhci_address_device(handle, slot_id, input_ctx_phys) < 0) {
                fprintf(stderr, "  All ADDRESS_DEVICE attempts failed\n");
                xhci_dma_free(handle, ctx_virt, 4096);
                xhci_close(handle);
                return 1;
            }
            printf("  Device addressed (full context)\n");
        }
        xhci_dma_free(handle, ctx_virt, 4096);

        /* Read descriptor after address */
        memset(&dev_desc, 0, sizeof(dev_desc));
        if (read_device_descriptor(handle, slot_id, ep0_ring, &dev_desc) < 0) {
            fprintf(stderr, "  Descriptor read failed after address\n");
            xhci_close(handle);
            return 1;
        }
    }

    printf("  VID=0x%04x PID=0x%04x class=0x%02x sub=0x%02x proto=0x%02x mps0=%d\n",
           dev_desc.idVendor, dev_desc.idProduct,
           dev_desc.bDeviceClass, dev_desc.bDeviceSubClass,
           dev_desc.bDeviceProtocol, dev_desc.bMaxPacketSize0);

    /* Now read config descriptor
     * -------------------------------------------------------
     * Step 5: Read config descriptor, find bulk endpoints
     * ------------------------------------------------------- */
    u16 config_len = 0;
    u8 *config_desc = read_config_descriptor(handle, slot_id, ep0_ring,
                                             &config_len);
    if (!config_desc) {
        fprintf(stderr, "  Config descriptor read failed\n");
        xhci_close(handle);
        return 1;
    }

    printf("  Config: %d interfaces, total_len=%u\n",
           config_desc[4], config_len);

    int bulk_in_ep, bulk_out_ep;
    u32 max_packet;
    if (parse_endpoints(config_desc, config_len,
                        &bulk_in_ep, &bulk_out_ep, &max_packet) < 0) {
        fprintf(stderr, "  No bulk IN/OUT endpoints found\n");
        free(config_desc);
        xhci_close(handle);
        return 1;
    }
    printf("  Bulk IN ep=%d, Bulk OUT ep=%d, max_packet=%u\n",
           bulk_in_ep, bulk_out_ep, max_packet);
    free(config_desc);

    /* -------------------------------------------------------
     * Step 6: Configure bulk transfer rings
     * ------------------------------------------------------- */
    void *bulk_in_ring = xhci_transfer_ring_create(handle, 32);
    void *bulk_out_ring = xhci_transfer_ring_create(handle, 32);
    if (!bulk_in_ring || !bulk_out_ring) {
        fprintf(stderr, "  Transfer ring creation failed\n");
        xhci_close(handle);
        return 1;
    }

    if (configure_bulk_endpoints(handle, slot_id,
                                 bulk_in_ep, bulk_out_ep, max_packet,
                                 bulk_in_ring, bulk_out_ring) < 0) {
        fprintf(stderr, "  Configure bulk endpoints failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("  Bulk endpoints configured\n\n");

    /* -------------------------------------------------------
     * Step 7: BOT — INQUIRY
     * ------------------------------------------------------- */
    struct bot_device bot;
    memset(&bot, 0, sizeof(bot));
    bot.handle = handle;
    bot.slot_id = slot_id;
    bot.ep0_ring = ep0_ring;
    bot.bulk_in_ring = bulk_in_ring;
    bot.bulk_out_ring = bulk_out_ring;
    bot.bulk_in_ep = bulk_in_ep;
    bot.bulk_out_ep = bulk_out_ep;
    bot.max_packet = max_packet;
    bot.next_tag = 0;

    printf("--- BOT: INQUIRY ---\n");
    struct scsi_inquiry_data inq;
    if (bot_inquiry(&bot, &inq) < 0) {
        fprintf(stderr, "  INQUIRY failed\n");
        struct scsi_request_sense sense;
        if (bot_request_sense(&bot, &sense) == 0)
            fprintf(stderr, "  Sense: key=0x%02x ASC=0x%02x ASCQ=0x%02x\n",
                    sense.sense_key[0] & 0x0f, sense.asc, sense.ascq);
        xhci_close(handle);
        return 1;
    }

    char vendor[9] = {}, product[17] = {}, rev[5] = {};
    memcpy(vendor, inq.vendor, 8);
    memcpy(product, inq.product, 16);
    memcpy(rev, inq.revision, 4);
    printf("  Peripheral: %d\n", inq.peripheral & 0x1f);
    printf("  Vendor : '%s'\n", vendor);
    printf("  Product: '%s'\n", product);
    printf("  Rev    : '%s'\n", rev);

    /* -------------------------------------------------------
     * Step 8: BOT — TEST UNIT READY
     * ------------------------------------------------------- */
    printf("\n--- BOT: TEST UNIT READY ---\n");
    if (bot_test_unit_ready(&bot) < 0) {
        fprintf(stderr, "  TEST UNIT READY failed\n");
        struct scsi_request_sense sense;
        if (bot_request_sense(&bot, &sense) == 0)
            fprintf(stderr, "  Sense: key=0x%02x ASC=0x%02x ASCQ=0x%02x\n",
                    sense.sense_key[0] & 0x0f, sense.asc, sense.ascq);
        xhci_close(handle);
        return 1;
    }
    printf("  Unit ready\n");

    /* -------------------------------------------------------
     * Step 9: BOT — READ CAPACITY
     * ------------------------------------------------------- */
    printf("\n--- BOT: READ CAPACITY ---\n");
    u64 last_lba;
    u32 block_size;
    if (bot_read_capacity(&bot, &last_lba, &block_size) < 0) {
        fprintf(stderr, "  READ CAPACITY failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("  Last LBA  : %llu (0x%llx)\n",
           (unsigned long long)last_lba, (unsigned long long)last_lba);
    printf("  Block size: %u bytes\n", block_size);
    printf("  Capacity  : %llu MB\n",
           (unsigned long long)(last_lba + 1) * block_size / (1024 * 1024));

    /* -------------------------------------------------------
     * Step 10: BOT — READ SECTOR 0
     * ------------------------------------------------------- */
    printf("\n--- BOT: READ SECTOR 0 (%u bytes) ---\n", block_size);

    void *sector = malloc(block_size);
    if (!sector) {
        fprintf(stderr, "  malloc failed\n");
        xhci_close(handle);
        return 1;
    }

    if (bot_read_10(&bot, 0, 1, sector) < 0) {
        fprintf(stderr, "  READ(10) LBA=0 failed\n");
        free(sector);
        xhci_close(handle);
        return 1;
    }

    /* Dump first 128 bytes */
    printf("  First 128 bytes:\n");
    for (int i = 0; i < 128 && i < (int)block_size; i += 16) {
        printf("    %04x: ", i);
        for (int j = 0; j < 16; j++) {
            printf("%02x ", ((u8 *)sector)[i + j]);
        }
        printf("  ");
        for (int j = 0; j < 16; j++) {
            unsigned char c = ((u8 *)sector)[i + j];
            printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("\n");
    }

    free(sector);

    printf("\n========================================\n");
    printf("BOT test PASSED\n");
    printf("========================================\n");

    xhci_close(handle);
    return 0;
}
