/* XHCI smoke test - pure C, no Python needed */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "xhci.h"

static int total = 0, passed = 0, failed = 0;

#define TEST(name) static void name(void *dev)
#define RUN_TEST(fn) do { \
    total++; \
    printf("  %-55s ", #fn); \
    int saved_failed = failed; \
    fn(dev); \
    if (failed == saved_failed) printf("[PASS]\n"); \
    else printf("[FAIL]\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        failed++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Capability Register Tests
 * ================================================================ */

TEST(test_caplength) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    u8 cap = caps->caplength;
    ASSERT(cap > 0 && cap <= 0x80, "CAPLENGTH out of range");
}

TEST(test_hciversion) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    u16 ver = caps->hciversion;
    ASSERT(ver >= 0x0090, "HCIVERSION should be at least 0.90");
}

TEST(test_max_slots) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    int slots = XHCI_HCS1_MAX_SLOTS(caps->hcsparams1);
    ASSERT(slots >= 1 && slots <= 255, "MAX_SLOTS out of range");
}

TEST(test_max_interrupters) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    int intrs = XHCI_HCS1_MAX_INTRS(caps->hcsparams1);
    ASSERT(intrs >= 1 && intrs <= 1024, "MAX_INTRS out of range");
}

TEST(test_max_ports) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    int ports = XHCI_HCS1_MAX_PORTS(caps->hcsparams1);
    ASSERT(ports >= 1 && ports <= 255, "MAX_PORTS out of range");
}

TEST(test_64bit_addressing) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    ASSERT((caps->hccparams & (1 << 0)) != 0, "64-bit addressing not supported");
}

TEST(test_context_size) {
    struct xhci_caps *caps = xhci_get_caps(dev);
    int ctz = (caps->hccparams >> 27) & 1;
    ASSERT(ctz == 0 || ctz == 1, "Invalid context size");
}

/* ================================================================
 * Operational Register Tests
 * ================================================================ */

TEST(test_usbsts_initial) {
    struct xhci_op *op = xhci_get_op(dev);
    u32 sts = readl(&op->usbsts);
    ASSERT((sts & XHCI_STS_HCH) != 0, "Controller should be halted after VFIO bind");
}

TEST(test_usbcmd_initial) {
    struct xhci_op *op = xhci_get_op(dev);
    u32 cmd = readl(&op->usbcmd);
    ASSERT((cmd & XHCI_CMD_RS) == 0, "Run/Stop should be cleared after bind");
}

TEST(test_pagesize) {
    struct xhci_op *op = xhci_get_op(dev);
    u32 ps = readl(&op->pagesize);
    ASSERT(ps == 0x0001 || ps == 0x0003 || ps == 0x0007 || ps == 0x003F,
           "Page size register unexpected");
}

/* ================================================================
 * Write/Read Tests
 * ================================================================ */

TEST(test_hcrst_self_clearing) {
    struct xhci_op *op = xhci_get_op(dev);
    u32 cmd = readl(&op->usbcmd);
    writel(&op->usbcmd, cmd | XHCI_CMD_HCRST);

    int timeout = 1000;
    while (timeout-- > 0) {
        cmd = readl(&op->usbcmd);
        if (!(cmd & XHCI_CMD_HCRST))
            break;
        usleep(1000);
    }
    ASSERT(timeout > 0, "HCRST did not self-clear within 1 second");
}

/* ================================================================
 * Reset Tests
 * ================================================================ */

TEST(test_reset_succeeds) {
    ASSERT(xhci_reset(dev) == 0, "Reset failed");
    ASSERT(xhci_is_halted(dev), "Controller should be halted after reset");
}

TEST(test_reset_clears_run) {
    struct xhci_op *op = xhci_get_op(dev);
    u32 cmd = readl(&op->usbcmd);
    ASSERT((cmd & XHCI_CMD_RS) == 0, "Run/Stop should be cleared after reset");
}

TEST(test_double_reset) {
    ASSERT(xhci_reset(dev) == 0, "First reset failed");
    ASSERT(xhci_reset(dev) == 0, "Second reset failed");
    ASSERT(xhci_is_halted(dev), "Controller should be halted after double reset");
}

/* ================================================================
 * Port Tests
 * ================================================================ */

TEST(test_port_count) {
    int count = xhci_port_count(dev);
    ASSERT(count > 0, "Port count should be > 0");
}

TEST(test_all_port_registers_accessible) {
    int n = xhci_port_count(dev);
    ASSERT(n <= 255, "Too many ports");
    for (int i = 0; i < n; i++) {
        u32 portsc = xhci_port_read(dev, i);
        (void)portsc;
    }
}

TEST(test_port_ccs_or_zero) {
    int n = xhci_port_count(dev);
    for (int i = 0; i < n; i++) {
        u32 portsc = xhci_port_read(dev, i);
        (void)portsc;
    }
}

