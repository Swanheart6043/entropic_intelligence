"""机器人内网接口，和云端接口保持独立。"""

from __future__ import annotations

from typing import Mapping

from .http_client import HttpClient


class RobotApi:
    DEFAULT_BASE_URL = "http://10.10.10.10:8080"
    DEFAULT_CURRENT_MAP_PATH = (
        "/gs-robot/real_time_data/current_initialize_status"
    )

    def __init__(
        self,
        http: HttpClient,
        *,
        current_map_path: str = DEFAULT_CURRENT_MAP_PATH,
    ):
        self._http = http
        self._current_map_path = current_map_path

    @classmethod
    def from_config(cls, config: Mapping) -> "RobotApi":
        """从配置快照创建实例，运行期间不热更新。"""
        robot = config.get("robot", {})
        if not isinstance(robot, Mapping):
            robot = {}
        return cls(
            HttpClient(
                str(robot.get("base_url") or cls.DEFAULT_BASE_URL),
                timeout=float(robot.get("timeout", 3.0)),
                verify_tls=robot.get("verify_tls", True),
            ),
            current_map_path=str(
                robot.get("current_map_path")
                or cls.DEFAULT_CURRENT_MAP_PATH
            ),
        )

    @property
    def base_url(self) -> str:
        return self._http.base_url

    def get_current_map(self) -> str:
        response = self._http.get(
            self._current_map_path,
            headers={"Accept": "application/json"},
        )
        payload = response.json()
        current_map = payload.get("currentMap", "")
        if not isinstance(current_map, str):
            raise ValueError(
                f"currentMap 类型错误: {type(current_map).__name__}"
            )
        return current_map.strip()
