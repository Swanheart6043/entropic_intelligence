"""
thermal_worker.py - 热成像高温检测 Worker (接入 DetectionWorker 框架)

与垃圾/烟雾等 worker 平级。核心区别: 不是 BPU 模型, 是纯 CV 温度反推。
- model_path / score_thres / nms_thres / classes_num 等 YOLO 参数不需要
- 关键参数是 温度阈值 / colorbar坐标 (在 thermal/config.py)

帧来源: 挂在【独立的热成像 FrameHub】上 (与可见光 hub 不同实例, dec_chn 必须错开)。
  FrameHub.get_frame_with_info() 已返回 BGR, 所以本 worker 直接吃 BGR, 无需转换。

上报(方案A): 高温目标适配成 send_detection 的元组格式, 复用现有 ZMQSender。
  class_id = 0 (单类: 高温目标)
  score = 温度值°C (藏在 confidence 字段; 协调层按 detection_type 解释)
  bbox = (x1,y1,x2,y2)
"""
import time
import logging
import numpy as np
from ...detection_worker import DetectionWorker
from .thermal_detector import ThermalDetector
from . import config as C

logger = logging.getLogger("ThermalWorker")

# 上报类别名 (单类). send_detection 用 class_names[class_id] 取标签。
THERMAL_CLASS_NAMES = ["OverTemp"]

CONFIG = {
    "detect_interval": 1.0, # 处理间隔秒 (温度变化慢, 可放大到 3~5 降负载)
    "alarm_cooldown": 30.0, # 报警冷却秒数
    "confirm_frames": 3, # 连续命中帧数才确认 (滤瞬时噪点)
}

class ThermalWorker(DetectionWorker):
    def __init__(self, hub, zmq_sender):
        cfg = dict(CONFIG)
        self.zmq_sender = zmq_sender
        self.cfg = cfg

        super().__init__(
            name="ThermalWorker",
            hub=hub,
            interval=cfg["detect_interval"],
            callback=self.alert,
        )

        self.detector = ThermalDetector()
        self._confirm_need = cfg["confirm_frames"]
        self._hit_streak = 0
        self._cooldown = cfg["alarm_cooldown"]
        self._last_alarm_time = 0.0
        self._frame_id = 0

        # 尺寸校验: hub 尺寸必须与标定坐标一致, 否则 detect 每帧都会因尺寸不符跳过
        if hub.width and (hub.width != C.FRAME_W or hub.height != C.FRAME_H):
            logger.warning(
                "热成像 hub 尺寸 %dx%d 与 config %dx%d 不一致! "
                "colorbar/OSD 坐标需用该分辨率重新标定 (见 calibrator.dump_layout)",
                hub.width, hub.height, C.FRAME_W, C.FRAME_H)

    # ---------- 启动预热: 主程序在 hub 出帧后主动调一次, 把首帧建LUT(~5s)的卡顿放在初始化 ----------
    def warmup(self, timeout=10.0):
        """阻塞等一帧并跑一次 detect, 触发 3D LUT 建表。主程序可选调用。"""
        t0 = time.time()
        while time.time() - t0 < timeout:
            frame, _, fid = self.hub.get_frame_with_info()
            if frame is not None:
                try:
                    self.detector.detect(frame)   # 触发建表
                    logger.info("热成像检测器预热完成 (LUT 已建)")
                except Exception as e:
                    logger.warning("预热 detect 异常(不致命): %s", e)
                return True
            time.sleep(0.2)
        logger.warning("预热超时, 未取到帧; LUT 将在首次检测时建立(首帧会卡~5s)")
        return False

    # ---------- 结果适配成 send_detection 元组 ----------
    @staticmethod
    def _to_results(detections):
        """Detection(bbox=(x,y,w,h),max_temp,...) -> (class_id,score,x1,y1,x2,y2)。
        score 位置塞温度值, 协调层按 detection_type='thermal_overtemp' 解释为°C。"""
        out = []
        for d in detections:
            x, y, w, h = d.bbox
            out.append((0, float(d.max_temp), x, y, x + w, y + h))
        return out

    # ---------- 主处理 (hub 给的是 BGR) ----------
    def process(self, frame: np.ndarray):
        self._frame_id += 1

        res = self.detector.detect(frame)
        if not res["ok"]:
            logger.debug("跳过检测: %s", res.get("reason"))
            self._hit_streak = 0
            return None

        dets = res["detections"]
        if not dets:
            self._hit_streak = 0
            return None

        # 多帧确认
        self._hit_streak += 1
        if self._hit_streak < self._confirm_need:
            return None

        # 冷却
        now = time.time()
        if now - self._last_alarm_time < self._cooldown:
            return None
        self._last_alarm_time = now
        self._hit_streak = 0

        max_t = max(d.max_temp for d in dets)
        logger.warning(
            "🔥 高温报警! 目标数=%d 最高温=%.1f°C 量程=[%.1f,%.1f]", 
            len(dets), 
            max_t, 
            res["t_min"], 
            res["t_max"]
        )

        return {
            "type": "thermal_overtemp",
            "results": self._to_results(dets), # 已适配成元组
            "frame": frame, # BGR, 给 send_detection 画框/编码
            "class_names": THERMAL_CLASS_NAMES,
            "max_temp": max_t,
            "frame_max_temp": res["frame_max_temp"],
            "t_range": (res["t_min"], res["t_max"]),
            "timestamp": now,
        }

    # ---------- 上报 ----------
    def alert(self, worker_name, result):
        self.zmq_sender.send_detection(
            result["frame"],
            result["results"],
            result["class_names"],
            self._frame_id,
            "thermal_overtemp",
        )
        logger.info(
            "已发送高温告警: 最高温=%.1f°C", 
            result["max_temp"]
        )
