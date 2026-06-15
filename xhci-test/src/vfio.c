// VFIO device handling for XHCI controllers.
//
// Opens a PCI device bound to vfio-pci, mmaps BAR0 for register access,
// sets up eventfd IRQ delivery, and manages DMA regions.

#include "xhci_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <linux/vfio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

// Get physical address from virtual address via /proc/self/pagemap.
// Required when IOMMU is in identity/passthrough mode.
// Returns 0 on failure, physical address on success.
static u64
virt_to_phys(void *vaddr)
{
    u64 pfn;
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open /proc/self/pagemap");
        return 0;
    }

    off_t off = ((uintptr_t)vaddr / 4096) * 8;
    if (lseek(fd, off, SEEK_SET) != off) {
        perror("pagemap lseek");
        close(fd);
        return 0;
    }

    if (read(fd, &pfn, sizeof(pfn)) != sizeof(pfn)) {
        perror("pagemap read");
        close(fd);
        return 0;
    }
    close(fd);

    // Bit 63 = present, bits 0-54 = PFN
    if (!(pfn & (1ULL << 63)))
        return 0;  // page not present

    u64 pfn_val = pfn & ((1ULL << 54) - 1);
    if (pfn_val == 0)
        return 0;  // PFN hidden by kernel security (no CAP_SYS_ADMIN)

    return (pfn_val * 4096) + ((uintptr_t)vaddr % 4096);
}

static void *xhci_dma_alloc_internal(struct usb_xhci_s *xhci, size_t size, u64 *phys);
static void xhci_dma_free_internal(struct usb_xhci_s *xhci, void *virt, size_t size);

/****************************************************************
 * VFIO helpers
 ****************************************************************/

static int
vfio_get_group_nr(const char *pci_bdf)
{
    char path[256];
    char link[256];
    ssize_t len;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/iommu_group", pci_bdf);
    len = readlink(path, link, sizeof(link) - 1);
    if (len < 0) {
        perror("readlink iommu_group");
        return -1;
    }
    link[len] = '\0';

    char *slash = strrchr(link, '/');
    if (!slash)
        return -1;
    return atoi(slash + 1);
}

static int
vfio_setup_irqs(struct usb_xhci_s *xhci)
{
    int irq_indices[] = {
        VFIO_PCI_INTX_IRQ_INDEX,
        VFIO_PCI_MSI_IRQ_INDEX,
        VFIO_PCI_MSIX_IRQ_INDEX,
    };

    for (int i = 0; i < 3; i++) {
        struct vfio_irq_info irq_info = {
            .argsz = sizeof(irq_info),
            .index = irq_indices[i],
        };

        int ret = ioctl(xhci->vfio_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info);
        if (ret < 0)
            continue;

        if (!(irq_info.flags & VFIO_IRQ_INFO_EVENTFD))
            continue;

        xhci->irq_efd = eventfd(0, EFD_NONBLOCK);
        if (xhci->irq_efd < 0) {
            perror("eventfd");
            return -1;
        }

        int fd = xhci->irq_efd;
        size_t data_size = sizeof(struct vfio_irq_set) + sizeof(fd);
        struct vfio_irq_set *irq_set = calloc(1, data_size);
        if (!irq_set) {
            close(xhci->irq_efd);
            xhci->irq_efd = -1;
            return -1;
        }

        irq_set->argsz = data_size;
        irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
        irq_set->index = irq_indices[i];
        irq_set->start = 0;
        irq_set->count = 1;
        memcpy(irq_set->data, &fd, sizeof(fd));

        ret = ioctl(xhci->vfio_fd, VFIO_DEVICE_SET_IRQS, irq_set);
        free(irq_set);
        if (ret < 0) {
            perror("VFIO_DEVICE_SET_IRQS");
            close(xhci->irq_efd);
            xhci->irq_efd = -1;
            return -1;
        }

        return 0;
    }

    fprintf(stderr, "No IRQ available on VFIO device\n");
    return -1;
}

static int
vfio_map_bar(struct usb_xhci_s *xhci)
{
    struct vfio_region_info reg = {
        .argsz = sizeof(reg),
        .index = VFIO_PCI_BAR0_REGION_INDEX,
    };

    int ret = ioctl(xhci->vfio_fd, VFIO_DEVICE_GET_REGION_INFO, &reg);
    if (ret < 0) {
        perror("VFIO_DEVICE_GET_REGION_INFO BAR0");
        return -1;
    }

    xhci->bar0_size = reg.size;
    xhci->bar0 = mmap(NULL, reg.size,
                      PROT_READ | PROT_WRITE, MAP_SHARED,
                      xhci->vfio_fd, reg.offset);
    if (xhci->bar0 == MAP_FAILED) {
        perror("mmap BAR0");
        xhci->bar0 = NULL;
        return -1;
    }

    return 0;
}

