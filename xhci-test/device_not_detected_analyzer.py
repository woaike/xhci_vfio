#!/usr/bin/env python3
"""
设备识别不到的USB trace分析
专门分析error_usb_device.usb文件
"""

import struct
from collections import defaultdict
import re

class DeviceNotDetectedAnalyzer:
    def __init__(self, trace_path):
        self.trace_path = trace_path
        self.enumeration_failures = []
        self.descriptor_failures = []
        self.protocol_errors = []

    def analyze(self):
        """分析设备识别失败的原因"""
        print("[DEVICE_NOT_DETECTED] 设备识别失败分析:")
        print("=" * 60)

        try:
            with open(self.trace_path, 'rb') as f:
                file_data = f.read()

            print(f"文件: {self.trace_path}")
            print(f"大小: {len(file_data):,} bytes ({len(file_data)/1024/1024:.2f} MB)")

            # 1. 分析设备枚举过程
            self.analyze_enumeration_process(file_data)

            # 2. 检测描述符请求失败
            self.detect_descriptor_failures(file_data)

            # 3. 分析协议错误
            self.analyze_protocol_errors(file_data)

            # 4. 检查USB状态
            self.check_usb_status(file_data)

            # 5. 对比正常情况
            self.compare_with_normal_trace()

            # 6. 生成诊断报告
            self.generate_diagnostic_report()

        except Exception as e:
            print(f"[ERROR] 分析失败: {e}")

    def analyze_enumeration_process(self, file_data):
        """分析设备枚举过程"""
        print("\n[ENUMERATION] 设备枚举过程分析:")
        print("-" * 40)

        # USB设备枚举的关键步骤
        enumeration_stages = {
            'reset': b'\x00\x00\x00\x00',  # USB Reset
            'get_descriptor': b'\x80\x06',     # Get Descriptor Request
            'set_address': b'\x00\x05',        # Set Address Request
            'get_config': b'\x80\x08',         # Get Configuration Request
            'set_config': b'\x00\x09',         # Set Configuration Request
        }

        print("枚举阶段检测:")
        for stage, pattern in enumeration_stages.items():
            count = file_data.count(pattern)
            if count > 0:
                print(f"  [OK] {stage}: {count} 次")
            else:
                print(f"  [FAIL] {stage}: 0 次 - 可能导致识别失败")
                self.enumeration_failures.append(stage)

        # 检测USB 3.0特定的枚举过程
        usb30_patterns = {
            'link_training': b'\x00\x00\x00\x01',  # Link Training Sequence
            'ltssm': b'\x00\x00\x00\x02',           # LTSSM state
            'polling': b'\x00\x00\x00\x03',          # Polling state
        }

        print("\nUSB 3.0枚举过程:")
        for stage, pattern in usb30_patterns.items():
            count = file_data.count(pattern)
            if count > 0:
                print(f"  [INFO] {stage}: {count} 次")
            else:
                print(f"  [WARN] {stage}: 0 次")

    def detect_descriptor_failures(self, file_data):
        """检测描述符请求失败"""
        print("\n[DESCRIPTOR] 描述符请求分析:")
        print("-" * 40)

        # 描述符请求模式
        descriptor_requests = {
            'device_descriptor': b'\x80\x06\x00\x01\x00\x00\x40',  # Get Device Descriptor
            'config_descriptor': b'\x80\x06\x00\x02\x00\x00\x40',   # Get Config Descriptor
            'string_descriptor': b'\x80\x06\x03\x03\x09\x04',       # Get String Descriptor
        }

        print("描述符请求检测:")
        for desc_type, pattern in descriptor_requests.items():
            count = file_data.count(pattern)
            if count > 0:
                print(f"  [OK] {desc_type}: {count} 次请求")
            else:
                print(f"  [FAIL] {desc_type}: 未检测到请求")
                self.descriptor_failures.append(desc_type)

        # 检测响应
        print("\n描述符响应检测:")
        # 查找描述符响应头
        response_pattern = b'\x12\x01'  # 设备描述符响应的前两个字节
        count = file_data.count(response_pattern)
        print(f"  设备描述符响应: {count} 次")

        if count == 0:
            print("  [CRITICAL] 缺少设备描述符响应 - 这是设备识别失败的主要原因!")
            self.descriptor_failures.append('device_descriptor_response')

    def analyze_protocol_errors(self, file_data):
        """分析协议错误"""
        print("\n[PROTOCOL] 协议错误分析:")
        print("-" * 40)

        # 常见的USB协议错误模式
        error_patterns = {
            'timeout': b'\x00\x00\x00\x00\x00\x00',
            'crc_error': b'\xff\xff\xff\xff',
            'pid_error': b'\xff\xff',
            'nak': b'\x5a',
            'stall': b'\x5f',
            'babble': b'\x87',
        }

        print("错误模式检测:")
        for error_type, pattern in error_patterns.items():
            count = file_data.count(pattern)
            if count > 0:
                print(f"  [ERROR] {error_type}: {count} 次")
                self.protocol_errors.append((error_type, count))
            else:
                print(f"  [OK] {error_type}: 0 次")

    def check_usb_status(self, file_data):
        """检查USB状态"""
        print("\n[STATUS] USB状态检查:")
        print("-" * 40)

        # 检查文件大小
        file_size = len(file_data)
        print(f"文件大小: {file_size:,} bytes")

        if file_size < 1000:
            print("  [WARN] 文件非常小，可能表示完全无响应")
        elif file_size < 100000:
            print("  [WARN] 文件较小，可能表示早期失败")
        else:
            print("  [OK] 文件大小正常")

        # 检查数据密度
        non_zero_count = sum(1 for byte in file_data[:10000] if byte != 0)
        data_density = non_zero_count / min(len(file_data), 10000)

        print(f"数据密度: {data_density:.2%}")

        if data_density < 0.1:
            print("  [CRITICAL] 数据密度极低，可能是设备完全无响应")
        elif data_density < 0.3:
            print("  [WARN] 数据密度较低")
        else:
            print("  [OK] 数据密度正常")

    def compare_with_normal_trace(self):
        """与正常trace对比"""
        print("\n[COMPARISON] 与正常trace对比:")
        print("-" * 40)

        try:
            # 查找正常trace文件
            import os
            trace_dir = os.path.dirname(self.trace_path)

            # 可能的正常文件
            normal_candidates = [
                'erji_poweron_d3_normal.usb',
                'erji_s0_d3_normal.usb',
                'CD32C_2USB3_0_1USB2_0.usb'
            ]

            for normal_file in normal_candidates:
                normal_path = os.path.join(trace_dir, normal_file)
                if os.path.exists(normal_path):
                    print(f"对比文件: {normal_file}")

                    # 对比文件大小
                    current_size = os.path.getsize(self.trace_path)
                    normal_size = os.path.getsize(normal_path)

                    print(f"  错误文件: {current_size:,} bytes")
                    print(f"  正常文件: {normal_size:,} bytes")

                    if normal_size > 0:
                        size_diff = abs(current_size - normal_size) / normal_size * 100
                        print(f"  大小差异: {size_diff:.1f}%")

                        if current_size < normal_size * 0.1:
                            print("  [CRITICAL] 错误文件大小远小于正常文件，确认设备无响应")
                        elif current_size < normal_size * 0.5:
                            print("  [WARN] 错误文件明显小于正常文件")
                    break
            else:
                print("  [INFO] 未找到正常trace文件进行对比")

        except Exception as e:
            print(f"  [ERROR] 对比分析失败: {e}")

    def generate_diagnostic_report(self):
        """生成诊断报告"""
        print("\n" + "=" * 60)
        print("[DIAGNOSIS] 诊断报告:")
        print("=" * 60)

        print("\n失败原因分析:")

        if self.enumeration_failures:
            print(f"  [FAIL] 枚举阶段失败:")
            for stage in self.enumeration_failures:
                print(f"    - {stage}")

        if self.descriptor_failures:
            print(f"  [FAIL] 描述符阶段失败:")
            for desc in self.descriptor_failures:
                print(f"    - {desc}")

        if self.protocol_errors:
            print(f"  [ERROR] 协议错误:")
            for error_type, count in self.protocol_errors:
                print(f"    - {error_type}: {count} 次")

        print("\n可能的根本原因:")

        if 'device_descriptor_response' in self.descriptor_failures:
            print("  * 设备完全无响应 - 硬件故障或供电问题")
            print("  * USB线缆问题 - 接触不良或线缆损坏")
            print("  * 设备初始化失败 - 固件问题")

        if 'get_descriptor' in self.enumeration_failures:
            print("  * USB控制器问题 - 主机端驱动问题")
            print("  * 设备响应超时 - 设备无法及时处理请求")

        if self.protocol_errors:
            print("  * 信号完整性问题 - 噪声干扰")
            print("  * 时序问题 - USB握手失败")

        print("\n建议措施:")

        if not self.enumeration_failures and not self.descriptor_failures:
            print("  * 检查设备硬件连接")
            print("  * 测试不同的USB端口")
            print("  * 更换USB线缆")
            print("  * 检查设备供电")
        else:
            print("  * 使用硬件USB分析仪检查物理层信号")
            print("  * 检查USB设备固件版本")
            print("  * 尝试在另一台主机上测试设备")
            print("  * 分析设备描述符返回的具体数据")

def main():
    import sys

    if len(sys.argv) > 1:
        trace_path = sys.argv[1]
    else:
        trace_path = r"D:\USBtrace\error_usb_device.usb"

    analyzer = DeviceNotDetectedAnalyzer(trace_path)
    analyzer.analyze()

    print(f"\n[COMPLETE] 设备识别失败分析完成")

if __name__ == "__main__":
    main()