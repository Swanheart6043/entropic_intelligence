import configparser
from pathlib import Path
from typing import Any, Dict, Iterable, Optional
from loguru import logger
from common import IniEditor

CONFIG_PATH = Path("/etc/patrol_robot_h1/config.ini")
EDITABLE_SECTIONS = {"device", "platform"}

class ConfigManager:
    def __init__(
        self,
        config_path: Path = CONFIG_PATH,
        editable_sections: Optional[Iterable[str]] = None,
    ):
        self.config_path = Path(config_path)
        self.editable_sections = (
            set(editable_sections) if editable_sections is not None else EDITABLE_SECTIONS
        )
        logger.info(f"Config manager initialized with {self.config_path}")

    def read_config(self) -> Dict:
        """读取配置文件全部内容,返回 {section: {key: value}} 嵌套字典"""
        if not self.config_path.exists():
            return {"error": f"Config file not exists: {self.config_path}"}
        try:
            parser = configparser.ConfigParser()
            parser.read(self.config_path, encoding="utf-8")
            return {section: dict(parser.items(section)) for section in parser.sections()}
        except Exception as e:
            logger.error(f"Error reading config: {e}")
            return {"error": str(e)}

    def update_config(self, sections: Dict[str, Dict[str, Any]]) -> Dict:
        """批量更新嵌套配置 {section: {key: value, ...}, ...},保留 INI 注释"""
        if not self.config_path.exists():
            return {"success": False, "message": f"Config file not exists: {self.config_path}"}

        if not isinstance(sections, dict) or not sections:
            return {"success": False, "message": "请求体必须是非空的 {section: {key: value}} 结构"}

        editor = IniEditor(str(self.config_path))
        updated, failed, skipped = [], [], []

        for section, kv in sections.items():
            if section not in self.editable_sections:
                skipped.append({"section": section, "reason": "不在可编辑白名单中"})
                logger.warning(f"Skip non-editable section: {section}")
                continue
            if not isinstance(kv, dict):
                failed.append({"section": section, "message": "section 内容必须是字典"})
                continue
            for key, value in kv.items():
                if value is None or value == "":
                    continue
                old = editor.get(section, key)
                if editor.set(section, key, str(value)):
                    updated.append({"key": f"{section}.{key}", "old": old, "new": str(value)})
                else:
                    failed.append(
                        {"key": f"{section}.{key}", "message": f"section [{section}] 不存在"}
                    )

        # 一次性落盘
        if updated:
            try:
                editor.save()
            except Exception as e:
                logger.exception("Save config failed")
                return {"success": False, "message": f"写文件失败: {e}"}

        if failed:
            logger.error(f"Config update partial failure: updated={updated}, failed={failed}")
            return {
                "success": False,
                "message": f"部分配置更新失败 (成功 {len(updated)}, 失败 {len(failed)})",
                "updated": updated,
                "failed": failed,
                "skipped": skipped,
            }

        logger.info(f"Config updated: updated={updated}, skipped={skipped}")
        return {
            "success": True,
            "message": f"已更新 {len(updated)} 项配置",
            "updated": updated,
            "skipped": skipped,
        }

_config_manager: Optional[ConfigManager] = None

def get_config_manager() -> ConfigManager:
    global _config_manager
    if _config_manager is None:
        _config_manager = ConfigManager()
    return _config_manager