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
    r = subprocess.run(f"systemctl is-enabled {name}", shell=True, capture_output=True, text=True)
    if r.stdout.strip() == "enabled":
        print(f"✓ {name} (已启用)")
        continue

    r = subprocess.run(f"systemctl enable {name}", shell=True, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"✓ {name} (启用成功)")
    else:
        print(f"✗ {name}: {r.stderr.strip()}")
        failed.append(name)

if failed:
    print(f"失败: {failed}")
    raise SystemExit(1)
else:
    print(f"全部启用成功 ({len(service_files)} 个)")