#!/usr/bin/env python3
"""
密度突变位置详细分析
专门分析force_gen1_2.usb中的关键位置
"""

def analyze_mutation_positions(trace_path):
    """分析密度突变位置"""
    print("[MUTATION] 密度突变位置详细分析:")
    print("=" * 60)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 关键位置
        positions = [245632, 317523]

        for pos in positions:
            print(f"\n位置 {pos}:")
            print("-" * 40)

            # 读取该位置前后的数据
            start = max(0, pos - 64)
            end = min(len(file_data), pos + 64)
            chunk = file_data[start:end]

            print(f"前后数据 (十六进制):")
            for i in range(0, len(chunk), 16):
                hex_part = ' '.join(f'{b:02x}' for b in chunk[i:i+16])
                ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk[i:i+16])
                print(f"  {start+i:06x}: {hex_part:48s} {ascii_part}")

            # 分析该位置的数据特征
            zero_count = chunk.count(0)
            ff_count = chunk.count(255)
            other_count = len(chunk) - zero_count - ff_count

            print(f"\n数据统计:")
            print(f"  0x00字节: {zero_count}")
            print(f"  0xFF字节: {ff_count}")
            print(f"  其他字节: {other_count}")

            # 检测可能的USB包边界
            if zero_count > len(chunk) * 0.5:
                print("  [PATTERN] 高密度0x00 - 可能是包分隔符")
            if ff_count > len(chunk) * 0.3:
                print("  [PATTERN] 高密度0xFF - 可能是错误指示符")

    except Exception as e:
        print(f"[ERROR] 位置分析失败: {e}")

def analyze_packet_boundaries(trace_path):
    """分析数据包边界"""
    print("\n[BOUNDARIES] 数据包边界分析:")
    print("=" * 60)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 查找连续的0x00作为包边界候选
        boundary_candidates = []
        consecutive_count = 0

        for i, byte in enumerate(file_data):
            if byte == 0:
                consecutive_count += 1
            else:
                if consecutive_count > 2:  # 3个或更多连续0x00
                    boundary_candidates.append((i - consecutive_count, consecutive_count))
                consecutive_count = 0

        print(f"发现 {len(boundary_candidates)} 个可能的包边界")
        print(f"前20个边界候选:")
        for i, (pos, count) in enumerate(boundary_candidates[:20]):
            print(f"  位置 {pos:08x}: {count} 个连续0x00")

        # 分析边界间隔
        if len(boundary_candidates) > 1:
            intervals = [boundary_candidates[i+1][0] - boundary_candidates[i][0]
                        for i in range(min(len(boundary_candidates)-1, 100))]
            if intervals:
                avg_interval = sum(intervals) / len(intervals)
                print(f"\n边界间隔分析:")
                print(f"  平均间隔: {avg_interval:.2f} bytes")
                print(f"  最小间隔: {min(intervals)}")
                print(f"  最大间隔: {max(intervals)}")

    except Exception as e:
        print(f"[ERROR] 边界分析失败: {e}")

def main():
    import sys

    if len(sys.argv) > 1:
        trace_path = sys.argv[1]
    else:
        trace_path = r"D:\USBtrace\force_gen1_loss_device\5_28_test\force_gen1_2.usb"

    analyze_mutation_positions(trace_path)
    analyze_packet_boundaries(trace_path)

    print(f"\n[COMPLETE] 突变位置分析完成: {trace_path}")

if __name__ == "__main__":
    main()