#!/usr/bin/env python3
"""
异常点检测和分析
专门分析force_gen1_2.usb中的异常位置
"""

import struct
from collections import defaultdict

class USBAnomalyDetector:
    def __init__(self, trace_path):
        self.trace_path = trace_path
        self.anomalies = []
        self.normal_regions = []

    def detect_all_anomalies(self):
        """检测所有异常点"""
        print("[ANOMALY] 开始异常点检测:")
        print("=" * 60)

        try:
            with open(self.trace_path, 'rb') as f:
                file_data = f.read()

            # 使用滑动窗口检测异常
            self.sliding_window_analysis(file_data)

            # 分析已知的关键位置
            self.analyze_known_anomaly_positions(file_data)

            # 检测USB协议异常
            self.detect_usb_protocol_anomalies(file_data)

            # 生成异常报告
            self.generate_anomaly_report()

        except Exception as e:
            print(f"[ERROR] 异常检测失败: {e}")

    def sliding_window_analysis(self, file_data):
        """滑动窗口分析"""
        print("\n[WINDOW] 滑动窗口异常检测:")
        print("-" * 40)

        window_size = 1024
        step_size = 512

        density_history = []
        anomaly_scores = []

        for i in range(0, min(len(file_data), 100000), step_size):  # 分析前100KB
            window = file_data[i:i+window_size]

            # 计算窗口特征
            density = sum(1 for byte in window if byte != 0) / len(window)
            zero_ratio = window.count(0) / len(window)
            entropy = self.calculate_entropy(window)

            density_history.append(density)

            # 检测异常
            if len(density_history) > 5:
                avg_density = sum(density_history[-5:]) / 5
                density_change = abs(density - avg_density)

                # 密度变化超过30%认为是异常
                if density_change > 0.3:
                    anomaly_score = density_change
                    anomaly_scores.append((i, anomaly_score, density))

                    if anomaly_score > 0.5:  # 高度异常
                        print(f"  [CRITICAL] 位置 {i:06x}: 密度变化 {density_change:.2%}")
                        self.anomalies.append({
                            'position': i,
                            'type': 'density_change',
                            'severity': 'critical',
                            'score': anomaly_score,
                            'density': density
                        })
                    elif anomaly_score > 0.3:
                        print(f"  [WARNING] 位置 {i:06x}: 密度变化 {density_change:.2%}")

        print(f"  检测到 {len(self.anomalies)} 个关键异常点")

    def calculate_entropy(self, data):
        """计算数据熵"""
        if not data:
            return 0

        byte_counts = defaultdict(int)
        for byte in data:
            byte_counts[byte] += 1

        entropy = 0
        data_len = len(data)
        for count in byte_counts.values():
            probability = count / data_len
            if probability > 0:
                import math
                entropy -= probability * math.log2(probability)

        return entropy

    def analyze_known_anomaly_positions(self, file_data):
        """分析已知异常位置"""
        print("\n[KNOWN] 已知异常位置详细分析:")
        print("-" * 40)

        known_positions = [245632, 317523]

        for pos in known_positions:
            print(f"\n位置 {pos} (0x{pos:05x}):")

            # 提取该位置的大块数据
            start = max(0, pos - 256)
            end = min(len(file_data), pos + 256)
            data_block = file_data[start:end]

            # 分析数据块特征
            self.analyze_data_block(data_block, pos)

    def analyze_data_block(self, data_block, position):
        """分析数据块"""
        print(f"  大小: {len(data_block)} bytes")

        # 字节统计
        byte_stats = defaultdict(int)
        for byte in data_block:
            byte_stats[byte] += 1

        # 找出最频繁的字节
        top_bytes = sorted(byte_stats.items(), key=lambda x: x[1], reverse=True)[:10]
        print("  最频繁字节:")
        for byte, count in top_bytes:
            print(f"    0x{byte:02x}: {count} 次 ({count/len(data_block)*100:.1f}%)")

        # 检测重复模式
        patterns = self.find_repeated_patterns(data_block)
        if patterns:
            print("  重复模式:")
            for pattern, count in patterns[:5]:
                print(f"    {pattern}: {count} 次")

        # 尝试解析USB结构
        self.try_parse_usb_structure(data_block)

    def find_repeated_patterns(self, data):
        """查找重复模式"""
        pattern_length = 4
        patterns = defaultdict(int)

        for i in range(len(data) - pattern_length):
            pattern = data[i:i+pattern_length]
            pattern_hex = ' '.join(f'{b:02x}' for b in pattern)
            patterns[pattern_hex] += 1

        # 返回出现次数最多的模式
        repeated = [(p, c) for p, c in patterns.items() if c > 3]
        return sorted(repeated, key=lambda x: x[1], reverse=True)

    def try_parse_usb_structure(self, data):
        """尝试解析USB数据结构"""
        print("  USB结构分析:")

        # 查找可能的USB包头
        potential_headers = []

        # USB 3.0 包头特征
        for i in range(0, len(data) - 16, 4):
            chunk = data[i:i+16]

            # 检查是否可能是包头
            if chunk[0:2] == b'\x00\x02' or chunk[0:2] == b'\x00\x01':
                potential_headers.append((i, chunk))

        if potential_headers:
            print(f"    发现 {len(potential_headers)} 个可能的包头")
            for offset, header in potential_headers[:3]:
                header_hex = ' '.join(f'{b:02x}' for b in header[:8])
                print(f"      偏移 {offset}: {header_hex}...")

        # 检查CRC错误特征
        error_indicators = [
            b'\xff\xff\xff\xff',  # 全1模式
            b'\x00\x00\x00\x00\x00\x00',  # 长串0
        ]

        for indicator in error_indicators:
            if indicator in data:
                positions = [i for i in range(len(data)) if data[i:i+len(indicator)] == indicator]
                print(f"    错误指示符 {indicator.hex()}: {len(positions)} 次")

    def detect_usb_protocol_anomalies(self, file_data):
        """检测USB协议异常"""
        print("\n[PROTOCOL] USB协议异常检测:")
        print("-" * 40)

        # 检测长串的相同字节（可能表示错误）
        max_repeat = 0
        repeat_position = 0
        current_repeat = 0
        current_byte = None

        for i, byte in enumerate(file_data[:100000]):  # 分析前100KB
            if byte == current_byte:
                current_repeat += 1
            else:
                if current_repeat > max_repeat:
                    max_repeat = current_repeat
                    repeat_position = i - current_repeat
                current_repeat = 1
                current_byte = byte

        print(f"  最大重复字节序列: {max_repeat} bytes @ 位置 {repeat_position:06x}")

        if max_repeat > 50:
            print(f"  [ANOMALY] 检测到异常长重复序列，可能是错误")
            self.anomalies.append({
                'position': repeat_position,
                'type': 'long_repeat',
                'severity': 'high',
                'length': max_repeat
            })

        # 检测CRC错误特征
        crc_error_pattern = b'\xff\xff\xff\xff'
        crc_count = file_data[:100000].count(crc_error_pattern)
        print(f"  CRC错误模式: {crc_count} 次")

        if crc_count > 10:
            print(f"  [ANOMALY] CRC错误模式频繁")
            self.anomalies.append({
                'position': 0,
                'type': 'crc_errors',
                'severity': 'medium',
                'count': crc_count
            })

    def generate_anomaly_report(self):
        """生成异常报告"""
        print("\n" + "=" * 60)
        print("[REPORT] 异常检测报告:")
        print("=" * 60)

        print(f"\n总异常数: {len(self.anomalies)}")

        if self.anomalies:
            print("\n异常详情:")
            print(f"{'位置':<10s} {'类型':<20s} {'严重性':<10s} {'详情'}")
            print("-" * 60)

            for anomaly in self.anomalies:
                pos = f"0x{anomaly['position']:05x}" if 'position' in anomaly else "N/A"
                atype = anomaly['type']
                severity = anomaly['severity']
                details = ""

                if 'score' in anomaly:
                    details = f"分数: {anomaly['score']:.2f}"
                elif 'length' in anomaly:
                    details = f"长度: {anomaly['length']}"
                elif 'count' in anomaly:
                    details = f"计数: {anomaly['count']}"

                print(f"{pos:<10s} {atype:<20s} {severity:<10s} {details}")

            print("\n严重性统计:")
            severity_count = defaultdict(int)
            for anomaly in self.anomalies:
                severity_count[anomaly['severity']] += 1

            for severity, count in severity_count.items():
                print(f"  {severity.upper()}: {count}")

        print("\n建议措施:")
        if any(a['severity'] == 'critical' for a in self.anomalies):
            print("  * 发现严重异常，需要重点关注")
            print("  * 建议使用LeCroy软件查看详细时序")
            print("  * 重点关注密度突变位置的数据包")
        elif any(a['severity'] == 'high' for a in self.anomalies):
            print("  * 检测到高度异常，建议进一步分析")
            print("  * 可能是丢包导致的重传现象")
        else:
            print("  * 异常程度较轻，属于正常测试范围")

def main():
    import sys

    if len(sys.argv) > 1:
        trace_path = sys.argv[1]
    else:
        trace_path = r"D:\USBtrace\force_gen1_loss_device\5_28_test\force_gen1_2.usb"

    detector = USBAnomalyDetector(trace_path)
    detector.detect_all_anomalies()

    print(f"\n[COMPLETE] 异常检测完成: {trace_path}")

if __name__ == "__main__":
    main()