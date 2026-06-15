# XHCI VFIO Test Framework

Direct user-space access to real XHCI (USB 3.0) hardware for testing and validation.

## Architecture

```
pytest (Python)
    │
    ├── ctypes bindings
    ▼
libxhci.so (C library)
    │
    ├── /dev/vfio/N  (VFIO device fd)
    ├── mmap BAR0    (MMIO register access)
    └── eventfd      (IRQ notification)
        │
        ▼
    Real XHCI PCIe controller
```

## Prerequisites

1. **IOMMU enabled** in BIOS (Intel VT-d or AMD-Vi)
2. **Kernel parameters**: `intel_iommu=on iommu=pt` or `amd_iommu=on`
3. **VFIO module**: `modprobe vfio-pci`
4. Python 3.8+, pytest

## Quick Start

```bash
# 1. Bind your XHCI controller to vfio-pci
#    Find the PCI address of your XHCI controller:
lspci -d ::0c03
#    Example output: 00:14.0 USB controller: Intel Corporation ...
sudo ./scripts/bind_vfio.sh 0000:00:14.0

# 2. Build the C library
make

# 3. Run tests
export XHCI_PCI=0000:00:14.0
python -m pytest tests/ -v

# 4. When done, restore the device to kernel driver
sudo ./scripts/unbind_vfio.sh 0000:00:14.0
```

## Test Categories

| File | What it tests |
|------|--------------|
| `test_xhci.py` | Capability registers, reset, ports, link states, DMA |
| `test_device.py` | Device enumeration, slot allocation, address device |
| `test_transfer.py` | Control/Bulk/Interrupt transfers via TRBs |
| `test_interrupt.py` | Event ring, IRQ delivery, interrupt moderation |

## Writing Your Own Tests

```python
from xhci import XHCIController

with XHCIController(pci_bdf="0000:00:14.0") as xhci:
    # Read any XHCI register
    hcs1 = xhci.read32(0x04)  # HCSParams1
    print(f"Max ports: {xhci.max_ports}")

    # Full init: reset, setup rings, start
    xhci.full_init()

    # Allocate a slot
    slot_id = xhci.enable_slot()

    # Allocate DMA buffer
    virt, phys = xhci.dma_alloc(4096)

    # Create transfer ring
    ring = xhci.create_transfer_ring(256)

    # Write data to DMA buffer, enqueue TRB, ring doorbell
    xhci.transfer_enqueue(ring, phys, 64, td_size=0, intr_target=0)
    xhci.doorbell(slot_id, 1)

    # Wait for completion event
    event = xhci.event_wait(timeout_ms=5000)
```

## Adding Tests

```python
import pytest
from xhci import XHCIController

@pytest.fixture()
def xhci():
    import os
    with XHCIController(pci_bdf=os.environ.get("XHCI_PCI", "0000:00:14.0")) as ctrl:
        yield ctrl

class TestMyFeature:
    def test_something(self, xhci):
        xhci.full_init()
        # ... your test logic
```

## Running Specific Tests

```bash
# Only capability tests
python -m pytest tests/test_xhci.py::TestCapabilityRegisters -v

# Only port tests
python -m pytest tests/test_xhci.py::TestPortRegisters -v

# Only with marker
python -m pytest tests/ -m xhci -v

# Skip slow tests
python -m pytest tests/ -v --ignore=tests/test_transfer.py
```

## Troubleshooting

| Error | Cause | Fix |
|-------|-------|-----|
| `open /dev/vfio/vfio: No such file` | VFIO module not loaded | `sudo modprobe vfio-pci` |
| `No IOMMU group found` | IOMMU not enabled | Add `intel_iommu=on` to kernel cmdline, reboot |
| `VFIO_DEVICE_GET_IRQ_INFO failed` | No IRQ support | Check kernel config `CONFIG_VFIO_PCI` |
| `Controller still halted after run` | Command ring not set up | Call `xhci_cmd_ring_init` before `xhci_run` |
| `Segmentation fault` | Invalid register offset | Check offsets against xHCI spec |

## File Structure

```
xhci-test/
├── include/
│   ├── xhci_regs.h    # Register offsets, bit definitions (mirror xHCI spec)
│   ├── xhci.h         # Public C API
│   └── xhci_internal.h # Internal structs
├── src/
│   ├── vfio.c         # VFIO open, mmap, DMA mapping, IRQ setup
│   └── xhci_ops.c     # XHCI commands, ports, transfers, events
├── tests/
│   ├── xhci.py        # Python ctypes bindings + XHCIController class
│   ├── conftest.py    # pytest fixtures
│   └── test_xhci.py   # Test cases
├── scripts/
│   ├── bind_vfio.sh   # Bind PCI device to vfio-pci
│   └── unbind_vfio.sh # Return device to kernel driver
└── Makefile           # Build libxhci.so
```