/****************************************************************
 * PCI Configuration space access via VFIO config region
 ****************************************************************/

static int
vfio_pci_config_read(struct usb_xhci_s *xhci, u32 offset, void *buf, size_t size)
{
    struct vfio_region_info reg = {
        .argsz = sizeof(reg),
        .index = VFIO_PCI_CONFIG_REGION_INDEX,
    };
    int ret = ioctl(xhci->vfio_fd, VFIO_DEVICE_GET_REGION_INFO, &reg);
    if (ret < 0)
        return -1;

    return pread(xhci->vfio_fd, buf, size, reg.offset + offset);
}

static int
vfio_pci_config_write(struct usb_xhci_s *xhci, u32 offset, void *buf, size_t size)
{
    struct vfio_region_info reg = {
        .argsz = sizeof(reg),
        .index = VFIO_PCI_CONFIG_REGION_INDEX,
    };
    int ret = ioctl(xhci->vfio_fd, VFIO_DEVICE_GET_REGION_INFO, &reg);
    if (ret < 0)
        return -1;

    return pwrite(xhci->vfio_fd, buf, size, reg.offset + offset);
}

/****************************************************************
 * Public API
 ****************************************************************/

void *
xhci_open(const char *pci_bdf)
{
    struct usb_xhci_s *xhci;
    int group_nr;
    char path[64];
    int ret;

    xhci = calloc(1, sizeof(*xhci));
    if (!xhci)
        return NULL;

    xhci->irq_efd = -1;
    xhci->container_fd = -1;
    xhci->group_fd = -1;
    xhci->vfio_fd = -1;
    xhci->next_iova = 0x10000000ULL;  // 256MB — low IOVA for controller DMA

    // Open VFIO container
    xhci->container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (xhci->container_fd < 0) {
        perror("open /dev/vfio/vfio");
        goto fail;
    }

    ret = ioctl(xhci->container_fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        fprintf(stderr, "VFIO API version mismatch: expected %d, got %d\n",
                VFIO_API_VERSION, ret);
        goto fail;
    }

    // Find IOMMU group for this PCI device
    group_nr = vfio_get_group_nr(pci_bdf);
    if (group_nr < 0) {
        fprintf(stderr, "Cannot find IOMMU group for %s\n", pci_bdf);
        goto fail;
    }

    snprintf(path, sizeof(path), "/dev/vfio/%d", group_nr);
    xhci->group_fd = open(path, O_RDWR);
    if (xhci->group_fd < 0) {
        perror("open VFIO group");
        fprintf(stderr, "Is the device bound to vfio-pci?\n");
        goto fail;
    }

    struct vfio_group_status gs = { .argsz = sizeof(gs) };
    ret = ioctl(xhci->group_fd, VFIO_GROUP_GET_STATUS, &gs);
    if (ret < 0) {
        perror("VFIO_GROUP_GET_STATUS");
        goto fail;
    }

    ret = ioctl(xhci->group_fd, VFIO_GROUP_SET_CONTAINER, &xhci->container_fd);
    if (ret < 0) {
        perror("VFIO_GROUP_SET_CONTAINER");
        goto fail;
    }

    // Try VFIO_TYPE1_IOMMU first (works but AMD IOMMU causes page faults)
    ret = ioctl(xhci->container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    if (ret == 0) {
        fprintf(stderr, "Using VFIO_TYPE1_IOMMU\n");
    } else {
        perror("VFIO_TYPE1_IOMMU failed, trying VFIO_NOIOMMU_IOMMU");
        ret = ioctl(xhci->container_fd, VFIO_SET_IOMMU, VFIO_NOIOMMU_IOMMU);
        if (ret < 0) {
            perror("VFIO_NOIOMMU_IOMMU also failed");
            goto fail;
        }
        fprintf(stderr, "Using VFIO_NOIOMMU_IOMMU\n");
        xhci->noiommu_mode = 1;
    }

    xhci->vfio_fd = ioctl(xhci->group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_bdf);
    if (xhci->vfio_fd < 0) {
        perror("VFIO_GROUP_GET_DEVICE_FD");
        goto fail;
    }

    // Map BAR0
    if (vfio_map_bar(xhci) < 0)
        goto fail;

    // Disable IRQs — pure polling mode (Hygon controller lacks interrupt remapping)
    xhci->irq_efd = -1;

    // Compute register pointers from BAR0 base
    xhci->caps = xhci->bar0;
    xhci->op   = xhci->bar0 + xhci->caps->caplength;
    xhci->pr   = xhci->bar0 + xhci->caps->caplength + 0x400;
    xhci->db   = xhci->bar0 + xhci->caps->dboff;
    xhci->ir   = xhci->bar0 + xhci->caps->rtsoff + 0x20;

    fprintf(stderr, "xhci: bar0=%p caplength=%u dboff=0x%x rtsoff=0x%x\n",
            xhci->bar0, xhci->caps->caplength, xhci->caps->dboff, xhci->caps->rtsoff);
    fprintf(stderr, "xhci: caps=%p op=%p pr=%p db=%p ir=%p\n",
            xhci->caps, xhci->op, xhci->pr, xhci->db, xhci->ir);

    // Print PCI configuration space (first 64 bytes)
    u8 pci_cfg[64];
    if (vfio_pci_config_read(xhci, 0, pci_cfg, sizeof(pci_cfg)) == sizeof(pci_cfg)) {
        fprintf(stderr, "xhci: PCI config space:\n");
        fprintf(stderr, "xhci:   Vendor/Device ID : 0x%04x/0x%04x\n",
                pci_cfg[2] | (pci_cfg[3] << 8), pci_cfg[0] | (pci_cfg[1] << 8));
        fprintf(stderr, "xhci:   Command          : 0x%04x (Mem=%d BusMaster=%d)\n",
                pci_cfg[4] | (pci_cfg[5] << 8),
                !!(pci_cfg[5] & 2), !!(pci_cfg[5] & 4));
        fprintf(stderr, "xhci:   Header Type      : 0x%02x\n", pci_cfg[0x0e]);
        fprintf(stderr, "xhci:   BAR0             : 0x%08x\n",
                pci_cfg[0x10] | (pci_cfg[0x11]<<8) | (pci_cfg[0x12]<<16) | (pci_cfg[0x13]<<24));
        fprintf(stderr, "xhci:   Subsystem VID/DID : 0x%04x/0x%04x\n",
                pci_cfg[0x2c] | (pci_cfg[0x2d] << 8),
                pci_cfg[0x2e] | (pci_cfg[0x2f] << 8));
    } else {
        fprintf(stderr, "xhci: WARNING: cannot read PCI config space\n");
    }

    // Enable Bus Master (DMA) — Linux does this via pci_enable_device().
    // Without this, the controller cannot issue DMA reads and HCE triggers
    // on first access to DCBAA/Event Ring.
    u16 cmd = 0x0007;  // IO Space + Memory Space + Bus Master Enable
    vfio_pci_config_write(xhci, 4, &cmd, sizeof(cmd));

    // Allocate DCBAA (Device Context Base Address Array)
    u64 dcbaa_phys;
    xhci->dcbaa = xhci_dma_alloc_internal(xhci, XHCI_MAX_SLOTS * sizeof(struct xhci_devlist),
                                          &dcbaa_phys);
    if (!xhci->dcbaa) {
        fprintf(stderr, "Failed to allocate DCBAA\n");
        goto fail;
    }
    xhci->dcbaa_phys = dcbaa_phys;
    memset(xhci->dcbaa, 0, XHCI_MAX_SLOTS * sizeof(struct xhci_devlist));

    return xhci;

fail:
    xhci_close(xhci);
    return NULL;
}

void
xhci_close(void *handle)
{
    struct usb_xhci_s *xhci = handle;
    if (!xhci) return;

    if (xhci->bar0)
        munmap(xhci->bar0, xhci->bar0_size);
    if (xhci->dcbaa)
        xhci_dma_free_internal(xhci, xhci->dcbaa, XHCI_MAX_SLOTS * sizeof(struct xhci_devlist));
    if (xhci->irq_efd >= 0)
        close(xhci->irq_efd);
    if (xhci->vfio_fd >= 0)
        close(xhci->vfio_fd);
    if (xhci->group_fd >= 0)
        close(xhci->group_fd);
    if (xhci->container_fd >= 0)
        close(xhci->container_fd);

    // Unmap all DMA regions
    for (int i = 0; i < xhci->dma_count; i++) {
        struct vfio_iommu_type1_dma_unmap unmap = {
            .argsz = sizeof(unmap),
            .iova = xhci->dma_iovas[i],
            .size = xhci->dma_sizes[i],
        };
        ioctl(xhci->container_fd, VFIO_IOMMU_UNMAP_DMA, &unmap);
        munlock(xhci->dma_vaddrs[i], xhci->dma_sizes[i]);
        munmap(xhci->dma_vaddrs[i], xhci->dma_sizes[i]);
    }

    free(xhci);
}

u32
xhci_read32(void *handle, int offset)
{
    struct usb_xhci_s *xhci = handle;
    volatile u32 *p = (volatile u32*)((u8 *)xhci->bar0 + offset);
    return *p;
}

struct xhci_caps *
xhci_get_caps(void *handle)
{
    return ((struct usb_xhci_s *)handle)->caps;
}

struct xhci_op *
xhci_get_op(void *handle)
{
    return ((struct usb_xhci_s *)handle)->op;
}

// Register accessor helpers for Python ctypes bindings
void *
xhci_cap_regs(void *handle)
{
    return ((struct usb_xhci_s *)handle)->caps;
}

void *
xhci_op_regs(void *handle)
{
    return ((struct usb_xhci_s *)handle)->op;
}

void *
xhci_rt_regs(void *handle)
{
    return ((struct usb_xhci_s *)handle)->ir;
}

void *
xhci_doorbell_regs(void *handle)
{
    return ((struct usb_xhci_s *)handle)->db;
}

void
xhci_write32(void *handle, int offset, u32 val)
{
    struct usb_xhci_s *xhci = handle;
    volatile u32 *p = (volatile u32*)((u8 *)xhci->bar0 + offset);
    *p = val;
}

u64
xhci_read64(void *handle, int offset)
{
    struct usb_xhci_s *xhci = handle;
    volatile u64 *p = (volatile u64*)((u8 *)xhci->bar0 + offset);
    return *p;
}

void
xhci_write64(void *handle, int offset, u64 val)
{
    struct usb_xhci_s *xhci = handle;
    volatile u64 *p = (volatile u64*)((u8 *)xhci->bar0 + offset);
    *p = val;
}

static void *
xhci_dma_alloc_internal(struct usb_xhci_s *xhci, size_t size, u64 *phys)
{
    // Align size to page
    size_t aligned = (size + XHCI_PAGE_SIZE - 1) & ~(XHCI_PAGE_SIZE - 1);

    // Allocate virtual memory for CPU access.
    // Touch pages to force page fault
    void *ptr = mmap(NULL, aligned, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    for (size_t i = 0; i < aligned; i += 64)
    *(volatile u64 *)(ptr + i) = 0xDEADBEEFCAFEBABEULL;
    if (ptr == MAP_FAILED)
        return NULL;

    // Force page fault
    volatile u8 *p = ptr;
    for (size_t i = 0; i < aligned; i += XHCI_PAGE_SIZE)
        p[i] = 0;

    if (mlock(ptr, aligned) != 0) {
        munmap(ptr, aligned);
        return NULL;
    }

    // Always use physical address.
    // VFIO_IOMMU_MAP_DMA reports success but AMD IOMMU doesn't actually
    // translate these addresses, causing IO_PAGE_FAULT.
    u64 dma_iova = virt_to_phys(ptr);
    if (dma_iova == 0) {
        fprintf(stderr, "xhci_dma_alloc: virt_to_phys failed for vaddr=%p\n", ptr);
        munlock(ptr, aligned);
        munmap(ptr, aligned);
        return NULL;
    }
    dma_iova &= ~(XHCI_PAGE_SIZE - 1);  // 4KB align for IOMMU page tables

    // Map DMA using physical address as IOVA.
    // AMD IOMMU needs explicit mapping even in passthrough mode.
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .vaddr = (u64)(uintptr_t)ptr,
        .iova = dma_iova,
        .size = aligned,
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
    };

    int map_ret = ioctl(xhci->container_fd, VFIO_IOMMU_MAP_DMA, &map);
    if (map_ret < 0) {
        fprintf(stderr, "xhci_dma_alloc: VFIO_IOMMU_MAP_DMA failed (errno=%d/%s)\n",
                errno, strerror(errno));
    }

    fprintf(stderr, "xhci_dma_alloc: vaddr=%p phys=0x%llx size=%zu map=%d\n",
            ptr, (unsigned long long)dma_iova, aligned, map_ret);

    // Verify read back first 8 bytes
    volatile u64 *test = (volatile u64 *)ptr;
    u64 pattern = 0xDEADBEEFCAFEBABEULL;
    *test = pattern;
    if (*test != pattern) {
        fprintf(stderr, "xhci_dma_alloc: readback verification FAILED at vaddr=%p iova=0x%llx\n",
                ptr, (unsigned long long)dma_iova);
        munlock(ptr, aligned);
        munmap(ptr, aligned);
        return NULL;
    }

    if (xhci->dma_count < MAX_DMA_REGIONS) {
        xhci->dma_vaddrs[xhci->dma_count] = ptr;
        xhci->dma_iovas[xhci->dma_count] = dma_iova;
        xhci->dma_sizes[xhci->dma_count] = aligned;
        xhci->dma_count++;
    }

    if (phys)
        *phys = dma_iova;

    return ptr;
}

void *
xhci_dma_alloc(void *handle, size_t size, u64 *phys)
{
    return xhci_dma_alloc_internal(handle, size, phys);
}

static void
xhci_dma_free_internal(struct usb_xhci_s *xhci, void *virt, size_t size)
{
    (void)size;  /* use xhci->dma_sizes[i] instead */

    for (int i = 0; i < xhci->dma_count; i++) {
        if (xhci->dma_vaddrs[i] == virt) {
            struct vfio_iommu_type1_dma_unmap unmap = {
                .argsz = sizeof(unmap),
                .iova = xhci->dma_iovas[i],
                .size = xhci->dma_sizes[i],  // use actual allocated size
            };
            ioctl(xhci->container_fd, VFIO_IOMMU_UNMAP_DMA, &unmap);

            munlock(virt, xhci->dma_sizes[i]);
            munmap(virt, xhci->dma_sizes[i]);

            memmove(&xhci->dma_vaddrs[i], &xhci->dma_vaddrs[i+1],
                    (xhci->dma_count - 1 - i) * sizeof(void *));
            memmove(&xhci->dma_iovas[i], &xhci->dma_iovas[i+1],
                    (xhci->dma_count - 1 - i) * sizeof(u64));
            memmove(&xhci->dma_sizes[i], &xhci->dma_sizes[i+1],
                    (xhci->dma_count - 1 - i) * sizeof(size_t));
            xhci->dma_count--;
            break;
        }
    }
}

void
xhci_dma_free(void *handle, void *virt, size_t size)
{
    xhci_dma_free_internal(handle, virt, size);
}

// Return pointer to DCBAA (Device Context Base Address Array).
// Allows caller to pre-populate device contexts before ADDRESS_DEVICE.
struct xhci_devlist *
xhci_get_dcbaa(void *handle, u64 *phys_out)
{
    struct usb_xhci_s *xhci = handle;
    if (phys_out)
        *phys_out = xhci->dcbaa_phys;
    return xhci->dcbaa;
}

// Re-enable PCI Bus Master. HCRST or kernel driver may have cleared it.
// Must be called after HCRST and before RUN.
void
xhci_pci_cmd_enable(void *handle)
{
    struct usb_xhci_s *xhci = handle;
    u16 cmd = 0x0007;  // IO Space + Memory Space + Bus Master Enable
    vfio_pci_config_write(xhci, 4, &cmd, sizeof(cmd));

    // Verify
    u16 verify = 0;
    vfio_pci_config_read(xhci, 4, &verify, sizeof(verify));
    fprintf(stderr, "xhci: PCI Command = 0x%04x (expected 0x0007)\n", verify);
}

// PCI function-level reset via VFIO. Clears HCE and all controller state.
// Same effect as unbind_vfio.sh's FLR step.
int
xhci_pci_flr(void *handle)
{
    struct usb_xhci_s *xhci = handle;
    return ioctl(xhci->vfio_fd, VFIO_DEVICE_RESET);
}

// Allocate device context for a slot after ENABLE_SLOT.
// Writes the DCBAA entry and zeroes the context.
// Returns physical address of the device context.
u64
xhci_alloc_dev_ctx(void *handle, int slot_id, u64 *phys_out)
{
    struct usb_xhci_s *xhci = handle;
    u64 dev_phys;
    void *dev = xhci_dma_alloc_internal(xhci, 1024, &dev_phys);
    if (!dev) return 0;
    memset(dev, 0, 1024);

    if (xhci->dcbaa && slot_id < XHCI_MAX_SLOTS) {
        xhci->dcbaa[slot_id].ptr_low = (u32)dev_phys;
        xhci->dcbaa[slot_id].ptr_high = (u32)(dev_phys >> 32);
    }
    if (phys_out) *phys_out = dev_phys;
    return dev_phys;
}
