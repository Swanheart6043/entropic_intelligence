import logging
from typing import Any
from common.config_provider import ConfigProvider
from .face_commands import FaceCommands
from .basic_info_command import BasicInfoCommands
from .command_receiver import CommandReceiver
from .date_time_command import DateTimeCommands

logger = logging.getLogger("CommandController")

class CommandController:
    """组装命令处理器并管理命令接收器的生命周期。"""
    def __init__(self,
        endpoint: str,
        config_provider: ConfigProvider,
        worker_map: dict[str, object],
        workers_by_name: dict[str, object],
        worker_lock: Any,
        cloud_api=None,
    ):
        self._endpoint = endpoint
        self._config_provider = config_provider
        self._worker_map = worker_map
        self._workers_by_name = workers_by_name
        self._worker_lock = worker_lock
        if cloud_api is None:
            from common.cloud_api import CloudApi
            cloud_api = CloudApi.from_config(config_provider.get_all())
        self._cloud_api = cloud_api
        self._receiver = CommandReceiver(endpoint)
        self._register_handlers()

    def _register_handlers(self):
        self._basic_info_commands = BasicInfoCommands(self._config_provider)
        self._date_time_commands = DateTimeCommands()
        self._face_commands = FaceCommands(self._worker_map, self._cloud_api)
        self._receiver.register("basic_info_set", self._basic_info_commands.handle_set)
        self._receiver.register("basic_info_get", self._basic_info_commands.handle_get)
        self._receiver.register("date_time_set", self._date_time_commands.set_date_and_time)
        self._receiver.register("face_sync", self._face_commands.handle_sync)
        self._receiver.register("face_sync_status", self._face_commands.handle_sync_status)
        self._receiver.register("detection_dynamic_control", self._handle_detection_dynamic_control)

    @property
    def is_running(self) -> bool:
        return self._receiver.is_running

    def start(self):
        self._receiver.start()
        logger.info("命令控制器已启动: %s", self._endpoint)

    def stop(self):
        self._receiver.stop()
        self._face_commands.close()
        logger.info("命令控制器已停止")

    def _handle_detection_dynamic_control(self, msg: dict) -> dict:
        detection_type = msg.get("type")
        switch = msg.get("switch")
        params = msg.get("params")
        if not isinstance(detection_type, str) or not detection_type:
            return {
                "error": "invalid_type",
                "message": "type must be a non-empty string",
            }
        if switch not in {"on", "off"}:
            return {
                "error": "invalid_switch",
                "message": "switch must be 'on' or 'off'",
            }
        with self._worker_lock:
            worker = self._workers_by_name.get(detection_type)
            if worker is None:
                supported = ", ".join(sorted(self._workers_by_name)) or "none"
                return {
                    "error": "worker_not_found",
                    "message": (f"detection worker not found: {detection_type}; " f"available types: {supported}"),
                }
            control = getattr(worker, "enable" if switch == "on" else "disable", None)
            if not callable(control) or not hasattr(worker, "is_enabled"):
                return {
                    "error": "dynamic_control_unsupported",
                    "message": f"worker does not support dynamic control: {detection_type}",
                }
            if not getattr(worker, "is_running", False):
                return {
                    "error": "worker_not_running",
                    "message": f"worker thread is not running: {detection_type}",
                }
            changed = control()
            enabled = bool(worker.is_enabled)
        logger.info(
            "动态检测控制: type=%s, switch=%s, changed=%s",
            detection_type,
            switch,
            changed,
        )
        return {
            "type": detection_type,
            "switch": switch,
            "state": "enabled" if enabled else "disabled",
            "changed": changed,
        }
