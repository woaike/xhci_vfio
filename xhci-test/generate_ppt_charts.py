#!/usr/bin/env python3
"""
PPT图表生成脚本
生成用于PPT展示的各种图表：性能对比图、流程图、硬件配置图等
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle, Circle
import numpy as np
import os

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# 创建输出目录
output_dir = "ppt_charts"
os.makedirs(output_dir, exist_ok=True)

def create_performance_chart():
    """生成性能对比柱状图 - 模块2"""
    fig, ax = plt.subplots(figsize=(10, 6))

    # 数据
    before_time = 5  # 优化前5分钟
    after_time = 1   # 优化后1分钟

    categories = ['优化前', '优化后']
    times = [before_time, after_time]
    colors = ['#FF6B6B', '#4ECDC4']

    # 创建柱状图
    bars = ax.bar(categories, times, color=colors, alpha=0.8, edgecolor='black', linewidth=2)

    # 添加数值标签
    for bar, time in zip(bars, times):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{time}分钟',
                ha='center', va='bottom', fontsize=16, fontweight='bold')

    # 标题和标签
    ax.set_ylabel('响应时间 (分钟)', fontsize=14, fontweight='bold')
    ax.set_title('openclaw查询响应时间对比', fontsize=16, fontweight='bold', pad=20)

    # 添加性能提升标注
    improvement = ((before_time - after_time) / before_time) * 100
    ax.annotate(f'性能提升 {improvement:.0f}%',
                xy=(1, after_time), xytext=(0.5, 3),
                arrowprops=dict(arrowstyle='->', lw=2, color='green'),
                fontsize=14, color='green', fontweight='bold',
                ha='center')

    # 设置y轴范围
    ax.set_ylim(0, 6)
    ax.grid(axis='y', alpha=0.3, linestyle='--')

    plt.tight_layout()
    plt.savefig(f'{output_dir}/performance_comparison.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("[OK] 性能对比图已生成")

def create_hardware_diagram():
    """生成硬件配置示意图 - 模块1"""
    fig, ax = plt.subplots(figsize=(12, 8))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')

    # 标题
    ax.text(5, 9.5, '本地AI服务器硬件配置',
            ha='center', fontsize=18, fontweight='bold')

    # 服务器机箱
    server_box = FancyBboxPatch((2, 2), 6, 6, boxstyle="round,pad=0.1",
                                  edgecolor='black', facecolor='#E8F4F8', linewidth=3)
    ax.add_patch(server_box)

    # CPU x2 (海光C86-4G 7480)
    cpu1 = Rectangle((2.5, 6.5), 1.5, 1, edgecolor='black', facecolor='#3498DB', linewidth=2)
    cpu2 = Rectangle((6, 6.5), 1.5, 1, edgecolor='black', facecolor='#3498DB', linewidth=2)
    ax.add_patch(cpu1)
    ax.add_patch(cpu2)

    ax.text(5, 7, 'CPU x2', ha='center', fontsize=12, fontweight='bold', color='white')
    ax.text(3.25, 7, '海光\nC86-4G\n7480', ha='center', va='center', fontsize=9, color='white')
    ax.text(6.75, 7, '海光\nC86-4G\n7480', ha='center', va='center', fontsize=9, color='white')

    # GPU x2 (NVIDIA A5000)
    gpu1 = Rectangle((2.5, 4.5), 1.5, 1, edgecolor='black', facecolor='#2ECC71', linewidth=2)
    gpu2 = Rectangle((6, 4.5), 1.5, 1, edgecolor='black', facecolor='#2ECC71', linewidth=2)
    ax.add_patch(gpu1)
    ax.add_patch(gpu2)

    ax.text(5, 5.2, 'GPU x2', ha='center', fontsize=12, fontweight='bold', color='white')
    ax.text(3.25, 5, 'NVIDIA\nA5000', ha='center', va='center', fontsize=9, color='white')
    ax.text(6.75, 5, 'NVIDIA\nA5000', ha='center', va='center', fontsize=9, color='white')

    # 其他配置
    ax.text(5, 3.5, '本地AI服务器环境就绪', ha='center', fontsize=14,
            fontweight='bold', color='#E74C3C')

    # 添加连接线
    ax.plot([5, 5], [6.5, 4.5], 'k--', alpha=0.3, linewidth=2)

    plt.tight_layout()
    plt.savefig(f'{output_dir}/hardware_config.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("[OK] 硬件配置图已生成")

def create_knowledge_flow():
    """生成知识库调用流程图 - 模块3"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')

    # 标题
    ax.text(7, 9.5, '知识库智能调用流程',
            ha='center', fontsize=18, fontweight='bold')

    # 流程节点
    nodes = [
        (2, 7, '用户\n问题', '#3498DB'),
        (5, 7, '向量匹配', '#2ECC71'),
        (8, 7, '关键字\n匹配', '#2ECC71'),
        (11, 7, 'agNet\n处理', '#F39C12'),
        (6.5, 4, '知识库\n覆盖?', '#9B59B6'),
        (3, 1.5, '执行\n回答', '#E74C3C'),
        (10, 1.5, '严格执行\n拒答策略', '#E74C3C')
    ]

    # 绘制节点
    for x, y, text, color in nodes:
        circle = Circle((x, y), 0.8, edgecolor='black', facecolor=color, linewidth=2, alpha=0.8)
        ax.add_patch(circle)
        ax.text(x, y, text, ha='center', va='center', fontsize=11,
                fontweight='bold', color='white')

    # 绘制箭头
    arrows = [
        ((2.8, 7), (4.2, 7)),     # 问题 -> 向量匹配
        ((8.8, 7), (10.2, 7)),    # 关键字匹配 -> agNet处理
        ((5.8, 7), (6.5, 4.8)),   # 向量匹配 -> 判断
        ((8.8, 7), (7.2, 4.8)),   # 关键字匹配 -> 判断
        ((6.5, 3.2), (3.8, 2.3)), # 覆盖 -> 回答
        ((6.5, 3.2), (9.2, 2.3)), # 不覆盖 -> 拒答
    ]

    for start, end in arrows:
        arrow = FancyArrowPatch(start, end, arrowstyle='->', lw=2, color='black')
        ax.add_patch(arrow)

    # 添加判断标签
    ax.text(5, 3.5, '是', fontsize=12, fontweight='bold', color='green')
    ax.text(8, 3.5, '否', fontsize=12, fontweight='bold', color='red')

    # 添加SSH同步说明
    ax.text(7, 0.5, 'SSH自动同步Confluence知识库',
            ha='center', fontsize=12, style='italic', color='#7F8C8D')

    plt.tight_layout()
    plt.savefig(f'{output_dir}/knowledge_flow.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("[OK] 知识库流程图已生成")

def create_automation_workflow():
    """生成自动化工作流流程图 - 模块4"""
    fig, ax = plt.subplots(figsize=(12, 10))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 12)
    ax.axis('off')

    # 标题
    ax.text(6, 11.5, '自动化代码工作流',
            ha='center', fontsize=18, fontweight='bold')

    # 主循环流程（圆形布局）
    center_x, center_y = 6, 6
    radius = 3.5

    phases = [
        (6, 9.5, 'AI\n代码生成', '#3498DB'),
        (9.5, 7.5, '自动化\n部署', '#2ECC71'),
        (9.5, 4.5, '环境\n搭建', '#F39C12'),
        (6, 2.5, 'XHC命令\n环实现', '#9B59B6'),
        (2.5, 4.5, '智能\n调试', '#E74C3C'),
        (2.5, 7.5, 'VFIO\n框架', '#1ABC9C')
    ]

    # 绘制节点
    for x, y, text, color in phases:
        circle = Circle((x, y), 1, edgecolor='black', facecolor=color,
                       linewidth=2, alpha=0.8)
        ax.add_patch(circle)
        ax.text(x, y, text, ha='center', va='center', fontsize=10,
                fontweight='bold', color='white')

    # 绘制循环箭头
    angles = [90, 30, -30, -90, -150, 150]  # 角度位置
    for i in range(len(angles)):
        start_angle = np.radians(angles[i])
        end_angle = np.radians(angles[(i + 1) % len(angles)])

        start_x = center_x + radius * np.cos(start_angle)
        start_y = center_y + radius * np.sin(start_angle)
        end_x = center_x + radius * np.cos(end_angle)
        end_y = center_y + radius * np.sin(end_angle)

        # 弧形箭头
        arrow = FancyArrowPatch((start_x, start_y), (end_x, end_y),
                               arrowstyle='->', lw=2.5, color='#34495E',
                               connectionstyle=f"arc3,rad={0.3}")
        ax.add_patch(arrow)

    # 中心说明
    ax.text(center_x, center_y, '闭环\n自动化', ha='center', va='center',
           fontsize=14, fontweight='bold', color='#2C3E50')

    # 底部重点标注
    ax.text(6, 1, '🎯 核心突破: VFIO XHC命令环成功实现',
            ha='center', fontsize=13, fontweight='bold', color='#E74C3C')

    plt.tight_layout()
    plt.savefig(f'{output_dir}/automation_workflow.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("[OK] 自动化工作流图已生成")

def create_summary_dashboard():
    """生成总览仪表板"""
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    fig.suptitle('AI本地化部署与效能优化方案总览', fontsize=20, fontweight='bold')

    # 硬件配置 (左上)
    ax1 = axes[0, 0]
    ax1.set_title('1️⃣ 硬件环境', fontsize=14, fontweight='bold')
    ax1.text(0.5, 0.7, '🖥️ 双海光C86-4G 7480', ha='center', fontsize=12)
    ax1.text(0.5, 0.5, '🎮 双NVIDIA A5000', ha='center', fontsize=12)
    ax1.text(0.5, 0.3, '✅ 本地AI服务器就绪', ha='center', fontsize=13,
             fontweight='bold', color='green')
    ax1.axis('off')

    # 性能优化 (右上)
    ax2 = axes[0, 1]
    ax2.set_title('2️⃣ 性能优化 ⚡', fontsize=14, fontweight='bold')
    categories = ['优化前', '优化后']
    times = [5, 1]
    colors = ['#FF6B6B', '#4ECDC4']
    bars = ax2.bar(categories, times, color=colors, alpha=0.8)
    for bar, time in zip(bars, times):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{time}分钟', ha='center', va='bottom', fontsize=12, fontweight='bold')
    ax2.set_ylabel('响应时间 (分钟)', fontsize=11)
    ax2.set_ylim(0, 6)
    improvement = 80
    ax2.text(0.5, 0.95, f'⚡ 性能提升 {improvement}%',
             transform=ax2.transAxes, ha='center', fontsize=13, fontweight='bold', color='green')

    # 知识库集成 (左下)
    ax3 = axes[1, 0]
    ax3.set_title('3️⃣ 知识库集成 🧠', fontsize=14, fontweight='bold')
    ax3.text(0.5, 0.8, '🔍 向量匹配 + 关键字匹配', ha='center', fontsize=11)
    ax3.text(0.5, 0.6, '🤖 agNet提示词优化', ha='center', fontsize=11)
    ax3.text(0.5, 0.4, '🛡️ 智能拒答策略', ha='center', fontsize=11)
    ax3.text(0.5, 0.2, '🔄 SSH同步Confluence', ha='center', fontsize=11)
    ax3.axis('off')

    # 自动化工作流 (右下)
    ax4 = axes[1, 1]
    ax4.set_title('4️⃣ 自动化工作流 🔄', fontsize=14, fontweight='bold')
    ax4.text(0.5, 0.8, '🔧 AI调试链路', ha='center', fontsize=11)
    ax4.text(0.5, 0.6, '🏗️ D3环境搭建', ha='center', fontsize=11)
    ax4.text(0.5, 0.4, '🎯 XHC命令环实现', ha='center', fontsize=11, fontweight='bold', color='red')
    ax4.text(0.5, 0.2, '✅ 完整自动化闭环', ha='center', fontsize=11)
    ax4.axis('off')

    plt.tight_layout()
    plt.savefig(f'{output_dir}/summary_dashboard.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("[OK] 总览仪表板已生成")

def main():
    """生成所有图表"""
    print("[START] 开始生成PPT图表...")
    print()

    create_performance_chart()
    create_hardware_diagram()
    create_knowledge_flow()
    create_automation_workflow()
    create_summary_dashboard()

    print()
    print(f"[SUCCESS] 所有图表已生成到 '{output_dir}' 目录!")
    print(f"[PATH] 输出文件位置: {os.path.abspath(output_dir)}")
    print()
    print("生成的图表文件:")
    print("  - performance_comparison.png  (性能对比图)")
    print("  - hardware_config.png        (硬件配置图)")
    print("  - knowledge_flow.png         (知识库流程图)")
    print("  - automation_workflow.png    (自动化工作流图)")
    print("  - summary_dashboard.png      (总览仪表板)")

if __name__ == "__main__":
    main()