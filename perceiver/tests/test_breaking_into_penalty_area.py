import sys
import unittest
from pathlib import Path

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.breaking_into_penalty_area import BreakingIntoPenaltyAreaWorker
from detection.breaking_into_penalty_area.breaking_into_penalty_area import (
    PenaltyAreaDetector,
)
from detection.object_detection.object_snapshot import (
    ObjectDetection,
    ObjectDetectionSnapshot,
)


def _person(bbox, confidence=0.9):
    return ObjectDetection(
        class_id=0,
        class_name="person",
        confidence=confidence,
        bbox=bbox,
    )


class PenaltyAreaDetectorTest(unittest.TestCase):
    def test_left_side_detects_any_overlap_and_keeps_center_line_safe(self):
        detector = PenaltyAreaDetector(side="left", area_ratio=0.5)
        detections = [
            _person((99, 10, 130, 90)),
            _person((100, 10, 150, 90)),
        ]

        intruders = detector.find_intruders(200, detections)

        self.assertEqual(intruders, [detections[0]])

    def test_right_side_detects_any_overlap_and_filters_low_confidence(self):
        detector = PenaltyAreaDetector(
            side="right",
            area_ratio=0.5,
            person_score_thres=0.5,
        )
        detections = [
            _person((50, 10, 101, 90)),
            _person((50, 10, 100, 90)),
            _person((120, 10, 160, 90), confidence=0.49),
        ]

        intruders = detector.find_intruders(200, detections)

        self.assertEqual(intruders, [detections[0]])


class _ObjectDetectionWorker:
    def __init__(self, frame):
        self.frame = frame
        self.frame_id = 0
        self.detections = ()

    def publish(self, *detections):
        self.frame_id += 1
        self.detections = detections

    def get_latest_snapshot(self, after_frame_id=None):
        if after_frame_id is not None and self.frame_id <= after_frame_id:
            return None
        return ObjectDetectionSnapshot(
            frame_id=self.frame_id,
            timestamp=float(self.frame_id),
            detections=tuple(self.detections),
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


class BreakingIntoPenaltyAreaWorkerTest(unittest.TestCase):
    def setUp(self):
        self.frame = np.zeros((100, 200, 3), dtype=np.uint8)
        self.object_worker = _ObjectDetectionWorker(self.frame)
        self.clock = _Clock()
        self.sender = _Sender()
        self.worker = BreakingIntoPenaltyAreaWorker(
            hub=object(),
            zmq_sender=self.sender,
            object_detection_worker=self.object_worker,
            config={
                "side": "left",
                "confirm_frames": 3,
                "alarm_interval": 10.0,
            },
            clock=self.clock,
        )

    def _process(self, *detections):
        self.object_worker.publish(*detections)
        return self.worker.process(self.frame)

    def test_confirms_then_repeats_alarm_while_area_remains_occupied(self):
        intruder = _person((20, 10, 80, 90))

        self.assertIsNone(self._process(intruder))
        self.assertIsNone(self._process(intruder))
        first_alarm = self._process(intruder)
        self.assertIsNotNone(first_alarm)
        self.assertEqual(first_alarm["frame_id"], 3)
        self.assertEqual(first_alarm["dets"], [(0, 0.9, 20, 10, 80, 90)])

        self.clock.now = 9.9
        self.assertIsNone(self._process(intruder))
        self.clock.now = 10.0
        repeated_alarm = self._process(intruder)
        self.assertIsNotNone(repeated_alarm)

    def test_leaving_area_requires_confirmation_again(self):
        intruder = _person((20, 10, 80, 90))
        safe_person = _person((120, 10, 180, 90))
        for _ in range(3):
            result = self._process(intruder)
        self.assertIsNotNone(result)

        self.assertIsNone(self._process(safe_person))
        self.clock.now = 20.0
        self.assertIsNone(self._process(intruder))
        self.assertIsNone(self._process(intruder))
        self.assertIsNotNone(self._process(intruder))

    def test_alert_uses_breaking_into_penalty_area_type(self):
        result = {
            "frame": self.frame,
            "dets": [(0, 0.9, 20, 10, 80, 90)],
            "class_names": ("person",),
            "frame_id": 7,
        }

        self.worker.alert(self.worker.name, result)

        self.assertEqual(len(self.sender.calls), 1)
        self.assertEqual(
            self.sender.calls[0][3:],
            (7, "breaking_into_penalty_area"),
        )

    def test_dynamic_control_resets_confirmation_and_alarm_state(self):
        self.worker._consecutive_intrusion_frames = 2
        self.worker._last_alarm_at = 5.0

        self.assertTrue(self.worker.disable())
        self.assertFalse(self.worker.is_enabled)
        self.assertEqual(self.worker._consecutive_intrusion_frames, 0)
        self.assertIsNone(self.worker._last_alarm_at)

        self.worker._consecutive_intrusion_frames = 2
        self.worker._last_alarm_at = 8.0
        self.assertTrue(self.worker.enable())
        self.assertTrue(self.worker.is_enabled)
        self.assertEqual(self.worker._consecutive_intrusion_frames, 0)
        self.assertIsNone(self.worker._last_alarm_at)


if __name__ == "__main__":
    unittest.main()
