import time
import subprocess
from pathlib import Path

systemd_dir = Path("/home/heyn/patrol_robot_h1/systemd")
if not systemd_dir.exists():
    raise SystemExit(f"服务配置目录不存在: {systemd_dir}")

service_files = list(systemd_dir.glob("*.service"))
if not service_files:
    raise SystemExit("未找到任何 .service 文件")

failed = []
for f in service_files:
    name = f.name
    r = subprocess.run(f"systemctl restart {name}", shell=True, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"✓ {name}")
    else:
        print(f"✗ {name}: {r.stderr.strip()}")
        failed.append(name)

print("等待服务启动...")
time.sleep(2)

# 状态总览
for f in service_files:
    name = f.name
    r = subprocess.run(f"systemctl is-active {name}", shell=True, capture_output=True, text=True)
    status = r.stdout.strip()
    mark = "●" if status == "active" else "✗" if status == "failed" else "○"
    print(f"{mark} {name.replace('.service', ''):40s} {status}")

if failed:
    print(f"启动失败: {failed}")
    raise SystemExit(1)
else:
    print(f"全部启动成功 ({len(service_files)} 个)")