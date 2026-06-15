#!/bin/bash
# Full XHCI test runner - no Python needed
# Usage: sudo ./scripts/run_test.sh [PCI_BDF]
#        ./scripts/run_test.sh --no-sudo [PCI_BDF]   (if already bound to vfio-pci)
# Default PCI_BDF: 0000:05:00.1

set -e

PCI_BDF="${2:-0000:05:00.1}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "XHCI Test Runner"
echo "========================================"
echo "Controller: $PCI_BDF"
echo ""

# Step 1: Build
echo ">> Step 1: Building..."
cd "$PROJ_DIR"
make -j4

# Check if device is already bound to vfio-pci
CURRENT_DRIVER=$(readlink -f /sys/bus/pci/devices/$PCI_BDF/driver 2>/dev/null || true)
ALREADY_VFIO=false
if echo "$CURRENT_DRIVER" | grep -q vfio; then
    ALREADY_VFIO=true
fi

if [ "$1" = "--no-sudo" ] || [ "$ALREADY_VFIO" = "true" ]; then
    if [ "$ALREADY_VFIO" != "true" ]; then
        echo "WARNING: Device is NOT bound to vfio-pci. Skipping bind/unbind."
        echo "Hint: sudo bash scripts/bind_vfio.sh $PCI_BDF"
    else
        echo ">> Device is already bound to vfio-pci"
    fi

    echo ""
    echo ">> Step 2: Running tests..."
    echo ""
    XHCI_PCI=$PCI_BDF LD_LIBRARY_PATH=build build/test_xhci

    echo ""
    echo "Done. To restore device: sudo bash scripts/unbind_vfio.sh $PCI_BDF"
else
    # Step 2: Bind VFIO (requires sudo)
    echo ""
    echo ">> Step 2: Binding $PCI_BDF to vfio-pci..."
    sudo bash "$SCRIPT_DIR/bind_vfio.sh" "$PCI_BDF"

    # Step 3: Set permissions
    echo ""
    echo ">> Step 3: Setting permissions..."
    GROUP_NR=$(cat /sys/bus/pci/devices/$PCI_BDF/iommu_group | cut -d/ -f5)
    sudo chmod 0666 /dev/vfio/$GROUP_NR
    echo "  /dev/vfio/$GROUP_NR -> 0666"

    # Step 4: Run tests
    echo ""
    echo ">> Step 4: Running tests..."
    echo ""
    XHCI_PCI=$PCI_BDF LD_LIBRARY_PATH=build build/test_xhci

    # Step 5: Restore
    echo ""
    echo ">> Step 5: Restoring device..."
    sudo bash "$SCRIPT_DIR/unbind_vfio.sh" "$PCI_BDF"
fi
