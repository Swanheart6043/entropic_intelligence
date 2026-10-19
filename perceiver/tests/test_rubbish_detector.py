import sys
import unittest
from pathlib import Path

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.rubbish.rubbish_detector import RubbishDetector


def _detector_without_model():
    detector = object.__new__(RubbishDetector)
    detector.CLASSES_NUM = 1
    detector.SCORE_THRESHOLD = 0.50
    detector.NMS_THRESHOLD = 0.45
    detector.confirm_frames = 2
    detector.confirm_window = 3
    detector.max_missed_frames = 2
    detector.match_thres = 0.10
    detector.match_iou_thres = 0.05
    detector.roi_bottom_ratio = 0.55
    detector.min_area_ratio = 0.0005
    detector.min_box_size = 12
    detector.aspect_ratio_min = 0.35
    detector.aspect_ratio_max = 4.0
    detector.border_margin_ratio = 0.01
    detector._alarm_gap_frames = 20
    detector._states = []
    detector.img_h = 200
    detector.img_w = 200
    detector.scale = 1.0
    detector.x_shift = 0
    detector.y_shift = 0
    return detector


class RubbishGeometryTest(unittest.TestCase):
    def test_vertical_high_texture_target_is_not_rejected(self):
        detector = _detector_without_model()
        rng = np.random.default_rng(7)
        frame = rng.integers(0, 256, size=(200, 200, 3), dtype=np.uint8)

        self.assertTrue(detector._is_valid_rubbish(frame, 80, 100, 100, 150))

    def test_extreme_aspect_ratio_is_still_rejected(self):
        detector = _detector_without_model()
        frame = np.zeros((200, 200, 3), dtype=np.uint8)

        self.assertFalse(detector._is_valid_rubbish(frame, 20, 100, 180, 112))


class RubbishTrackingTest(unittest.TestCase):
    def setUp(self):
        self.detector = _detector_without_model()
        self.frame = np.zeros((200, 200, 3), dtype=np.uint8)
        self.first = (0, 0.70, 80, 100, 100, 150)
        self.shifted = (0, 0.72, 85, 100, 105, 150)

    def test_two_hits_in_three_frames_allow_one_miss(self):
        self.assertEqual(
            self.detector.get_rubbish_events([self.first], self.frame, fid=1),
            [],
        )
        self.assertEqual(
            self.detector.get_rubbish_events([], self.frame, fid=2),
            [],
        )

        alarms = self.detector.get_rubbish_events(
            [self.shifted], self.frame, fid=3
        )

        self.assertEqual(len(alarms), 1)
        self.assertEqual(alarms[0].bbox, [85, 100, 105, 150])

    def test_matched_track_center_follows_target(self):
        self.detector.get_rubbish_events([self.first], self.frame, fid=1)
        self.detector.get_rubbish_events([self.shifted], self.frame, fid=2)

        self.assertEqual(len(self.detector._states), 1)
        self.assertAlmostEqual(self.detector._states[0].cx, 95 / 200)

    def test_track_expires_only_after_miss_tolerance(self):
        self.detector.get_rubbish_events([self.first], self.frame, fid=1)
        self.detector.get_rubbish_events([], self.frame, fid=2)
        self.detector.get_rubbish_events([], self.frame, fid=3)
        self.assertEqual(len(self.detector._states), 1)

        self.detector.get_rubbish_events([], self.frame, fid=4)
        self.assertEqual(self.detector._states, [])


class RubbishPostprocessTest(unittest.TestCase):
    def test_single_output_model_is_supported(self):
        detector = _detector_without_model()
        raw_output = np.array([[[100.0, 120.0, 20.0, 40.0, 0.90]]])

        detections = detector.postprocess([raw_output])

        self.assertEqual(len(detections), 1)
        class_id, score, x1, y1, x2, y2 = detections[0]
        self.assertEqual(class_id, 0)
        self.assertAlmostEqual(score, 0.90)
        self.assertEqual((x1, y1, x2, y2), (90, 100, 110, 140))


if __name__ == "__main__":
    unittest.main()
