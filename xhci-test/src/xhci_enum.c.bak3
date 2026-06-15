// XHCI device enumeration — follows SeaBIOS usb_enumerate() flow exactly.
//
// SeaBIOS flow:
//   1. xhci_hub_reset() → port reset, get speed
//   2. usb_alloc_device() → xhci_alloc_pipe(usbdev, ep0_desc)
//   3. xhci_alloc_pipe():
//      a. xhci_alloc_inctx() → build input context
//      b. enable_slot()
//      c. allocate device context (1024B, aligned 1024)
//      d. set DCBAA[slot_id]
//      e. address_device(slot_id, input_ctx)

#include "xhci_state.h"
#include "xhci.h"
#include "xhci_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

static void
msleep(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/****************************************************************
// SeaBIOS-style input context builder (xhci_alloc_inctx equivalent)
//
// Layout for 32-byte contexts (context64=0):
//   in[0]  = ctx[0..7]   = Control (DEL, ADD, reserved)
//   in[1]  = ctx[8..15]  = Slot Context
//   in[2]  = ctx[16..23] = EP0 Context
 ****************************************************************/

static void *
build_input_context(void *handle, int port, int speed, int n_endpoints,
                     void *ep0_ring, u64 *phys_out)
{
    u32 hcc = xhci_read32(handle, 0x10);
    int context64 = (hcc >> 2) & 1;        // HCCPARAMS1 bit 2 = CSZ

    u32 hci_params = xhci_read32(handle, 0x00);
    u16 hciversion = (hci_params >> 16) & 0xFFFF;
    int xhci_1_1_plus = (hciversion >= 0x0110);

    fprintf(stdout, "[ENUM] HCIVersion=0x%04x context64=%d xhci_1_1+=%d\n",
            hciversion, context64, xhci_1_1_plus);

    // Input Context: 分配 4K（16-byte 对齐即可，4K 更简单）
    u64 input_phys;
    void *ctx_virt = xhci_dma_alloc(handle, 4096, &input_phys);
    if (!ctx_virt) {
        fprintf(stdout, "[ENUM] input context alloc FAILED\n");
        return NULL;
    }
    memset(ctx_virt, 0, 4096);
    *phys_out = input_phys;

    u32 *ctx = (u32 *)ctx_virt;

    // === Input Control Context (DWORD 0~7, fixed 32 bytes) ===
    // DWORD 0: Drop flags (none)
    // DWORD 1: Add flags — bit0=Slot, bit1=EP0 → 0x03
    ctx[0] = 0x00000000;  // Drop flags: none
    ctx[1] = 0x00000003;  // Add flags: Slot + EP0

    // === Slot Context (after Input Control Context) ===
    // Linux kernel: xhci_get_slot_ctx() = ctx->bytes + CTX_SIZE(hcc)
    // CTX_SIZE = 32 or 64 depending on HCC.CSZ (context64)
    int ctx_size = context64 ? 64 : 32;
    struct xhci_slotctx *slot = (struct xhci_slotctx *)((u8 *)ctx_virt + ctx_size);

    // DWORD 0: ContextEntries[31:27] | Speed[26:20]
    // DWORD 1: Root Hub Port Number[31:16]
    // 32B 和 64B 模式下字段布局完全相同
    slot->ctx[0] = (1 << 27) | ((u32)speed << 20);  // ContextEntries=1
    // Note: kernel does not set slot->ctx[2] speed bits for xHCI 1.1+
    slot->ctx[1] = (u32)(port + 1) << 16;            // RHPortNumber

    // === EP0 Context ===
    // Linux kernel: xhci_get_ep_ctx(ctx, 0) => ep_index(0+1+1) * CTX_SIZE
    int ep0_offset_bytes = 2 * ctx_size;  // 2 * CTX_SIZE
    struct xhci_epctx *ep0 = (struct xhci_epctx *)((u8 *)ctx_virt + ep0_offset_bytes);

    u64 ep0_phys = xhci_transfer_ring_phys(handle, ep0_ring);
    assert((ep0_phys & 0xF) == 0);

    int dcs = xhci_transfer_ring_cycle_state(ep0_ring);
    ep0->deq_low  = (u32)ep0_phys | (dcs & 1);
    ep0->deq_high = (u32)(ep0_phys >> 32);
    fprintf(stdout, "[ENUM] EP0 TR Dequeue Pointer (full 64-bit): 0x%llx (DCS=%d)\n",
            (unsigned long long)(((u64)ep0->deq_low | ((u64)ep0->deq_high << 32)) & ~1ULL),
            (int)(ep0->deq_low & 1));
    fprintf(stdout, "[ENUM]   deq_low=0x%08x deq_high=0x%08x ep0_phys=0x%llx\n",
            ep0->deq_low, ep0->deq_high, (unsigned long long)ep0_phys);
    // EP0: EP Type = Control (4), CErr = 3, MaxPacketSize
    // EP Type values (xHCI spec Table 6-9):
    //   1 = Isoch OUT, 2 = Bulk OUT, 3 = Intr OUT, 4 = Control,
    //   5 = Isoch IN, 6 = Bulk IN, 7 = Intr IN
    u32 maxpacket;
    switch (speed) {
    case 4:  // SuperSpeed
    case 5:  // SuperSpeed Plus
        maxpacket = 512;
        break;
    default: // FullSpeed, HighSpeed, LowSpeed
        maxpacket = 64;
        break;
    }
    ep0->ctx[0] = 0;
    ep0->ctx[1] = (4 << 3) | (3 << 1) | (maxpacket << 16);  // EP_TYPE=Control, CErr=3, MaxPacket
    ep0->length = maxpacket;

    // === Debug output ===
    fprintf(stdout, "[ENUM] Input Context: phys=0x%llx context64=%d\n",
            (unsigned long long)input_phys, context64);
    fprintf(stdout, "[ENUM] DEL=0x%08x ADD=0x%08x\n", ctx[0], ctx[1]);
    // fprintf(stdout, "[ENUM] Slot DWORD0=0x%08x DWORD1=0x%08x\n",
    //         slot->ctx[0], slot->ctx[1]);
    fprintf(stdout, "[ENUM] EP0 DWORD0=0x%08x DWORD1=0x%08x "
            "deq=0x%08x%08x length=0x%08x\n",
            ep0->ctx[0], ep0->ctx[1],
            ep0->deq_high, ep0->deq_low, ep0->length);

    // === Dump ===
    u32 *dump = (u32 *)ctx_virt;
    fprintf(stdout, "[ENUM] Input Context dump (60 DWORDs):\n");
    for (int i = 0; i < 60; i++) {
        if (i % 4 == 0) fprintf(stdout, "[ENUM] ");
        fprintf(stdout, "[%2d]=0x%08x ", i, dump[i]);
        if (i % 4 == 3) fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");

    (void)n_endpoints;
    (void)xhci_1_1_plus;

    return ctx_virt;
}
/****************************************************************
// Descriptor reads via control transfers
 ****************************************************************/
static int
read_device_descriptor8(void *handle, int slot_id, void *ep0_ring,
                       struct usb_device_descriptor *desc, u64 desc_phys)
{
    struct usb_ctrlrequest req = {
        .bRequestType = USB_REQ_TYPE(USB_DIR_IN, USB_TYPE_STANDARD, USB_RECIP_DEVICE),
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DT_DEVICE << 8),
        .wIndex = 0,
        .wLength = 8,
    };

    memset(desc, 0, sizeof(*desc));
    return xhci_control_transfer_phys(handle, slot_id, ep0_ring, (const u8 *)&req,
                                       desc_phys, sizeof(*desc), 1);
}

static int
read_device_descriptor(void *handle, int slot_id, void *ep0_ring,
                       struct usb_device_descriptor *desc, u64 desc_phys)
{
    struct usb_ctrlrequest req = {
        .bRequestType = USB_REQ_TYPE(USB_DIR_IN, USB_TYPE_STANDARD, USB_RECIP_DEVICE),
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DT_DEVICE << 8),
        .wIndex = 0,
        .wLength = sizeof(*desc),
    };

    memset(desc, 0, sizeof(*desc));
    return xhci_control_transfer_phys(handle, slot_id, ep0_ring, (const u8 *)&req,
                                       desc_phys, sizeof(*desc), 1);
}

