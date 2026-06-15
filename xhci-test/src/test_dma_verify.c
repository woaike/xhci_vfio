/* Verify that DMA through VFIO TYPE1 IOMMU actually works for the XHCI controller */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/vfio.h>
#include <errno.h>
#include <stdint.h>
#include "xhci.h"
#include "xhci_regs.h"
#include "xhci_internal.h"

static void msleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main() {
    printf("=== DMA Verification Test ===\n");

    void *h = xhci_open("0000:05:00.1");
    if (!h) { fprintf(stderr, "xhci_open failed\n"); return 1; }

    struct usb_xhci_s *xhci = h;
    printf("noiommu_mode = %d\n", xhci->noiommu_mode);

    // Allocate DMA buffer
    u64 phys;
    void *buf = xhci_dma_alloc(h, 4096, &phys);
    if (!buf) { fprintf(stderr, "dma alloc failed\n"); xhci_close(h); return 1; }
    printf("DMA: vaddr=%p iova=0x%llx\n", buf, (unsigned long long)phys);

    // Write a pattern via CPU
    memset(buf, 0, 4096);
    volatile u32 *p = buf;
    p[0] = 0xAAAAAAAA;
    p[1] = 0x55555555;
    p[2] = 0xDEADBEEF;

    // Verify CPU can read it back
    printf("CPU readback: p[0]=0x%08x p[1]=0x%08x p[2]=0x%08x\n", p[0], p[1], p[2]);

    // Now halt the controller
    xhci_halt(h);
    msleep(10);

    // Write CONFIG and DCBAAP
    struct xhci_caps *caps = xhci_get_caps(h);
    int max_slots = XHCI_HCS1_MAX_SLOTS(caps->hcsparams1);
    xhci_write32(h, 0x38, max_slots);
    printf("CONFIG = %d\n", xhci_read32(h, 0x38));

    // Set DCBAAP to our DMA buffer
    xhci_write32(h, 0x30, (u32)phys);
    xhci_write32(h, 0x34, (u32)((u64)phys >> 32));
    printf("DCBAAP = 0x%08x%08x\n", xhci_read32(h, 0x34), xhci_read32(h, 0x30));

    // Now set CRCR to our DMA buffer (command ring base)
    u64 crcr_val = (phys & ~0x3FULL) | 1;  // RCS=1
    xhci_write32(h, 0x18, (u32)crcr_val);
    xhci_write32(h, 0x1C, (u32)(crcr_val >> 32));
    printf("CRCR written = 0x%llx\n", (unsigned long long)crcr_val);
    printf("CRCR readback = 0x%08x%08x\n", xhci_read32(h, 0x1C), xhci_read32(h, 0x18));

    // Set up event ring
    u64 evt_phys;
    void *evt_buf = xhci_dma_alloc(h, 4096, &evt_phys);
    if (!evt_buf) { fprintf(stderr, "evt dma alloc failed\n"); xhci_close(h); return 1; }
    memset(evt_buf, 0, 4096);

    u64 seg_phys;
    void *seg_buf = xhci_dma_alloc(h, 4096, &seg_phys);
    if (!seg_buf) { fprintf(stderr, "seg dma alloc failed\n"); xhci_close(h); return 1; }
    memset(seg_buf, 0, 4096);

    // Set up ERST entry
    struct xhci_er_seg *seg = seg_buf;
    seg->ptr_low = (u32)(u64)(uintptr_t)evt_buf;
    seg->ptr_high = (u32)((u64)(uintptr_t)evt_buf >> 32);
    seg->size = 256;

    u32 rtsoff = caps->rtsoff;
    u32 ir_off = rtsoff + 0x20;
    xhci_write32(h, ir_off + 0x00, 1);
    xhci_write32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_buf);
    xhci_write32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_buf >> 32));
    xhci_write32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)seg_buf);
    xhci_write32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)seg_buf >> 32));

    printf("\nKey registers before RUN:\n");
    printf("  USBSTS = 0x%08x\n", xhci_read32(h, 0x04));
    printf("  USBCMD = 0x%08x\n", xhci_read32(h, 0x00));
    printf("  CONFIG = %d\n", xhci_read32(h, 0x38));
    printf("  DCBAAP = 0x%08x%08x\n", xhci_read32(h, 0x34), xhci_read32(h, 0x30));
    printf("  CRCR   = 0x%08x%08x\n", xhci_read32(h, 0x1C), xhci_read32(h, 0x18));
    printf("  ERSTSZ = 0x%08x\n", xhci_read32(h, ir_off + 0x00));
    printf("  ERSTBA = 0x%08x%08x\n", xhci_read32(h, ir_off + 0x0C), xhci_read32(h, ir_off + 0x08));
    printf("  ERDP   = 0x%08x%08x\n", xhci_read32(h, ir_off + 0x14), xhci_read32(h, ir_off + 0x10));
    printf("  HCC    = 0x%08x\n", xhci_read32(h, 0x0C));

    // Now run
    printf("\nSetting RS...\n");
    u32 cmd = xhci_read32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    xhci_write32(h, 0x00, cmd);

    msleep(5);
    u32 sts = xhci_read32(h, 0x04);
    printf("After RUN: USBSTS = 0x%08x (HCE=%d HCH=%d RS=%d)\n",
           sts, !!(sts&XHCI_STS_HCE), !!(sts&XHCI_STS_HCH), !!(sts&XHCI_CMD_RS));

    // Try different approach: run WITHOUT CRCR set
    printf("\n--- Test 2: Run without CRCR ---\n");
    xhci_halt(h);
    msleep(10);
    xhci_write32(h, 0x18, 0);  // CRCR = 0
    xhci_write32(h, 0x1C, 0);
    cmd = xhci_read32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    xhci_write32(h, 0x00, cmd);
    msleep(5);
    sts = xhci_read32(h, 0x04);
    printf("Without CRCR: USBSTS = 0x%08x (HCE=%d HCH=%d)\n",
           sts, !!(sts&XHCI_STS_HCE), !!(sts&XHCI_STS_HCH));

    xhci_close(h);
    return 0;
}
