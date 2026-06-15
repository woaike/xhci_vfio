/* XHCI VFIO test library public API */
#ifndef XHCI_H
#define XHCI_H

#include "xhci_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

// --------------------------------------------------------------
// Device open/close

// Open a VFIO-bound XHCI controller. Returns handle or NULL.
void *xhci_open(const char *pci_bdf);

// Close device and release resources.
void xhci_close(void *handle);

// --------------------------------------------------------------
// Register access

u32 xhci_read32(void *handle, int offset);
void xhci_write32(void *handle, int offset, u32 val);
u64 xhci_read64(void *handle, int offset);
void xhci_write64(void *handle, int offset, u64 val);

// Caps accessor — for test code that needs struct-based register access.
struct xhci_caps *xhci_get_caps(void *handle);
struct xhci_op *xhci_get_op(void *handle);

// Register base pointer accessors (for Python ctypes bindings)
void *xhci_cap_regs(void *handle);
void *xhci_op_regs(void *handle);
void *xhci_rt_regs(void *handle);
void *xhci_doorbell_regs(void *handle);

// --------------------------------------------------------------
// Controller lifecycle

int xhci_halt(void *handle);
int xhci_reset(void *handle);
void xhci_run(void *handle);
int xhci_stop(void *handle);
int xhci_is_halted(void *handle);

// --------------------------------------------------------------
// DMA helpers

void *xhci_dma_alloc(void *handle, size_t size, u64 *phys);
void xhci_dma_free(void *handle, void *virt, size_t size);

// --------------------------------------------------------------
// Command Ring

int xhci_cmd_ring_init(void *handle);

int xhci_cmd_issue(void *handle, u32 trb_type, u64 parameter,
                   u32 trb_ctrl, u64 *event_trb_out);

// --------------------------------------------------------------
// Slots / Devices

int xhci_enable_slot(void *handle);
int xhci_address_device(void *handle, int slot_id, u64 input_ctx_phys);
int xhci_configure_endpoint(void *handle, int slot_id, u64 input_ctx_phys);

// Verify DCBAA entry for a slot. Returns 0 if valid.
int xhci_dcbaa_verify(void *handle, int slot_id);

// --------------------------------------------------------------
// Ports

int xhci_port_count(void *handle);
u32 xhci_port_read(void *handle, int port);
void xhci_port_write(void *handle, int port, u32 val);
int xhci_port_link_state(void *handle, int port);
int xhci_port_set_link_state(void *handle, int port, int state);
int xhci_port_warm_reset(void *handle, int port);
int xhci_port_find_changed(void *handle);

// --------------------------------------------------------------
// Interrupts / Events

int xhci_event_ring_init(void *handle, int num_entries);
int xhci_event_wait(void *handle, u64 *event_trb, int timeout_ms);
void xhci_irq_ack(void *handle);
void xhci_irq_set_imod(void *handle, int interrupter, u16 imod);

// --------------------------------------------------------------
// Transfer Ring

void *xhci_transfer_ring_create(void *handle, int num_entries);
void xhci_transfer_ring_free(void *handle, void *ring);
u64 xhci_transfer_ring_phys(void *handle, void *ring);
int xhci_transfer_ring_cycle_state(void *ring);

int xhci_transfer_enqueue(void *handle, void *ring,
                          u64 data_phys, u32 len,
                          u32 td_size, u32 intr_target,
                          u32 trb_ctrl);

void xhci_doorbell(void *handle, int slot_id, int endpoint);

// --------------------------------------------------------------
// Full initialization

int xhci_full_init(void *handle);

// --------------------------------------------------------------
// State save/restore

#include "xhci_state.h"

#ifdef __cplusplus
}
#endif

#endif /* XHCI_H */
