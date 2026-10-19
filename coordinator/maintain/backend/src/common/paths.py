# common/paths.py
import sys
from pathlib import Path

# bundle 解压目录 _internal/，开发态指向项目根
BASE_DIR = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent.parent))

DEPLOY_ROOT = Path("/home/heyn/patrol_robot_h1_maintain")
LOG_DIR = DEPLOY_ROOT / "logs"
CONF_DIR = DEPLOY_ROOT / "conf"

BUNDLED_CONFIGS = BASE_DIR / "configs"
BUNDLED_WEB = BASE_DIR / "web"