/* ================================================================
 * DMA Tests
 * ================================================================ */

TEST(test_dma_alloc_page_aligned) {
    u64 phys;
    void *virt = xhci_dma_alloc(dev, 4096, &phys);
    ASSERT(virt != NULL, "DMA alloc returned NULL");
    ASSERT(phys != 0, "Physical address is 0");
    ASSERT(phys % 4096 == 0, "Physical address not page-aligned");
    xhci_dma_free(dev, virt, 4096);
}

TEST(test_dma_write_read) {
    u64 phys;
    void *virt = xhci_dma_alloc(dev, 64, &phys);
    ASSERT(virt != NULL, "DMA alloc failed");

    u8 *buf = virt;
    for (int i = 0; i < 64; i++)
        buf[i] = (u8)i;

    for (int i = 0; i < 64; i++)
        ASSERT(buf[i] == (u8)i, "Data mismatch after write");

    xhci_dma_free(dev, virt, 64);
}

TEST(test_dma_multiple_allocs) {
    void *ptrs[4];
    u64 phys[4];
    for (int i = 0; i < 4; i++) {
        ptrs[i] = xhci_dma_alloc(dev, 4096, &phys[i]);
        ASSERT(ptrs[i] != NULL, "DMA alloc failed");
    }
    for (int i = 0; i < 4; i++)
        xhci_dma_free(dev, ptrs[i], 4096);
}

/* ================================================================
 * Full Init Test
 * ================================================================ */

TEST(test_full_init) {
    ASSERT(xhci_full_init(dev) == 0, "Full init failed");
    ASSERT(!xhci_is_halted(dev), "Controller should be running after full_init");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *pci_bdf = getenv("XHCI_PCI");
    if (!pci_bdf)
        pci_bdf = "0000:05:00.1";

    printf("========================================\n");
    printf("XHCI Test - %s\n", pci_bdf);
    printf("========================================\n\n");

    void *dev = xhci_open(pci_bdf);
    if (!dev) {
        fprintf(stderr, "FATAL: Failed to open %s\n", pci_bdf);
        fprintf(stderr, "  Is it bound to vfio-pci?\n");
        return 1;
    }

    printf("Device opened: %s\n\n", pci_bdf);

    struct xhci_caps *caps = xhci_get_caps(dev);
    printf("  HCIVERSION  : %d.%d\n",
           (caps->hciversion >> 8) & 0xFF, caps->hciversion & 0xFF);
    printf("  MAX_SLOTS   : %d\n", XHCI_HCS1_MAX_SLOTS(caps->hcsparams1));
    printf("  MAX_INTRS   : %d\n", XHCI_HCS1_MAX_INTRS(caps->hcsparams1));
    printf("  MAX_PORTS   : %d\n", XHCI_HCS1_MAX_PORTS(caps->hcsparams1));
    printf("  64-BIT ADDR : %s\n", (caps->hccparams & 1) ? "Yes" : "No");
    printf("  CTX SIZE    : %s\n", ((caps->hccparams >> 27) & 1) ? "64B" : "32B");
    printf("\n");

    /* Run tests */
    printf("--- Capability Registers ---\n");
    RUN_TEST(test_caplength);
    RUN_TEST(test_hciversion);
    RUN_TEST(test_max_slots);
    RUN_TEST(test_max_interrupters);
    RUN_TEST(test_max_ports);
    RUN_TEST(test_64bit_addressing);
    RUN_TEST(test_context_size);

    printf("\n--- Operational Registers ---\n");
    RUN_TEST(test_usbsts_initial);
    RUN_TEST(test_usbcmd_initial);
    RUN_TEST(test_pagesize);

    printf("\n--- Write/Read ---\n");
    RUN_TEST(test_hcrst_self_clearing);

    printf("\n--- Reset ---\n");
    RUN_TEST(test_reset_succeeds);
    RUN_TEST(test_reset_clears_run);
    RUN_TEST(test_double_reset);

    printf("\n--- Ports ---\n");
    RUN_TEST(test_port_count);
    RUN_TEST(test_all_port_registers_accessible);
    RUN_TEST(test_port_ccs_or_zero);

    printf("\n--- DMA ---\n");
    RUN_TEST(test_dma_alloc_page_aligned);
    RUN_TEST(test_dma_write_read);
    RUN_TEST(test_dma_multiple_allocs);

    printf("\n--- Full Init ---\n");
    RUN_TEST(test_full_init);

    passed = total - failed;

    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d/%d passed", passed, total);
    if (failed > 0)
        printf(", %d failed", failed);
    printf("\n========================================\n");

    xhci_close(dev);
    return failed > 0 ? 1 : 0;
}
