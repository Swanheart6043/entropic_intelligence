"""进程管理服务 - 通过 systemctl 监控、启动、停止、重启进程"""
import subprocess
import time
from typing import Dict, List, Tuple, Optional
from loguru import logger
from common.process_config import PROCESSES, ProcessSpec

class ProcessManager:
    def __init__(self):
        self.processes_config = PROCESSES
        logger.info(f"进程管理器初始化完成，管理 {len(self.processes_config)} 个进程")

    def _run_systemctl(self, action: str, service: Optional[str] = None) -> Tuple[bool, str]:
        cmd = ['sudo', '-n', 'systemctl', action]
        if service:
            cmd.append(service)
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            return result.returncode == 0, (result.stdout or result.stderr).strip()
        except subprocess.TimeoutExpired:
            return False, "Command timeout"
        except Exception as e:
            return False, str(e)

    def get_status(self, process_name: str) -> Dict:
        base_status = {
            'name': process_name,
            'service': process_name,
            'status': 'stopped',
            'pid': None,
            'cpu_percent': 0,
            'memory_mb': 0,
            'active_state': 'inactive',
        }

        try:
            result = subprocess.run(
                ['systemctl', 'show', process_name,
                 '--property=ActiveState,MainPID,MemoryCurrent,CPUUsageNSec'],
                capture_output=True, text=True, timeout=5,
            )
            if result.returncode != 0:
                return base_status

            props = {}
            for line in result.stdout.strip().split('\n'):
                if '=' in line:
                    key, value = line.split('=', 1)
                    props[key] = value

            active_state = props.get('ActiveState', 'inactive')
            pid = props.get('MainPID', '0')
            memory = props.get('MemoryCurrent', '0')

            try:
                memory_mb = int(memory) / 1024 / 1024 if memory.isdigit() else 0
            except (ValueError, TypeError) as e:
                logger.warning(f"解析 {process_name} 内存失败: memory={memory!r}, {e}")
                memory_mb = 0

            return {
                'name': process_name,
                'service': process_name,
                'status': 'running' if active_state == 'active' else 'stopped',
                'active_state': active_state,
                'pid': int(pid) if pid and pid != '0' else None,
                'memory_mb': round(memory_mb, 2),
                'cpu_percent': 0,
            }
        except Exception as e:
            logger.error(f"获取 {process_name} 状态失败: {e}")
            return base_status

    def get_all_status(self) -> List[Dict]:
        return [self.get_status(name) for name in self.processes_config.keys()]

    def start_process(self, process_name: str) -> Dict:
        if process_name not in self.processes_config:
            return {'success': False, 'message': f'未知进程: {process_name}'}

        # 幂等：已运行视为成功，前端不用区分错误类型
        if self.get_status(process_name)['status'] == 'running':
            return {'success': True, 'message': f'{process_name} 已在运行', 'already_running': True}

        success, output = self._run_systemctl('start', process_name)
        if success:
            time.sleep(0.5)
            status = self.get_status(process_name)
            logger.info(f"已启动 {process_name}, PID: {status.get('pid')}")
            return {'success': True, 'message': f'{process_name} 已启动', 'pid': status.get('pid')}

        logger.error(f"启动 {process_name} 失败: {output}")
        return {'success': False, 'message': f'启动失败: {output}'}

    def stop_process(self, process_name: str, force: bool = False) -> Dict:
        if process_name not in self.processes_config:
            return {'success': False, 'message': f'未知进程: {process_name}'}

        # 幂等：已停止视为成功
        if self.get_status(process_name)['status'] != 'running':
            return {'success': True, 'message': f'{process_name} 已停止', 'already_stopped': True}

        if force:
            success, output = self._run_systemctl('kill', process_name)
            if not success:
                # 第一次 kill 失败（默认 SIGTERM），升级到 SIGKILL
                result = subprocess.run(
                    ['sudo', '-n', 'systemctl', 'kill', '-s', 'SIGKILL', process_name],
                    capture_output=True, text=True, timeout=10,
                )
                success = result.returncode == 0
                output = (result.stderr or result.stdout).strip()
            if success:
                logger.warning(f"已强杀 {process_name}")
        else:
            success, output = self._run_systemctl('stop', process_name)

        if success:
            logger.info(f"已停止 {process_name}")
            return {'success': True, 'message': f'{process_name} 已停止'}

        logger.error(f"停止 {process_name} 失败: {output}")
        return {'success': False, 'message': f'停止失败: {output}'}

    def restart_process(self, process_name: str) -> Dict:
        if process_name not in self.processes_config:
            return {'success': False, 'message': f'未知进程: {process_name}'}

        logger.info(f"正在重启 {process_name}...")
        success, output = self._run_systemctl('restart', process_name)

        if success:
            time.sleep(0.5)
            status = self.get_status(process_name)
            logger.info(f"已重启 {process_name}, PID: {status.get('pid')}")
            return {'success': True, 'message': f'{process_name} 已重启', 'pid': status.get('pid')}

        logger.error(f"重启 {process_name} 失败: {output}")
        return {'success': False, 'message': f'重启失败: {output}'}

    def reload_daemon(self) -> Dict:
        """重新加载 systemd 配置（用于 unit 文件改动后）"""
        success, output = self._run_systemctl('daemon-reload')
        return {'success': success, 'message': 'daemon-reload 完成' if success else output}

# 全局单例
_process_manager: Optional[ProcessManager] = None

def get_process_manager() -> ProcessManager:
    global _process_manager
    if _process_manager is None:
        _process_manager = ProcessManager()
    return _process_manager