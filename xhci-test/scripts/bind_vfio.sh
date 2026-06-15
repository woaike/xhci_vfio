#!/bin/bash
# Bind a PCI XHCI device to vfio-pci for user-space access.
# Usage: sudo ./bind_vfio.sh [PCI_BDF]
# Default PCI_BDF: 0000:00:14.0

set -e

PCI_BDF="${1:-0000:05:00.1}"

# Check if device exists
if [ ! -d "/sys/bus/pci/devices/$PCI_BDF" ]; then
    echo "ERROR: Device $PCI_BDF not found"
    lspci -d ::0c03
    exit 1
fi

echo "Binding $PCI_BDF to vfio-pci..."

# Step 0: Wake device from D3hot if needed (PCI reset)
POWER_STATE=$(cat /sys/bus/pci/devices/$PCI_BDF/power_state 2>/dev/null || true)
if [ "$POWER_STATE" = "D3hot" ] || [ "$POWER_STATE" = "D3cold" ]; then
    echo "  Step 0: Waking device from $POWER_STATE (PCI reset)..."
    echo 1 > /sys/bus/pci/devices/$PCI_BDF/reset 2>/dev/null || true
    sleep 1
fi

# Step 1: Unbind USB bus devices first (usbN)
echo "  Step 1: Unbinding USB bus devices..."
for usbdev in /sys/bus/pci/devices/$PCI_BDF/usb?; do
    if [ -d "$usbdev" ]; then
        busname=$(basename "$usbdev")
        echo "    Unbinding $busname from usb..."
        echo "$busname" > /sys/bus/usb/drivers/usb/unbind 2>/dev/null || true
    fi
done

# Step 2: Unbind from current PCI driver
echo "  Step 2: Unbinding from current PCI driver..."
CURRENT_DRIVER=$(readlink -f /sys/bus/pci/devices/$PCI_BDF/driver 2>/dev/null || true)
if [ -n "$CURRENT_DRIVER" ]; then
    DRIVER_NAME=$(basename "$CURRENT_DRIVER")
    echo "    Current driver: $DRIVER_NAME"
    echo "$PCI_BDF" > /sys/bus/pci/drivers/$DRIVER_NAME/unbind 2>/dev/null || true
    sleep 0.5
fi

# Step 3: Bind to vfio-pci
echo "  Step 3: Binding to vfio-pci..."
modprobe vfio-pci

# Try direct bind
echo "$PCI_BDF" > /sys/bus/pci/drivers/vfio-pci/bind 2>/dev/null && echo "    Bind succeeded." || {
    # Fallback: use new_id
    VENDOR=$(cat /sys/bus/pci/devices/$PCI_BDF/vendor)
    DEVICE=$(cat /sys/bus/pci/devices/$PCI_BDF/device)
    echo "    Direct bind failed, trying new_id ($VENDOR:$DEVICE)..."
    echo "$VENDOR $DEVICE" > /sys/bus/pci/drivers/vfio-pci/new_id 2>/dev/null || true
    sleep 1
    echo "$PCI_BDF" > /sys/bus/pci/drivers/vfio-pci/bind 2>/dev/null || {
        echo "    Bind still failed, checking state..."
        NEW_DRIVER=$(readlink -f /sys/bus/pci/devices/$PCI_BDF/driver 2>/dev/null || true)
        echo "    Current driver: $(basename "$NEW_DRIVER" 2>/dev/null || echo 'none')"
        if echo "$NEW_DRIVER" | grep -q vfio; then
            echo "    Already bound to vfio-pci!"
        else
            echo "ERROR: Failed to bind to vfio-pci"
            exit 1
        fi
    }
}

# Verify
echo ""
echo "  Current driver: $(basename "$(readlink -f /sys/bus/pci/devices/$PCI_BDF/driver)")"

# Check IOMMU group
GROUP=$(readlink /sys/bus/pci/devices/$PCI_BDF/iommu_group 2>/dev/null || true)
if [ -z "$GROUP" ]; then
    echo "WARNING: No IOMMU group found. Is IOMMU enabled in BIOS?"
    exit 1
fi
echo "  IOMMU group: $(basename $GROUP)"

echo ""
echo "Done. Run: export XHCI_PCI=$PCI_BDF"
echo "Then:  cd /home/hygon/code/jinzixiang/xhci-test && python -m pytest tests/test_xhci.py -v"
