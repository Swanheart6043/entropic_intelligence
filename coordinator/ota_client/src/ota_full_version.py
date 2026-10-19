#!/usr/bin/env python3
import os
import sys
import time
import json
import requests
import logging
import hashlib
import tarfile
import shutil
import subprocess
from enum import Enum
from dataclasses import dataclass
from typing import Optional, Dict, Any, Tuple
from datetime import datetime
from pathlib import Path

# ==================== 配置 ====================
class Config:
    ROBOT_API = os.getenv('ROBOT_API', 'http://localhost:9090/api')
    OTA_SERVER = os.getenv('OTA_SERVER', 'https://ota.yourcompany.com')
    INSTALL_DIR = Path('/opt/patrol_robot')
    BACKUP_DIR = Path('/opt/patrol_robot.backup')
    UPDATE_DIR = Path('/tmp/patrol_robot_updates')
    VERSION_FILE = INSTALL_DIR / 'version.txt'
    LOG_FILE = Path('/var/log/patrol_robot/ota.log')
    DEVICE_ID_FILE = Path('/data/patrol_robot/device_id')
    DEVICE_TOKEN_FILE = Path('/data/patrol_robot/device_token')
    PUBLIC_KEY_FILE = INSTALL_DIR / 'keys/public.pem'
    
    # 超时设置（秒）
    BATTERY_TIMEOUT = 3600      # 1小时
    TASK_TIMEOUT = 1800         # 30分钟
    ENVIRONMENT_TIMEOUT = 7200  # 2小时
    HEALTH_CHECK_TIMEOUT = 60   # 1分钟
    
    # 检查间隔（秒）
    BATTERY_CHECK_INTERVAL = 60
    TASK_CHECK_INTERVAL = 30
    ENVIRONMENT_CHECK_INTERVAL = 300
    
    MAX_RETRY = 3

# ==================== 枚举定义 ====================
class OTAStatus(Enum):
    CHECKING = "checking"
    DOWNLOADING = "downloading"
    APPLYING = "applying"
    VERIFYING = "verifying"
    SUCCESS = "success"
    FAILED = "failed"
    ROLLED_BACK = "rolled_back"

class BlockReason(Enum):
    NONE = "none"
    BATTERY_LOW = "battery_too_low"
    TASK_RUNNING = "task_in_progress"
    UNSAFE_ENVIRONMENT = "unsafe_outdoor_environment"
    ROBOT_WORKING = "robot_is_working"
    SYSTEM_ERROR = "system_error"
    EXTREME_TEMPERATURE = "extreme_temperature"

# ==================== 数据类 ====================
@dataclass
class RobotStatus:
    running_state: str
    task_status: str
    battery_level: int
    is_charging: bool
    outdoor_safety: str
    temperature: float
    
    @property
    def battery_sufficient(self) -> bool:
        if self.battery_level < 50:
            return False
        if not self.is_charging and self.battery_level < 80:
            return False
        return True
    
    @property
    def is_idle(self) -> bool:
        return self.running_state in ['idle', 'charging']
    
    @property
    def has_running_task(self) -> bool:
        return self.task_status == 'executing'
    
    @property
    def is_environment_safe(self) -> bool:
        return self.outdoor_safety == 'safe'
    
    @property
    def is_temperature_ok(self) -> bool:
        return 0 <= self.temperature <= 50

@dataclass
class OTAReadyResult:
    ready: bool
    reason: BlockReason
    details: Dict[str, Any]
    
    def __str__(self) -> str:
        if self.ready:
            return "✅ System ready for OTA"
        return f"❌ Blocked: {self.reason.value}"

# ==================== 日志配置 ====================
def setup_logging():
    Config.LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    
    logging.basicConfig(
        level=logging.INFO,
        format='[%(asctime)s] %(levelname)s: %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S',
        handlers=[
            logging.FileHandler(Config.LOG_FILE),
            logging.StreamHandler(sys.stdout)
        ]
    )

logger = logging.getLogger(__name__)

