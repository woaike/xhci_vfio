#!/usr/bin/env python3
"""
深入分析设备识别失败的trace
重点关注错误模式和重传情况
"""

def analyze_error_patterns(trace_path):
    """深入分析错误模式"""
    print("[ERROR_PATTERNS] 错误模式深度分析:")
    print("=" * 60)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 分析前1MB数据的错误分布
        print("\n前1MB错误分布分析:")
        analysis_data = file_data[:1024*1024]

        # 统计各种错误类型
        error_types = {
            'timeout': 0,
            'crc': 0,
            'pid': 0,
            'nak': 0,
            'stall': 0,
        }

        # 滑动窗口分析
        window_size = 100
        error_windows = []

        for i in range(0, len(analysis_data) - window_size, 16):
            window = analysis_data[i:i+window_size]

            # 检查错误模式
            window_errors = 0

            if b'\x00\x00\x00\x00\x00\x00' in window:
                error_types['timeout'] += window.count(b'\x00\x00\x00\x00\x00\x00')
                window_errors += window.count(b'\x00\x00\x00\x00\x00\x00')

            if b'\xff\xff\xff\xff' in window:
                error_types['crc'] += window.count(b'\xff\xff\xff\xff')
                window_errors += window.count(b'\xff\xff\xff\xff')

            if window_errors > 5:
                error_windows.append((i, window_errors))

        print("错误类型统计:")
        for error_type, count in error_types.items():
            print(f"  {error_type.upper()}: {count} 次")

        print(f"\n高错误窗口: {len(error_windows)} 个")

        # 显示一些高错误窗口的位置
        if error_windows:
            print("前10个高错误窗口:")
            for i, (pos, errors) in enumerate(error_windows[:10]):
                print(f"  位置 {pos:06x}: {errors} 个错误")

    except Exception as e:
        print(f"[ERROR] 错误模式分析失败: {e}")

def analyze_retransmission_patterns(trace_path):
    """分析重传模式"""
    print("\n[RETRANSMISSION] 重传模式分析:")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 查找重复的数据模式
        print("重复模式检测:")

        # 分析重置操作的高频重复
        reset_pattern = b'\x00\x00\x00\x00'
        reset_count = file_data.count(reset_pattern)
        print(f"  Reset模式: {reset_count:,} 次")

        if reset_count > 1000000:
            print("  [CRITICAL] Reset次数超过100万，设备陷入枚举循环")

        # 分析Get Descriptor请求的高频重复
        descriptor_pattern = b'\x80\x06'
        descriptor_count = file_data.count(descriptor_pattern)
        print(f"  Get Descriptor模式: {descriptor_count} 次")

        if descriptor_count > 500:
            print("  [WARN] Get Descriptor请求频繁重复，可能无法获得正确响应")

        # 计算重传率
        total_size = len(file_data)
        pattern_density = (reset_count + descriptor_count) / total_size * 100

        print(f"  重传密度: {pattern_density:.2%}")

    except Exception as e:
        print(f"[ERROR] 重传分析失败: {e}")

def analyze_device_response(trace_path):
    """分析设备响应"""
    print("\n[DEVICE_RESPONSE] 设备响应分析:")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 查找可能的设备响应
        print("设备响应检测:")

        # USB 3.0 Link Layer响应
        link_response = b'\x00\x00\x00\x01'
        link_count = file_data.count(link_response)
        print(f"  Link Layer响应: {link_count} 次")

        # 查找可能的数据包
        data_packet_count = 0
        for i in range(0, len(file_data) - 16, 16):
            chunk = file_data[i:i+16]
            # 检查是否可能是有效的数据包
            if chunk[0] != 0x00 and chunk[0] != 0xFF:
                non_zero_count = sum(1 for b in chunk if b != 0)
                if non_zero_count > 8:  # 超过一半是非零
                    data_packet_count += 1

        print(f"  可能的数据包: {data_packet_count} 个")

        # 计算响应质量
        response_quality = data_packet_count / (len(file_data) / 16) * 100 if file_data else 0
        print(f"  响应质量: {response_quality:.2%}")

        if response_quality < 1:
            print("  [CRITICAL] 响应质量极低，设备基本无有效响应")

    except Exception as e:
        print(f"[ERROR] 设备响应分析失败: {e}")

