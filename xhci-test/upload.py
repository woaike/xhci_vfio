#!/usr/bin/env python3
import sys
import os
import paramiko
import time

def upload_via_jump(jump_host, jump_user, jump_pass, target_host, target_user, target_pass, files, remote_dir):
    for attempt in range(5):
        try:
            print(f"Attempt {attempt+1}...")
            jump = paramiko.SSHClient()
            jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            jump.connect(jump_host, username=jump_user, password=jump_pass, timeout=15,
                        allow_agent=False, look_for_keys=False)
            transport = jump.get_transport()
            channel = transport.open_channel("direct-tcpip", (target_host, 22), ("127.0.0.1", 2222))
            target = paramiko.SSHClient()
            target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            target.connect(target_host, username=target_user, password=target_pass,
                          sock=channel, timeout=15, allow_agent=False, look_for_keys=False)
            break
        except Exception as e:
            print(f"Failed: {e}")
            time.sleep(5)
    else:
        print("Failed to connect after 5 attempts")
        return False

    print("Connected!")

    # Upload files
    sftp = target.open_sftp()
    for local, remote in files:
        try:
            sftp.put(local, remote)
            print(f"  Success: {os.path.basename(local)}")
        except Exception as e:
            print(f"  Failed: {os.path.basename(local)} - {e}")
    sftp.close()

    # Build
    stdin, stdout, stderr = target.exec_command(f"cd {remote_dir} && rm -rf build && make 2>&1")
    print("Build output:")
    out = stdout.read().decode()
    print(out[-500:] if len(out) > 500 else out)

    target.close()
    jump.close()
    return True

if __name__ == "__main__":
    if len(sys.argv) < 7:
        print("Usage: python upload.py JUMP_HOST JUMP_USER JUMP_PASS TARGET_HOST TARGET_USER TARGET_PASS")
        sys.exit(1)

    files = [
        (r"d:\code\10_65_46_49\xhci-test\src\xhci_enum.c", "/home/hygo/jzx/xhci-test/src/xhci_enum.c"),
        (r"d:\code\10_65_46_49\xhci-test\src\xhci_ops.c", "/home/hygo/jzx/xhci-test/src/xhci_ops.c"),
        (r"d:\code\10_65_46_49\xhci-test\src\xhci_state.c", "/home/hygo/jzx/xhci-test/src/xhci_state.c"),
        (r"d:\code\10_65_46_49\xhci-test\src\vfio.c", "/home/hygo/jzx/xhci-test/src/vfio.c"),
        (r"d:\code\10_65_46_49\xhci-test\include\xhci_state.h", "/home/hygo/jzx/xhci-test/include/xhci_state.h"),
    ]

    success = upload_via_jump(
        jump_host=sys.argv[1],
        jump_user=sys.argv[2],
        jump_pass=sys.argv[3],
        target_host=sys.argv[4],
        target_user=sys.argv[5],
        target_pass=sys.argv[6],
        files=files,
        remote_dir="/home/hygo/jzx/xhci-test"
    )

    sys.exit(0 if success else 1)
