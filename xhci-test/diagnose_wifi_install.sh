#!/usr/bin/env python3
"""诊断wifi工具安装失败问题"""

import os, sys
import paramiko

JUMP_HOST = "10.65.10.1"
JUMP_USER = "jinzixiang"
TARGET_HOST = "10.65.46.174"
TARGET_USER = "root"
TIMEOUT = 15

def run_command(client, command):
    """在远程主机上执行命令"""
    stdin, stdout, stderr = client.exec_command(command, timeout=TIMEOUT)
    output = stdout.read().decode('utf-8')
    error = stderr.read().decode('utf-8')
    return output, error

def main():
    jump_pass = os.environ.get("JUMP_PASS")
    target_pass = os.environ.get("TARGET_PASS")
    if not jump_pass or not target_pass:
        print("请设置 JUMP_PASS 和 TARGET_PASS 环境变量")
        sys.exit(1)

    print("正在连接跳板机...")
    jump = paramiko.SSHClient()
    jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump.connect(JUMP_HOST, username=JUMP_USER, password=jump_pass, timeout=TIMEOUT)
    print(f"[OK] 跳板机连接成功")

    print("正在连接目标机...")
    transport = jump.get_transport()
    channel = transport.open_channel("direct-tcpip", (TARGET_HOST, 22), (JUMP_HOST, 22))
    target = paramiko.SSHClient()
    target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    target.connect(TARGET_HOST, username=TARGET_USER, password=target_pass, sock=channel, timeout=TIMEOUT)
    print(f"[OK] 目标机连接成功\n")

    commands = [
        ("=== 1. 检查网络连接状态 ===", "ping -c 3 8.8.8.8"),
        ("=== 2. 检查DNS解析 ===", "nslookup archive.ubuntu.com"),
        ("=== 3. 检查apt源配置 ===", "cat /etc/apt/sources.list"),
        ("=== 4. 检查apt缓存 ===", "ls -lh /var/cache/apt/archives/ | head -10"),
        ("=== 5. 检查磁盘空间 ===", "df -h"),
        ("=== 6. 尝试更新apt缓存 ===", "apt-get update 2>&1 | tail -20"),
        ("=== 7. 检查最近的apt历史 ===", "cat /var/log/apt/history.log | tail -50"),
        ("=== 8. 检查dpkg状态 ===", "dpkg --configure -a 2>&1"),
    ]

    for title, cmd in commands:
        print(title)
        print(f"执行命令: {cmd}")
        try:
            output, error = run_command(target, cmd)
            if output.strip():
                print(output)
            if error.strip():
                print(f"错误: {error}")
        except Exception as e:
            print(f"执行出错: {e}")
        print()

    target.close()
    jump.close()
    print("诊断完成")

if __name__ == "__main__":
    main()