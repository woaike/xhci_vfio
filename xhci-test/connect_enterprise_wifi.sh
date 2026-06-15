#!/usr/bin/env python3
"""连接802.1X企业wifi网络"""

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

    # 检查是否已有同名连接，如果有则删除
    print("=== 清理旧连接 ===")
    output, error = run_command(target, "nmcli connection show | grep '企业wifi'")
    if output.strip():
        print("发现旧连接，删除中...")
        run_command(target, "nmcli connection delete '企业wifi'")
        print("旧连接已删除")

    print("\n=== 开始配置企业wifi连接 ===\n")

    commands = [
        ("1. 创建wifi连接配置", "nmcli connection add type wifi con-name '企业wifi' ifname wlp1s0 ssid 'hytest'"),
        ("2. 设置WPA-EAP认证", "nmcli connection modify '企业wifi' wifi-sec.key-mgmt wpa-eap"),
        ("3. 设置EAP方法为TTLS", "nmcli connection modify '企业wifi' 802-1x.eap ttls"),
        ("4. 设置阶段2认证为PAP", "nmcli connection modify '企业wifi' 802-1x.phase2-auth pap"),
        ("5. 设置用户名", "nmcli connection modify '企业wifi' 802-1x.identity 'jinzixiang'"),
        ("6. 设置密码", "nmcli connection modify '企业wifi' 802-1x.password 'hygon@123'"),
        ("7. 启动连接", "nmcli connection up '企业wifi'"),
    ]

    for step, cmd in commands:
        print(f"{step}")
        print(f"执行: {cmd}")
        try:
            output, error = run_command(target, cmd)
            if output.strip():
                print(output)
            if error.strip():
                print(f"注意: {error}")
        except Exception as e:
            print(f"错误: {e}")
        print()

    # 检查连接状态
    print("=== 检查连接状态 ===")
    output, error = run_command(target, "nmcli connection show '企业wifi'")
    print(output if output.strip() else error)

    print("\n=== 检查网络状态 ===")
    output, error = run_command(target, "nmcli device status | grep wlp1s0")
    print(output if output.strip() else error)

    print("\n=== 测试网络连通性 ===")
    output, error = run_command(target, "ping -c 3 8.8.8.8")
    print(output if output.strip() else error)

    target.close()
    jump.close()
    print("\n配置完成!")

if __name__ == "__main__":
    main()