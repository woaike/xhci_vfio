#!/usr/bin/env python3
"""检查目标机器上的wifi接收器驱动情况"""

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
    print(f"[OK] 跳板机 {JUMP_HOST} 连接成功")

    print("正在连接目标机...")
    transport = jump.get_transport()
    channel = transport.open_channel("direct-tcpip", (TARGET_HOST, 22), (JUMP_HOST, 22))
    target = paramiko.SSHClient()
    target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    target.connect(TARGET_HOST, username=TARGET_USER, password=target_pass, sock=channel, timeout=TIMEOUT)
    print(f"[OK] 目标机 {TARGET_HOST} 连接成功\n")

    commands = [
        ("=== 1. 检查USB设备 (wifi接收器) ===", "lsusb | grep -iE 'wireless|wifi|wlan|ralink|realtek|atheros'"),
        ("=== 2. 检查PCI设备 ===", "lspci | grep -iE 'wireless|wifi|wlan|network'"),
        ("=== 3. 检查网络接口 ===", "ip link show | grep -E '^\\d+:\\s+wlan|wl'"),
        ("=== 4. 检查已加载的无线内核模块 ===", "lsmod | grep -E 'wifi|wlan|wireless|rtl|ath|rt2|rt8|brcm'"),
        ("=== 5. 检查内核日志 ===", "dmesg | grep -iE 'wireless|wifi|wlan|firmware.*(rtl|ath)' | tail -20"),
        ("=== 6. 检查无线工具 ===", "which iw && echo 'iw 工具已安装' || echo 'iw 工具未安装'; which iwconfig && echo 'iwconfig 工具已安装' || echo 'iwconfig 工具未安装'"),
        ("=== 7. 检查系统信息 ===", "uname -r && cat /etc/os-release | grep -E 'NAME|VERSION'"),
        ("=== 8. 检查所有网络接口详细信息 ===", "ip link show"),
    ]

    for title, cmd in commands:
        print(title)
        print(f"执行命令: {cmd}")
        output, error = run_command(target, cmd)
        if output.strip():
            print(output)
        if error.strip():
            print(f"错误: {error}")
        print()

    target.close()
    jump.close()
    print("检查完成")

if __name__ == "__main__":
    main()