import configparser
from pathlib import Path
from typing import Any, Dict, Iterable, Optional
from urllib.parse import urlsplit
from loguru import logger
from common import IniEditor

CONFIG_PATH = Path("/etc/patrol_robot_h1/config.ini")
EDITABLE_SECTIONS = {"device", "platform"}
LEGACY_PLATFORM_KEYS = {
    "wss_server_ip",
    "wss_server_port",
    "wss_server_path",
    "websocket_scheme",
    "http_server_ip",
    "http_server_port",
    "http_server_path",
    "http_server_scheme",
    "rtsp_server_ip",
    "rtsp_server_port",
}
URL_SCHEMES = {
    "websocket_url": {"ws", "wss"},
    "http_base_url": {"http", "https"},
    "rtsp_base_url": {"rtsp", "rtsps"},
}
EMPTY_VALUE_KEYS = {"ca_cert_path"}


def _normalize_bool(value: Any, key: str) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    normalized = str(value).strip().lower()
    if normalized not in {"true", "false", "1", "0", "yes", "no", "on", "off"}:
        raise ValueError(f"platform.{key} 必须是布尔值")
    return "true" if normalized in {"true", "1", "yes", "on"} else "false"


def _normalize_url(key: str, value: Any) -> str:
    normalized = str(value).strip()
    if any(character.isspace() for character in normalized):
        raise ValueError(f"platform.{key} 不能包含空白字符")
    try:
        parsed = urlsplit(normalized.replace("{sn}", "DEVICE_SN"))
        # 访问 port 属性会同时校验端口范围及格式。
        _ = parsed.port
    except ValueError as exc:
        raise ValueError(f"platform.{key} 不是有效 URL: {exc}") from exc

    allowed_schemes = URL_SCHEMES[key]
    if parsed.scheme.lower() not in allowed_schemes or not parsed.hostname:
        schemes = " / ".join(sorted(allowed_schemes))
        raise ValueError(f"platform.{key} 必须是包含主机名的完整 URL，协议为 {schemes}")
    if key in {"http_base_url", "rtsp_base_url"} and (parsed.query or parsed.fragment):
        raise ValueError(f"platform.{key} 不能包含 query 或 fragment")
    if key in {"http_base_url", "rtsp_base_url"}:
        normalized = normalized.rstrip("/")
    return normalized


def _normalize_update_value(section: str, key: str, value: Any) -> str:
    if section == "platform" and key in LEGACY_PLATFORM_KEYS:
        raise ValueError(
            f"platform.{key} 已废弃，请改用 websocket_url、http_base_url 或 rtsp_base_url"
        )
    if section == "platform" and key in URL_SCHEMES:
        return _normalize_url(key, value)
    if section == "platform" and key == "tls_verify":
        return _normalize_bool(value, key)
    return str(value).strip()

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

        prepared: Dict[str, Dict[str, str]] = {}
        skipped = []

        for section, kv in sections.items():
            if section not in self.editable_sections:
                skipped.append({"section": section, "reason": "不在可编辑白名单中"})
                logger.warning(f"Skip non-editable section: {section}")
                continue
            if not isinstance(kv, dict):
                return {"success": False, "message": f"section [{section}] 内容必须是字典"}
            prepared[section] = {}
            for key, value in kv.items():
                if value is None or (value == "" and key not in EMPTY_VALUE_KEYS):
                    continue
                try:
                    prepared[section][key] = _normalize_update_value(section, key, value)
                except ValueError as exc:
                    return {"success": False, "message": str(exc)}

        editor = IniEditor(str(self.config_path))
        updated, failed = [], []
        for section, kv in prepared.items():
            for key, value in kv.items():
                old = editor.get(section, key)
                if editor.set(section, key, value):
                    updated.append({"key": f"{section}.{key}", "old": old, "new": value})
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
