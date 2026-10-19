#!/usr/bin/env bash
# 打包板端 door 模块（不含评测 data / tools）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src/door"
DEST="${1:-$ROOT/dist/door}"

if [[ ! -d "$SRC/baselines" ]]; then
  echo "错误: 找不到 $SRC/baselines" >&2
  exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST/baselines"

for f in __init__.py detector.py motion_gate.py door_detection_worker.py capture_baseline.py README.md; do
  cp "$SRC/$f" "$DEST/"
done

for d in "$SRC/baselines"/*关门; do
  [[ -d "$d" ]] || continue
  cp -a "$d" "$DEST/baselines/"
done

echo "已打包到: $DEST"
echo "内容:"
find "$DEST" -type f | head -50