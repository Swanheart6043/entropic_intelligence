#!/usr/bin/env bash
# 构建并打包 patrol_robot_h1 运维后端
# 产物: build/patrol_robot_h1_maintain_<timestamp>.tar.gz

set -euo pipefail

# 始终从脚本所在目录运行（无论从哪里调用）
cd "$(dirname "$(readlink -f "$0")")"

# ---- 配置 ----
APP_NAME="patrol_robot_h1_maintain"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RELEASE_DIR="build"
ARCHIVE_PATH="${RELEASE_DIR}/${APP_NAME}_${TIMESTAMP}.tar.gz"

RUNTIME_DIR="dist/runtime"
UPDATE_SCRIPT="script/update.sh"
SYSTEMD_UNIT="systemd/${APP_NAME}.service"

# ---- 预检 ----
command -v pyinstaller >/dev/null || { echo "ERROR: pyinstaller 未安装" >&2; exit 1; }

for f in "$UPDATE_SCRIPT" "$SYSTEMD_UNIT" main.spec; do
  [[ -f "$f" ]] || { echo "ERROR: 缺少必要文件: $f" >&2; exit 1; }
done

# ---- 构建 ----
echo ">>> 清理旧构建产物..."
rm -rf build/ dist/

echo ">>> 运行 PyInstaller..."
pyinstaller --clean main.spec

[[ -d "$RUNTIME_DIR" ]] || { echo "ERROR: 未生成 $RUNTIME_DIR" >&2; exit 1; }

# ---- 打包 ----
echo ">>> 准备发布产物..."
mkdir -p "$RELEASE_DIR"

# 用临时目录组织 tar 内部结构，避免出现 dist/runtime/... 这种丑路径
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

cp -r "$RUNTIME_DIR"   "$STAGING/runtime"
cp    "$UPDATE_SCRIPT" "$STAGING/"
cp    "$SYSTEMD_UNIT"  "$STAGING/"

# 保险起见显式给 update.sh 加可执行位（防止源文件权限丢失）
chmod +x "$STAGING/update.sh"

echo ">>> 创建归档: $ARCHIVE_PATH"
tar -czf "$ARCHIVE_PATH" -C "$STAGING" .

# ---- 完成 ----
SIZE=$(du -h "$ARCHIVE_PATH" | cut -f1)
echo ""
echo "============================================"
echo " 构建成功"
echo " 产物: $ARCHIVE_PATH ($SIZE)"
echo "============================================"