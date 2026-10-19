#!/usr/bin/env python3
"""一键部署：update -> enable -> start"""
import subprocess
import sys

scripts = [
    ("更新配置", "update.py"),
    ("启用自启", "enable.py"),
    ("启动服务", "start.py"),
]

for desc, script in scripts:
    print(f"\n{'='*40}")
    print(f"[{desc}] {script}")
    print('='*40)
    r = subprocess.run([sys.executable, script])
    if r.returncode != 0:
        raise SystemExit(f"{script} 执行失败，中止部署")

print(f"\n{'='*40}")
print("部署完成")
print('='*40)