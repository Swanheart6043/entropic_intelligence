"""保留注释的 INI 文件解析工具"""
import re
from pathlib import Path
from typing import Optional, Dict, Any
from loguru import logger

class IniEditor:
    """保留注释的 INI 文件编辑器"""
    def __init__(self, file_path: str):
        self.file_path = Path(file_path)
        self.lines: list[str] = []
        self._load()
    
    def _load(self):
        """加载文件"""
        if self.file_path.exists():
            self.lines = self.file_path.read_text(encoding='utf-8').splitlines(keepends=True)
            # 确保每行都有换行符
            self.lines = [line if line.endswith('\n') else line + '\n' for line in self.lines]
    
    def get(self, section: str, key: str, fallback: str = None) -> Optional[str]:
        """获取值"""
        in_section = False
        section_pattern = re.compile(r'^\[(.+)\]')
        
        for line in self.lines:
            stripped = line.strip()
            
            # 跳过空行和注释
            if not stripped or stripped.startswith('#') or stripped.startswith(';'):
                continue
            
            # 检查 section
            match = section_pattern.match(stripped)
            if match:
                in_section = (match.group(1) == section)
                continue
            
            # 在目标 section 中查找 key
            if in_section and '=' in stripped:
                k, v = stripped.split('=', 1)
                if k.strip() == key:
                    return v.strip()
        
        return fallback
    
    def set(self, section: str, key: str, value: str) -> bool:
        """设置值（保留注释和格式）"""
        in_section = False
        section_found = False
        key_found = False
        section_pattern = re.compile(r'^\[(.+)\]')
        
        for i, line in enumerate(self.lines):
            stripped = line.strip()
            
            # 检查 section
            match = section_pattern.match(stripped)
            if match:
                if in_section and not key_found:
                    # 离开目标 section 但没找到 key，在这里插入
                    self.lines.insert(i, f'{key}={value}\n')
                    return True
                in_section = (match.group(1) == section)
                if in_section:
                    section_found = True
                continue
            
            # 在目标 section 中查找并替换 key
            if in_section and '=' in stripped and not stripped.startswith('#'):
                k, _ = stripped.split('=', 1)
                if k.strip() == key:
                    # 保留原有的缩进
                    indent = line[:len(line) - len(line.lstrip())]
                    self.lines[i] = f'{indent}{key}={value}\n'
                    key_found = True
                    return True
        
        # 文件末尾，还在目标 section 中，追加 key
        if in_section and not key_found:
            self.lines.append(f'{key}={value}\n')
            return True
        
        # section 不存在
        if not section_found:
            logger.warning(f"Section [{section}] 不存在")
            return False
        
        return False
    
    def save(self):
        """保存文件"""
        self.file_path.write_text(''.join(self.lines), encoding='utf-8')
    
    def update(self, section: str, key: str, value: str) -> Dict[str, Any]:
        """更新并保存，返回结果"""
        old_value = self.get(section, key)
        
        if self.set(section, key, value):
            self.save()
            logger.info(f"{self.file_path} [{section}] {key}: {old_value} -> {value}")
            return {
                'success': True,
                'message': f'{key} 更新成功',
                'old_value': old_value,
                'new_value': value
            }
        else:
            return {
                'success': False,
                'message': f'更新失败，section [{section}] 或 key [{key}] 不存在'
            }

def update_ini_file(file_path: str, section: str, key: str, value: str) -> Dict[str, Any]:
    """便捷函数：更新 ini 文件中的值"""
    try:
        if not Path(file_path).exists():
            return {'success': False, 'message': f'文件不存在: {file_path}'}
        
        editor = IniEditor(file_path)
        return editor.update(section, key, value)
    except Exception as e:
        logger.error(f"更新 {file_path} 异常: {e}")
        return {'success': False, 'message': str(e)}
