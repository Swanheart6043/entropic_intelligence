import subprocess
import shutil
from pathlib import Path

source_dir = Path("/home/heyn/patrol_robot_h1/systemd")
dest_dir = Path("/etc/systemd/system")

if not source_dir.exists():
    raise SystemExit(f"源目录不存在: {source_dir}")

service_files = list(source_dir.glob("*.service"))
if not service_files:
    raise SystemExit("未找到任何 .service 文件")

failed = []
for f in service_files:
    try:
        shutil.copy2(f, dest_dir / f.name)
        print(f"✓ {f.name}")
    except Exception as e:
        print(f"✗ {f.name}: {e}")
        failed.append(f.name)

print("重新加载 systemd 配置...")
subprocess.run("systemctl daemon-reload", shell=True, check=True)

if failed:
    print(f"失败: {failed}")
    raise SystemExit(1)
else:
    print(f"全部成功 ({len(service_files)} 个)")