# ==================== 工具函数 ====================
class Utils:
    @staticmethod
    def get_device_id() -> str:
        """获取设备ID"""
        try:
            if Config.DEVICE_ID_FILE.exists():
                return Config.DEVICE_ID_FILE.read_text().strip()
            
            # 备用方案：使用系统UUID
            uuid_file = Path('/sys/class/dmi/id/product_uuid')
            if uuid_file.exists():
                return uuid_file.read_text().strip()
            
            return "unknown"
        except Exception as e:
            logger.error(f"Failed to get device ID: {e}")
            return "unknown"
    
    @staticmethod
    def get_device_token() -> str:
        """获取设备令牌"""
        try:
            if Config.DEVICE_TOKEN_FILE.exists():
                return Config.DEVICE_TOKEN_FILE.read_text().strip()
            return ""
        except Exception as e:
            logger.error(f"Failed to get device token: {e}")
            return ""
    
    @staticmethod
    def verify_signature(file_path: Path, signature_path: Path) -> bool:
        """验证文件签名"""
        try:
            cmd = [
                'openssl', 'dgst', '-sha256',
                '-verify', str(Config.PUBLIC_KEY_FILE),
                '-signature', str(signature_path),
                str(file_path)
            ]
            result = subprocess.run(cmd, capture_output=True, text=True)
            return result.returncode == 0
        except Exception as e:
            logger.error(f"Signature verification failed: {e}")
            return False
    
    @staticmethod
    def run_script(script_path: Path, *args) -> bool:
        """运行脚本"""
        try:
            cmd = [str(script_path)] + list(args)
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                logger.error(f"Script failed: {result.stderr}")
                return False
            return True
        except Exception as e:
            logger.error(f"Failed to run script: {e}")
            return False

# ==================== API 客户端 ====================
class RobotAPIClient:
    def __init__(self, base_url: str):
        self.base_url = base_url
        self.session = requests.Session()
        self.session.timeout = 10
    
    def get_status(self) -> Optional[RobotStatus]:
        """获取机器人状态"""
        try:
            response = self.session.get(f"{self.base_url}/status")
            response.raise_for_status()
            data = response.json()
            
            return RobotStatus(
                running_state=data['running_state'],
                task_status=data['task_status'],
                battery_level=data['battery_level'],
                is_charging=data['is_charging'],
                outdoor_safety=data['outdoor_safety'],
                temperature=data['temperature']
            )
        except Exception as e:
            logger.error(f"Failed to get robot status: {e}")
            return None
    
    def check_ota_ready(self) -> Optional[Dict[str, Any]]:
        """检查是否可以OTA"""
        try:
            response = self.session.get(f"{self.base_url}/ota/ready")
            response.raise_for_status()
            return response.json()
        except Exception as e:
            logger.error(f"Failed to check OTA ready: {e}")
            return None
    
    def send_idle_command(self) -> bool:
        """发送空闲命令"""
        try:
            response = self.session.post(f"{self.base_url}/command/idle")
            response.raise_for_status()
            return response.json().get('success', False)
        except Exception as e:
            logger.error(f"Failed to send idle command: {e}")
            return False
    
    def enter_ota_mode(self) -> bool:
        """进入OTA模式"""
        try:
            response = self.session.post(f"{self.base_url}/command/ota_mode")
            response.raise_for_status()
            return response.json().get('success', False)
        except Exception as e:
            logger.error(f"Failed to enter OTA mode: {e}")
            return False

