import sys
import unittest
from pathlib import Path

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.cats_and_dogs import CatsAndDogsDetector, CatsAndDogsWorker
from detection.object_detection.object_snapshot import (
    ObjectDetection,
    ObjectDetectionSnapshot,
)


def _detection(class_id, class_name, confidence, bbox):
    return ObjectDetection(
        class_id=class_id,
        class_name=class_name,
        confidence=confidence,
        bbox=bbox,
    )


class CatsAndDogsDetectorTest(unittest.TestCase):
    def test_filters_cat_and_dog_by_name_and_confidence(self):
        detections = [
            _detection(15, "cat", 0.90, (10, 20, 40, 60)),
            _detection(16, "dog", 0.80, (50, 20, 90, 70)),
            _detection(0, "person", 0.99, (0, 0, 100, 100)),
            _detection(15, "cat", 0.24, (1, 2, 3, 4)),
        ]

        animals = CatsAndDogsDetector(score_thres=0.25).detect(detections)

        self.assertEqual(animals, detections[:2])

    def test_rejects_invalid_threshold(self):
        with self.assertRaisesRegex(ValueError, "score_thres"):
            CatsAndDogsDetector(score_thres=1.01)


class _ObjectDetectionWorker:
    def __init__(self, frame):
        self.frame = frame
        self.frame_id = 0
        self.detections = ()

    def publish(self, *detections):
        self.frame_id += 1
        self.detections = tuple(detections)

    def get_latest_snapshot(self, after_frame_id=None):
        if after_frame_id is not None and self.frame_id <= after_frame_id:
            return None
        return ObjectDetectionSnapshot(
            frame_id=self.frame_id,
            timestamp=float(self.frame_id),
            detections=self.detections,
            frame=self.frame,
        )


class _Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class _Sender:
    def __init__(self):
        self.calls = []

    def send_detection(self, *args):
        self.calls.append(args)


class CatsAndDogsWorkerTest(unittest.TestCase):
    def setUp(self):
        self.frame = np.zeros((100, 200, 3), dtype=np.uint8)
        self.object_worker = _ObjectDetectionWorker(self.frame)
        self.clock = _Clock()
        self.sender = _Sender()
        self.worker = CatsAndDogsWorker(
            hub=object(),
            zmq_sender=self.sender,
            object_detection_worker=self.object_worker,
            config={"alarm_interval": 5.0},
            clock=self.clock,
        )

    def test_reuses_snapshot_and_preserves_coco_class_ids(self):
        cat = _detection(15, "cat", 0.9, (10, 20, 40, 60))
        dog = _detection(16, "dog", 0.8, (50, 20, 90, 70))
        person = _detection(0, "person", 0.99, (0, 0, 100, 100))
        self.object_worker.publish(cat, dog, person)

        result = self.worker.process(self.frame)

        self.assertEqual(
            result["dets"],
            [
                (15, 0.9, 10, 20, 40, 60),
                (16, 0.8, 50, 20, 90, 70),
            ],
        )
        self.assertIs(result["frame"], self.frame)
        self.assertEqual(result["frame_id"], 1)
        self.assertEqual(result["class_names"][15:17], ("cat", "dog"))
        self.assertIsNone(self.worker.process(self.frame))

    def test_applies_alarm_interval_and_reports_again(self):
        cat = _detection(15, "cat", 0.9, (10, 20, 40, 60))
        self.object_worker.publish(cat)
        self.assertIsNotNone(self.worker.process(self.frame))

        self.clock.now = 4.9
        self.object_worker.publish(cat)
        self.assertIsNone(self.worker.process(self.frame))

        self.clock.now = 5.0
        self.object_worker.publish(cat)
        self.assertIsNotNone(self.worker.process(self.frame))

    def test_alert_sends_cat_and_dog_detection_type(self):
        cat = _detection(15, "cat", 0.9, (10, 20, 40, 60))
        self.object_worker.publish(cat)
        result = self.worker.process(self.frame)

        self.worker.alert(self.worker.name, result)

        self.assertEqual(len(self.sender.calls), 1)
        self.assertEqual(self.sender.calls[0][3:], (1, "cats_and_dogs"))


if __name__ == "__main__":
    unittest.main()
