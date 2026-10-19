#!/usr/bin/env python3
"""部署配置管理"""
from pathlib import Path

class DeployConfig:
    # ========== 本地路径配置（开发环境） ==========
    SCRIPT_DIR = Path(__file__).parent.absolute()
    PROJECT_ROOT = SCRIPT_DIR.parent.parent
    
    # 本地perception目录结构
    LOCAL_PERCEPTION = PROJECT_ROOT / "perception"
    LOCAL_SRC = LOCAL_PERCEPTION / "src"
    LOCAL_COMMON = LOCAL_SRC / "common"
    LOCAL_VISION = LOCAL_SRC / "vision"  # 如果有vision子目录
    LOCAL_MODELS = LOCAL_PERCEPTION / "models"
    LOCAL_SYSTEMD = LOCAL_PERCEPTION / "systemds"
    LOCAL_SCRIPTS = PROJECT_ROOT / "perception" / "scripts"
    LOCAL_OFFLINE = LOCAL_PERCEPTION / "offline"
    
    # ========== 远程服务器配置 ==========
    REMOTE_USER = "root"
    REMOTE_HOST = "36.134.171.85"
    REMOTE_PORT = 31212
    REMOTE_PASSWORD = "root"
    
    # ========== 远程路径配置（生产环境） ==========
    REMOTE_BASE = "/home/root"
    REMOTE_TMP = "/tmp"
    REMOTE_PROJECT = f"{REMOTE_BASE}/patrol_robot_h1_ai"
    REMOTE_COMMON = f"{REMOTE_PROJECT}/common"
    REMOTE_DETECTORS = f"{REMOTE_PROJECT}/detectors"
    REMOTE_MODELS = f"{REMOTE_PROJECT}/models"
    REMOTE_SCRIPTS = f"{REMOTE_PROJECT}/scripts"
    REMOTE_SYSTEMD_SRC = f"{REMOTE_PROJECT}/systemds"
    REMOTE_SYSTEMD_DEST = "/etc/systemd/system"
    REMOTE_LOGS = f"{REMOTE_PROJECT}/logs"

    # ========== 检测器配置 ==========
    DETECTORS = [
        "fire_detector",
        "knife_detector",
        "pet_detector",
        "road_litter_detector",
        "road_water_detector",
        "smoking_detector"
    ]
    
    # ========== 构建配置 ==========
    PACKAGE_PREFIX = "patrol_robot_h1_ai"
    EXCLUDE_PATTERNS = [
        "__pycache__",
        "*.pyc",
        "*.pyo",
        ".git",
        ".gitignore",
        ".DS_Store",
        "*.bak",
        "*.swp",
        ".vscode",
        ".idea"
    ]
    
    @classmethod
    def get_remote_address(cls):
        """获取远程地址字符串"""
        return f"{cls.REMOTE_USER}@{cls.REMOTE_HOST}"
    
    @classmethod
    def get_service_name(cls, detector_name):
        """获取systemd服务名"""
        return f"rdk-{detector_name}.service"
    
    @classmethod
    def get_detector_filename(cls, detector_name):
        """获取检测器文件名"""
        return f"{detector_name}.py"
    
    @classmethod
    def get_model_filename(cls, detector_name):
        """获取模型文件名"""
        # 特殊处理带yolo的名字
        if "road_litter" in detector_name or "road_water" in detector_name:
            return f"{detector_name}_yolo_rdk_x5.bin"
        return f"{detector_name}_rdk_x5.bin"