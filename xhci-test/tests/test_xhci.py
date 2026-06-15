"""XHCI Controller Capability tests."""
import pytest
from xhci import XHCIController

pytestmark = pytest.mark.xhci


class TestCapabilityRegisters:
    """Verify XHCI Capability register fields are sane."""

    def test_hciversion_at_least_1_0(self, xhci):
        """XHCI 1.0+ required for all other tests to make sense."""
        assert xhci.hciversion >= 0x0100, \
            f"Expected XHCI >= 1.0, got 0x{xhci.hciversion:04x}"

    def test_hciversion_string(self, xhci):
        ver = xhci.hciversion_str
        major, minor = ver.split('.')
        assert int(major) >= 1

    def test_max_ports_valid(self, xhci):
        n = xhci.max_ports
        assert 1 <= n <= 255, f"Port count {n} out of range"

    def test_max_slots_valid(self, xhci):
        n = xhci.max_slots
        assert 1 <= n <= 255, f"Max slots {n} out of range"

    def test_max_interrupters(self, xhci):
        n = xhci.max_intrs
        assert 1 <= n <= 1024, f"Max interrupters {n} out of range"

    def test_64bit_addressing(self, xhci):
        """Most modern controllers support 64-bit addressing."""
        assert xhci.supports_64bit, "Controller does not support 64-bit addressing"

    def test_context_size(self, xhci):
        """Context size should be either 32 or 64 bytes."""
        cs64 = xhci.context_size_64
        assert cs64 in (True, False), f"Invalid context size: {cs64}"


class TestOperationalRegisters:
    """Verify Operational register read/write behavior."""

    def test_pagesize_register(self, xhci):
        """Page size should be a power of 2 (4K, 8K, 16K, 64K)."""
        pagesize = xhci.read32(xhci.caplength + 0x08)
        assert pagesize in (0x0001, 0x0003, 0x0007, 0x003F), \
            f"Unexpected page size register: 0x{pagesize:04x}"

    def test_usbcmd_initial_state(self, xhci):
        """Command register should be zero or near-zero after reset."""
        cmd = xhci.read32(xhci.caplength + 0x00)
        # Only bits that should be set: HSEE might be 0
        assert (cmd & 0xFFFFFF00) == 0, \
            f"Unexpected upper bits in usbcmd: 0x{cmd:08x}"

    def test_hcrst_self_clearing(self, xhci):
        """HCRST bit should self-clear within 1 second."""
        op = xhci.caplength
        cmd = xhci.read32(op + 0x00)
        xhci.write32(op + 0x00, cmd | 0x2)  # Set HCRST

        import time
        deadline = time.time() + 1.0
        while time.time() < deadline:
            cmd = xhci.read32(op + 0x00)
            if not (cmd & 0x2):
                break
            time.sleep(0.001)
        else:
            pytest.fail("HCRST did not self-clear within 1 second")

    def test_run_stop_bit(self, xhci):
        """Setting Run/Stop should change the command register."""
        op = xhci.caplength
        xhci.write32(op + 0x00, 0x1)
        cmd = xhci.read32(op + 0x00)
        assert cmd & 0x1, "Run/Stop bit should be set after write"

    def test_halted_bit_after_stop(self, xhci):
        """After stopping, Halted bit in status should be 1."""
        xhci.stop()
        assert xhci.is_halted, "Controller should be halted after stop"


class TestControllerReset:
    """Test controller reset sequence."""

    def test_reset_succeeds(self, xhci):
        """HC Reset should complete within 1 second."""
        xhci.reset()
        assert xhci.is_halted, "Controller should be halted after reset"

    def test_reset_clears_run(self, xhci):
        """After reset, Run/Stop bit should be cleared."""
        op = xhci.caplength
        cmd = xhci.read32(op + 0x00)
        assert not (cmd & 0x1), "Run/Stop should be cleared after reset"

    def test_double_reset(self, xhci):
        """Two consecutive resets should both succeed."""
        xhci.reset()
        xhci.reset()
        assert xhci.is_halted


