import sys
import time
import unittest
from pathlib import Path
from unittest import mock

import numpy as np


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from detection.detection_manager import DetectionManager
from detection.object_detection.object_detection_worker import ObjectDetectionWorker
from detection.object_detection.object_snapshot import ObjectDetection
from detection.smoking.hand_detector import HandDetector
from detection.smoking.hand_mouth_detector import HandMouthSmokingDetector
from detection.smoking.smoking_thread import CONFIG, SmokingWorker


class FakeDetector:
    def __init__(self, detections):
        self.detections = detections
        self.frames = []

    def detect(self, frame):
        self.frames.append(frame)
        return self.detections(frame) if callable(self.detections) else list(self.detections)


def face():
    return {"bbox": [60, 20, 140, 120], "score": 0.95,
            "landmarks": [[80, 50], [120, 50], [100, 70], [90, 90], [110, 90]]}


class HandDetectorTest(unittest.TestCase):
    def setUp(self):
        self.detector = HandDetector.__new__(HandDetector)
        self.detector.anchors = HandDetector._make_anchors()
        self.detector.score_thres = 0.5
        self.detector.nms_thres = 0.3

    def test_rgb_nhwc_letterbox(self):
        frame = np.full((384, 192, 3), (0, 127, 255), dtype=np.uint8)
        blob, scale, padding = self.detector._preprocess(frame)
        self.assertEqual(blob.shape, (1, 192, 192, 3))
        self.assertEqual(scale, 0.5)
        self.assertEqual(padding, (48, 0))
        np.testing.assert_allclose(blob[0, 90, 96], [1.0, 127 / 255, 0.0])
        self.assertTrue(np.all(blob[0, :, :48] == 0))

    def test_decodes_letterboxed_boxes_and_suppresses_duplicate_palms(self):
        regression = np.zeros((1, 2016, 18), dtype=np.float32)
        scores = np.full((1, 2016, 1), -100.0, dtype=np.float32)
        # 第一锚点位于模型输入 (4,4)，解码为原图 (20,100,60,140)。
        regression[0, 0, :4] = [64, 56, 20, 20]
        regression[0, 1, :4] = [65, 56, 20, 20]
        scores[0, :2, 0] = [10, 8]
        results = self.detector._postprocess([scores, regression], (384, 192, 3), 0.5, (48, 0))
        self.assertEqual(len(results), 1)
        np.testing.assert_allclose(results[0]["bbox"], [20, 100, 60, 140])
        self.assertGreater(results[0]["score"], 0.99)

    def test_incompatible_model_is_not_silently_treated_as_no_hands(self):
        with self.assertRaisesRegex(ValueError, "不兼容"):
            self.detector._postprocess([np.zeros((1, 1)), np.zeros((1, 4))], (100, 100, 3), 1, (0, 0))

    def test_missing_model_has_actionable_error(self):
        with self.assertRaisesRegex(FileNotFoundError, "download_hand_model"):
            HandDetector("nonexistent-palm-model-for-test.onnx")


