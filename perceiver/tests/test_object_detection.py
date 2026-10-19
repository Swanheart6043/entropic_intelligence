import sys
import threading
import time
import unittest
from pathlib import Path

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.object_detection.object_detection_worker import (
    ObjectDetectionWorker,
)
from detection.object_detection.object_snapshot import ObjectDetection
from detection.object_detection.person_roi_detector import PersonRoiDetector
from detection.safety_helmet.detector import SafetyHelmetDetector


class _ObjectDetector:
    def __init__(self, detections):
        self.detections = detections
        self.frames = []

    def detect(self, frame):
        self.frames.append(frame)
        return list(self.detections)


class ObjectDetectionWorkerTest(unittest.TestCase):
    def test_publishes_same_frame_with_metadata_and_class_names(self):
        detector = _ObjectDetector([
            (0, 0.9, 10, 20, 50, 90),
            (2, 0.8, 60, 30, 100, 80),
        ])
        frame = np.zeros((100, 120, 3), dtype=np.uint8)
        worker = ObjectDetectionWorker(
            hub=object(),
            detector=detector,
            config={
                "class_names": ("person", "bicycle", "car"),
                "snapshot_max_age": 10.0,
            },
        )

        worker.process_frame(frame, timestamp=123.5, frame_id=7)
        snapshot = worker.get_latest_snapshot()

        self.assertIsNotNone(snapshot)
        self.assertIs(snapshot.frame, frame)
        self.assertEqual(snapshot.frame_id, 7)
        self.assertEqual(snapshot.timestamp, 123.5)
        self.assertEqual(snapshot.person_boxes, ((10, 20, 50, 90),))
        self.assertEqual(snapshot.detections[1].class_name, "car")
        self.assertIsNone(worker.get_latest_snapshot(after_frame_id=7))

    def test_worker_loop_preserves_frame_hub_metadata(self):
        frame = np.zeros((20, 20, 3), dtype=np.uint8)

        class FrameHub:
            def get_frame_with_info(self):
                return frame, 456.0, 12

        published = threading.Event()

        class Detector(_ObjectDetector):
            def detect(self, current_frame):
                published.set()
                return super().detect(current_frame)

        worker = ObjectDetectionWorker(
            hub=FrameHub(),
            detector=Detector([]),
            config={"detect_interval": 0.01},
        )
        self.assertTrue(worker.start())
        try:
            self.assertTrue(published.wait(timeout=1.0))
            deadline = time.monotonic() + 1.0
            snapshot = worker.get_latest_snapshot()
            while snapshot is None and time.monotonic() < deadline:
                time.sleep(0.005)
                snapshot = worker.get_latest_snapshot()
            self.assertIsNotNone(snapshot)
            self.assertEqual(snapshot.frame_id, 12)
            self.assertEqual(snapshot.timestamp, 456.0)
        finally:
            worker.stop()

    def test_rejects_expired_snapshot(self):
        worker = ObjectDetectionWorker(
            hub=object(),
            detector=_ObjectDetector([]),
            config={"snapshot_max_age": 0.01},
        )
        worker.process_frame(
            np.zeros((10, 10, 3), dtype=np.uint8),
            timestamp=1.0,
            frame_id=1,
        )
        worker._snapshot_published_at = time.monotonic() - 1.0

        self.assertIsNone(worker.get_latest_snapshot())


class PersonRoiDetectorTest(unittest.TestCase):
    def test_uses_person_roi_maps_coordinates_and_filters_target_class(self):
        stage_two = _ObjectDetector([
            (0, 0.7, 2, 3, 12, 13),
            (1, 0.9, 1, 2, 11, 12),
        ])
        detector = PersonRoiDetector(
            model_path="unused.bin",
            classes_num=2,
            score_thres=0.2,
            nms_thres=0.45,
            target_class_ids={1},
            person_min_size=1,
            roi_height_ratio=1.0,
            crop_expand=0.0,
            confirm_frames=1,
            detector=stage_two,
        )
        frame = np.zeros((100, 100, 3), dtype=np.uint8)
        person = ObjectDetection(
            class_id=0,
            class_name="person",
            confidence=0.95,
            bbox=(10, 20, 50, 80),
        )

        detections, events = detector.detect(frame, [person])

        self.assertEqual(detections, [(1, 0.9, 11, 22, 21, 32)])
        self.assertEqual(events[0]["person_bbox"], [10, 20, 50, 80])
        self.assertEqual(events[0]["target_bbox"], [11, 22, 21, 32])
        self.assertEqual(stage_two.frames[0].shape[:2], (60, 40))


class SafetyHelmetPersonGateTest(unittest.TestCase):
    def test_helmet_must_be_near_person_head_region(self):
        detector = SafetyHelmetDetector.__new__(SafetyHelmetDetector)
        detector.person_iou_thres = 0.08
        person = [20, 20, 80, 180]

        self.assertTrue(
            detector.helmet_matches_person([35, 10, 60, 40], person)
        )
        self.assertFalse(
            detector.helmet_matches_person([120, 120, 150, 150], person)
        )


if __name__ == "__main__":
    unittest.main()
