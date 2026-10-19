"""共享目标检测结果快照。"""

from dataclasses import dataclass, field

import numpy as np


@dataclass(frozen=True)
class ObjectDetection:
    class_id: int
    class_name: str
    confidence: float
    bbox: tuple[int, int, int, int]
    track_id: int | None = None

    def as_legacy_tuple(self) -> tuple[int, float, int, int, int, int]:
        x1, y1, x2, y2 = self.bbox
        return self.class_id, self.confidence, x1, y1, x2, y2


@dataclass(frozen=True)
class ObjectDetectionSnapshot:
    frame_id: int
    timestamp: float
    detections: tuple[ObjectDetection, ...]
    frame: np.ndarray = field(repr=False, compare=False)

    def for_class(self, class_name: str) -> tuple[ObjectDetection, ...]:
        return tuple(
            detection
            for detection in self.detections
            if detection.class_name == class_name
        )

    @property
    def person_detections(self) -> tuple[ObjectDetection, ...]:
        return self.for_class("person")

    @property
    def person_boxes(self) -> tuple[tuple[int, int, int, int], ...]:
        return tuple(detection.bbox for detection in self.person_detections)
