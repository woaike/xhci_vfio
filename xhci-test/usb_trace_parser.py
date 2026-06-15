#!/usr/bin/env python3
"""
LeCroy USB Trace 解析器
解析USB分析仪捕获的数据包
"""

import struct
import os
from pathlib import Path

class LeCroyUSBParser:
    def __init__(self, trace_path):
        self.trace_path = trace_path
        self.packets = []
        self.device_info = {}

    def parse_usb_file(self):
        """解析LeCroy USB trace文件"""
        try:
            with open(self.trace_path, 'rb') as f:
                # 读取文件头
                header = f.read(256)
                if b'LeCroy Analyzer' in header:
                    print("[OK] LeCroy Analyzer Trace File 检测成功")
                    print(f"[OK] 协议: {self.extract_protocol(header)}")
                    return self.parse_trace_data(f)
                else:
                    print("[ERROR] 未知的文件格式")
                    return None
        except Exception as e:
            print(f"[ERROR] 文件读取错误: {e}")
            return None

    def extract_protocol(self, header):
        """从头部提取协议信息"""
        try:
            protocol = header.split(b'Protocol')[1].split(b'Suite')[0].strip()
            return protocol.decode('utf-8', errors='ignore')
        except:
            return "Unknown"

    def parse_trace_data(self, file_obj):
        """解析trace数据"""
        packets = []
        packet_count = 0

        print("开始解析USB数据包...")

        # 跳过头部，开始读取数据包
        while True:
            # 读取数据包头 (假设格式)
            packet_header = file_obj.read(16)
            if not packet_header or len(packet_header) < 16:
                break

            # 解析包头 (需要根据实际格式调整)
            try:
                timestamp, packet_type, device_addr, endpoint = struct.unpack('<QIHH', packet_header)

                packet_info = {
                    'timestamp': timestamp,
                    'type': packet_type,
                    'device_addr': device_addr,
                    'endpoint': endpoint,
                    'data': None
                }

                # 根据包类型读取数据
                if packet_type in [1, 2]:  # 假设1=IN, 2=OUT
                    data_length = struct.unpack('<I', file_obj.read(4))[0]
                    packet_data = file_obj.read(data_length)
                    packet_info['data'] = packet_data
                    packet_info['data_length'] = data_length

                packets.append(packet_info)
                packet_count += 1

                # 限制处理数量，避免过载
                if packet_count >= 1000:
                    print(f"已处理 {packet_count} 个数据包 (显示前1000个)")
                    break

            except struct.error:
                break

        print(f"[OK] 成功解析 {packet_count} 个USB数据包")
        return packets

    def analyze_packets(self, packets):
        """分析USB数据包"""
        if not packets:
            return

        print("\n=== USB数据包分析 ===")
        print(f"总数据包数: {len(packets)}")

        # 统计信息
        device_addrs = set()
        endpoints = set()
        packet_types = {}

        for packet in packets:
            device_addrs.add(packet['device_addr'])
            endpoints.add(packet['endpoint'])
            ptype = packet['type']
            packet_types[ptype] = packet_types.get(ptype, 0) + 1

        print(f"设备地址: {sorted(device_addrs)}")
        print(f"端点: {sorted(endpoints)}")
        print(f"包类型统计: {packet_types}")

        # 显示前几个数据包
        print("\n=== 前10个数据包详情 ===")
        for i, packet in enumerate(packets[:10]):
            print(f"包#{i+1}:")
            print(f"  时间戳: {packet['timestamp']}")
            print(f"  类型: {packet['type']}")
            print(f"  设备地址: {packet['device_addr']}")
            print(f"  端点: {packet['endpoint']}")
            if packet.get('data'):
                print(f"  数据长度: {packet['data_length']}")
                print(f"  数据(hex): {packet['data'][:32].hex()}")
            print()

def analyze_trace_directory(trace_dir):
    """分析trace目录"""
    trace_path = Path(trace_dir)
    if not trace_path.exists():
        print(f"[ERROR] 路径不存在: {trace_dir}")
        return

    print(f"=== 分析USB Trace目录: {trace_path} ===\n")

    # 查找.usb文件
    usb_files = list(trace_path.glob("*.usb"))
    if not usb_files:
        print("[ERROR] 未找到.usb文件")
        return

    # 解析每个USB文件
    for usb_file in usb_files:
        print(f"\n处理文件: {usb_file.name}")
        print("=" * 50)

        parser = LeCroyUSBParser(str(usb_file))
        packets = parser.parse_usb_file()

        if packets:
            parser.analyze_packets(packets)

        print()

def main():
    import sys

    if len(sys.argv) > 1:
        trace_dir = sys.argv[1]
    else:
        # 默认路径
        trace_dir = r"D:\USBtrace\force_gen1_loss_device\5_28_test"

    analyze_trace_directory(trace_dir)

if __name__ == "__main__":
    main()