/* Minimal diagnostic: step through full_init manually to isolate HCE trigger */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include "xhci.h"
#include "xhci_regs.h"

static void msleep(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void
check(const char *step, void *h)
{
    u32 sts = xhci_read32(h, 0x04);  /* USBSTS */
    u32 cmd = xhci_read32(h, 0x00);  /* USBCMD */
    u32 cfg = xhci_read32(h, 0x38);  /* CONFIG */
    u32 crcr_l = xhci_read32(h, 0x18);
    u32 crcr_h = xhci_read32(h, 0x1C);
    u32 dcbaap_l = xhci_read32(h, 0x30);
    u32 dcbaap_h = xhci_read32(h, 0x34);
    int hce = !!(sts & XHCI_STS_HCE);
    int hch = !!(sts & XHCI_STS_HCH);
    printf("  %s: USBSTS=0x%08x USBCMD=0x%08x CONFIG=%d HCE=%d HCH=%d\n"
           "    DCBAAP=0x%08x%08x CRCR=0x%08x%08x\n",
           step, sts, cmd, cfg, hce, hch,
           dcbaap_h, dcbaap_l, crcr_h, crcr_l);
    if (hce) {
        fprintf(stderr, "  *** HCE detected at step: %s ***\n", step);
    }
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;

    const char *pci = getenv("XHCI_PCI");
    if (!pci) pci = "0000:05:00.1";

    printf("=== HCE Diagnostic Test ===\n");
    printf("Device: %s\n\n", pci);

    void *h = xhci_open(pci);
    if (!h) {
        fprintf(stderr, "Failed to open device\n");
        return 1;
    }

    printf("[1] After open:\n");
    check("open", h);

    printf("\n[2] After halt:\n");
    if (xhci_halt(h) < 0) { fprintf(stderr, "halt failed\n"); return 1; }
    check("halt", h);

    printf("\n[3] Asserting HCRST...\n");
    xhci_write32(h, 0x00, XHCI_CMD_HCRST);
    /* Wait for HCRST to self-clear */
    int timeout = 1000;
    while (timeout-- > 0) {
        u32 cmd = xhci_read32(h, 0x00);
        if (!(cmd & XHCI_CMD_HCRST)) break;
        msleep(1);
    }
    if (timeout <= 0) { fprintf(stderr, "HCRST timeout\n"); return 1; }
    /* Wait for CNR to clear */
    timeout = 5000;
    while (timeout-- > 0) {
        u32 sts = xhci_read32(h, 0x04);
        if (!(sts & XHCI_STS_CNR)) break;
        msleep(1);
    }
    if (timeout <= 0) { fprintf(stderr, "CNR timeout\n"); return 1; }
    check("reset", h);

    printf("\n[4] Set CONFIG (SeaBIOS order: CONFIG first)...\n");
    /* Read from caps struct to get HCSParams1 */
    struct xhci_caps *caps = xhci_get_caps(h);
    int max_slots = XHCI_HCS1_MAX_SLOTS(caps->hcsparams1);
    int max_ports = XHCI_HCS1_MAX_PORTS(caps->hcsparams1);
    printf("    max_slots=%d max_ports=%d\n", max_slots, max_ports);
    xhci_write32(h, 0x38, max_slots);  /* CONFIG */
    check("config", h);

    printf("\n[5] Set DCBAAP...\n");
    /* We need to get the DCBAAP phys from the internal xhci struct.
       Since we can't access it directly, allocate a test buffer. */
    u64 test_phys;
    void *test_buf = xhci_dma_alloc(h, 4096, &test_phys);
    if (!test_buf) { fprintf(stderr, "DMA alloc failed\n"); return 1; }
    memset(test_buf, 0, 4096);
    printf("    DMA: virt=%p phys=0x%llx\n", test_buf, (unsigned long long)test_phys);
    xhci_write32(h, 0x30, (u32)test_phys);
    xhci_write32(h, 0x34, (u32)((u64)test_phys >> 32));
    check("dcbaap", h);

    printf("\n[6] Set CRCR...\n");
    void *cmd_ring = xhci_dma_alloc(h, 4096, &test_phys);
    if (!cmd_ring) { fprintf(stderr, "CMD ring DMA alloc failed\n"); return 1; }
    memset(cmd_ring, 0, 4096);
    printf("    CMD ring: virt=%p phys=0x%llx\n", cmd_ring, (unsigned long long)test_phys);
    u64 crcr = (test_phys & ~0x3FULL) | 1;  /* base | RCS=1 */
    xhci_write32(h, 0x18, (u32)crcr);
    xhci_write32(h, 0x1C, (u32)(crcr >> 32));
    check("crcr", h);

    printf("\n[7] Set Event Ring (SeaBIOS order: ERSTSZ, ERDP, ERSTBA)...\n");
    void *evt_ring = xhci_dma_alloc(h, 4096, &test_phys);
    if (!evt_ring) { fprintf(stderr, "EVT ring DMA alloc failed\n"); return 1; }
    memset(evt_ring, 0, 4096);
    printf("    EVT ring: virt=%p phys=0x%llx\n", evt_ring, (unsigned long long)test_phys);

    void *evt_seg = xhci_dma_alloc(h, 4096, &test_phys);
    if (!evt_seg) { fprintf(stderr, "EVT seg DMA alloc failed\n"); return 1; }
    memset(evt_seg, 0, 4096);
    printf("    EVT seg:  virt=%p phys=0x%llx\n", evt_seg, (unsigned long long)test_phys);

    /* Set up ERST entry */
    struct xhci_er_seg *seg = evt_seg;
    seg->ptr_low = (u32)(u64)(uintptr_t)evt_ring;
    seg->ptr_high = (u32)((u64)(uintptr_t)evt_ring >> 32);
    seg->size = 256;

    u32 rtsoff = caps->rtsoff;
    u32 ir_off = rtsoff + 0x20;

    /* SeaBIOS order: ERSTSZ, ERDP, ERSTBA */
    xhci_write32(h, ir_off + 0x00, 1);  /* ERSTSZ */
    xhci_write32(h, ir_off + 0x10, (u32)(u64)(uintptr_t)evt_ring);     /* ERDP low */
    xhci_write32(h, ir_off + 0x14, (u32)((u64)(uintptr_t)evt_ring >> 32));  /* ERDP high */
    xhci_write32(h, ir_off + 0x08, (u32)(u64)(uintptr_t)evt_seg);     /* ERSTBA low */
    xhci_write32(h, ir_off + 0x0C, (u32)((u64)(uintptr_t)evt_seg >> 32));  /* ERSTBA high */
    check("evt_ring", h);

    printf("\n[8] Before RUN - dumping all key registers:\n");
    check("before_run", h);

    printf("\n[9] Setting RS (Run/Stop)...\n");
    u32 cmd = xhci_read32(h, 0x00);
    cmd |= XHCI_CMD_RS;
    xhci_write32(h, 0x00, cmd);
    printf("    Wrote USBCMD=0x%08x\n", cmd);

    msleep(1);
    check("immediately_after_run", h);

    msleep(10);
    check("10ms_after_run", h);

    printf("\n[10] Attempting to wait for HCH=0...\n");
    timeout = 2000;
    while (timeout-- > 0) {
        u32 sts = xhci_read32(h, 0x04);
        if (!(sts & XHCI_STS_HCH)) {
            printf("    Controller came out of halt after %d ms\n", 2000 - timeout);
            check("running", h);
            break;
        }
        msleep(1);
    }
    if (timeout <= 0) {
        printf("    Controller never came out of halt!\n");
        check("still_halted", h);
    }

    xhci_dma_free(h, evt_seg, 4096);
    xhci_dma_free(h, evt_ring, 4096);
    xhci_dma_free(h, cmd_ring, 4096);
    xhci_dma_free(h, test_buf, 4096);
    xhci_close(h);
    return 0;
}
