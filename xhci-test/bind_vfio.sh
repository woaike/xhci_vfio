#!/bin/bash
# bind_vfio.sh - Bind PCIe device to vfio-pci with AMD-Vi unsafe interrupts
# Usage: ./bind_vfio.sh 0000:05:00.1

set -e

PCI_BDF="$1"
if [ -z "$PCI_BDF" ]; then
    echo "Usage: $0 <PCI_BDF>"
    echo "  e.g. $0 0000:05:00.1"
    exit 1
fi

SYSFS_DEV="/sys/bus/pci/devices/$PCI_BDF"
if [ ! -d "$SYSFS_DEV" ]; then
    echo "Error: device $PCI_BDF not found"
    exit 1
fi

# Step 1: Check current driver
CURRENT_DRIVER=""
if [ -L "$SYSFS_DEV/driver" ]; then
    CURRENT_DRIVER=$(basename $(readlink "$SYSFS_DEV/driver"))
fi

# Step 2: Unbind from current driver
if [ -n "$CURRENT_DRIVER" ]; then
    echo "Unbinding $PCI_BDF from $CURRENT_DRIVER..."
    echo "$PCI_BDF" > "$SYSFS_DEV/driver/unbind"
    sleep 0.5
fi

# Step 3: Reload vfio-pci (this may also reload vfio_iommu_type1)
rmmod vfio-pci 2>/dev/null || true
sleep 0.5
modprobe vfio-pci
sleep 0.5

# Step 4: Set allow_unsafe_interrupts AFTER modules are loaded
# Must be done AFTER modprobe, not before (module reload resets the parameter)
if [ -f /sys/module/vfio_iommu_type1/parameters/allow_unsafe_interrupts ]; then
    echo "Setting allow_unsafe_interrupts=Y..."
    echo Y > /sys/module/vfio_iommu_type1/parameters/allow_unsafe_interrupts
    echo "  allow_unsafe_interrupts = $(cat /sys/module/vfio_iommu_type1/parameters/allow_unsafe_interrupts)"
else
    echo "Warning: vfio_iommu_type1 parameter not found"
fi

# Step 5: Bind to vfio-pci
echo "Binding $PCI_BDF to vfio-pci..."
echo "vfio-pci" > "$SYSFS_DEV/driver_override"
echo "$PCI_BDF" > /sys/bus/pci/drivers/vfio-pci/bind
sleep 0.5

# Verify
NEW_DRIVER=""
if [ -L "$SYSFS_DEV/driver" ]; then
    NEW_DRIVER=$(basename $(readlink "$SYSFS_DEV/driver"))
fi

if [ "$NEW_DRIVER" = "vfio-pci" ]; then
    echo "OK: $PCI_BDF is now bound to vfio-pci"
else
    echo "Error: $PCI_BDF is bound to '$NEW_DRIVER', not vfio-pci"
    exit 1
fi