class OTAServerClient:
    def __init__(self, base_url: str, device_id: str, token: str):
        self.base_url = base_url
        self.device_id = device_id
        self.token = token
        self.session = requests.Session()
        if token:
            self.session.headers['Authorization'] = f'Bearer {token}'
    
    def get_latest_version(self) -> Optional[str]:
        """获取最新版本号"""
        try:
            url = f"{self.base_url}/api/latest_version?device_id={self.device_id}"
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.text.strip()
        except Exception as e:
            logger.error(f"Failed to get latest version: {e}")
            return None
    
    def download_update(self, version: str, output_path: Path) -> bool:
        """下载更新包"""
        try:
            url = f"{self.base_url}/api/download/{version}"
            logger.info(f"Downloading from {url}")
            
            with self.session.get(url, stream=True, timeout=300) as response:
                response.raise_for_status()
                total_size = int(response.headers.get('content-length', 0))
                
                with open(output_path, 'wb') as f:
                    downloaded = 0
                    for chunk in response.iter_content(chunk_size=8192):
                        f.write(chunk)
                        downloaded += len(chunk)
                        if total_size > 0:
                            percent = (downloaded / total_size) * 100
                            print(f"\rDownloading: {percent:.1f}%", end='', flush=True)
                print()  # 换行
            
            logger.info(f"Downloaded to {output_path}")
            return True
        except Exception as e:
            logger.error(f"Failed to download update: {e}")
            return False
    
    def download_signature(self, version: str, output_path: Path) -> bool:
        """下载签名文件"""
        try:
            url = f"{self.base_url}/api/download/{version}.sig"
            response = self.session.get(url, timeout=30)
            response.raise_for_status()
            output_path.write_bytes(response.content)
            return True
        except Exception as e:
            logger.error(f"Failed to download signature: {e}")
            return False
    
    def report_status(self, status: OTAStatus, version: str, message: str = "") -> bool:
        """上报更新状态"""
        try:
            url = f"{self.base_url}/api/update_status"
            data = {
                'device_id': self.device_id,
                'status': status.value,
                'version': version,
                'message': message,
                'timestamp': int(time.time())
            }
            response = self.session.post(url, json=data, timeout=10)
            response.raise_for_status()
            return True
        except Exception as e:
            logger.warning(f"Failed to report status: {e}")
            return False

