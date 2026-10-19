import os
import subprocess
import logging
from datetime import datetime

logger = logging.getLogger("DateTimeCommands")

class DateTimeCommands:
    HELPER_PATH = "/usr/local/bin/set_time_helper"
    _CMD_TIMEOUT = 5

    # 合理范围（毫秒）：2020-01-01 ~ 2100-01-01
    _MIN_MS = 1577808000000
    _MAX_MS = 4102444800000

    # 错误码 → 中文消息（和 helper 的 exit code 一一对应）
    _EXIT_MSG = {
        2: "参数错误",
        3: "时间戳超出合理范围",
        4: "clock_settime 失败（CAP_SYS_TIME 缺失？）",
        5: "打开 /dev/rtc0 失败（CAP_DAC_OVERRIDE 缺失？）",
        6: "写 RTC 失败",
    }

    def __init__(self):
        if not os.path.exists(self.HELPER_PATH):
            logger.error("校时 helper 不存在: %s", self.HELPER_PATH)

    def set_date_and_time(self, params: dict) -> dict:
        if "date" not in params:
            return {"status": "error", "msg": "缺少时间戳参数: date"}

        raw = params["date"]
        try:
            ms = int(float(raw))   # 兼容 int / str / 偶发的 float
        except (TypeError, ValueError):
            return {"status": "error", "msg": f"时间戳必须是数字(毫秒): {raw!r}"}

        if not (self._MIN_MS <= ms < self._MAX_MS):
            return {
                "status": "error",
                "msg": f"时间戳超出合理范围(毫秒): {ms}",
            }

        try:
            r = subprocess.run(
                [self.HELPER_PATH, str(ms)],
                capture_output=True,
                text=True,
                timeout=self._CMD_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            logger.error("校时超时")
            return {"status": "error", "msg": "校时超时"}
        except FileNotFoundError:
            logger.error("校时 helper 不存在: %s", self.HELPER_PATH)
            return {"status": "error", "msg": f"helper 不存在: {self.HELPER_PATH}"}

        if r.returncode != 0:
            tag = self._EXIT_MSG.get(r.returncode, f"未知错误 (rc={r.returncode})")
            err = (r.stderr or "").strip()
            full = f"{tag}: {err}" if err else tag
            logger.error("校时失败: %s", full)
            return {"status": "error", "msg": full}

        dt_str = datetime.fromtimestamp(ms / 1000.0).strftime("%Y-%m-%d %H:%M:%S")
        logger.info("校时成功: ms=%s -> %s", ms, dt_str)

        return {
            "status": "ok", 
            "msg": f"校时成功: {dt_str}"
        }