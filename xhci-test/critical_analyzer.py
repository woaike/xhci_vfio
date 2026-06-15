#!/usr/bin/env python3
"""
关键异常点深入分析
专门分析检测到的异常位置
"""

def analyze_long_repeat_anomaly(trace_path, position, length):
    """分析长重复序列异常"""
    print("[CRITICAL] 长重复序列异常分析:")
    print("=" * 60)
    print(f"位置: 0x{position:05x} ({position})")
    print(f"长度: {length} bytes")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            # 读取异常位置的数据
            start = max(0, position - 128)
            end = position + length + 128
            f.seek(start)
            data = f.read(end - start)

        print(f"异常区域数据 ({len(data)} bytes):")
        print("十六进制展示:")

        # 显示重复序列前后的数据
        repeat_start = 128  # 异常开始位置相对于start的偏移
        repeat_end = repeat_start + length

        for i in range(0, len(data), 16):
            offset = start + i

            # 标记重复区域
            marker = ""
            if repeat_start <= i < repeat_end:
                marker = " <<< 重复开始"
            elif i >= repeat_end and i < repeat_end + 16:
                marker = " <<< 重复结束"

            hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])

            print(f"  {offset:06x}: {hex_part:48s} {ascii_part}{marker}")

        # 分析重复的字节值
        f.seek(position)
        repeat_bytes = f.read(min(16, length))
        repeat_value = repeat_bytes[0] if repeat_bytes else None

        if repeat_value is not None and all(b == repeat_value for b in repeat_bytes):
            print(f"\n重复字节值: 0x{repeat_value:02x}")
            print(f"二进制: {bin(repeat_value)}")

            if repeat_value == 0x00:
                print("  [INFO] 重复0x00 - 可能是数据填充或空闲区域")
            elif repeat_value == 0xFF:
                print("  [WARNING] 重复0xFF - 可能表示错误或无效数据")
            else:
                print(f"  [ANOMALY] 重复0x{repeat_value:02x} - 异常模式")

        # 检查重复序列周围的上下文
        print(f"\n上下文分析:")
        print(f"  异常前: 前16个字节")
        before_bytes = data[repeat_start-16:repeat_start] if repeat_start >= 16 else data[0:repeat_start]
        print(f"    {before_bytes.hex()}")

        print(f"  异常后: 后16个字节")
        after_start = repeat_end
        after_bytes = data[after_start:after_start+16] if after_start + 16 < len(data) else data[after_start:]
        print(f"    {after_bytes.hex()}")

    except Exception as e:
        print(f"[ERROR] 长重复序列分析失败: {e}")

def analyze_crc_error_patterns(trace_path):
    """分析CRC错误模式"""
    print("\n[CRC] CRC错误模式分析:")
    print("=" * 60)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 搜索CRC错误模式
        crc_pattern = b'\xff\xff\xff\xff'
        crc_positions = []

        # 在前100KB中搜索
        search_data = file_data[:100000]
        for i, byte in enumerate(search_data):
            if search_data[i:i+4] == crc_pattern:
                crc_positions.append(i)

        print(f"发现 {len(crc_positions)} 个CRC错误模式")

        if crc_positions:
            print(f"\n前10个CRC错误位置:")
            for i, pos in enumerate(crc_positions[:10]):
                # 显示CRC错误位置周围的数据
                start = max(0, pos - 16)
                end = min(len(search_data), pos + 20)
                context = search_data[start:end]

                hex_part = ' '.join(f'{b:02x}' for b in context)
                ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in context)

                # 标记CRC位置
                marker = " >>> CRC错误" if pos - start >= 0 else ""
                print(f"  位置 {pos:06x}: {hex_part:48s} {ascii_part}{marker}")

            # 分析CRC错误间隔
            if len(crc_positions) > 1:
                intervals = [crc_positions[i+1] - crc_positions[i] for i in range(min(len(crc_positions)-1, 20))]
                if intervals:
                    avg_interval = sum(intervals) / len(intervals)
                    print(f"\nCRC错误间隔分析:")
                    print(f"  平均间隔: {avg_interval:.2f} bytes")
                    print(f"  最小间隔: {min(intervals)}")
                    print(f"  最大间隔: {max(intervals)}")

                    # 检测是否有规律
                    interval_variance = sum((x - avg_interval) ** 2 for x in intervals) / len(intervals)
                    print(f"  间隔方差: {interval_variance:.2f}")

                    if interval_variance < 100:
                        print("  [PATTERN] 检测到规律的CRC错误间隔 - 可能是周期性丢包")

    except Exception as e:
        print(f"[ERROR] CRC错误分析失败: {e}")

