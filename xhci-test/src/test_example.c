/* Example test case — compiled as a .so and loaded via dlopen */
#include <stdio.h>
#include <string.h>
#include "xhci.h"

/* Each test case must export this function */
__attribute__((visibility("default")))
int test_main(void *handle, struct xhci_state *state)
{
    int total = 0, passed = 0, failed = 0;

#define ASSERT(name, cond) do { \
    total++; \
    if (cond) { printf("  %-50s [PASS]\n", name); passed++; } \
    else      { printf("  %-50s [FAIL]\n", name); failed++; } \
} while(0)

    printf("State has %d slot(s)\n", state->n_slots);

    if (state->n_slots == 0) {
        printf("  No devices to test\n");
        return 0;
    }

    /* Use the first restored slot for testing */
    struct xhci_state_slot_entry *slot = &state->slots[0];
    if (!slot->restored) {
        fprintf(stderr, "  Slot %d not restored, skipping\n", slot->slot_id);
        return 1;
    }

    /* Test 1: Device descriptor matches saved state */
    struct usb_device_descriptor desc;
    void *ring = xhci_transfer_ring_create(handle, 32);
    if (ring) {
        u8 setup[8] = {0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00};
        int ret = xhci_control_transfer(handle, slot->slot_id, ring,
                                        setup, &desc, 18, 1);
        ASSERT("Read device descriptor", ret == 0);
        if (ret == 0) {
            ASSERT("Vendor ID matches saved state",
                   desc.idVendor == slot->dev_descriptor.idVendor);
            ASSERT("Product ID matches saved state",
                   desc.idProduct == slot->dev_descriptor.idProduct);
        }
        xhci_transfer_ring_free(handle, ring);
    } else {
        failed += 3;
    }

    /* Test 2: DMA alloc works */
    u64 phys;
    void *buf = xhci_dma_alloc(handle, 4096, &phys);
    ASSERT("DMA allocation", buf != NULL && phys != 0);
    if (buf) {
        memset(buf, 0xAA, 4096);
        ASSERT("DMA write/read roundtrip", ((u8 *)buf)[0] == 0xAA);
        xhci_dma_free(handle, buf, 4096);
    }

    /* Test 3: Controller is running */
    ASSERT("Controller running", !xhci_is_halted(handle));

    printf("\n");
    return failed > 0 ? 1 : 0;
}