class HandMouthSmokingTest(unittest.TestCase):
    def setUp(self):
        self.frame = np.full((700, 900, 3), (80, 110, 140), dtype=np.uint8)
        self.person = ObjectDetection(0, "person", 0.95, (100, 100, 300, 500))
        self.hands = FakeDetector([])
        self.faces = FakeDetector([])
        self.smoking = FakeDetector([(0, 0.9, 5, 7, 15, 11)])

    def pipeline(self, **overrides):
        cfg = {**CONFIG, "crop_expand": 0, "hand_crop_expand": 0.75,
               "confirm_frames": 1, **overrides}
        return HandMouthSmokingDetector(cfg, self.hands, self.faces, self.smoking)

    def test_hand_only_uses_expanded_local_crop_and_maps_to_original_frame(self):
        self.hands.detections = [{"bbox": [40, 200, 60, 220], "score": 0.9}]
        detector = self.pipeline()
        detections, events = detector.detect(self.frame, [self.person])
        self.assertEqual(detections, [(0, 0.9, 130, 292, 140, 296)])
        self.assertEqual(events[0]["regions"], ["hand"])
        self.assertEqual(events[0]["person_bbox"], [100, 100, 300, 500])
        self.assertEqual(detector.last_regions[0]["bbox"], [125, 285, 175, 335])
        np.testing.assert_array_equal(self.smoking.frames[0], self.frame[285:335, 125:175])
        self.assertTrue(np.shares_memory(self.smoking.frames[0], self.frame))

    def test_mouth_only_uses_landmarks_without_requiring_a_hand(self):
        self.faces.detections = [face()]
        detector = self.pipeline()
        detections, events = detector.detect(self.frame, [self.person])
        self.assertEqual(detections, [(0, 0.9, 173, 179, 183, 183)])
        self.assertEqual(events[0]["regions"], ["mouth"])
        self.assertEqual(detector.last_regions[0]["bbox"], [168, 172, 232, 208])

    def test_no_localization_never_falls_back_to_full_person_inference(self):
        detector = self.pipeline()
        self.assertEqual(detector.detect(self.frame, [self.person]), ([], []))
        self.assertEqual(len(self.smoking.frames), 0)
        self.assertEqual(detector.last_stats["persons"], 1)

    def test_missing_mouth_landmarks_does_not_guess_from_face_box(self):
        invalid = face()
        invalid["landmarks"] = []
        self.faces.detections = [invalid]
        self.assertEqual(self.pipeline().detect(self.frame, [self.person]), ([], []))
        self.assertEqual(len(self.smoking.frames), 0)

    def test_no_people_skips_both_localizers(self):
        car = ObjectDetection(2, "car", 0.9, (100, 100, 300, 500))
        self.assertEqual(self.pipeline().detect(self.frame, [car]), ([], []))
        self.assertEqual(len(self.hands.frames), 0)
        self.assertEqual(len(self.faces.frames), 0)

    def test_hand_and_mouth_duplicate_counts_as_one_hit_per_frame(self):
        self.hands.detections = [{"bbox": [90, 80, 110, 100], "score": 0.95}]
        self.faces.detections = [face()]
        self.smoking.detections = lambda roi: (
            [(0, 0.9, 20, 20, 30, 25)] if roi.shape[:2] == (50, 50)
            else [(0, 0.85, 27, 13, 37, 18)]
        )
        detector = self.pipeline(confirm_frames=2)
        self.assertEqual(detector.detect(self.frame, [self.person]), ([], []))
        self.assertEqual(detector.last_stats["smoking_candidates"], 1)
        detections, events = detector.detect(self.frame, [self.person])
        self.assertEqual(detections, [(0, 0.9, 195, 185, 205, 190)])
        self.assertEqual(events[0]["regions"], ["hand", "mouth"])

    def test_temporal_confirmation_does_not_emit_old_boxes_on_a_missed_frame(self):
        self.faces.detections = [face()]
        detector = self.pipeline(confirm_frames=2)
        self.assertEqual(detector.detect(self.frame, [self.person]), ([], []))
        self.assertEqual(len(detector.detect(self.frame, [self.person])[1]), 1)
        self.faces.detections = []
        self.assertEqual(detector.detect(self.frame, [self.person]), ([], []))
        self.faces.detections = [face()]
        self.assertEqual(len(detector.detect(self.frame, [self.person])[1]), 1)

    def test_multiple_people_keep_separate_coordinate_offsets(self):
        self.faces.detections = [face()]
        second = ObjectDetection(0, "person", 0.95, (500, 100, 700, 500))
        detections, events = self.pipeline().detect(self.frame, [self.person, second])
        self.assertEqual([detection[2] for detection in detections], [173, 573])
        self.assertEqual([event["person_bbox"][0] for event in events], [100, 500])

    def test_invalid_or_low_score_candidates_do_not_enter_confirmation(self):
        self.faces.detections = [face()]
        self.smoking.detections = [(0, 0.59, 5, 7, 15, 11), (0, 0.9, 15, 7, 5, 11),
                                   (0, float("nan"), 5, 7, 15, 11), (1, 0.9, 5, 7, 15, 11)]
        self.assertEqual(self.pipeline().detect(self.frame, [self.person]), ([], []))

    def test_border_regions_are_clipped_and_never_wrap_numpy_indices(self):
        self.hands.detections = [{"bbox": [0, 0, 20, 20], "score": 0.9}]
        detector = self.pipeline()
        detector.detect(self.frame, [self.person])
        self.assertEqual(detector.last_regions[0]["bbox"], [100, 100, 135, 135])
        self.assertEqual(self.smoking.frames[0].shape[:2], (35, 35))

    def test_default_expansion_covers_cigarette_below_hanging_hand(self):
        # 现场样例：掌心在约 y=587，香烟伸到画面底部 y=742。
        frame = np.zeros((743, 2295, 3), dtype=np.uint8)
        for person_box, palm_box in (
            ((1040, 0, 1710, 743), [148.13, 535.54, 251.44, 638.84]),
            ((1100, 0, 1770, 743), [84, 535, 187, 638]),
        ):
            with self.subTest(person_box=person_box):
                self.hands.detections = [{"bbox": palm_box, "score": 0.85}]
                person = ObjectDetection(0, "person", 0.95, person_box)
                detector = self.pipeline(
                    crop_expand=0.12, hand_crop_expand=CONFIG["hand_crop_expand"]
                )
                detector.detect(frame, [person])
                x1, y1, x2, y2 = detector.last_regions[0]["bbox"]
                self.assertLessEqual(x1, 1170)
                self.assertLessEqual(y1, 670)
                self.assertGreaterEqual(x2, 1235)
                self.assertGreater(y2, 742)
                self.assertLessEqual(y2, frame.shape[0])

    def test_worker_uses_snapshot_frame_skips_duplicates_and_preserves_alarm_frame_id(self):
        self.faces.detections = [face()]
        pipeline = self.pipeline()
        shared = ObjectDetectionWorker(object(), detector=FakeDetector([(0, 0.95, 100, 100, 300, 500)]))
        sender = mock.Mock()
        worker = SmokingWorker(object(), sender, shared, detector=pipeline)
        worker._last_alarm_time = -worker._cooldown
        shared.process_frame(self.frame, 123.0, 42)
        result = worker.process(np.zeros_like(self.frame))
        self.assertIs(result["frame"], self.frame)
        self.assertEqual(result["timestamp"], 123.0)
        worker.alert(worker.name, result)
        self.assertEqual(sender.send_detection.call_args.args[3], 42)
        self.assertIsNone(worker.process(self.frame))
        self.assertEqual(len(self.faces.frames), 1)
        shared.process_frame(self.frame, 124.0, 43)
        self.assertIsNone(worker.process(self.frame))  # cooldown
        shared.process_frame(self.frame, 125.0, 44)
        shared._snapshot_published_at = time.monotonic() - 3
        self.assertIsNone(worker.process(self.frame))  # expired
        self.assertEqual(len(self.faces.frames), 2)


class SmokingManagerTest(unittest.TestCase):
    def test_forwards_independent_smoking_config(self):
        provider = mock.Mock()
        provider.get_all.return_value = {"smoking": {"hand_model_path": "/models/palm.onnx"}}
        manager = DetectionManager(object(), object(), object(), provider)
        manager._object_detection_worker = shared = object()
        with mock.patch("detection.smoking.smoking_thread.SmokingWorker") as worker_class:
            manager._init_smoking()
        self.assertIs(worker_class.call_args.kwargs["object_detection_worker"], shared)
        self.assertEqual(worker_class.call_args.kwargs["config"], {"hand_model_path": "/models/palm.onnx"})


if __name__ == "__main__":
    unittest.main()
