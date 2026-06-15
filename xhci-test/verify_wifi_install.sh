#!/usr/bin/env python3
"""验证wifi工具安装状态"""

import os, sys
import paramiko

JUMP_HOST = "10.65.10.1"
JUMP_USER = "jinzixiang"
TARGET_HOST = "10.65.46.174"
TARGET_USER = "root"
TIMEOUT = 15

def run_command(client, command):
    stdin, stdout, stderr = client.exec_command(command, timeout=TIMEOUT)
    output = stdout.read().decode('utf-8')
    error = stderr.read().decode('utf-8')
    return output, error

def main():
    jump_pass = os.environ.get("JUMP_PASS")
    target_pass = os.environ.get("TARGET_PASS")

    jump = paramiko.SSHClient()
    jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump.connect(JUMP_HOST, username=JUMP_USER, password=jump_pass, timeout=TIMEOUT)

    transport = jump.get_transport()
    channel = transport.open_channel("direct-tcpip", (TARGET_HOST, 22), (JUMP_HOST, 22))
    target = paramiko.SSHClient()
    target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    target.connect(TARGET_HOST, username=TARGET_USER, password=target_pass, sock=channel, timeout=TIMEOUT)

    commands = [
        ("=== 检查已安装的wifi工具 ===", "dpkg -l | grep -E 'iw|wireless|wpasupplicant|network-manager'"),
        ("=== 检查iw工具是否可用 ===", "which iw && iw --version"),
        ("=== 检查iwconfig工具 ===", "which iwconfig && iwconfig --version"),
        ("=== 扫描wifi网络 ===", "iwlist wlp1s0 scan 2>&1 | head -30"),
        ("=== 检查NetworkManager状态 ===", "systemctl status NetworkManager 2>&1 | head -10"),
        ("=== 列出可用的wifi接口 ===", "nmcli device status 2>&1"),
    ]

    for title, cmd in commands:
        print(title)
        print(f"执行: {cmd}")
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

if __name__ == "__main__":
    main()