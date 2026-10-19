"""项目初始化服务 - 代替人工执行一系列操作"""
import subprocess
from pathlib import Path
from typing import Dict, Tuple, List
from loguru import logger
from common import update_ini_file

class ProjectInitializer:
    def __init__(self):
        self.config_ini_path = "/etc/patrol_robot_h1/config.ini"
        self.npc_conf_path = "/home/heyn/npc/conf/npc.conf"
    
    # 跑shell命令
    def _run_command(self, cmd: List[str], timeout: int = 30) -> Tuple[bool, str]:
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            success = result.returncode == 0
            output = result.stdout or result.stderr
            return success, output.strip()
        except subprocess.TimeoutExpired:
            return False, "命令执行超时"
        except Exception as e:
            return False, str(e)
    
    # 修改所有配置文件
    def modify_configuration_file(self, sn: str, vkey: str) -> Dict:
        results = {
            'config_ini': update_ini_file(self.config_ini_path, 'device', 'sn', sn),
            'npc_conf': update_ini_file(self.npc_conf_path, 'common', 'vkey', vkey),
        }
        all_success = all(r['success'] for r in results.values())
        return {
            'success': all_success,
            'message': '配置文件更新完成' if all_success else '部分配置更新失败',
            'details': results
        }
    
    # 设置系统时间 time_str格式 2025-01-15 10:30:00
    def set_system_time(self, time_str: str) -> Dict:
        try:
            # 禁用 NTP 同步
            success, output = self._run_command(['sudo', '-n', 'timedatectl', 'set-ntp', 'false'])
            if not success:
                return {'success': False, 'message': f'禁用NTP失败: {output}'}
            
            # 设置时间
            success, output = self._run_command(['sudo', '-n', 'timedatectl', 'set-time', time_str])
            if not success:
                return {'success': False, 'message': f'设置时间失败: {output}'}
            
            # 同步到硬件时钟
            success, output = self._run_command(['sudo', '-n', 'hwclock', '--systohc'])
            if not success:
                logger.warning(f"同步硬件时钟失败: {output}")
            
            logger.info("系统时间设置成功")
            return {'success': True, 'message': '时间设置成功'}
        except Exception as e:
            logger.error(f"设置时间异常: {e}")
            return {'success': False, 'message': str(e)}
    
    # 完整初始化流程
    def initialization(self, sn: str, vkey: str, time_str: str = None) -> Dict:
        steps = {}

        # 步骤1: 修改配置文件
        steps['modify_config'] = self.modify_configuration_file(sn, vkey)
        if not steps['modify_config']['success']:
            return {'success': False, 'message': '配置文件修改失败，初始化终止', 'steps': steps}
        
        # 步骤2: 设置系统时间
        if time_str:
            steps['set_time'] = self.set_system_time(time_str)

        all_success = all(s['success'] for s in steps.values())
        return {
            'success': all_success,
            'message': '初始化完成' if all_success else '初始化完成（部分步骤失败）',
            'steps': steps
        }

# 全局实例
_project_initializer = None

def get_project_initializer() -> ProjectInitializer:
    global _project_initializer
    if _project_initializer is None:
        _project_initializer = ProjectInitializer()
    return _project_initializer