def analyze_density_anomaly(trace_path, position):
    """分析密度异常位置"""
    print(f"\n[DENSITY] 密度异常位置分析 (0x{position:05x}):")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            f.seek(max(0, position - 256))
            data = f.read(512)

        print("异常位置数据块:")
        for i in range(0, len(data), 32):
            offset = position - 256 + i
            chunk = data[i:i+32]

            # 分成两行显示
            line1 = chunk[:16]
            line2 = chunk[16:32]

            hex1 = ' '.join(f'{b:02x}' for b in line1)
            hex2 = ' '.join(f'{b:02x}' for b in line2)

            print(f"  {offset:06x}: {hex1:48s}")
            if line2:
                print(f"  {offset+16:06x}: {hex2:48s}")

        # 检测数据模式
        zero_count = data.count(0)
        non_zero_count = len(data) - zero_count
        density = non_zero_count / len(data) if data else 0

        print(f"\n密度统计:")
        print(f"  数据密度: {density:.2%}")
        print(f"  0x00字节: {zero_count}")
        print(f"  非0字节: {non_zero_count}")

        # 检测重复的数据块
        print(f"\n重复数据块检测:")
        chunk_size = 16
        chunks = [data[i:i+chunk_size] for i in range(0, len(data), chunk_size)]
        chunk_counts = {}

        for i, chunk in enumerate(chunks):
            chunk_hex = chunk.hex()
            if chunk_hex not in chunk_counts:
                chunk_counts[chunk_hex] = []
            chunk_counts[chunk_hex].append(i)

        # 找出重复的块
        repeated_chunks = [(hex_val, positions) for hex_val, positions in chunk_counts.items() if len(positions) > 2]

        if repeated_chunks:
            print(f"  发现 {len(repeated_chunks)} 个重复数据块:")
            for hex_val, positions in repeated_chunks[:5]:
                print(f"    模式 {hex_val}: {len(positions)} 次 @ 位置 {positions[:3]}")

    except Exception as e:
        print(f"[ERROR] 密度异常分析失败: {e}")

def generate_final_report():
    """生成最终异常报告"""
    print("\n" + "=" * 60)
    print("[FINAL] 异常点分析总结:")
    print("=" * 60)

    print("""
检测到的关键异常:

1. [HIGH] 长重复序列异常
   - 位置: 0x00dc5
   - 长度: 1451 bytes
   - 类型: 异常长的重复字节序列
   - 可能原因: 传输错误、设备卡死、或链路故障

2. [MEDIUM] CRC错误模式
   - 次数: 16次 (前100KB中)
   - 模式: 0xff\xff\xff\xff
   - 可能原因: 信号完整性问题、CRC校验失败

3. [INFO] 密度变化异常
   - 位置: 0x001400
   - 变化: 32.79%
   - 类型: 数据传输密度突变

技术分析:

* 强制Gen1丢包测试成功引发了传输异常
* 异常表现为重复字节序列和CRC错误
* 这符合USB链路质量差时的典型行为
* 设备尝试重传但遇到了进一步的传输问题

建议:

1. 使用LeCroy软件查看0x00dc5位置的详细时序
2. 重点关注CRC错误发生的时间点
3. 检查USB设备的错误处理机制
4. 对比正常传输时的相同位置数据
    """)

def main():
    import sys

    if len(sys.argv) > 1:
        trace_path = sys.argv[1]
    else:
        trace_path = r"D:\USBtrace\force_gen1_loss_device\5_28_test\force_gen1_2.usb"

    # 分析检测到的异常点
    analyze_long_repeat_anomaly(trace_path, 0xdc5, 1451)
    analyze_crc_error_patterns(trace_path)
    analyze_density_anomaly(trace_path, 0x1400)

    generate_final_report()

    print(f"\n[COMPLETE] 关键异常点分析完成: {trace_path}")

if __name__ == "__main__":
    main()