static u8 *
read_config_descriptor(void *handle, int slot_id, void *ep0_ring,
                       u16 *total_len_out,
                       u8 **buf_out, u64 *phys_out)
{
    // First read the header
    u64 hdr_phys;
    u8 *hdr_buf = xhci_dma_alloc(handle, 9, &hdr_phys);
    if (!hdr_buf) return NULL;
    memset(hdr_buf, 0, 9);

    struct usb_ctrlrequest req = {
        .bRequestType = USB_REQ_TYPE(USB_DIR_IN, USB_TYPE_STANDARD, USB_RECIP_DEVICE),
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DT_CONFIG << 8),
        .wIndex = 0,
        .wLength = 9,
    };

    int ret = xhci_control_transfer_phys(handle, slot_id, ep0_ring, (const u8 *)&req,
                                          hdr_phys, 9, 1);
    if (ret < 0) {
        fprintf(stdout, "[ENUM]   read_config_descriptor: header read failed\n");
        xhci_dma_free(handle, hdr_buf, 9);
        return NULL;
    }

    u16 total_len = hdr_buf[2] | (hdr_buf[3] << 8);
    xhci_dma_free(handle, hdr_buf, 9);
    if (total_len < 9 || total_len > 4096) {
        fprintf(stdout, "[ENUM]   read_config_descriptor: bad total length %u\n", total_len);
        return NULL;
    }

    u8 *full_buf = xhci_dma_alloc(handle, total_len, phys_out);
    if (!full_buf) return NULL;
    memset(full_buf, 0, total_len);
    *buf_out = full_buf;

    // Update request with full length
    req.wLength = total_len;

    ret = xhci_control_transfer_phys(handle, slot_id, ep0_ring, (const u8 *)&req,
                                      *phys_out, total_len, 1);
    if (ret < 0) {
        fprintf(stdout, "[ENUM]   read_config_descriptor: full read failed\n");
        xhci_dma_free(handle, full_buf, total_len);
        *buf_out = NULL;
        return NULL;
    }

    *total_len_out = total_len;
    return full_buf;
}

