import json
import os
import tempfile
import logging
from common.config_provider import ConfigProvider

logger = logging.getLogger("BasicInfoCommands")

class BasicInfoCommands:
    def __init__(self, provider: ConfigProvider):
        self._provider = provider

    def handle_set(self, params: dict) -> dict:
        required = ["sn", "web_http_url"]
        missing = [k for k in required if k not in params]
        if missing:
            return {"status": "error", "msg": f"缺少字段: {missing}"}
        path = self._provider.get_path()
        try:
            config = dict(self._provider.get_all())
            config.update(params)
            dir_name = os.path.dirname(path)
            fd, tmp_path = tempfile.mkstemp(dir=dir_name, suffix=".tmp")
            try:
                with os.fdopen(fd, "w") as f:
                    json.dump(config, f, indent=4, ensure_ascii=False)
                os.replace(tmp_path, path)
            except Exception:
                if os.path.exists(tmp_path):
                    os.unlink(tmp_path)
                raise
            # Provider 中的基础信息立即更新；HTTP 部署配置由客户端缓存，
            # 按约定在进程重启后生效。
            self._provider.reload()
            logger.info(f"基础信息已更新: sn={params.get('sn')}")
            return {"status": "ok"}
        except Exception as e:
            logger.error(f"写入配置失败: {e}")
            return {"status": "error", "msg": str(e)}

    def handle_get(self, _params: dict) -> dict:
        return {"status": "ok", "data": self._provider.get_all()}
