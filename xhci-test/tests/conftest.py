import pytest
import sys
import os

# Add the tests directory to the path so xhci.py can be imported
sys.path.insert(0, os.path.dirname(__file__))


@pytest.fixture(scope="session")
def xhci_lib():
    """Load the C library once per session."""
    from xhci import load_lib
    return load_lib()


@pytest.fixture()
def xhci(xhci_lib):
    """Create a fresh XHCI controller connection for each test.

    Set XHCI_PCI env var to override the PCI BDF (default: 0000:00:14.0).
    """
    pci_bdf = os.environ.get("XHCI_PCI", "0000:05:00.1")

    from xhci import XHCIController
    with XHCIController(pci_bdf=pci_bdf) as ctrl:
        yield ctrl
