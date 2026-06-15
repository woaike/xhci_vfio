#!/bin/bash
# Unbind a PCI device from vfio-pci and return it to the original driver.
# Usage: sudo ./unbind_vfio.sh [PCI_BDF]
# Default PCI_BDF: 0000:00:14.0

set -e

PCI_BDF="${1:-0000:05:00.1}"

if [ ! -d "/sys/bus/pci/devices/$PCI_BDF" ]; then
    echo "ERROR: Device $PCI_BDF not found"
    exit 1
fi

echo "Unbinding $PCI_BDF from vfio-pci..."

# Step 1: PCI function-level reset to clear all controller registers.
# This is CRITICAL — VFIO leaves the hardware in whatever state the
# user-space program left it (DCBAA, CRCR, config all point to now-invalid
# addresses). Without this reset, xhci_hcd probe will fail because the
# controller tries DMA to stale addresses during its own reset sequence.
if [ -f "/sys/bus/pci/devices/$PCI_BDF/reset" ]; then
    echo "  Resetting PCI device (FLR) to clear stale register state..."
    echo 1 > /sys/bus/pci/devices/$PCI_BDF/reset 2>/dev/null && echo "    FLR succeeded." || \
        echo "    WARNING: FLR not supported or failed — controller may have stale state!"
    sleep 1
fi

# Step 2: Remove from vfio-pci
echo "  Removing from vfio-pci..."
echo "$PCI_BDF" > /sys/bus/pci/drivers/vfio-pci/unbind 2>/dev/null || true
sleep 0.5

# Step 3: Ensure device is in D0 (wake up from D3 if VFIO left it sleeping)
POWER_STATE=$(cat /sys/bus/pci/devices/$PCI_BDF/power_state 2>/dev/null || true)
if [ "$POWER_STATE" = "D3hot" ] || [ "$POWER_STATE" = "D3cold" ]; then
    echo "  Waking device from $POWER_STATE..."
    # Writing any config read triggers the kernel to restore power state
    lspci -s $PCI_BDF > /dev/null
    sleep 0.5
fi

# Step 4: Re-bind to xhci_hcd (or whichever driver the kernel picks)
echo "  Probing for native driver..."
echo "$PCI_BDF" > /sys/bus/pci/drivers_probe

# Verify
sleep 1
CURRENT_DRIVER=$(readlink /sys/bus/pci/devices/$PCI_BDF/driver 2>/dev/null || true)
echo "  Current driver: $(basename $CURRENT_DRIVER 2>/dev/null || echo 'none')"

# Quick sanity check — read PCI config to confirm device is responsive
if lspci -s $PCI_BDF > /dev/null 2>&1; then
    echo "  Device is responsive on PCI bus."
else
    echo "  WARNING: Device not responding on PCI bus — try a reboot."
fi

echo "Done."
