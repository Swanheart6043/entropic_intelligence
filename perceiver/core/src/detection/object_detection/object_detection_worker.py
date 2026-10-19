"""共享通用目标检测 Worker。"""
import logging
import threading
import time
import numpy as np
from ..detection_worker import DetectionWorker
from .class_names import COCO_CLASS_NAMES
from .object_detector import ObjectDetector
from .object_snapshot import ObjectDetection, ObjectDetectionSnapshot

logger = logging.getLogger("ObjectDetectionWorker")

CONFIG = {
    "model_path": "/home/heyn/model/yolov8s_rdk_x5.bin",
    "class_names": COCO_CLASS_NAMES,
    "score_thres": 0.25,
    "nms_thres": 0.45,
    "detect_interval": 0.2,
    "snapshot_max_age": 2.0,
}

class ObjectDetectionWorker(DetectionWorker):
    """运行一次通用 YOLO，并向多个业务 Worker 发布同帧检测结果。"""
    def __init__(self, hub, config: dict | None = None, detector=None):
        cfg = dict(CONFIG)
        if config:
            cfg.update(config)
        self.cfg = cfg
        self.class_names = tuple(cfg["class_names"])
        self.detector = detector or ObjectDetector(
            model_path=cfg["model_path"],
            classes_num=len(self.class_names),
            score_thres=cfg["score_thres"],
            nms_thres=cfg["nms_thres"],
            model_name="通用目标检测",
        )
        self._snapshot: ObjectDetectionSnapshot | None = None
        self._snapshot_published_at = 0.0
        self._snapshot_lock = threading.RLock()

        super().__init__(
            name="ObjectDetectionWorker",
            hub=hub,
            interval=cfg["detect_interval"],
        )

    def process(self, frame: np.ndarray):
        """兼容直接调用；正常线程路径使用 process_frame 保留帧元数据。"""
        self._detect_and_publish(frame, time.time(), self._last_frame_id)
        return None

    def process_frame(self, frame: np.ndarray, timestamp: float, frame_id: int):
        self._detect_and_publish(frame, timestamp, frame_id)
        return None

    def _detect_and_publish(self, frame: np.ndarray, timestamp: float, frame_id: int) -> ObjectDetectionSnapshot:
        raw_detections = self.detector.detect(frame)
        detections = []
        for class_id, confidence, x1, y1, x2, y2 in raw_detections:
            class_name = (
                self.class_names[class_id]
                if 0 <= class_id < len(self.class_names)
                else f"class_{class_id}"
            )
            detections.append(
                ObjectDetection(
                    class_id=int(class_id),
                    class_name=class_name,
                    confidence=float(confidence),
                    bbox=(int(x1), int(y1), int(x2), int(y2)),
                )
            )

        snapshot = ObjectDetectionSnapshot(
            frame_id=frame_id,
            timestamp=timestamp,
            frame=frame,
            detections=tuple(detections),
        )
        with self._snapshot_lock:
            self._snapshot = snapshot
            self._snapshot_published_at = time.monotonic()
        return snapshot

    def get_latest_snapshot(self, after_frame_id: int | None = None, max_age_seconds: float | None = None) -> ObjectDetectionSnapshot | None:
        with self._snapshot_lock:
            snapshot = self._snapshot
            published_at = self._snapshot_published_at

        if snapshot is None:
            return None
        if after_frame_id is not None and snapshot.frame_id <= after_frame_id:
            return None

        max_age = (
            self.cfg["snapshot_max_age"]
            if max_age_seconds is None
            else max_age_seconds
        )
        if max_age >= 0 and time.monotonic() - published_at > max_age:
            return None
        return snapshot

    def get_person_boxes(self) -> list[list[int]]:
        snapshot = self.get_latest_snapshot()
        if snapshot is None:
            return []
        return [list(box) for box in snapshot.person_boxes]