// Parse endpoint descriptors from config descriptor tree
static int
parse_endpoints(const u8 *config_desc, u16 total_len,
                struct xhci_state_ep_info **eps_out, u32 *n_eps_out)
{
    u16 pos = 9;
    int endpoint_count = 0;
    int max_eps = 8;

    *eps_out = calloc(max_eps, sizeof(struct xhci_state_ep_info));
    if (!*eps_out) return -1;

    while (pos < total_len) {
        if (pos + 1 >= total_len) break;
        u8 bLength = config_desc[pos];
        u8 bDescriptorType = config_desc[pos + 1];

        if (bLength == 0) break;

        if (bDescriptorType == 0x05 && bLength >= 7) {
            if (endpoint_count >= max_eps) {
                max_eps *= 2;
                *eps_out = realloc(*eps_out, max_eps * sizeof(struct xhci_state_ep_info));
                if (!*eps_out) return -1;
            }
            struct xhci_state_ep_info *ep = &(*eps_out)[endpoint_count];
            u8 ep_addr = config_desc[pos + 2];
            u16 wMaxPacketSize = config_desc[pos + 4] | (config_desc[pos + 5] << 8);
            u8 bmAttributes = config_desc[pos + 3];

            u8 ep_num = ep_addr & 0x7f;
            u8 dir_in = (ep_addr & 0x80) != 0;

            // xHCI EP_ID encoding: OUT=(ep_num*2-1), IN=(ep_num*2)
            ep->ep_index = dir_in ? (ep_num * 2) : (ep_num * 2 - 1);
            // USB bmAttributes -> xHCI EP Type: Ctrl=1, Bulk=2, Intr=3, Iso=4
            static const u8 usb_to_xhci_ep_type[] = {1, 4, 2, 3};
            ep->ep_type = usb_to_xhci_ep_type[bmAttributes & 0x03];
            ep->max_packet_size = wMaxPacketSize & 0x07ff;
            ep->avg_trb_len = wMaxPacketSize;
            ep->mult = 0;
            ep->cerr = 3;
            ep->max_burst = (wMaxPacketSize >> 11) & 0x03;
            ep->interval = config_desc[pos + 6];

            endpoint_count++;
        }

        pos += bLength;
    }

    *n_eps_out = endpoint_count;

    // Prepend EP0 if not present
    if (endpoint_count == 0 || (*eps_out)[0].ep_index != 0) {
        memmove(*eps_out + 1, *eps_out, endpoint_count * sizeof(struct xhci_state_ep_info));
        (*eps_out)[0].ep_index = 0;
        (*eps_out)[0].ep_type = 1;
        (*eps_out)[0].max_packet_size = 64;
        (*eps_out)[0].avg_trb_len = 64;
        (*eps_out)[0].cerr = 3;
        *n_eps_out = endpoint_count + 1;
    }

