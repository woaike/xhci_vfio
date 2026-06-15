/* Soft takeover test — minimal setup without HCRST. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "xhci.h"
#include "xhci_internal.h"
#include "xhci_regs.h"

static void msleep(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(void)
{
    const char *pci_bdf = getenv("XHCI_PCI");
    if (!pci_bdf) pci_bdf = "0000:05:00.1";

    printf("=== Soft Takeover Test on %s ===\n\n", pci_bdf);

    /* Step 1: Open VFIO */
    void *handle = xhci_open(pci_bdf);
    if (!handle) {
        fprintf(stderr, "Failed to open %s\n", pci_bdf);
        return 1;
    }
    printf("VFIO opened\n");

    /* Step 2: Read current state */
    u32 cmd = xhci_read32(handle, 0x04);
    u32 sts = xhci_read32(handle, 0x08);
    printf("USBCMD=0x%08x USBSTS=0x%08x\n", cmd, sts);
    printf("  RS=%d HCE=%d HCH=%d\n",
           !!(cmd & 1), !!(sts & (1<<12)), !!(sts & (1<<24)));

    /* Step 3: If running, stop first */
    if (cmd & 1) {
        printf("Stopping controller...\n");
        xhci_write32(handle, 0x04, cmd & ~1);
        for (int i = 0; i < 50; i++) {
            msleep(20);
            sts = xhci_read32(handle, 0x08);
            if (sts & (1<<24)) break;
        }
    }

    /* Step 4: Scan ports using library's correct offset */
    int n_ports = xhci_port_count(handle);
    printf("Ports=%d\n\n", n_ports);

    for (int i = 0; i < n_ports; i++) {
        u32 p = xhci_port_read(handle, i);
        if (p & XHCI_PORTSC_CCS) {
            u32 spd = (p >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
            printf("  Port %d: CCS=1 SPEED=%d PLS=%d PED=%d PORTSC=0x%08x\n",
                   i, spd, (p>>10)&0xf, (p>>1)&1, p);
        }
    }

    /* Step 5: Find first connected port */
    int found_port = -1;
    u32 portsc = 0;
    for (int i = 0; i < n_ports; i++) {
        portsc = xhci_port_read(handle, i);
        if (portsc & XHCI_PORTSC_CCS) {
            found_port = i;
            break;
        }
    }
    if (found_port < 0) {
        printf("\nNo devices found\n");
        xhci_close(handle);
        return 1;
    }

    u8 speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
    printf("\nUsing Port %d (speed=%d)\n", found_port, speed);

    /* Step 6: Warm reset */
    printf("Warm reset...\n");
    xhci_port_write(handle, found_port,
                    XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |
                    XHCI_PORTSC_WRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC);
    msleep(100);
    portsc = xhci_port_read(handle, found_port);
    xhci_port_write(handle, found_port, portsc | XHCI_PORTSC_PR);
    printf("  PR set, waiting...\n");
    msleep(500);
    for (int i = 0; i < 20; i++) {
        portsc = xhci_port_read(handle, found_port);
        if (!(portsc & XHCI_PORTSC_PR)) break;
        msleep(100);
    }
    xhci_port_write(handle, found_port,
                    XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |
                    XHCI_PORTSC_WRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC);
    msleep(500);
    portsc = xhci_port_read(handle, found_port);
    speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & 0xf;
    printf("  After: CCS=%d speed=%d PED=%d PLS=%d PORTSC=0x%08x\n",
           !!(portsc&XHCI_PORTSC_CCS), speed, (portsc>>1)&1,
           (portsc>>10)&0xf, portsc);

    if (!(portsc & XHCI_PORTSC_CCS)) {
        printf("Device disconnected\n");
        xhci_close(handle);
        return 1;
    }

    /* Step 7: Setup rings and DCBAA */
    u32 hcs1 = xhci_read32(handle, 0x00);
    int max_slots = (hcs1 >> 0) & 0xff;

    /* DCBAA */
    u64 dcbaa_phys;
    struct xhci_devlist *dcbaa = xhci_dma_alloc(handle, 65*sizeof(struct xhci_devlist), &dcbaa_phys);
    memset(dcbaa, 0, 65*sizeof(struct xhci_devlist));

    /* Command ring */
    u64 cmd_phys;
    struct xhci_trb *cmd_ring = xhci_dma_alloc(handle, 256*16, &cmd_phys);
    memset(cmd_ring, 0, 256*16);

    /* Event ring */
    u64 evt_phys;
    struct xhci_trb *evt_ring = xhci_dma_alloc(handle, 256*16, &evt_phys);
    memset(evt_ring, 0, 256*16);

    /* ERST */
    u64 erst_phys;
    struct xhci_er_seg *erst = xhci_dma_alloc(handle, 4096, &erst_phys);
    memset(erst, 0, 4096);
    erst->ptr_low = (u32)evt_phys;
    erst->ptr_high = (u32)(evt_phys >> 32);
    erst->size = 256;

    /* Write registers */
    u32 rtsoff = xhci_read32(handle, 0x1c);
    u32 ir_base = rtsoff + 0x20;

    xhci_write32(handle, 0x30, (u32)dcbaa_phys);
    xhci_write32(handle, 0x34, (u32)(dcbaa_phys >> 32));
    xhci_write32(handle, 0x38, max_slots);  /* CONFIG */

    u64 crcr = (cmd_phys & ~0x3FULL) | 1;
    xhci_write32(handle, 0x18, (u32)crcr);
    xhci_write32(handle, 0x1c, (u32)(crcr >> 32));

    xhci_write32(handle, ir_base, 256);  /* ERSTSZ */
    xhci_write32(handle, ir_base+0x08, (u32)evt_phys);  /* ERDP low */
    xhci_write32(handle, ir_base+0x0c, (u32)(evt_phys >> 32));  /* ERDP high */
    xhci_write32(handle, ir_base+0x10, (u32)erst_phys);  /* ERSTBA low */
    xhci_write32(handle, ir_base+0x14, (u32)(erst_phys >> 32));  /* ERSTBA high */
    xhci_write32(handle, ir_base+0x04, 0);  /* IMAN: no IRQs */

    /* Step 8: Run */
    printf("\nStarting controller...\n");
    cmd = xhci_read32(handle, 0x04);
    cmd |= 1;   /* RS */
    cmd &= ~4;  /* No INTE */
    xhci_write32(handle, 0x04, cmd);
    msleep(200);
    sts = xhci_read32(handle, 0x08);
    printf("USBSTS=0x%08x HCE=%d HCH=%d RS=%d\n", sts,
           !!(sts&(1<<12)), !!(sts&(1<<24)), !!(sts&1));

    /* Step 9: Enable slot */
    printf("\nEnable slot...\n");
    int slot_id = xhci_enable_slot(handle);
    printf("Slot enabled: %d\n", slot_id);

    if (slot_id <= 0 || slot_id > 64) {
        printf("Invalid slot ID\n");
        xhci_close(handle);
        return 1;
    }

    /* Step 10: Set up device context and address device */
    u64 dev_phys;
    void *dev_ctx = xhci_dma_alloc(handle, 1024, &dev_phys);
    memset(dev_ctx, 0, 1024);
    u32 *dsc = dev_ctx;
    dsc[0] = 0;  /* device address = 0 */
    dsc[1] = speed & 0xf;
    dsc[2] = 2;
    dsc[4] = found_port + 1;
    dcbaa[slot_id].ptr_low = (u32)dev_phys;
    dcbaa[slot_id].ptr_high = (u32)(dev_phys >> 32);
    fprintf(stderr, "DCBAA[%d]=0x%llx\n", slot_id, (unsigned long long)dev_phys);

    /* Address device */
    u64 in_phys;
    void *in_ctx = xhci_dma_alloc(handle, 4096, &in_phys);
    memset(in_ctx, 0, 4096);
    u32 *ic = in_ctx;
    ic[0] = (1<<0) | (1<<1);  /* slot + EP0 */
    u32 *isc = ic + 8;
    isc[0] = 0;  /* addr = 0 */
    isc[1] = speed & 0xf;
    isc[2] = 2;
    isc[4] = found_port + 1;
    u32 *epc = ic + 16;
    epc[0] = 1;  /* EP0 running */

    printf("Address device...\n");
    int ret = xhci_address_device(handle, slot_id, in_phys);
    printf("xhci_address_device returned: %d\n", ret);

    if (ret != 0) {
        printf("FAILED\n");
        xhci_dma_free(handle, in_ctx, 4096);
        xhci_close(handle);
        return 1;
    }

    printf("SUCCESS: Device addressed!\n");
    xhci_dma_free(handle, in_ctx, 4096);
    xhci_close(handle);
    return 0;
}
