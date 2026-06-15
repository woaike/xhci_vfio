#!/usr/bin/env python3
"""Deploy xhci-test to target machine through jump host and run tests.

Uploads directly to target via SSH tunnel through jump host — no tools
needed on the jump host beyond a running SSH server.
"""

import os
import sys
import stat
import tempfile

import paramiko
from scp import SCPClient

JUMP_HOST = "10.65.10.1"
JUMP_USER = "jinzixiang"
JUMP_PASS = os.environ.get("JUMP_PASS", "")

TARGET_HOST = "10.65.46.207"
TARGET_USER = "root"
TARGET_PASS = os.environ.get("TARGET_PASS", "")
TARGET_PATH = "/home/hygon/jzx"

SRC_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ssh_exec(ssh, cmd, timeout=120):
    """Execute a command via SSH and return (rc, stdout, stderr)."""
    print(f"  >> {cmd[:150]}...")
    chan = ssh.get_transport().open_session()
    chan.settimeout(timeout)
    chan.exec_command(cmd)
    stdout = b""
    stderr = b""
    while True:
        if chan.recv_ready():
            stdout += chan.recv(65536)
        if chan.recv_stderr_ready():
            stderr += chan.recv_stderr(65536)
        if chan.exit_status_ready():
            break
    while chan.recv_ready():
        stdout += chan.recv(65536)
    while chan.recv_stderr_ready():
        stderr += chan.recv_stderr(65536)
    rc = chan.recv_exit_status()
    out = stdout.decode(errors='replace')
    err = stderr.decode(errors='replace')
    return rc, out, err


def make_tarball():
    """Create tarball of xhci-test source (excluding build/)."""
    import tarfile

    tarball = os.path.join(tempfile.gettempdir(), "xhci-test.tar.gz")
    print(f"Creating tarball from {SRC_DIR}...")
    with tarfile.open(tarball, 'w:gz') as tar:
        for root, dirs, files in os.walk(SRC_DIR):
            dirs[:] = [d for d in dirs if d != 'build']
            for f in files:
                full = os.path.join(root, f)
                arcname = os.path.relpath(full, os.path.dirname(SRC_DIR))
                tar.add(full, arcname=arcname)
    size = os.path.getsize(tarball)
    print(f"  Created: {tarball} ({size} bytes)")
    return tarball


def main():
    if not JUMP_PASS or not TARGET_PASS:
        print("FATAL: JUMP_PASS and TARGET_PASS env vars must be set")
        return 1

    tarball = make_tarball()
    try:
        # 1. Connect to jump host
        print(f"\n[1/4] Connecting to jump host {JUMP_USER}@{JUMP_HOST}...")
        jump = paramiko.SSHClient()
        jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        jump.connect(JUMP_HOST, username=JUMP_USER, password=JUMP_PASS,
                     look_for_keys=False)
        print(f"  Connected OK")

        # 2. Open tunnel through jump to target
        print(f"[2/4] Opening tunnel to target {TARGET_USER}@{TARGET_HOST}...")
        jump_transport = jump.get_transport()
        dest_addr = (TARGET_HOST, 22)
        tunnel = jump_transport.open_channel('direct-tcpip', dest_addr, ('', 0))
        print(f"  Tunnel OK")

        # 3. Connect to target through tunnel
        target = paramiko.SSHClient()
        target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        target.connect(TARGET_HOST, username=TARGET_USER, password=TARGET_PASS,
                       sock=tunnel, look_for_keys=False)
        print(f"  Connected to target OK")

        # 4. Upload tarball directly to target through tunnel
        print(f"[3/4] Uploading to target {TARGET_PATH}...")
        with SCPClient(target.get_transport()) as scp:
            scp.put(tarball, TARGET_PATH + '/xhci-test.tar.gz')
        print(f"  Upload OK")

        # 5. Extract
        rc, out, err = ssh_exec(target,
            f"cd {TARGET_PATH} && rm -rf xhci-test && tar xzf xhci-test.tar.gz && ls xhci-test/Makefile")
        if rc != 0:
            print(f"  Extract FAILED: {err[:300]}")
            return 1
        print(f"  Extract OK")

        # 6. Build
        print(f"[4/4] Building and testing...")
        rc, out, err = ssh_exec(target,
            f"cd {TARGET_PATH}/xhci-test && make clean && make all 2>&1", timeout=120)
        print(out[-2000:] if len(out) > 2000 else out)
        if rc != 0:
            print(f"  Build FAILED")
            if err.strip():
                print(f"  stderr: {err[:500]}")
            return 1
        print(f"  Build OK")

        # 7. Run tests
        print(f"\n  Running tests...")
        rc, out, err = ssh_exec(target,
            f"cd {TARGET_PATH}/xhci-test && LD_LIBRARY_PATH=build ./build/test_xhci 2>&1",
            timeout=300)
        print(out[-5000:] if len(out) > 5000 else out)
        if err.strip():
            print(f"  stderr: {err[:1000]}")
        if rc == 0:
            print(f"\n=== Tests completed successfully! ===")
        else:
            print(f"\n=== Tests exited with rc={rc} ===")

        target.close()
        jump.close()
        return rc

    finally:
        if os.path.exists(tarball):
            os.unlink(tarball)


if __name__ == '__main__':
    sys.exit(main())
