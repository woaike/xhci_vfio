#!/usr/bin/env python3
"""Minimal test: open VFIO device, read XHCI registers, print results."""
import os
import sys
import ctypes

sys.path.insert(0, os.path.dirname(__file__))
from xhci import XHCIController

PCI_BDF = os.environ.get("XHCI_PCI", "0000:05:00.1")

print(f"Opening XHCI device {PCI_BDF}...")
try:
    xhci = XHCIController(pci_bdf=PCI_BDF)
except RuntimeError as e:
    print(f"FATAL: {e}")
    sys.exit(1)

print("OK — device opened successfully!\n")

# Read capability registers
print("=== Capability Registers ===")
print(f"  CAPLENGTH    = {xhci.caplength} (0x{xhci.caplength:x})")
print(f"  HCIVERSION   = {xhci.hciversion_str}")
print(f"  MAX_SLOTS    = {xhci.max_slots}")
print(f"  MAX_INTRS    = {xhci.max_intrs}")
print(f"  MAX_PORTS    = {xhci.max_ports}")
print(f"  64-BIT ADDR  = {xhci.supports_64bit}")
print(f"  CTX SIZE 64  = {xhci.context_size_64}")

# Read operational registers
print("\n=== Operational Registers ===")
op = xhci.caplength
cmd = xhci.read32(op + 0x00)
sts = xhci.read32(op + 0x04)
print(f"  USBCMD       = 0x{cmd:08x}  (Run={cmd&1}, HCRST={(cmd>>1)&1})")
print(f"  USBSTS       = 0x{sts:08x}  (Halted={sts&1}, EI={(sts>>3)&1})")

# Dump ports
print(f"\n=== Ports ({xhci.port_count} total) ===")
xhci.dump_ports()

# DMA test
print("\n=== DMA Alloc Test ===")
virt, phys = xhci.dma_alloc(4096)
print(f"  Virtual  = 0x{virt:x}")
print(f"  Physical = 0x{phys:x}")
print(f"  Aligned  = {phys % 4096 == 0}")
xhci.dma_free(virt, 4096)
print("  Free OK")

print("\n=== ALL TESTS PASSED ===")
xhci.close()
