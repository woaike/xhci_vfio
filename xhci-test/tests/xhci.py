#!/usr/bin/env python3
"""Python wrapper for the XHCI VFIO test library via ctypes."""

import ctypes
import ctypes.util
import os
import struct

# Load the shared library
_lib = None

def load_lib(path=None):
    global _lib
    if _lib is not None:
        return _lib
    if path is None:
        path = os.path.join(os.path.dirname(__file__), '..', 'build', 'libxhci.so')
        path = os.path.abspath(path)
    _lib = ctypes.CDLL(path)
    _setup_prototypes()
    return _lib


def _setup_prototypes():
    """Set ctypes function signatures."""
    lib = _lib

    # void *xhci_open(const char *vfio_path)
    lib.xhci_open.argtypes = [ctypes.c_char_p]
    lib.xhci_open.restype = ctypes.c_void_p

    # void xhci_close(void *handle)
    lib.xhci_close.argtypes = [ctypes.c_void_p]
    lib.xhci_close.restype = None

    # uint32_t xhci_read32(void *handle, uint32_t offset)
    lib.xhci_read32.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.xhci_read32.restype = ctypes.c_uint32

    # void xhci_write32(void *handle, uint32_t offset, uint32_t val)
    lib.xhci_write32.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]

    # uint64_t xhci_read64(void *handle, uint32_t offset)
    lib.xhci_read64.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.xhci_read64.restype = ctypes.c_uint64

    # void xhci_write64(void *handle, uint32_t offset, uint64_t val)
    lib.xhci_write64.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64]

    # int xhci_reset(void *handle)
    lib.xhci_reset.argtypes = [ctypes.c_void_p]
    lib.xhci_reset.restype = ctypes.c_int

    # void xhci_run(void *handle)
    lib.xhci_run.argtypes = [ctypes.c_void_p]

    # int xhci_stop(void *handle)
    lib.xhci_stop.argtypes = [ctypes.c_void_p]
    lib.xhci_stop.restype = ctypes.c_int

    # int xhci_is_halted(void *handle)
    lib.xhci_is_halted.argtypes = [ctypes.c_void_p]
    lib.xhci_is_halted.restype = ctypes.c_int

    # void *xhci_dma_alloc(void *handle, size_t size, uint64_t *phys)
    lib.xhci_dma_alloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint64)]
    lib.xhci_dma_alloc.restype = ctypes.c_void_p

    # void xhci_dma_free(void *handle, void *virt, size_t size)
    lib.xhci_dma_free.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]

    # int xhci_cmd_ring_init(void *handle)
    lib.xhci_cmd_ring_init.argtypes = [ctypes.c_void_p]
    lib.xhci_cmd_ring_init.restype = ctypes.c_int

    # int xhci_cmd_issue(...)
    lib.xhci_cmd_issue.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
                                    ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64)]
    lib.xhci_cmd_issue.restype = ctypes.c_int

    # int xhci_enable_slot(void *handle)
    lib.xhci_enable_slot.argtypes = [ctypes.c_void_p]
    lib.xhci_enable_slot.restype = ctypes.c_int

    # int xhci_address_device(...)
    lib.xhci_address_device.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64]
    lib.xhci_address_device.restype = ctypes.c_int

    # int xhci_configure_endpoint(...)
    lib.xhci_configure_endpoint.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64]
    lib.xhci_configure_endpoint.restype = ctypes.c_int

    # int xhci_port_count(void *handle)
    lib.xhci_port_count.argtypes = [ctypes.c_void_p]
    lib.xhci_port_count.restype = ctypes.c_int

    # uint32_t xhci_port_read(...)
    lib.xhci_port_read.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_port_read.restype = ctypes.c_uint32

    # void xhci_port_write(...)
    lib.xhci_port_write.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]

    # int xhci_port_link_state(...)
    lib.xhci_port_link_state.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_port_link_state.restype = ctypes.c_int

    # int xhci_port_set_link_state(...)
    lib.xhci_port_set_link_state.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    lib.xhci_port_set_link_state.restype = ctypes.c_int

    # int xhci_port_warm_reset(...)
    lib.xhci_port_warm_reset.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_port_warm_reset.restype = ctypes.c_int

    # int xhci_port_find_changed(...)
    lib.xhci_port_find_changed.argtypes = [ctypes.c_void_p]
    lib.xhci_port_find_changed.restype = ctypes.c_int

    # int xhci_event_ring_init(...)
    lib.xhci_event_ring_init.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_event_ring_init.restype = ctypes.c_int

    # int xhci_event_wait(...)
    lib.xhci_event_wait.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
    lib.xhci_event_wait.restype = ctypes.c_int

    # void xhci_irq_ack(...)
    lib.xhci_irq_ack.argtypes = [ctypes.c_void_p]

    # void xhci_irq_set_imod(...)
    lib.xhci_irq_set_imod.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16]

    # void *xhci_transfer_ring_create(...)
    lib.xhci_transfer_ring_create.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_transfer_ring_create.restype = ctypes.c_void_p

    # void xhci_transfer_ring_free(...)
    lib.xhci_transfer_ring_free.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

    # uint64_t xhci_transfer_ring_phys(...)
    lib.xhci_transfer_ring_phys.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.xhci_transfer_ring_phys.restype = ctypes.c_uint64

    # int xhci_transfer_enqueue(...)
    lib.xhci_transfer_enqueue.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                           ctypes.c_uint64, ctypes.c_uint32,
                                           ctypes.c_uint32, ctypes.c_uint32,
                                           ctypes.c_uint32]
    lib.xhci_transfer_enqueue.restype = ctypes.c_int

    # void xhci_doorbell(...)
    lib.xhci_doorbell.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]

    # int xhci_full_init(...)
    lib.xhci_full_init.argtypes = [ctypes.c_void_p]
    lib.xhci_full_init.restype = ctypes.c_int

    # void *xhci_cap_regs(void *handle)
    lib.xhci_cap_regs.argtypes = [ctypes.c_void_p]
    lib.xhci_cap_regs.restype = ctypes.c_void_p

    # void *xhci_op_regs(void *handle)
    lib.xhci_op_regs.argtypes = [ctypes.c_void_p]
    lib.xhci_op_regs.restype = ctypes.c_void_p

    # void *xhci_rt_regs(void *handle)
    lib.xhci_rt_regs.argtypes = [ctypes.c_void_p]
    lib.xhci_rt_regs.restype = ctypes.c_void_p

    # void *xhci_doorbell_regs(void *handle)
    lib.xhci_doorbell_regs.argtypes = [ctypes.c_void_p]
    lib.xhci_doorbell_regs.restype = ctypes.c_void_p

    # int xhci_control_transfer(...)
    lib.xhci_control_transfer.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                           ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint32,
                                           ctypes.c_int]
    lib.xhci_control_transfer.restype = ctypes.c_int

    # int xhci_transfer_ring_cycle_state(void *ring)
    lib.xhci_transfer_ring_cycle_state.argtypes = [ctypes.c_void_p]
    lib.xhci_transfer_ring_cycle_state.restype = ctypes.c_int

    # int xhci_dcbaa_verify(void *handle, int slot_id)
    lib.xhci_dcbaa_verify.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.xhci_dcbaa_verify.restype = ctypes.c_int


