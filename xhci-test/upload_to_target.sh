#!/bin/bash
# upload_to_target.sh - 上传文件到目标机器并编译
# 需要设置环境变量: JUMP_PASS 和 TARGET_PASS

set -e

# Configuration
JUMP_HOST="10.65.10.1"
JUMP_USER="jinzixiang"
TARGET_HOST="10.65.46.174"
TARGET_USER="root"
REMOTE_DIR="/home/hygo/jzx/xhci-test"

# Check environment variables
if [ -z "$JUMP_PASS" ]; then
    echo "Error: JUMP_PASS environment variable not set"
    exit 1
fi
if [ -z "$TARGET_PASS" ]; then
    echo "Error: TARGET_PASS environment variable not set"
    exit 1
fi

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Uploading files to $TARGET_HOST ==="

# Files to upload
FILES=(
    "src/xhci_enum.c"
    "src/xhci_ops.c"
    "src/xhci_state.c"
    "src/vfio.c"
    "src/xhci_bot.c"
    "src/main.c"
    "src/test_xhci.c"
    "src/test_example.c"
    "src/test_bot.c"
    "include/xhci.h"
    "include/xhci_internal.h"
    "include/xhci_regs.h"
    "include/xhci_state.h"
    "include/xhci_bot.h"
    "Makefile"
)

# Create remote directory first
sshpass -p "$JUMP_PASS" ssh -o StrictHostKeyChecking=no \
    -o "ProxyCommand=sshpass -p \"$JUMP_PASS\" ssh -o StrictHostKeyChecking=no -W %h:%p $JUMP_USER@$JUMP_HOST" \
    "$TARGET_USER@$TARGET_HOST" "mkdir -p $REMOTE_DIR/src $REMOTE_DIR/include $REMOTE_DIR/build"

# Upload each file
for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "Uploading $file..."
        sshpass -p "$JUMP_PASS" scp -o StrictHostKeyChecking=no \
            -o "ProxyCommand=sshpass -p \"$JUMP_PASS\" ssh -o StrictHostKeyChecking=no -W %h:%p $JUMP_USER@$JUMP_HOST" \
            "$file" "$TARGET_USER@$TARGET_HOST:$REMOTE_DIR/$file"
    else
        echo "Warning: $file not found, skipping"
    fi
done

echo ""
echo "=== Building on target ==="
sshpass -p "$JUMP_PASS" ssh -o StrictHostKeyChecking=no \
    -o "ProxyCommand=sshpass -p \"$JUMP_PASS\" ssh -o StrictHostKeyChecking=no -W %h:%p $JUMP_USER@$JUMP_HOST" \
    "$TARGET_USER@$TARGET_HOST" "cd $REMOTE_DIR && rm -rf build && make"

echo ""
echo "=== Done! ==="
echo "To run tests:"
echo "  sshpass -p \"\$JUMP_PASS\" ssh -o StrictHostKeyChecking=no -o \"ProxyCommand=sshpass -p '\\$JUMP_PASS' ssh -o StrictHostKeyChecking=no -W %h:%p $JUMP_USER@$JUMP_HOST\" root@$TARGET_HOST 'cd $REMOTE_DIR && ./build/main --enum'"