# ==================== OTA 管理器 ====================
class OTAManager:
    def __init__(self):
        self.robot_api = RobotAPIClient(Config.ROBOT_API)
        self.ota_server = OTAServerClient(
            Config.OTA_SERVER,
            Utils.get_device_id(),
            Utils.get_device_token()
        )
        Config.UPDATE_DIR.mkdir(parents=True, exist_ok=True)
    
    def get_current_version(self) -> str:
        """获取当前版本"""
        try:
            if Config.VERSION_FILE.exists():
                return Config.VERSION_FILE.read_text().strip()
            return "unknown"
        except Exception as e:
            logger.error(f"Failed to read version file: {e}")
            return "unknown"
    
    def check_for_updates(self) -> Optional[str]:
        """检查是否有更新"""
        logger.info("=== Checking for updates ===")
        
        current = self.get_current_version()
        logger.info(f"Current version: {current}")
        
        latest = self.ota_server.get_latest_version()
        if not latest:
            logger.error("Failed to get latest version")
            return None
        
        logger.info(f"Latest version: {latest}")
        
        if current == latest:
            logger.info("Already up to date")
            return None
        
        logger.info(f"Update available: {current} -> {latest}")
        return latest
    
    def check_ota_ready(self) -> OTAReadyResult:
        """检查是否可以执行OTA"""
        logger.info("=== Checking OTA readiness ===")
        
        # 调用C++接口获取状态
        result = self.robot_api.check_ota_ready()
        if not result:
            return OTAReadyResult(
                ready=False,
                reason=BlockReason.SYSTEM_ERROR,
                details={'error': 'Cannot connect to robot API'}
            )
        
        ready = result.get('ready', False)
        reason_str = result.get('reason', 'system_error')
        details = result.get('details', {})
        
        # 转换为枚举
        try:
            reason = BlockReason(reason_str)
        except ValueError:
            reason = BlockReason.SYSTEM_ERROR
        
        # 显示详细信息
        if not ready:
            logger.info(f"❌ OTA blocked: {reason.value}")
            
            battery_info = details.get('battery', {})
            if battery_info:
                logger.info(f"  Battery: {battery_info.get('level')}% "
                          f"(Charging: {battery_info.get('charging')})")
            
            task_info = details.get('tasks', {})
            if task_info:
                logger.info(f"  Task: {task_info.get('status')}")
            
            env_info = details.get('environment', {})
            if env_info:
                logger.info(f"  Environment: {env_info.get('outdoor_safe')}, "
                          f"Temp: {env_info.get('temperature')}°C")
        else:
            logger.info("✅ System ready for OTA")
        
        return OTAReadyResult(ready=ready, reason=reason, details=details)
    
    def wait_for_condition(
        self,
        check_func,
        timeout: int,
        interval: int,
        description: str
    ) -> bool:
        """等待某个条件满足"""
        logger.info(f"{description} (timeout: {timeout}s)")
        
        start_time = time.time()
        attempt = 0
        
        while time.time() - start_time < timeout:
            attempt += 1
            elapsed = int(time.time() - start_time)
            
            if check_func():
                logger.info(f"✅ Condition met after {elapsed}s")
                return True
            
            logger.info(f"  Attempt {attempt}: Still waiting... ({elapsed}s/{timeout}s)")
            time.sleep(interval)
        
        logger.error(f"❌ Timeout after {timeout}s")
        return False
    
    def wait_for_battery(self) -> bool:
        """等待电量充足"""
        def check():
            status = self.robot_api.get_status()
            if not status:
                return False
            
            logger.info(f"  Battery: {status.battery_level}%, "
                       f"Charging: {status.is_charging}")
            return status.battery_sufficient
        
        return self.wait_for_condition(
            check,
            Config.BATTERY_TIMEOUT,
            Config.BATTERY_CHECK_INTERVAL,
            "Waiting for sufficient battery"
        )
    
    def wait_for_task_completion(self) -> bool:
        """等待任务完成"""
        def check():
            status = self.robot_api.get_status()
            if not status:
                return False
            
            logger.info(f"  Task status: {status.task_status}")
            return not status.has_running_task
        
        return self.wait_for_condition(
            check,
            Config.TASK_TIMEOUT,
            Config.TASK_CHECK_INTERVAL,
            "Waiting for task completion"
        )
    
    def wait_for_safe_environment(self) -> bool:
        """等待安全环境"""
        def check():
            status = self.robot_api.get_status()
            if not status:
                return False
            
            logger.info(f"  Environment: {status.outdoor_safety}, "
                       f"Temp: {status.temperature}°C")
            return status.is_environment_safe and status.is_temperature_ok
        
        return self.wait_for_condition(
            check,
            Config.ENVIRONMENT_TIMEOUT,
            Config.ENVIRONMENT_CHECK_INTERVAL,
            "Waiting for safe environment"
        )
    
    def handle_block_reason(self, reason: BlockReason) -> bool:
        """处理阻塞原因"""
        if reason == BlockReason.ROBOT_WORKING:
            logger.info("Sending idle command...")
            if self.robot_api.send_idle_command():
                time.sleep(10)
                return True
            return False
        
        elif reason == BlockReason.TASK_RUNNING:
            return self.wait_for_task_completion()
        
        elif reason == BlockReason.BATTERY_LOW:
            return self.wait_for_battery()
        
        elif reason == BlockReason.UNSAFE_ENVIRONMENT:
            return self.wait_for_safe_environment()
        
        elif reason == BlockReason.EXTREME_TEMPERATURE:
            return self.wait_for_safe_environment()
        
        else:
            logger.error(f"Cannot handle block reason: {reason.value}")
            return False
    
    def wait_for_ota_ready(self) -> bool:
        """等待OTA就绪"""
        for attempt in range(1, Config.MAX_RETRY + 1):
            logger.info(f"=== Attempt {attempt}/{Config.MAX_RETRY} ===")
            
            result = self.check_ota_ready()
            if result.ready:
                return True
            
            if attempt < Config.MAX_RETRY:
                logger.info(f"Trying to resolve: {result.reason.value}")
                if not self.handle_block_reason(result.reason):
                    logger.warning("Failed to resolve blocking condition")
                    continue
        
        logger.error("Failed to meet OTA conditions after all retries")
        return False
    
    def download_update(self, version: str) -> Optional[Path]:
        """下载更新包"""
        logger.info("=== Downloading update ===")
        
        update_file = Config.UPDATE_DIR / f"update_{version}.tar.gz"
        sig_file = Config.UPDATE_DIR / f"update_{version}.tar.gz.sig"
        
        # 下载更新包
        if not self.ota_server.download_update(version, update_file):
            return None
        
        # 下载签名
        logger.info("Downloading signature...")
        if not self.ota_server.download_signature(version, sig_file):
            logger.error("Failed to download signature")
            update_file.unlink(missing_ok=True)
            return None
        
        # 验证签名
        logger.info("Verifying signature...")
        if not Utils.verify_signature(update_file, sig_file):
            logger.error("Signature verification failed!")
            update_file.unlink(missing_ok=True)
            sig_file.unlink(missing_ok=True)
            return None
        
        logger.info("✅ Download and verification successful")
        return update_file
    
    def backup_current_version(self) -> bool:
        """备份当前版本"""
        logger.info("Backing up current version...")
        try:
            if Config.BACKUP_DIR.exists():
                shutil.rmtree(Config.BACKUP_DIR)
            shutil.copytree(Config.INSTALL_DIR, Config.BACKUP_DIR)
            logger.info(f"Backup created at {Config.BACKUP_DIR}")
            return True
        except Exception as e:
            logger.error(f"Backup failed: {e}")
            return False
    
    def stop_services(self) -> bool:
        """停止所有服务"""
        logger.info("Stopping services...")
        script = Config.INSTALL_DIR / "start_all.sh"
        return Utils.run_script(script, "stop")
    
    def start_services(self) -> bool:
        """启动所有服务"""
        logger.info("Starting services...")
        script = Config.INSTALL_DIR / "start_all.sh"
        return Utils.run_script(script, "start")
    
    def apply_update(self, update_file: Path, version: str) -> bool:
        """应用更新"""
        logger.info("=== Applying update ===")
        
        try:
            # 备份
            if not self.backup_current_version():
                return False
            
            # 停止服务
            self.stop_services()
            time.sleep(3)
            
            # 解压更新
            logger.info("Extracting update...")
            with tarfile.open(update_file, 'r:gz') as tar:
                tar.extractall(Config.INSTALL_DIR)
            
            # 更新版本文件
            Config.VERSION_FILE.write_text(version)
            
            # 设置权限
            logger.info("Setting permissions...")
            for bin_file in (Config.INSTALL_DIR / 'bin').glob('*'):
                bin_file.chmod(0o755)
            for script in Config.INSTALL_DIR.glob('*.sh'):
                script.chmod(0o755)
            
            logger.info("✅ Update applied successfully")
            return True
            
        except Exception as e:
            logger.error(f"Failed to apply update: {e}")
            return False
    
    def verify_update(self, expected_version: str) -> bool:
        """验证更新"""
        logger.info("=== Verifying update ===")
        
        # 启动服务
        if not self.start_services():
            logger.error("Failed to start services")
            return False
        
        # 等待服务启动
        logger.info("Waiting for services to start...")
        time.sleep(30)
        
        # 检查版本
        actual_version = self.get_current_version()
        if actual_version != expected_version:
            logger.error(f"Version mismatch! Expected: {expected_version}, "
                        f"Got: {actual_version}")
            return False
        
        # 健康检查
        logger.info("Performing health check...")
        time.sleep(10)
        
        status = self.robot_api.get_status()
        if not status:
            logger.error("Health check failed - cannot get status")
            return False
        
        logger.info("✅ Update verified successfully")
        return True
    
    def rollback(self) -> bool:
        """回滚到之前的版本"""
        logger.info("=== Rolling back ===")
        
        try:
            if not Config.BACKUP_DIR.exists():
                logger.error("No backup found")
                return False
            
            # 停止服务
            self.stop_services()
            time.sleep(3)
            
            # 恢复备份
            logger.info("Restoring backup...")
            if Config.INSTALL_DIR.exists():
                shutil.rmtree(Config.INSTALL_DIR)
            shutil.copytree(Config.BACKUP_DIR, Config.INSTALL_DIR)
            
            # 启动服务
            self.start_services()
            
            logger.info("✅ Rollback completed")
            return True
            
        except Exception as e:
            logger.error(f"Rollback failed: {e}")
            return False
    
    def perform_ota(self, force: bool = False) -> bool:
        """执行OTA更新"""
        logger.info("=" * 60)
        logger.info("Starting OTA update process")
        logger.info(f"Device ID: {Utils.get_device_id()}")
        logger.info(f"Force mode: {force}")
        logger.info("=" * 60)
        
        # 1. 检查更新
        new_version = self.check_for_updates()
        if not new_version:
            logger.info("No update available")
            return True
        
        self.ota_server.report_status(OTAStatus.CHECKING, new_version)
        
        # 2. 检查是否可以升级
        if not force:
            if not self.wait_for_ota_ready():
                self.ota_server.report_status(
                    OTAStatus.FAILED,
                    new_version,
                    "Failed to meet OTA conditions"
                )
                return False
        else:
            logger.warning("⚠️  Force mode: skipping safety checks")
        
        # 3. 进入OTA模式
        logger.info("=== Entering OTA mode ===")
        if not self.robot_api.enter_ota_mode():
            logger.warning("Failed to enter OTA mode, continuing anyway...")
        time.sleep(5)
        
        # 4. 下载更新
        self.ota_server.report_status(OTAStatus.DOWNLOADING, new_version)
        update_file = self.download_update(new_version)
        if not update_file:
            self.ota_server.report_status(
                OTAStatus.FAILED,
                new_version,
                "Download failed"
            )
            return False
        
        # 5. 应用更新
        self.ota_server.report_status(OTAStatus.APPLYING, new_version)
        if not self.apply_update(update_file, new_version):
            self.ota_server.report_status(
                OTAStatus.FAILED,
                new_version,
                "Apply failed"
            )
            logger.error("Failed to apply update, attempting rollback...")
            self.rollback()
            return False
        
        # 6. 验证更新
        self.ota_server.report_status(OTAStatus.VERIFYING, new_version)
        if not self.verify_update(new_version):
            logger.error("Update verification failed, rolling back...")
            self.rollback()
            self.ota_server.report_status(
                OTAStatus.ROLLED_BACK,
                new_version,
                "Verification failed"
            )
            return False
        
        # 7. 清理
        logger.info("Cleaning up...")
        update_file.unlink(missing_ok=True)
        (update_file.parent / f"{update_file.name}.sig").unlink(missing_ok=True)
        
        # 8. 成功
        logger.info("=" * 60)
        logger.info(f"✅ OTA update completed successfully: {new_version}")
        logger.info("=" * 60)
        
        self.ota_server.report_status(
            OTAStatus.SUCCESS,
            new_version,
            "Update completed successfully"
        )
        return True

# ==================== 命令行接口 ====================
def main():
    setup_logging()
    
    if len(sys.argv) < 2:
        print("Usage: ota_manager.py {check|update|status|rollback}")
        sys.exit(1)
    
    command = sys.argv[1]
    manager = OTAManager()
    
    try:
        if command == "check":
            # 检查更新
            version = manager.check_for_updates()
            if version:
                print(f"Update available: {version}")
                sys.exit(0)
            else:
                print("No update available")
                sys.exit(1)
        elif command == "status":
            # 检查OTA就绪状态
            result = manager.check_ota_ready()
            print(result)
            sys.exit(0 if result.ready else 1)
        elif command == "update":
            # 正常更新（带安全检查）
            success = manager.perform_ota(force=False)
            sys.exit(0 if success else 1)
        elif command == "rollback":
            # 回滚
            success = manager.rollback()
            sys.exit(0 if success else 1)
        else:
            print(f"Unknown command: {command}")
            sys.exit(1)
    except KeyboardInterrupt:
        logger.info("\nOTA process interrupted by user")
        sys.exit(130)
    except Exception as e:
        logger.error(f"Unexpected error: {e}", exc_info=True)
        sys.exit(1)

if __name__ == "__main__":
    main()