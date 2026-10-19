import subprocess
import sys
from pathlib import Path

wheels = list(Path("../../lib").glob("*.whl"))

if not wheels:
    print("未找到任何 .whl 文件")
else:
    subprocess.run([
        sys.executable, "-m", "pip", "install",
        "--no-index", "--find-links=../../lib",
        *[str(w) for w in wheels]
    ], check=True)
    
print("执行完毕!")