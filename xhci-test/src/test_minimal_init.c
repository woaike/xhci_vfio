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

static u32 r32(void *h, int off) { return xhci_read32(h, off); }
static void w32(void *h, int off, u32 v) { xhci_write32(h, off, v); }

static void dump(const char *s, void *h) {
    u32 sts = r32(h, 0x04);
    printf("  %-20s: USBSTS=0x%08x HCH=%d HCE=%d CNR=%d\n",
           s, sts, !!(sts&1), !!(sts&(1<<12)), !!(sts&(1<<11)));
}

int main() {
    printf("=== Minimal Init Test ===\n");

    void *h = xhci_open("0000:05:00.1");
    if (!h) { fprintf(stderr, "open failed\n"); return 1; }

    /* Fresh reset */
    xhci_reset(h);
    dump("after_reset", h);

    struct xhci_caps *caps = xhci_get_caps(h);
    int max_slots = XHCI_HCS1_MAX_SLOTS(caps->hcsparams1);
    u32 hcc = r32(h, 0x0C);
    u32 hcs2 = r32(h, 0x10);
    printf("  HCC=0x%08x SPB=%d HCS2=0x%08x\n", hcc, !!(hcc & (1<<26)), hcs2);

    /* Extract scratchpad count - try both interpretations */
    u32 sp_h_v1 = (hcs2 >> 21) & 0x1f;   /* 5-bit high */
    u32 sp_l_v1 = (hcs2 >> 27) & 0x1f;   /* 5-bit low */
    u32 sp_h_v2 = (hcs2 >> 21) & 0x1;    /* 1-bit high */
    u32 sp_l_v2 = (hcs2 >> 27) & 0xf;    /* 4-bit low */
    u32 count_v1 = ((sp_h_v1 << 5) | sp_l_v1) + 1;
    u32 count_v2 = ((sp_h_v2 << 4) | sp_l_v2) + 1;
    printf("  Scratchpad: v1=sp_high=%d sp_low=%d count=%d\n", sp_h_v1, sp_l_v1, count_v1);
    printf("              v2=sp_high=%d sp_low=%d count=%d\n", sp_h_v2, sp_l_v2, count_v2);
    printf("  64-bit addr: %d ctx32: %d\n", !!(hcc & 1), !!(hcc & (1<<27)));

    /* Test 1: CONFIG + DCBAAP only, no rings, no scratchpad */
    printf("\n--- Test 1: CONFIG + DCBAAP + RUN ---\n");
    u64 phys;
    void *buf = xhci_dma_alloc(h, 4096, &phys);
    memset(buf, 0, 4096);

    w32(h, 0x38, max_slots);
    w32(h, 0x30, (u32)phys);
    w32(h, 0x34, (u32)((u64)phys >> 32));
    dump("after_cfg_dcbaap", h);

    /* Run without any rings */
    u32 cmd = r32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    w32(h, 0x00, cmd);
    msleep(50);
    dump("after_run_no_rings", h);

    /* Halt and reset for next test */
    xhci_reset(h);
    dump("after_reset2", h);

    /* Test 2: CONFIG + DCBAAP + CRCR only */
    printf("\n--- Test 2: + CRCR (cmd ring) ---\n");
    u64 crcr_phys;
    void *cmd_ring = xhci_dma_alloc(h, 4096, &crcr_phys);
    memset(cmd_ring, 0, 4096);
    printf("  cmd_ring phys=0x%llx\n", (unsigned long long)crcr_phys);

    w32(h, 0x38, max_slots);
    w32(h, 0x30, (u32)phys);
    w32(h, 0x34, (u32)((u64)phys >> 32));
    u64 crcr = (crcr_phys & ~0x3FULL) | 1;
    w32(h, 0x18, (u32)crcr);
    w32(h, 0x1C, (u32)(crcr >> 32));
    dump("after_crcr", h);

    cmd = r32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    w32(h, 0x00, cmd);
    msleep(50);
    dump("after_run_crcr", h);

    /* Halt and reset */
    xhci_reset(h);
    dump("after_reset3", h);

    /* Test 3: + Event Ring */
    printf("\n--- Test 3: + Event Ring ---\n");
    u64 evt_phys, seg_phys;
    void *evt_ring = xhci_dma_alloc(h, 4096, &evt_phys);
    void *evt_seg = xhci_dma_alloc(h, 4096, &seg_phys);
    memset(evt_ring, 0, 4096);
    memset(evt_seg, 0, 4096);
    printf("  evt_ring phys=0x%llx evt_seg phys=0x%llx\n",
           (unsigned long long)evt_phys, (unsigned long long)seg_phys);

    struct xhci_er_seg *seg = evt_seg;
    seg->ptr_low = (u32)(u64)(uintptr_t)evt_ring;
    seg->ptr_high = (u32)((u64)(uintptr_t)evt_ring >> 32);
    seg->size = 256;

    u32 rtsoff = caps->rtsoff;
    u32 ir_off = rtsoff + 0x20;

    w32(h, 0x38, max_slots);
    w32(h, 0x30, (u32)phys);
    w32(h, 0x34, (u32)((u64)phys >> 32));
    crcr = (crcr_phys & ~0x3FULL) | 1;
    w32(h, 0x18, (u32)crcr);
    w32(h, 0x1C, (u32)(crcr >> 32));
    w32(h, ir_off + 0x00, 1);
    w32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);
    w32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));
    w32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);
    w32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));
    w32(h, ir_off + 0x00, XHCI_IMAN_IE);
    dump("after_evt", h);

    cmd = r32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    w32(h, 0x00, cmd);
    msleep(50);
    dump("after_run_evt", h);

    /* Halt and reset */
    xhci_reset(h);
    dump("after_reset4", h);

    /* Test 4: + Scratchpad (v1 count: 289 buffers) */
    printf("\n--- Test 4: + Scratchpad (%d buffers, v1) ---\n", count_v1);
    w32(h, 0x38, max_slots);
    w32(h, 0x30, (u32)phys);
    w32(h, 0x34, (u32)((u64)phys >> 32));
    crcr = (crcr_phys & ~0x3FULL) | 1;
    w32(h, 0x18, (u32)crcr);
    w32(h, 0x1C, (u32)(crcr >> 32));
    w32(h, ir_off + 0x00, 1);
    w32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);
    w32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));
    w32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);
    w32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));
    w32(h, ir_off + 0x00, XHCI_IMAN_IE);

    /* Allocate scratchpad - v1 count */
    u64 spba_phys;
    u64 *spba = xhci_dma_alloc(h, count_v1 * sizeof(u64), &spba_phys);
    memset(spba, 0, count_v1 * sizeof(u64));

    struct usb_xhci_s *xhci = h;
    xhci->dcbaa[0].ptr_low = (u32)(spba_phys);
    xhci->dcbaa[0].ptr_high = (u32)(spba_phys >> 32);

    u64 pad_phys;
    void *pad = xhci_dma_alloc(h, XHCI_PAGE_SIZE * count_v1, &pad_phys);
    memset(pad, 0, XHCI_PAGE_SIZE * count_v1);
    for (u32 i = 0; i < count_v1; i++)
        spba[i] = pad_phys + (i * XHCI_PAGE_SIZE);

    dump("after_sp_v1", h);
    printf("  spba_phys=0x%llx pad_phys=0x%llx\n",
           (unsigned long long)spba_phys, (unsigned long long)pad_phys);

    cmd = r32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    w32(h, 0x00, cmd);
    msleep(50);
    dump("after_run_sp_v1", h);

    /* Halt and reset */
    xhci_reset(h);
    dump("after_reset5", h);

    /* Test 5: v2 count (small) */
    printf("\n--- Test 5: + Scratchpad (%d buffers, v2) ---\n", count_v2);
    w32(h, 0x38, max_slots);
    w32(h, 0x30, (u32)phys);
    w32(h, 0x34, (u32)((u64)phys >> 32));
    crcr = (crcr_phys & ~0x3FULL) | 1;
    w32(h, 0x18, (u32)crcr);
    w32(h, 0x1C, (u32)(crcr >> 32));
    w32(h, ir_off + 0x00, 1);
    w32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);
    w32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));
    w32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);
    w32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));
    w32(h, ir_off + 0x00, XHCI_IMAN_IE);

    spba = xhci_dma_alloc(h, count_v2 * sizeof(u64), &spba_phys);
    memset(spba, 0, count_v2 * sizeof(u64));
    xhci->dcbaa[0].ptr_low = (u32)(spba_phys);
    xhci->dcbaa[0].ptr_high = (u32)(spba_phys >> 32);

    pad = xhci_dma_alloc(h, XHCI_PAGE_SIZE * count_v2, &pad_phys);
    memset(pad, 0, XHCI_PAGE_SIZE * count_v2);
    for (u32 i = 0; i < count_v2; i++)
        spba[i] = pad_phys + (i * XHCI_PAGE_SIZE);

    dump("after_sp_v2", h);

    cmd = r32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    w32(h, 0x00, cmd);
    msleep(50);
    dump("after_run_sp_v2", h);

    xhci_close(h);
    return 0;
}
