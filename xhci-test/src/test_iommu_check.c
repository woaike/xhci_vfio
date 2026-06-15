/* Quick diagnostic: which IOMMU type does xhci_open actually use? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include <errno.h>

int main() {
    printf("=== VFIO IOMMU diagnostic ===\n");

    int container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) { perror("open container"); return 1; }
    printf("container_fd = %d\n", container_fd);

    int api = ioctl(container_fd, VFIO_GET_API_VERSION);
    printf("VFIO_API_VERSION = %d (expected %d)\n", api, VFIO_API_VERSION);

    int group_fd = open("/dev/vfio/18", O_RDWR);
    if (group_fd < 0) { perror("open group"); return 1; }
    printf("group_fd = %d\n", group_fd);

    struct vfio_group_status gs = { .argsz = sizeof(gs) };
    int ret = ioctl(group_fd, VFIO_GROUP_GET_STATUS, &gs);
    printf("GROUP STATUS: ret=%d, flags=0x%x\n", ret, gs.flags);

    ret = ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);
    printf("SET_CONTAINER: ret=%d errno=%d\n", ret, ret<0?errno:0);

    // Try TYPE1
    ret = ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    printf("TYPE1_IOMMU: ret=%d errno=%d (%s)\n", ret, ret<0?errno:0, ret<0?strerror(errno):"ok");

    if (ret < 0) {
        ret = ioctl(container_fd, VFIO_SET_IOMMU, VFIO_NOIOMMU_IOMMU);
        printf("NOIOMMU_IOMMU: ret=%d errno=%d (%s)\n", ret, ret<0?errno:0, ret<0?strerror(errno):"ok");
    }

    if (ret == 0) {
        // Try DMA map
        void *buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (buf != MAP_FAILED) {
            ((char*)buf)[0] = 0;  // fault page
            mlock(buf, 4096);

            struct vfio_iommu_type1_dma_map map = {
                .argsz = sizeof(map),
                .vaddr = (u64)(uintptr_t)buf,
                .iova = 0x10000000ULL,
                .size = 4096,
                .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
            };
            ret = ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &map);
            printf("IOMMU_MAP_DMA: ret=%d errno=%d (%s)\n", ret, ret<0?errno:0, ret<0?strerror(errno):"ok");

            // Now try reading back through BAR0 at this address
            // (we need the actual XHCI open for this)

            munmap(buf, 4096);
        }
    }

    close(group_fd);
    close(container_fd);
    return 0;
}
