import sys
import threading
import time
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from command.face_commands import FaceCommands
from detection.face.components.engine import FaceEngine


class _Engine:
    def __init__(self, embedding=None):
        self.embedding = (
            np.ones(4, dtype=np.float32)
            if embedding is None
            else embedding
        )
        self.replacements = []

    def get_gallery_size(self):
        return 2

    def extract_embedding(self, _img):
        return self.embedding

    def replace_gallery(self, registrations, path):
        self.replacements.append((registrations, path))
        return len({item[0] for item in registrations})


class _FaceWorker:
    def __init__(self, engine):
        self.engine = engine
        self.cfg = {"gallery_path": "gallery.json"}


class FaceCommandsTest(unittest.TestCase):
    def setUp(self):
        self.engine = _Engine()
        self.commands = FaceCommands(
            {"face": _FaceWorker(self.engine)},
            mock.Mock(),
        )

    def tearDown(self):
        self.commands.close()

    def _wait_for_terminal_state(self, job_id: str) -> dict:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            status = self.commands.handle_sync_status({"job_id": job_id})
            if status["state"] in {"succeeded", "failed"}:
                return status
            time.sleep(0.01)
        self.fail("face sync job did not finish")

    def test_sync_returns_immediately_and_rejects_concurrent_job(self):
        release = threading.Event()

        def download(_url):
            release.wait(timeout=1)
            return np.zeros((2, 2, 3), dtype=np.uint8)

        self.commands._download_image = download
        started = time.monotonic()
        accepted = self.commands.handle_sync({
            "sn": "robot-1",
            "faces": [{"name": "Alice", "picUrl": "/alice.jpg", "id": 7}],
        })
        elapsed = time.monotonic() - started

        self.assertEqual(accepted["status"], "accepted")
        self.assertLess(elapsed, 0.2)
        busy = self.commands.handle_sync({
            "faces": [{"name": "Bob", "picUrl": "/bob.jpg"}],
        })
        self.assertEqual(busy["error"], "busy")

        release.set()
        status = self._wait_for_terminal_state(accepted["job_id"])
        self.assertEqual(status["state"], "succeeded")
        self.assertEqual(status["processed"], 1)
        self.assertEqual(status["success"], 1)
        registrations, path = self.engine.replacements[0]
        self.assertEqual(path, "gallery.json")
        self.assertEqual(registrations[0][0], "Alice")
        self.assertEqual(registrations[0][2], {"id": 7, "sn": "robot-1"})

    def test_failed_sync_keeps_existing_gallery(self):
        self.commands._download_image = mock.Mock(return_value=None)
        accepted = self.commands.handle_sync({
            "faces": [{"name": "Alice", "picUrl": "/missing.jpg"}],
        })

        status = self._wait_for_terminal_state(accepted["job_id"])

        self.assertEqual(status["state"], "failed")
        self.assertEqual(status["processed"], 1)
        self.assertEqual(status["success"], 0)
        self.assertEqual(len(status["failed"]), 1)
        self.assertEqual(self.engine.replacements, [])

    def test_downloads_one_bounded_batch_concurrently(self):
        active = 0
        max_active = 0
        lock = threading.Lock()
        all_started = threading.Event()
        release = threading.Event()

        def download(_url):
            nonlocal active, max_active
            with lock:
                active += 1
                max_active = max(max_active, active)
                if active == self.commands.DOWNLOAD_WORKERS:
                    all_started.set()
            release.wait(timeout=1)
            with lock:
                active -= 1
            return np.zeros((2, 2, 3), dtype=np.uint8)

        self.commands._download_image = download
        accepted = self.commands.handle_sync({
            "faces": [
                {"name": f"person-{index}", "picUrl": f"/{index}.jpg"}
                for index in range(self.commands.DOWNLOAD_WORKERS)
            ],
        })

        self.assertTrue(all_started.wait(timeout=1))
        release.set()
        status = self._wait_for_terminal_state(accepted["job_id"])
        self.assertEqual(status["state"], "succeeded")
        self.assertEqual(max_active, self.commands.DOWNLOAD_WORKERS)

    def test_status_requires_known_job_id(self):
        invalid = self.commands.handle_sync_status({})
        missing = self.commands.handle_sync_status({"job_id": "unknown"})

        self.assertEqual(invalid["error"], "invalid_params")
        self.assertEqual(missing["error"], "job_not_found")


class FaceEngineGalleryReplacementTest(unittest.TestCase):
    def setUp(self):
        self.engine = FaceEngine.__new__(FaceEngine)
        self.engine._gallery_lock = threading.RLock()
        self.engine._gallery_names = ["old"]
        self.engine._gallery_embeddings = np.array(
            [[1.0, 0.0]],
            dtype=np.float32,
        )
        self.engine._metadata = {"old": {"id": 1}}

    def test_replace_gallery_updates_disk_and_memory_together(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            gallery_path = str(Path(temp_dir) / "gallery.json")
            size = self.engine.replace_gallery(
                [
                    (
                        "new",
                        np.array([0.0, 1.0], dtype=np.float32),
                        {"id": 2},
                    ),
                ],
                gallery_path,
            )

            self.assertEqual(size, 1)
            self.assertEqual(self.engine.get_gallery_names(), ["new"])
            self.assertEqual(self.engine.get_metadata("new"), {"id": 2})
            self.assertTrue(Path(gallery_path).exists())

    def test_persistence_failure_keeps_old_in_memory_gallery(self):
        with mock.patch.object(
            self.engine,
            "_write_gallery",
            side_effect=OSError("disk full"),
        ):
            with self.assertRaisesRegex(OSError, "disk full"):
                self.engine.replace_gallery(
                    [
                        (
                            "new",
                            np.array([0.0, 1.0], dtype=np.float32),
                            {},
                        ),
                    ],
                    "gallery.json",
                )

        self.assertEqual(self.engine.get_gallery_names(), ["old"])
        self.assertEqual(self.engine.get_metadata("old"), {"id": 1})


if __name__ == "__main__":
    unittest.main()
