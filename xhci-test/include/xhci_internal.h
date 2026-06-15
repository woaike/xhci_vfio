#ifndef XHCI_INTERNAL_H
#define XHCI_INTERNAL_H

#include "xhci.h"
#include <sys/mman.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_DMA_REGIONS 64

// Ring capacity — aligned with SeaBIOS approach
#define XHCI_RING_ITEMS      256

// --------------------------------------------------------------
// Debug logging (controlled by XHCI_DEBUG env var)
//   0 = errors only (default)
//   1 = + commands/slots
//   2 = + ports/rings
//   3 = + TRB details

extern int xhci_debug_level;

void xhci_debug_init(void);
const char *trb_type_name(u32 type);
const char *cc_name_safe(u32 cc);

#define DPRINTF(level, fmt, ...) \
    do { \
        if (xhci_debug_level >= (level)) \
            fprintf(stderr, "xhci: " fmt "\n", ##__VA_ARGS__); \
    } while (0)

// Unified ring struct — matches SeaBIOS struct xhci_ring
struct xhci_ring {
    struct xhci_trb  *ring;       // allocated ring buffer
    u64               ring_phys;
    int               size;       // number of TRBs
    struct xhci_trb  evt;         // completion event (SeaBIOS pattern)
    int               eidx;
    int               nidx;
    int               cs;
};

struct usb_xhci_s {
    int container_fd;
    int group_fd;
    int vfio_fd;
    int irq_efd;
    int noiommu_mode;  // 1 if using VFIO_NOIOMMU_IOMMU

    // Register pointers (computed from mmap'd BAR0)
    struct xhci_caps *caps;
    struct xhci_op   *op;
    struct xhci_pr   *pr;
    struct xhci_db   *db;
    struct xhci_ir   *ir;

    void *bar0;
    size_t bar0_size;

    // DMA tracking
    void *dma_vaddrs[MAX_DMA_REGIONS];
    u64  dma_iovas[MAX_DMA_REGIONS];   // IOVA (may differ from vaddr)
    size_t dma_sizes[MAX_DMA_REGIONS];
    int dma_count;

    // DCBAA
    struct xhci_devlist *dcbaa;
    u64 dcbaa_phys;

    // Rings (unified struct xhci_ring)
    struct xhci_ring *cmds;   // command ring
    struct xhci_ring *evts;   // event ring

    // Event ring segment table
    struct xhci_er_seg *evt_seg;
    u64 evt_seg_phys;

    // IOVA allocator — low addresses that controllers accept
    u64 next_iova;

    // Controller capabilities
    int max_slots;
    int max_ports;
};

struct xhci_devlist *xhci_get_dcbaa(void *handle, u64 *phys_out);
void xhci_pci_cmd_enable(void *handle);
int xhci_pci_flr(void *handle);
#endif /* XHCI_INTERNAL_H */