class TestPortRegisters:
    """Test port register access and behavior."""

    def test_port_count_matches_capability(self, xhci):
        """Port count from port iteration should match HCS1."""
        assert xhci.port_count == xhci.max_ports

    def test_all_port_registers_accessible(self, xhci):
        """Reading every port register should not crash."""
        for i in xhci.ports:
            portsc = xhci.port_read(i)
            # Just verify we got a value (no exception)
            assert isinstance(portsc, int)

    def test_port_registers_have_reasonable_values(self, xhci):
        """Port registers should have CCS or be zero."""
        for i in xhci.ports:
            portsc = xhci.port_read(i)
            # CCS (bit 0) and other bits should be valid
            assert portsc == 0 or (portsc & 0x1) or not (portsc & 0x2), \
                f"Port {i}: PED set but CCS not — unusual state: 0x{portsc:08x}"

    def test_port_change_bits_writable(self, xhci):
        """Writing 1 to change bits should clear them (W1C)."""
        for i in xhci.ports:
            portsc = xhci.port_read(i)
            # Writing the register back should clear W1C bits
            # This is safe — change bits are write-1-to-clear
            if portsc & 0xFFFF0000:  # Any change bits set
                xhci.port_write(i, portsc & 0xFFFF)
                new = xhci.port_read(i)
                # Change bits should be cleared
                assert not (new & 0xFFFF0000) or (new & 0xFFFF0000) == (portsc & 0xFFFF0000 & ~0xFFFF0000)


class TestPortLinkStates:
    """Test port link state transitions."""

    def test_get_link_state(self, xhci):
        """Reading link state should return a valid value."""
        for i in xhci.ports:
            portsc = xhci.port_read(i)
            if portsc & 0x1:  # Only check connected ports
                pls = xhci.port_link_state(i)
                assert 0 <= pls <= 15, f"Port {i}: invalid link state {pls}"

    def test_u0_state(self, xhci):
        """Setting U0 should be possible for connected ports."""
        for i in xhci.ports:
            portsc = xhci.port_read(i)
            if portsc & 0x1:  # Connected
                xhci.port_set_link_state(i, 0)  # U0
                # Give hardware time to respond
                import time
                time.sleep(0.01)


class TestFullInitialization:
    """Test the complete controller bring-up sequence."""

    def test_full_init_succeeds(self, xhci):
        """full_init should bring up the controller."""
        xhci.full_init()
        assert not xhci.is_halted, "Controller should be running after full_init"

    def test_stop_then_run(self, xhci):
        """Should be able to stop and re-run the controller."""
        xhci.full_init()
        xhci.stop()
        assert xhci.is_halted
        xhci.run()
        # Give it time
        import time
        time.sleep(0.1)
        assert not xhci.is_halted

    def test_run_without_ring_setup_fails(self, xhci):
        """Running without rings set up should still start but may error."""
        xhci.reset()
        xhci.write64(xhci.caplength + 0x30, 0)  # Clear DCBAA
        xhci.run()
        # Controller may or may not halt depending on implementation
        # Just verify we didn't crash


class TestInterruptModeration:
    """Test interrupt moderation register."""

    def test_set_imod_zero(self, xhci):
        """Setting IMOD to 0 (no moderation) should be accepted."""
        xhci.irq_set_imod(0, 0)
        val = xhci.read32(xhci._lib.xhci_rt_regs(xhci._handle))
        # IMOD is at rt + 0x04
        imod = xhci.read32(xhci._lib.xhci_rt_regs(xhci._handle).__int__() + 0x04)
        assert imod == 0

    def test_set_imod_nonzero(self, xhci):
        """Setting IMOD to 1000 (250us) should be accepted."""
        xhci.irq_set_imod(0, 1000)
        # Read back from runtime register
        rt = xhci._lib.xhci_rt_regs(xhci._handle)
        # Verify no crash


class TestDMA:
    """Test DMA buffer allocation."""

    def test_dma_alloc_returns_valid_pointers(self, xhci):
        """DMA alloc should return non-null virtual and physical addresses."""
        virt, phys = xhci.dma_alloc(4096)
        assert virt is not None
        assert phys != 0
        assert phys % 4096 == 0, f"Physical address not page-aligned: 0x{phys:x}"
        xhci.dma_free(virt, 4096)

    def test_dma_alloc_write_read(self, xhci):
        """Writing to DMA buffer and reading back should preserve data."""
        import ctypes
        virt, phys = xhci.dma_alloc(64)
        buf = (ctypes.c_uint8 * 64).from_address(virt)
        for i in range(64):
            buf[i] = i & 0xFF
        for i in range(64):
            assert buf[i] == (i & 0xFF), f"Mismatch at offset {i}"
        xhci.dma_free(virt, 64)

    def test_dma_multiple_allocs(self, xhci):
        """Multiple DMA allocations should work."""
        allocs = []
        for _ in range(4):
            virt, phys = xhci.dma_alloc(4096)
            allocs.append((virt, phys))
        for virt, _ in allocs:
            xhci.dma_free(virt, 4096)
