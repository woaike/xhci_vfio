#!/bin/bash
# 检查wifi接收器驱动安装情况

echo "=== 1. 检查USB设备 ==="
lsusb | grep -i "wireless\|wifi\|wlan\|ralink\|realtek\|atheros\|intel.*wireless"

echo -e "\n=== 2. 检查PCI设备 ==="
lspci | grep -i "wireless\|wifi\|wlan\|network"

echo -e "\n=== 3. 检查网络接口 ==="
ip link show | grep -E "^\d+:\s+wlan|wl"

echo -e "\n=== 4. 检查已加载的无线内核模块 ==="
lsmod | grep -E "wifi|wlan|wireless|rtl|ath|rt2|rt8|brcm"

echo -e "\n=== 5. 检查内核日志中的无线相关信息 ==="
dmesg | grep -iE "wireless|wifi|wlan|firmware.*rtl|firmware.*ath" | tail -20

echo -e "\n=== 6. 检查无线工具是否安装 ==="
which iw && echo "iw 工具已安装" || echo "iw 工具未安装"
which iwconfig && echo "iwconfig 工具已安装" || echo "iwconfig 工具未安装"

echo -e "\n=== 7. 检查可用无线内核模块 ==="
modinfo $(find /lib/modules/$(uname -r) -name "*wifi*" -o -name "*wlan*" | grep -E "ko$" | head -5) 2>/dev/null | head -10