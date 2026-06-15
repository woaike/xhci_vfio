#!/usr/bin/env python3
"""wifi连接示例脚本"""

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

    print("连接到目标机器...")
    jump = paramiko.SSHClient()
    jump.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump.connect(JUMP_HOST, username=JUMP_USER, password=jump_pass, timeout=TIMEOUT)

    transport = jump.get_transport()
    channel = transport.open_channel("direct-tcpip", (TARGET_HOST, 22), (JUMP_HOST, 22))
    target = paramiko.SSHClient()
    target.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    target.connect(TARGET_HOST, username=TARGET_USER, password=target_pass, sock=channel, timeout=TIMEOUT)
    print("连接成功!\n")

    # 首先列出可用的wifi网络
    print("=== 可用的wifi网络 ===")
    output, error = run_command(target, "nmcli device wifi list")
    print(output if output.strip() else error)
    print()

    # 示例1: 使用nmcli连接 (推荐)
    print("=== 方法1: 使用nmcli连接wifi ===")
    print("命令格式: nmcli device wifi connect \"网络名称\" password \"密码\"")
    print("示例: nmcli device wifi connect \"sgcdzt\" password \"your_password_here\"")
    print()

    # 示例2: 使用交互式界面
    print("=== 方法2: 使用nmtui交互式界面 ===")
    print("命令: nmtui")
    print("(在文本界面中选择wifi网络并输入密码)")
    print()

    # 示例3: 创建配置文件
    print("=== 方法3: 创建wifi配置文件 (永久保存) ===")
    print("可以使用以下命令创建连接配置:")
    print("nmcli connection add type wifi con-name \"我的wifi\" ifname wlp1s0 ssid \"sgcdzt\"")
    print("nmcli connection modify \"我的wifi\" wifi-sec.psk \"your_password_here\"")
    print("nmcli connection up \"我的wifi\"")
    print()

    # 检查当前连接状态
    print("=== 当前wifi连接状态 ===")
    output, error = run_command(target, "nmcli connection show")
    print(output if output.strip() else error)

    target.close()
    jump.close()
    print("\n脚本执行完成")

if __name__ == "__main__":
    main()