import sys
import unittest
from pathlib import Path
from unittest import mock

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from common.zmq_sender import ZMQSender


def _sender_without_connection(result_store=None):
    sender = ZMQSender.__new__(ZMQSender)
    sender.socket = mock.Mock()
    sender.result_store = (
        result_store if result_store is not None else mock.Mock()
    )
    return sender


class ZMQSenderResultStorageTest(unittest.TestCase):
    def test_send_detection_saves_the_same_encoded_jpeg_before_sending(self):
        result_store = mock.Mock()
        sender = _sender_without_connection(result_store)

        sent = sender.send_detection(
            np.zeros((24, 32, 3), dtype=np.uint8),
            [(0, 0.9, 2, 3, 12, 14)],
            ["person"],
            frame_id=7,
            detection_type="test_alarm",
        )

        self.assertTrue(sent)
        jpeg_bytes = result_store.save_jpeg.call_args.args[0]
        self.assertTrue(jpeg_bytes.startswith(b"\xff\xd8"))
        self.assertEqual(
            result_store.save_jpeg.call_args.kwargs,
            {"detection_type": "test_alarm", "frame_id": 7},
        )
        sender.socket.send_string.assert_called_once()

    def test_storage_failure_does_not_block_zmq_send(self):
        result_store = mock.Mock()
        result_store.save_jpeg.side_effect = OSError("disk full")
        sender = _sender_without_connection(result_store)

        with self.assertLogs("RDK_YOLO", level="ERROR"):
            sent = sender.send_detection_message(
                {
                    "type": "detection_result",
                    "detection_type": "fire",
                    "frame_id": 9,
                },
                b"jpeg",
            )

        self.assertTrue(sent)
        sender.socket.send_string.assert_called_once()

    def test_zmq_failure_happens_after_the_image_is_saved(self):
        events = []
        result_store = mock.Mock()

        def save_image(*args, **kwargs):
            events.append("save")
            return Path("alarm.jpg")

        def fail_to_send(*args, **kwargs):
            events.append("send")
            raise OSError("offline")

        result_store.save_jpeg.side_effect = save_image
        sender = _sender_without_connection(result_store)
        sender.socket.send_string.side_effect = fail_to_send

        with self.assertLogs("RDK_YOLO", level="ERROR"):
            sent = sender.send_detection_message(
                {
                    "type": "detection_result",
                    "detection_type": "fire",
                    "frame_id": 10,
                },
                b"jpeg",
            )

        self.assertFalse(sent)
        self.assertEqual(events, ["save", "send"])


if __name__ == "__main__":
    unittest.main()
