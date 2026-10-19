import os
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from common.result_image_store import ResultImageStore


class ResultImageStoreTest(unittest.TestCase):
    def test_creates_directory_and_saves_jpeg(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            result_dir = Path(temp_dir) / "missing" / "result"
            store = ResultImageStore(result_dir)

            saved_path = store.save_jpeg(
                b"jpeg-data",
                detection_type="smoking",
                frame_id=42,
            )

            self.assertEqual(saved_path.read_bytes(), b"jpeg-data")
            self.assertEqual(saved_path.parent, result_dir)
            self.assertIn("smoking_42", saved_path.name)

    def test_deletes_oldest_image_when_limit_is_exceeded(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            store = ResultImageStore(temp_dir, max_images=3)
            saved_paths = []
            for index in range(5):
                path = store.save_jpeg(b"jpeg", frame_id=index)
                os.utime(path, ns=(index + 1, index + 1))
                saved_paths.append(path)

            self.assertEqual(len(list(Path(temp_dir).glob("*.jpg"))), 3)
            self.assertFalse(saved_paths[0].exists())
            self.assertFalse(saved_paths[1].exists())
            self.assertTrue(all(path.exists() for path in saved_paths[2:]))

    def test_concurrent_saves_still_honor_the_limit(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            store = ResultImageStore(temp_dir, max_images=30)
            with ThreadPoolExecutor(max_workers=8) as executor:
                list(executor.map(
                    lambda frame_id: store.save_jpeg(
                        b"jpeg",
                        detection_type="alarm",
                        frame_id=frame_id,
                    ),
                    range(50),
                ))

            self.assertEqual(len(list(Path(temp_dir).glob("*.jpg"))), 30)


if __name__ == "__main__":
    unittest.main()