# ========================================================================
# Constants (mirror xhci_regs.h)
# ========================================================================

# Capability register offsets
XHCI_CAP_CAPLENGTH   = 0x00
XHCI_CAP_HCI_VERSION = 0x02
XHCI_CAP_HCSPARAMS1  = 0x04
XHCI_CAP_HCSPARAMS2  = 0x08
XHCI_CAP_HCSPARAMS3  = 0x0C
XHCI_CAP_HCCPARAMS   = 0x10
XHCI_CAP_DBOFF       = 0x14
XHCI_CAP_RTSOFF      = 0x18
XHCI_CAP_HCCPARAMS2  = 0x1C

# Operational register offsets
XHCI_OP_USBCMD   = 0x00
XHCI_OP_USBSTS   = 0x04
XHCI_OP_PAGESIZE = 0x08
XHCI_OP_DNCTRL   = 0x14
XHCI_OP_CRCR     = 0x18
XHCI_OP_DCBAAP   = 0x30
XHCI_OP_CONFIG   = 0x38
XHCI_OP_PORTSC_BASE = 0x400

# Port link states
PORTSC_PLS_U0 = 0
PORTSC_PLS_U1 = 1
PORTSC_PLS_U2 = 2
PORTSC_PLS_U3 = 3
PORTSC_PLS_RX_DET = 5

# TRB completion codes
TRB_CC_SUCCESS               = 1
TRB_CC_STALL_ERROR           = 6
TRB_CC_USB_TRANSACTION_ERROR = 4
TRB_CC_SHORT_PACKET          = 13

