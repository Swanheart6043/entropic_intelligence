import sys
import threading
import time
import unittest
from pathlib import Path


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.detection_worker import DetectionWorker
from command.command_controller import CommandController


class _FrameHub:
    def __init__(self):
        self._frame_id = 0

    def get_frame_with_info(self):
        self._frame_id += 1
        return object(), time.time(), self._frame_id


class _CountingWorker(DetectionWorker):
    def __init__(self):
        super().__init__("CountingWorker", _FrameHub(), interval=0.01)
        self.count = 0
        self.processed = threading.Event()

    def process(self, frame):
        self.count += 1
        self.processed.set()


class DetectionWorkerControlTest(unittest.TestCase):
    def test_disable_pauses_inference_without_stopping_thread(self):
        worker = _CountingWorker()
        worker.disable()
        self.assertTrue(worker.start())

        time.sleep(0.05)
        self.assertEqual(worker.count, 0)
        self.assertTrue(worker.is_running)
        self.assertFalse(worker.is_enabled)

        self.assertTrue(worker.enable())
        self.assertTrue(worker.processed.wait(timeout=1.0))

        self.assertTrue(worker.disable())
        time.sleep(0.03)
        paused_count = worker.count
        time.sleep(0.05)
        self.assertEqual(worker.count, paused_count)
        self.assertTrue(worker.is_running)

        self.assertTrue(worker.stop())
        self.assertFalse(worker.is_running)


class DynamicControlCommandTest(unittest.TestCase):
    def setUp(self):
        self.controller = CommandController.__new__(CommandController)
        self.controller._worker_lock = threading.RLock()
        self.worker = _CountingWorker()
        self.worker.start()
        self.controller._workers_by_name = {"human_intrusion": self.worker}

    def tearDown(self):
        self.worker.stop()

    def test_command_is_idempotent(self):
        reply = self.controller._handle_detection_dynamic_control({
            "type": "human_intrusion",
            "switch": "off",
        })
        self.assertEqual(reply["state"], "disabled")
        self.assertTrue(reply["changed"])

        reply = self.controller._handle_detection_dynamic_control({
            "type": "human_intrusion",
            "switch": "off",
        })
        self.assertFalse(reply["changed"])

        reply = self.controller._handle_detection_dynamic_control({
            "type": "human_intrusion",
            "switch": "on",
        })
        self.assertEqual(reply["state"], "enabled")
        self.assertTrue(reply["changed"])

    def test_unknown_worker_returns_error(self):
        reply = self.controller._handle_detection_dynamic_control({
            "type": "unknown",
            "switch": "on",
        })
        self.assertEqual(reply["error"], "worker_not_found")

    def test_internal_object_detection_is_not_a_control_target(self):
        object_worker = _CountingWorker()
        object_worker.start()
        try:
            reply = self.controller._handle_detection_dynamic_control({
                "type": "object_detection",
                "switch": "off",
            })
            self.assertEqual(reply["error"], "worker_not_found")
            self.assertTrue(object_worker.is_enabled)
        finally:
            object_worker.stop()

    def test_penalty_area_worker_can_be_turned_off_and_on(self):
        self.controller._workers_by_name = {
            "breaking_into_penalty_area": self.worker,
        }

        reply = self.controller._handle_detection_dynamic_control({
            "type": "breaking_into_penalty_area",
            "switch": "off",
        })
        self.assertEqual(reply["state"], "disabled")

        reply = self.controller._handle_detection_dynamic_control({
            "type": "breaking_into_penalty_area",
            "switch": "on",
        })
        self.assertEqual(reply["state"], "enabled")


if __name__ == "__main__":
    unittest.main()
