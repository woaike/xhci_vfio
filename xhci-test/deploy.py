#!/usr/bin/env python3
"""将 xhci-test 部署到 10.65.46.174"""

import os, sys
from pathlib import Path

try:
    import paramiko
except ImportError:
    print("pip install paramiko")
    sys.exit(1)

JUMP_HOST = "10.65.10.1"
JUMP_USER = "jinzixiang"
TARGET_HOST = "10.65.46.174"
TARGET_USER = "root"
REMOTE_DIR = "/home/hygon/jzx/xhci-test"
LOCAL_DIR = Path(__file__).resolve().parent
TIMEOUT = 15

def main():
    jump_pass = os.environ.get("JUMP_PASS")
    target_pass = os.environ.get("TARGET_PASS")
    if not jump_pass or not target_pass:
        print("请设置 JUMP_PASS 和 TARGET_PASS 环境变量")
        sys.exit(1)

    # 连跳板机
    jump = paramiko.SSHClient()
    jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump.connect(JUMP_HOST, username=JUMP_USER, password=jump_pass, timeout=TIMEOUT)
    print(f"[*] 跳板机 {JUMP_HOST} OK")

    # 连目标机
    transport = jump.get_transport()
    channel = transport.open_channel("direct-tcpip", (TARGET_HOST, 22), (JUMP_HOST, 22))
    target = paramiko.SSHClient()
    target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    target.connect(TARGET_HOST, username=TARGET_USER, password=target_pass, sock=channel, timeout=TIMEOUT)
    print(f"[*] 目标机 {TARGET_HOST} OK")

    sftp = target.open_sftp()
    uploaded = 0

    for f in LOCAL_DIR.rglob("*"):
        if not f.is_file():
            continue
        if any(x in f.parts for x in (".git", "__pycache__", "build", "orig_src")):
            continue

        rel = f.relative_to(LOCAL_DIR).as_posix()
        remote_path = f"{REMOTE_DIR}/{rel}"

        # 创建远程目录
        for i in range(2, len(remote_path.rsplit("/", 1)[0].split("/")) + 1):
            d = "/".join(remote_path.rsplit("/", 1)[0].split("/")[:i])
            try:
                sftp.stat(d)
            except IOError:
                sftp.mkdir(d)

        # 上传
        try:
            rstat = sftp.stat(remote_path)
            if rstat.st_size != f.stat().st_size:
                sftp.put(str(f), remote_path)
                print(f"  [U] {rel}")
                uploaded += 1
        except IOError:
            sftp.put(str(f), remote_path)
            print(f"  [+] {rel}")
            uploaded += 1

    sftp.close()
    target.close()
    jump.close()
    print(f"\n完成, 上传 {uploaded} 个文件到 {REMOTE_DIR}")

if __name__ == "__main__":
    main()