    return 0;
}

/****************************************************************
// Main enumeration
 ****************************************************************/

int
xhci_state_enumerate(void *handle, struct xhci_state **state_out)
{
  
    // 1. Full controller init
    fprintf(stdout, "[ENUM] === Starting enumeration ===\n");
    if (xhci_full_init(handle) < 0) {
        fprintf(stderr, "[ENUM] FATAL: xhci_full_init failed\n");
        return -1;
    }
    fprintf(stdout, "[ENUM] xhci_full_init OK\n");
    fprintf(stdout, "[ENUM] Waiting 3s for ports to stabilize after HCRST...\n");
    for (int i = 0; i < 30; i++) {
        msleep(100);
    }

    int n_ports = xhci_port_count(handle);
    fprintf(stdout, "[ENUM] Controller has %d ports\n", n_ports);
    fprintf(stdout, "Enumerating %d ports...\n", n_ports);

    // Allocate state
    struct xhci_state *state = calloc(1, sizeof(*state));
    if (!state) {
        fprintf(stdout, "[ENUM] malloc failed\n");
        return -1;
    }
    state->max_slots = n_ports;
    state->slots = calloc(n_ports, sizeof(struct xhci_state_slot_entry));
    if (!state->slots) {
        free(state);
        return -1;
    }

    u32 n_slots = 0;
    // 2. Walk ports
    for (int port = 0; port < n_ports; port++) {
        u32 portsc = xhci_port_read(handle, port);
        u32 ccs = portsc & XHCI_PORTSC_CCS;
        u32 ped = (portsc >> 1) & 1;
        u32 pls = (portsc >> XHCI_PORTSC_PLS_SHIFT) & 0xf;
        u32 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;

        fprintf(stdout, "[ENUM] --- Port %d: PORTSC=0x%08x CCS=%u PED=%u PLS=%u SPD=%u ---\n",
                port, portsc, ccs, ped, pls, speed);

        // Check CCS
        if (!ccs) {
            fprintf(stdout, "[ENUM]   Port %d: no device (CCS=0), skipping\n", port);
            continue;
        }

        fprintf(stdout, "  Port %d: device detected (PORTSC=0x%08x)\n", port, portsc);

        // SeaBIOS relies on the controller to handle reset/link training.
        // Manually setting PR on Haiguang controllers via VFIO causes device disconnect (CCS=0).
        // Skip explicit PR and just wait for the port to reach U0.

        // Wait for port to be in U0 (PLS=0) before attempting enumeration
        fprintf(stdout, "[ENUM]   Port %d: waiting for U0 (no PR)...\n", port);
        // xHCI spec: device must be in U0 for control transfers to work
        fprintf(stdout, "[ENUM]   Port %d: waiting for U0...\n", port);
        for (int retries = 0; retries < 100; retries++) {
            portsc = xhci_port_read(handle, port);
            u32 pls = (portsc >> XHCI_PORTSC_PLS_SHIFT) & XHCI_PORTSC_PLS_MASK;
                if (pls == 0)  // U0
                    break;
            msleep(100);
        }
        portsc = xhci_port_read(handle, port);
        u32 pls_after = (portsc >> XHCI_PORTSC_PLS_SHIFT) & XHCI_PORTSC_PLS_MASK;
        fprintf(stdout, "[ENUM]   Port %d after PR: PORTSC=0x%08x CCS=%u PED=%u PLS=%u SPD=%u\n",
                port, portsc, !!(portsc & XHCI_PORTSC_CCS),
                (portsc >> 1) & 1, pls_after,
                (portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
        if (!(portsc & XHCI_PORTSC_CCS)) {
            fprintf(stdout, "[ENUM]   Port %d: device disconnected after PR\n", port);
            fprintf(stdout, "    device disconnected after reset\n");
            continue;
        }
        if (!((portsc >> 1) & 1)) {
            fprintf(stdout, "[ENUM]   Port %d: PED=0 after PR, skipping\n", port);
            continue;
        }
        speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK;
        fprintf(stdout, "[ENUM]   Port %d settled: PLS=%u SPD=%u\n", port, pls_after, speed);
        if (pls_after != 0) {
            fprintf(stdout, "[ENUM]   Port %d: not in U0 (PLS=%u), skipping\n", port, pls_after);
            continue;
        }

        // SeaBIOS xhci_alloc_pipe() for EP0:
        // Step 1: Build input context
        void *ep0_ring = xhci_transfer_ring_create(handle, 32);
        if (!ep0_ring) {
            fprintf(stdout, "[ENUM]   Port %d: EP0 ring creation FAILED\n", port);
            fprintf(stdout, "    EP0 ring creation failed\n");
            continue;
        }

        u64 input_ctx_phys;
        void *ctx_virt = build_input_context(handle, port, speed, 1,
                                             ep0_ring, &input_ctx_phys);
        if (!ctx_virt) {
            fprintf(stdout, "    input context alloc failed\n");
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }

        // 或者更简单的做法：在 ENABLE_SLOT 之前分配好 dev_ctx
        u64 dev_ctx_phys;
        void *dev_ctx = xhci_dma_alloc(handle, 4096, &dev_ctx_phys);
        memset(dev_ctx, 0, 4096);


        // Step 2: Enable slot
        fprintf(stdout, "[ENUM]   Port %d: calling xhci_enable_slot()...\n", port);
        int slot_id = xhci_enable_slot(handle);
        if (slot_id < 0) {
            fprintf(stdout, "[ENUM]   Port %d: ENABLE_SLOT returned %d\n", port, slot_id);
            fprintf(stdout, "    enable_slot failed\n");
            xhci_dma_free(handle, ctx_virt, 4096);
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        // 立即设置 DCBAA，在等待之前！
        u64 dcbaa_phys_out;

        struct xhci_devlist *dcbaa = xhci_get_dcbaa(handle, &dcbaa_phys_out);
        dcbaa[slot_id].ptr_low = (u32)dev_ctx_phys;
        dcbaa[slot_id].ptr_high = (u32)(dev_ctx_phys >> 32);
        fprintf(stdout, "[ENUM] DCBAA[%d] = 0x%llx\n", slot_id, (unsigned long long)dev_ctx_phys);

        msleep(100);  // 等 xHC 写 Output DC

        fprintf(stdout, "    Slot %d enabled\n", slot_id);
        fprintf(stdout, "[ENUM]   Port %d: ENABLE_SLOT returned slot_id=%d\n", port, slot_id);

        // Small delay after enable_slot before address_device
        msleep(100);

        // Step 3: Allocate device context (SeaBIOS: 1024B aligned to 1024)
        // u64 dev_ctx_phys;
        // void *dev_ctx = xhci_dma_alloc(handle, 4096, &dev_ctx_phys);
        u32 *slot_out = (u32 *)dev_ctx;  // dev_ctx = DCBAA[slot_id] 指向的 Output DC
        u32 slot_state_dword = slot_out[4];  // Offset 10h
        u32 slot_state = (slot_state_dword >> 27) & 0x7;  // Slot State in bits 31:27
dcbaa[slot_id].ptr_low = (u32)dev_ctx_phys;
dcbaa[slot_id].ptr_high = (u32)(dev_ctx_phys >> 32);
        fprintf(stderr, "[ENUM] Checking S  lot State after ENABLE_SLOT...\n");
        fprintf(stderr, "[ENUM] Slot State after ENABLE_SLOT: %u (0=Disabled 1=Enabled)\n", slot_state);
        if (!dev_ctx) {
            fprintf(stdout, "[ENUM]   Port %d: device context alloc FAILED\n", port);
            fprintf(stdout, "    device context alloc failed\n");
            xhci_dma_free(handle, ctx_virt, 4096);
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        memset(dev_ctx, 0, 4096);

        // Step 4: Set DCBAA entry
        // u64 dcbaa_phys_out;
        // struct xhci_devlist *dcbaa = xhci_get_dcbaa(handle, &dcbaa_phys_out);
        // if (dcbaa && slot_id < 64) {
        //     dcbaa[slot_id].ptr_low = (u32)dev_ctx_phys;
        //     dcbaa[slot_id].ptr_high = (u32)(dev_ctx_phys >> 32);
        //     fprintf(stdout, "[ENUM]   DCBAA[%d] = 0x%llx\n",
        //             slot_id, (unsigned long long)dev_ctx_phys);
        // }

        // Step 5: Address device
        if (xhci_address_device(handle, slot_id, input_ctx_phys) < 0) {
            fprintf(stdout, "[ENUM]   Port %d: ADDRESS_DEVICE FAILED\n", port);
            fprintf(stdout, "    address_device failed\n");
            // NOTE: dev_ctx must NOT be freed here - DCBAA[slot_id] still points to it
        // It will be freed when the slot is cleaned up
            xhci_dma_free(handle, ctx_virt, 4096);
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        fprintf(stdout, "[ENUM]   Port %d: ADDRESS_DEVICE SUCCESS\n", port);

        // Verify DCBAA entry
        fprintf(stdout, "[ENUM]   Port %d: verifying DCBAA[%d]...\n", port, slot_id);
        if (xhci_dcbaa_verify(handle, slot_id) < 0) {
            fprintf(stdout, "[ENUM]   Port %d: DCBAA[%d] verification FAILED\n", port, slot_id);
            fprintf(stdout, "    DCBAA entry not set after address_device\n");
            // NOTE: dev_ctx must NOT be freed here - DCBAA[slot_id] still points to it
        // It will be freed when the slot is cleaned up
            xhci_dma_free(handle, ctx_virt, 4096);
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        fprintf(stdout, "    Device addressed, DCBAA verified\n");
        fprintf(stdout, "[ENUM]   Port %d: DCBAA verified OK\n", port);

        // Wait for controller to settle after Address Device
        msleep(100);

        // Free input context and device context
        xhci_dma_free(handle, ctx_virt, 4096);

        {
                        // Out DevCtx DWORD[4] (Offset 10h) 的 Slot State
            u32 *out_ctx = (u32 *)dev_ctx;
            u32 dev_state_dword = out_ctx[4];
            u32 slot_state = (dev_state_dword >> 0) & 0x1F;
            fprintf(stdout, "[ENUM]   Port %d: Slot State after address_device: %u\n", port, slot_state);
            // 0=Disabled, 1=Enabled, 2=Default, 3=Addressed, 4=Configured
        }
        // NOTE: dev_ctx must NOT be freed here - DCBAA[slot_id] still points to it
        // It will be freed when the slot is cleaned up

        // Allocate DMA buffer for device descriptor
        u64 desc_phys;
        struct usb_device_descriptor *dev_desc_buf = xhci_dma_alloc(handle,
            sizeof(struct usb_device_descriptor), &desc_phys);
        if (!dev_desc_buf) {
            fprintf(stdout, "    device descriptor buffer alloc failed\n");
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }

        // Step 1: Read first 8 bytes to get bMaxPacketSize0
        fprintf(stdout, "[ENUM]   Port %d: reading device descriptor (8 bytes)...\n", port);
        if (read_device_descriptor8(handle, slot_id, ep0_ring, dev_desc_buf, desc_phys) < 0) {
            fprintf(stdout, "[ENUM]   Port %d: device descriptor read FAILED\n", port);
            fprintf(stdout, "    device descriptor read failed\n");
            xhci_dma_free(handle, dev_desc_buf, sizeof(struct usb_device_descriptor));
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        fprintf(stdout, "[ENUM]   bMaxPacketSize0=%d bcdUSB=0x%04x\n",
                dev_desc_buf->bMaxPacketSize0, dev_desc_buf->bcdUSB);

        // Step 2: Read full 18-byte device descriptor
        fprintf(stdout, "[ENUM]   Port %d: reading full device descriptor...\n", port);
        if (read_device_descriptor(handle, slot_id, ep0_ring, dev_desc_buf, desc_phys) < 0) {
            fprintf(stdout, "[ENUM]   Port %d: full device descriptor read FAILED\n", port);
            xhci_dma_free(handle, dev_desc_buf, sizeof(struct usb_device_descriptor));
            xhci_transfer_ring_free(handle, ep0_ring);
            continue;
        }
        fprintf(stdout, "    Device: VID=0x%04x PID=0x%04x class=0x%02x\n",
                dev_desc_buf->idVendor, dev_desc_buf->idProduct, dev_desc_buf->bDeviceClass);
        fprintf(stdout, "[ENUM]   VID=0x%04x PID=0x%04x class=0x%02x bcdUSB=0x%04x mps0=%d configs=%d\n",
                dev_desc_buf->idVendor, dev_desc_buf->idProduct, dev_desc_buf->bDeviceClass,
                dev_desc_buf->bcdUSB, dev_desc_buf->bMaxPacketSize0, dev_desc_buf->bNumConfigurations);

        // Read config descriptor
        fprintf(stdout, "[ENUM]   Port %d: reading config descriptor...\n", port);
        u16 config_len = 0;
        u8 *config_desc_buf = NULL;
        u64 config_desc_phys = 0;
        u8 *config_desc = read_config_descriptor(handle, slot_id, ep0_ring, &config_len,
                                                  &config_desc_buf, &config_desc_phys);

        // Parse endpoints
        struct xhci_state_ep_info *eps = NULL;
        u32 n_eps = 0;
        if (config_desc) {
            parse_endpoints(config_desc, config_len, &eps, &n_eps);
            if (n_eps > 0 && eps[0].ep_index == 0)
                eps[0].max_packet_size = dev_desc_buf->bMaxPacketSize0;
            fprintf(stdout, "    Config: %d interfaces, %d endpoints\n",
                    config_desc[4], n_eps);
            fprintf(stdout, "[ENUM]   Endpoints parsed: %d\n", n_eps);
            for (u32 i = 0; i < n_eps; i++) {
                fprintf(stdout, "[ENUM]     EP[%d] index=%d type=%d mps=%d interval=%d\n",
                        i, eps[i].ep_index, eps[i].ep_type,
                        eps[i].max_packet_size, eps[i].interval);
            }
            // Copy config descriptor from DMA buffer to malloc'd memory
            config_desc = (u8 *)malloc(config_len);
            if (config_desc)
                memcpy((void *)config_desc, config_desc_buf, config_len);
            xhci_dma_free(handle, config_desc_buf, config_len);
        } else {
            eps = calloc(1, sizeof(struct xhci_state_ep_info));
            if (eps) {
                eps[0].ep_index = 0;
                eps[0].ep_type = 1;
                eps[0].max_packet_size = dev_desc_buf->bMaxPacketSize0;
                eps[0].avg_trb_len = dev_desc_buf->bMaxPacketSize0;
                eps[0].cerr = 3;
            }
            n_eps = 1;
            fprintf(stdout, "[ENUM]   No config descriptor, EP0 only\n");
            fprintf(stdout, "    No config descriptor, EP0 only\n");
        }

        // Store in state
        struct xhci_state_slot_entry *sl = &state->slots[n_slots];
        sl->slot_id = slot_id;
        sl->port_index = port;
        sl->portsc_snapshot = portsc;
        sl->speed = speed;
        sl->route_string = 0;
        memcpy(&sl->dev_descriptor, dev_desc_buf, sizeof(struct usb_device_descriptor));
        sl->config_descr_total_len = config_len;
        sl->config_descriptor = config_desc;
        sl->n_endpoints = n_eps;
        sl->endpoints = eps;

        // Free device descriptor DMA buffer
        xhci_dma_free(handle, dev_desc_buf, sizeof(struct usb_device_descriptor));
        sl->n_rings = 1;
        sl->rings = calloc(1, sizeof(struct xhci_state_transfer_ring *));
        if (sl->rings) {
            sl->rings[0] = calloc(1, sizeof(struct xhci_state_transfer_ring));
            if (sl->rings[0]) {
                sl->rings[0]->n_trbs = 32;
                sl->rings[0]->enqueue_pos = 0;
                sl->rings[0]->cycle_state = xhci_transfer_ring_cycle_state(ep0_ring);
                sl->rings[0]->trb_data = NULL;
            }
        }
        sl->ep0_ring = ep0_ring;
        sl->restored = 0;

        n_slots++;
        fprintf(stdout, "[ENUM]   Port %d: device stored in slot[%d]\n", port, n_slots - 1);
    }

    if (n_slots == 0) {
        fprintf(stdout, "[ENUM] No devices found on any port\n");
        fprintf(stdout, "No devices found\n");
        xhci_state_free(state);
        *state_out = NULL;
        return -1;
    }

    state->n_slots = n_slots;
    fprintf(stdout, "[ENUM] === Enumeration complete: %d device(s) found ===\n", n_slots);
    fprintf(stdout, "Enumeration complete: %d device(s) found\n", n_slots);
    *state_out = state;
    return 0;
}