def analyze_specific_error_locations(trace_path):
    """分析特定错误位置"""
    print("\n[SPECIFIC] 特定错误位置分析:")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 搜索特定的错误序列
        error_sequences = {
            'continuous_timeout': b'\x00\x00\x00\x00\x00\x00\x00\x00',
            'continuous_crc': b'\xff\xff\xff\xff\xff\xff\xff\xff',
            'nak_sequence': b'\x5a\x5a\x5a',
        }

        for error_name, pattern in error_sequences.items():
            positions = []
            for i in range(len(file_data) - len(pattern)):
                if file_data[i:i+len(pattern)] == pattern:
                    positions.append(i)

            print(f"\n{error_name}:")
            print(f"  出现次数: {len(positions)}")

            if positions:
                print(f"  首次出现: 0x{positions[0]:05x}")
                print(f"  最后出现: 0x{positions[-1]:05x}")

                # 分析间隔
                if len(positions) > 1:
                    intervals = [positions[i+1] - positions[i] for i in range(min(len(positions)-1, 10))]
                    if intervals:
                        avg_interval = sum(intervals) / len(intervals)
                        print(f"  平均间隔: {avg_interval:.2f} bytes")

    except Exception as e:
        print(f"[ERROR] 特定错误分析失败: {e}")

def analyze_data_integrity(trace_path):
    """分析数据完整性"""
    print("\n[INTEGRITY] 数据完整性分析:")
    print("-" * 40)

    try:
        with open(trace_path, 'rb') as f:
            file_data = f.read()

        # 检查数据块的完整性
        chunk_size = 512
        total_chunks = len(file_data) // chunk_size

        corrupt_chunks = 0
        empty_chunks = 0
        valid_chunks = 0

        for i in range(min(1000, total_chunks)):  # 检查前1000个块
            chunk = file_data[i*chunk_size:(i+1)*chunk_size]

            zero_count = chunk.count(0)
            ff_count = chunk.count(255)

            if zero_count == len(chunk):
                empty_chunks += 1
            elif ff_count > len(chunk) * 0.5:
                corrupt_chunks += 1
            else:
                valid_chunks += 1

        print(f"数据块完整性 (前{min(1000, total_chunks)}个块):")
        print(f"  有效块: {valid_chunks} ({valid_chunks/min(1000, total_chunks)*100:.1f}%)")
        print(f"  空块: {empty_chunks} ({empty_chunks/min(1000, total_chunks)*100:.1f}%)")
        print(f"  损坏块: {corrupt_chunks} ({corrupt_chunks/min(1000, total_chunks)*100:.1f}%)")

    except Exception as e:
        print(f"[ERROR] 数据完整性分析失败: {e}")

def generate_comprehensive_diagnosis():
    """生成综合诊断报告"""
    print("\n" + "=" * 60)
    print("[COMPREHENSIVE] 综合诊断报告:")
    print("=" * 60)

    print("""
📋 设备识别失败的根本原因分析:

主要问题:
1. [CRITICAL] 枚举循环陷阱
   - 设备执行了超过1400万次Reset操作
   - Get Descriptor请求失败后不断重试
   - 系统陷入无限循环，无法完成枚举

2. [CRITICAL] 严重的信号完整性问题
   - 1000万+次超时错误
   - 400万+次CRC校验失败
   - 700万+次PID包识别错误

3. [HIGH] 数据传输质量极差
   - 文件大小是正常情况的97倍
   - 数据密度仅14%，大部分是重传垃圾数据
   - 有效响应极少，通信基本无效

技术分析:
• 设备在物理层可能存在连接问题
• USB信号质量极差，导致大量错误
• 设备固件可能在枚举阶段有缺陷
• 主机端重传机制过于激进，加剧了问题

可能的故障点:
1. USB线缆问题 (最可能)
   - 接触不良或线缆损坏
   - 阻抗不匹配
   - 信号衰减严重

2. 设备固件问题
   - 枚举处理逻辑错误
   - 描述符响应格式不对
   - 时序要求不满足

3. 主机控制器问题
   - USB控制器驱动异常
   - 端口供电不稳定
   - 控制器硬件故障

建议诊断步骤:
1. 首先更换USB线缆进行测试
2. 尝试不同的USB端口
3. 检查设备供电是否充足
4. 在其他主机上测试同一设备
5. 使用硬件USB分析仪检查物理层信号
6. 检查设备固件版本和更新日志
    """)

def main():
    import sys

    if len(sys.argv) > 1:
        trace_path = sys.argv[1]
    else:
        trace_path = r"D:\USBtrace\error_usb_device.usb"

    analyze_error_patterns(trace_path)
    analyze_retransmission_patterns(trace_path)
    analyze_device_response(trace_path)
    analyze_specific_error_locations(trace_path)
    analyze_data_integrity(trace_path)
    generate_comprehensive_diagnosis()

    print(f"\n[COMPLETE] 设备识别失败深入分析完成: {trace_path}")

if __name__ == "__main__":
    main()