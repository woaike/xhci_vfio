CC ?= gcc
CFLAGS := -Wall -Wextra -O2 -fPIC -Iinclude

LIB_SRCS = src/vfio.c src/xhci_ops.c src/xhci_state.c src/xhci_enum.c src/xhci_bot.c

.PHONY: all clean test run enum

all: build/libxhci.so build/main build/test_xhci build/test_example.so build/test_bot

build/libxhci.so: $(LIB_SRCS) include/xhci.h include/xhci_internal.h include/xhci_regs.h include/xhci_state.h include/xhci_bot.h
	mkdir -p build
	$(CC) $(CFLAGS) -shared -o $@ $^

build/main: src/main.c build/libxhci.so
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -lxhci -ldl

build/test_xhci: src/test_xhci.c build/libxhci.so
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -lxhci

build/test_example.so: src/test_example.c build/libxhci.so
	$(CC) $(CFLAGS) -shared -o $@ $< -Lbuild -lxhci

build/test_bot: src/test_bot.c build/libxhci.so
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -lxhci

# Generic pattern for additional test cases: make build/test_mytest.so
build/test_%.so: src/test_%.c build/libxhci.so
	$(CC) $(CFLAGS) -shared -o $@ $< -Lbuild -lxhci

clean:
	rm -rf build

# Quick test - just run the old test binary
test: build/test_xhci
	LD_LIBRARY_PATH=build ./build/test_xhci

# Enumerate and save state
enum: build/main build/libxhci.so
	XHCI_PCI=$(PCI) ./build/main --enum

# Run a test case with saved state
# Usage: make test-case SO=build/test_example.so PCI=0000:86:00.1
test-case: build/main build/libxhci.so
	@if [ -z "$(SO)" ]; then echo "Usage: make test-case SO=build/test_example.so"; exit 1; fi
	XHCI_PCI=$(PCI) ./build/main --test $(SO)

# Full run - bind VFIO, set permissions, run test, unbind
# Usage: make run PCI=0000:86:00.1
PCI ?= 0000:05:00.1
run: build/test_xhci
	sudo bash scripts/bind_vfio.sh $(PCI)
	sudo chmod 0666 /dev/vfio/$$(cat /sys/bus/pci/devices/$(PCI)/iommu_group 2>/dev/null | cut -d/ -f5)
	@echo ""
	@echo "Running tests..."
	@echo ""
	XHCI_PCI=$(PCI) LD_LIBRARY_PATH=build ./build/test_xhci
	@echo ""
	@echo "Restoring device to kernel driver..."
	sudo bash scripts/unbind_vfio.sh $(PCI)
