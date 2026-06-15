#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include "xhci.h"
#include "xhci_regs.h"
#include "xhci_internal.h"

static void msleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void check(const char *step, void *h) {
    u32 sts = xhci_read32(h, 0x04);
    u32 cmd = xhci_read32(h, 0x00);
    u32 cfg = xhci_read32(h, 0x38);
    u32 crcr_l = xhci_read32(h, 0x18);
    u32 crcr_h = xhci_read32(h, 0x1C);
    u32 dcbaap_l = xhci_read32(h, 0x30);
    u32 dcbaap_h = xhci_read32(h, 0x34);
    printf("  %-25s: USBSTS=0x%08x USBCMD=0x%08x CFG=%d "
           "DCBAAP=0x%08x%08x CRCR=0x%08x%08x HCH=%d HCE=%d\n",
           step, sts, cmd, cfg, dcbaap_h, dcbaap_l, crcr_h, crcr_l,
           !!(sts & XHCI_STS_HCH), !!(sts & XHCI_STS_HCE));
    if (sts & XHCI_STS_HCE)
        fprintf(stderr, "    *** HCE at step: %s ***\n", step);
}

int main() {
    printf("=== HCE Step-by-Step Diagnostic ===\n");

    void *h = xhci_open("0000:05:00.1");
    if (!h) { fprintf(stderr, "open failed\n"); return 1; }

    printf("[1] After open:\n");
    check("open", h);

    printf("\n[2] After xhci_halt:\n");
    xhci_halt(h);
    check("halt", h);

    printf("\n[3] After xhci_reset (HCRST + wait CNR):\n");
    xhci_reset(h);
    check("reset", h);

    /* Read HCS params */
    struct xhci_caps *caps = xhci_get_caps(h);
    int max_slots = XHCI_HCS1_MAX_SLOTS(caps->hcsparams1);
    u32 hcc = xhci_read32(h, 0x0C);
    u32 hcs2 = xhci_read32(h, 0x10);
    printf("  max_slots=%d, HCC=0x%08x, HCS2=0x%08x\n", max_slots, hcc, hcs2);

    /* Step 4: Write CONFIG */
    printf("\n[4] Write CONFIG=%d:\n", max_slots);
    xhci_write32(h, 0x38, max_slots);
    msleep(5);
    check("after CONFIG write", h);

    /* Step 5: Write DCBAAP */
    printf("\n[5] Write DCBAAP:\n");
    u64 phys;
    void *buf = xhci_dma_alloc(h, 4096, &phys);
    if (!buf) { fprintf(stderr, "DMA alloc failed\n"); return 1; }
    memset(buf, 0, 4096);
    printf("  DMA buffer: vaddr=%p iova=0x%llx\n", buf, (unsigned long long)phys);
    xhci_write32(h, 0x30, (u32)phys);
    xhci_write32(h, 0x34, (u32)((u64)phys >> 32));
    msleep(5);
    check("after DCBAAP write", h);

    /* Step 6: Write CRCR */
    printf("\n[6] Write CRCR:\n");
    u64 crcr_phys;
    void *cmd_ring = xhci_dma_alloc(h, 4096, &crcr_phys);
    if (!cmd_ring) { fprintf(stderr, "CMD ring alloc failed\n"); return 1; }
    memset(cmd_ring, 0, 4096);
    printf("  CMD ring: vaddr=%p iova=0x%llx\n", cmd_ring, (unsigned long long)crcr_phys);
    u64 crcr_val = (crcr_phys & ~0x3FULL) | 1;
    xhci_write32(h, 0x18, (u32)crcr_val);
    xhci_write32(h, 0x1C, (u32)(crcr_val >> 32));
    msleep(5);
    check("after CRCR write", h);

    /* Step 7: Setup Event Ring */
    printf("\n[7] Setup Event Ring:\n");
    u64 evt_phys, seg_phys;
    void *evt_ring = xhci_dma_alloc(h, 4096, &evt_phys);
    void *evt_seg = xhci_dma_alloc(h, 4096, &seg_phys);
    if (!evt_ring || !evt_seg) { fprintf(stderr, "EVT alloc failed\n"); return 1; }
    memset(evt_ring, 0, 4096);
    memset(evt_seg, 0, 4096);

    struct xhci_er_seg *seg = evt_seg;
    seg->ptr_low = (u32)(u64)(uintptr_t)evt_ring;
    seg->ptr_high = (u32)((u64)(uintptr_t)evt_ring >> 32);
    seg->size = 256;

    u32 rtsoff = caps->rtsoff;
    u32 ir_off = rtsoff + 0x20;
    xhci_write32(h, ir_off + 0x00, 1);
    xhci_write32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);
    xhci_write32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));
    xhci_write32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);
    xhci_write32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));
    check("after EVT setup", h);

    /* Step 8: Allocate scratchpad */
    printf("\n[8] Scratchpad:\n");
    u32 sp_high = (hcs2 >> 21) & 0x1f;
    u32 sp_low  = (hcs2 >> 27) & 0x1f;
    u32 max_scratchpad = (sp_high << 5) | sp_low;
    if (max_scratchpad) max_scratchpad += 1;
    printf("  max_scratchpad=%d (sp_high=%d sp_low=%d)\n", max_scratchpad, sp_high, sp_low);

    if (max_scratchpad > 0) {
        u64 spba_phys;
        u64 *spba = xhci_dma_alloc(h, max_scratchpad * sizeof(u64), &spba_phys);
        if (!spba) { fprintf(stderr, "SPBA alloc failed\n"); return 1; }
        memset(spba, 0, max_scratchpad * sizeof(u64));

        struct usb_xhci_s *xhci = h;
        xhci->dcbaa[0].ptr_low = (u32)(spba_phys);
        xhci->dcbaa[0].ptr_high = (u32)(spba_phys >> 32);

        u64 pad_phys;
        void *pad = xhci_dma_alloc(h, XHCI_PAGE_SIZE * max_scratchpad, &pad_phys);
        if (!pad) { fprintf(stderr, "scratchpad alloc failed\n"); return 1; }
        memset(pad, 0, XHCI_PAGE_SIZE * max_scratchpad);

        for (u32 i = 0; i < max_scratchpad; i++)
            spba[i] = pad_phys + (i * XHCI_PAGE_SIZE);
        printf("  Scratchpad: %d buffers, spba_phys=0x%llx, pad_phys=0x%llx\n",
               max_scratchpad, (unsigned long long)spba_phys, (unsigned long long)pad_phys);
    }
    check("after scratchpad", h);

    /* Step 9: Enable interrupter */
    printf("\n[9] Enable interrupter (IMAN.IE):\n");
    xhci_write32(h, ir_off + 0x00, XHCI_IMAN_IE);
    check("after IMAN.IE", h);

    /* Step 10: Before RUN */
    printf("\n[10] Summary before RUN:\n");
    check("before_run", h);

    /* Step 11: RUN */
    printf("\n[11] Setting RS (Run/Stop)...\n");
    u32 cmd = xhci_read32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    xhci_write32(h, 0x00, cmd);
    printf("  Wrote USBCMD=0x%08x\n", cmd);

    for (int i = 0; i < 5; i++) {
        msleep(5);
        check("after_run", h);
        u32 sts = xhci_read32(h, 0x04);
        if (sts & XHCI_STS_HCE) break;
    }

    /* Now try: Run WITHOUT scratchpad setup to see if scratchpad is the trigger */
    printf("\n\n=== Test B: Run without scratchpad ===\n");
    xhci_halt(h);
    msleep(10);
    xhci_reset(h);
    check("after_reset_B", h);

    /* CONFIG + DCBAAP + CRCR + EVT only, NO scratchpad */
    xhci_write32(h, 0x38, max_slots);
    xhci_write32(h, 0x30, (u32)phys);
    xhci_write32(h, 0x34, (u32)((u64)phys >> 32));
    crcr_val = (crcr_phys & ~0x3FULL) | 1;
    xhci_write32(h, 0x18, (u32)crcr_val);
    xhci_write32(h, 0x1C, (u32)(crcr_val >> 32));
    xhci_write32(h, ir_off + 0x00, 1);
    xhci_write32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);
    xhci_write32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));
    xhci_write32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);
    xhci_write32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));
    xhci_write32(h, ir_off + 0x00, XHCI_IMAN_IE);

    /* Leave DCBAA[0] = 0 (no scratchpad) */
    struct usb_xhci_s *xhci = h;
    xhci->dcbaa[0].ptr_low = 0;
    xhci->dcbaa[0].ptr_high = 0;

    check("before_run_no_sp", h);

    cmd = xhci_read32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    xhci_write32(h, 0x00, cmd);
    msleep(10);
    check("after_run_no_sp", h);

    xhci_close(h);
    return 0;
}