# TRB types
TRB_CMD_ENABLE_SLOT     = 2
TRB_CMD_ADDRESS_DEVICE  = 3
TRB_CMD_CONFIGURE_EP    = 4

# USB descriptor types
USB_DT_DEVICE = 1
USB_DT_CONFIG = 2


# ========================================================================
# High-level Python class
# ========================================================================

class XHCIController:
    """High-level wrapper around the C XHCI library."""

    def __init__(self, pci_bdf=None, lib_path=None):
        if pci_bdf is None:
            pci_bdf = os.environ.get("XHCI_PCI", "0000:00:14.0")

        self._lib = load_lib(lib_path)
        self._handle = self._lib.xhci_open(pci_bdf.encode())
        if not self._handle:
            raise RuntimeError(f"Failed to open XHCI device {pci_bdf}. "
                             "Is it bound to vfio-pci?")
        self._pci_bdf = pci_bdf
        self._transfer_rings = {}

    def close(self):
        if self._handle:
            for key, ring in self._transfer_rings.items():
                self._lib.xhci_transfer_ring_free(self._handle, ring)
            self._lib.xhci_close(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    # ---- Raw register access ----

    def read32(self, offset):
        return self._lib.xhci_read32(self._handle, offset)

    def write32(self, offset, val):
        self._lib.xhci_write32(self._handle, offset, val)

    def read64(self, offset):
        return self._lib.xhci_read64(self._handle, offset)

    def write64(self, offset, val):
        self._lib.xhci_write64(self._handle, offset, val)

    # ---- Capability registers ----

    @property
    def caplength(self):
        return self.read32(XHCI_CAP_CAPLENGTH) & 0xFF

    @property
    def hciversion(self):
        return self.read32(XHCI_CAP_HCI_VERSION) & 0xFFFF

    @property
    def hciversion_str(self):
        v = self.hciversion
        return f"{(v >> 8) & 0xFF}.{v & 0xFF}"

    @property
    def hcsparams1(self):
        return self.read32(XHCI_CAP_HCSPARAMS1)

    @property
    def hcsparams2(self):
        return self.read32(XHCI_CAP_HCSPARAMS2)

    @property
    def hccparams(self):
        return self.read32(XHCI_CAP_HCCPARAMS)

    @property
    def max_slots(self):
        return self.hcsparams1 & 0xFF

    @property
    def max_intrs(self):
        return (self.hcsparams1 >> 8) & 0x7FF

    @property
    def max_ports(self):
        return (self.hcsparams1 >> 24) & 0xFF

    @property
    def supports_64bit(self):
        return bool(self.hccparams & 0x1)

    @property
    def context_size_64(self):
        return bool((self.hccparams >> 27) & 0x1)

    # ---- Controller lifecycle ----

    def reset(self):
        rc = self._lib.xhci_reset(self._handle)
        if rc != 0:
            raise RuntimeError("xhci_reset timeout")

    def run(self):
        self._lib.xhci_run(self._handle)

    def stop(self):
        rc = self._lib.xhci_stop(self._handle)
        if rc != 0:
            raise RuntimeError("xhci_stop timeout")

    @property
    def is_halted(self):
        return bool(self._lib.xhci_is_halted(self._handle))

    def full_init(self):
        rc = self._lib.xhci_full_init(self._handle)
        if rc != 0:
            raise RuntimeError("xhci_full_init failed")

    # ---- Ports ----

    @property
    def port_count(self):
        return self._lib.xhci_port_count(self._handle)

    def port_read(self, port):
        return self._lib.xhci_port_read(self._handle, port)

    def port_write(self, port, val):
        self._lib.xhci_port_write(self._handle, port, val)

    def port_link_state(self, port):
        return self._lib.xhci_port_link_state(self._handle, port)

    def port_set_link_state(self, port, state):
        self._lib.xhci_port_set_link_state(self._handle, port, state)

    def port_warm_reset(self, port):
        rc = self._lib.xhci_port_warm_reset(self._handle, port)
        if rc != 0:
            raise RuntimeError(f"Port warm reset failed on port {port}")

    def find_port_changed(self):
        return self._lib.xhci_port_find_changed(self._handle)

    @property
    def ports(self):
        """Iterate over all port indices."""
        return range(self.port_count)

    def dump_ports(self):
        """Print all port states for debugging."""
        for i in self.ports:
            portsc = self.port_read(i)
            ccs = bool(portsc & 0x1)
            ped = bool(portsc & 0x2)
            pls = (portsc >> 5) & 0xF
            pp = bool(portsc & 0x200)
            csc = bool(portsc & 0x10000)
            print(f"  Port {i}: CCS={ccs} PED={ped} PLS={pls} PP={pp} CSC={csc} (0x{portsc:08x})")

    # ---- Slots / Devices ----

    def enable_slot(self):
        slot_id = self._lib.xhci_enable_slot(self._handle)
        if slot_id < 0:
            raise RuntimeError("Enable slot failed")
        return slot_id

    def address_device(self, slot_id, input_ctx_phys):
        rc = self._lib.xhci_address_device(self._handle, slot_id, input_ctx_phys)
        if rc != 0:
            raise RuntimeError(f"Address device failed for slot {slot_id}")

    def configure_endpoint(self, slot_id, input_ctx_phys):
        rc = self._lib.xhci_configure_endpoint(self._handle, slot_id, input_ctx_phys)
        if rc != 0:
            raise RuntimeError(f"Configure endpoint failed for slot {slot_id}")

    # ---- DMA ----

    def dma_alloc(self, size):
        phys = ctypes.c_uint64()
        virt = self._lib.xhci_dma_alloc(self._handle, size, ctypes.byref(phys))
        if not virt:
            raise RuntimeError(f"DMA alloc failed for size {size}")
        return virt, phys.value

    def dma_free(self, virt, size):
        self._lib.xhci_dma_free(self._handle, virt, size)

    # ---- Transfer Ring ----

    def create_transfer_ring(self, num_entries=256):
        ring = self._lib.xhci_transfer_ring_create(self._handle, num_entries)
        if not ring:
            raise RuntimeError("Transfer ring creation failed")
        return ring

    def free_transfer_ring(self, ring):
        self._lib.xhci_transfer_ring_free(self._handle, ring)

    def transfer_ring_phys(self, ring):
        return self._lib.xhci_transfer_ring_phys(self._handle, ring)

    def transfer_enqueue(self, ring, data_phys, length, td_size=0, intr_target=0, ctrl=0):
        return self._lib.xhci_transfer_enqueue(self._handle, ring,
                                                data_phys, length, td_size,
                                                intr_target, ctrl)

    def doorbell(self, slot_id, endpoint):
        self._lib.xhci_doorbell(self._handle, slot_id, endpoint)

    # ---- Events ----

    def event_ring_init(self, num_entries=256):
        rc = self._lib.xhci_event_ring_init(self._handle, num_entries)
        if rc != 0:
            raise RuntimeError("Event ring init failed")

    def event_wait(self, timeout_ms=5000):
        event = ctypes.c_uint64()
        rc = self._lib.xhci_event_wait(self._handle, ctypes.byref(event), timeout_ms)
        if rc != 0:
            raise RuntimeError("Event wait timeout")
        return event.value

    def irq_ack(self):
        self._lib.xhci_irq_ack(self._handle)

    def irq_set_imod(self, interrupter=0, imod=0):
        self._lib.xhci_irq_set_imod(self._handle, interrupter, imod)

    # ---- Control Transfers (EP0) ----

    def control_transfer(self, slot_id, ep0_ring, setup_data,
                         data_buf=None, data_len=0, direction=1):
        """Perform a control transfer on EP0.

        Args:
            slot_id: The slot ID of the device.
            ep0_ring: EP0 transfer ring handle.
            setup_data: 8-byte SETUP packet (bytes or bytearray).
            data_buf: Buffer for data stage (writable for IN transfers).
            data_len: Length of data stage.
            direction: 0=OUT, 1=IN.

        Returns:
            0 on success, -1 on failure.
        """
        if data_buf is None:
            # Allocate a zero-filled buffer for the data stage
            data_buf = ctypes.create_string_buffer(data_len)
        setup_bytes = ctypes.create_string_buffer(bytes(setup_data), 8)
        rc = self._lib.xhci_control_transfer(
            self._handle, slot_id, ep0_ring,
            setup_bytes, data_buf, data_len, direction)
        return rc, data_buf

    def dcbaa_verify(self, slot_id):
        """Verify that DCBAA entry for a slot is valid."""
        rc = self._lib.xhci_dcbaa_verify(self._handle, slot_id)
        if rc != 0:
            raise RuntimeError(f"DCBAA entry for slot {slot_id} is invalid")
        return rc

    def transfer_ring_cycle_state(self, ring):
        """Get the current cycle state of a transfer ring."""
        return self._lib.xhci_transfer_ring_cycle_state(ring)
