import json
import logging

from common.paths import CONFIG_PATH


logger = logging.getLogger("ConfigProvider")


class ConfigProvider:
    def __init__(self, config_path: str = CONFIG_PATH):
        self._path = config_path
        self._config: dict = {}
        self.reload()

    def get_all(self) -> dict:
        return dict(self._config)   # 返回副本，避免外部误改

    def get_path(self) -> str:
        return self._path

    def reload(self) -> None:
        try:
            with open(self._path, "r") as f:
                new_config = json.load(f)
        except Exception as e:
            logger.warning(f"读取配置失败: {e}, 保留旧配置")
            return
        self._config = new_config

    def get_rtsp_url(self) -> str:
        return self._config.get("rtsp_url", "")

    def get_enable_detection(self) -> list[str]:
        return self._config.get("enable", [])

    def get_sn(self) -> str:
        return self._config.get("sn", "")

    def get_web_http_url(self) -> str:
        return self._config.get("web_http_url", "")

    def get_web_img_url(self) -> str:
        return self._config.get("web_img_url", "")
