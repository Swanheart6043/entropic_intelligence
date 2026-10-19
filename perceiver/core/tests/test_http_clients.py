import sys
import unittest
from pathlib import Path


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from common.cloud_api import CloudApi
from common.http_client import HttpClient
from common.robot_api import RobotApi


class _Response:
    def __init__(self, body=b"ok"):
        self._body = body
        self.headers = {"Content-Type": "text/plain"}

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def getcode(self):
        return 200

    def read(self):
        return self._body


class _Opener:
    def __init__(self):
        self.calls = []

    def open(self, request, timeout):
        self.calls.append((request, timeout))
        return _Response()


class HttpClientTest(unittest.TestCase):
    def test_uses_scheme_from_cached_base_url(self):
        http_opener = _Opener()
        https_opener = _Opener()
        http = HttpClient(
            "http://cloud.example.test:8080",
            opener=http_opener,
        )
        https = HttpClient(
            "https://cloud.example.test",
            opener=https_opener,
        )

        http.get("/robotboot/status")
        https.get("robotboot/status")

        self.assertEqual(
            http_opener.calls[0][0].full_url,
            "http://cloud.example.test:8080/robotboot/status",
        )
        self.assertEqual(
            https_opener.calls[0][0].full_url,
            "https://cloud.example.test/robotboot/status",
        )

    def test_rejects_non_http_protocol(self):
        with self.assertRaisesRegex(ValueError, "HTTP/HTTPS"):
            HttpClient("ftp://cloud.example.test")


class CachedApiConfigTest(unittest.TestCase):
    def test_cloud_api_keeps_startup_snapshot(self):
        config = {
            "cloud": {
                "base_url": "https://old.example.test",
                "image_base_url": "https://images-old.example.test",
            }
        }
        api = CloudApi.from_config(config)

        config["cloud"]["base_url"] = "http://new.example.test"
        config["cloud"]["image_base_url"] = "http://images-new.example.test"

        self.assertEqual(api.api_base_url, "https://old.example.test")
        self.assertEqual(
            api.image_base_url,
            "https://images-old.example.test",
        )

    def test_legacy_web_url_is_supported_when_new_base_is_empty(self):
        api = CloudApi.from_config({
            "web_http_url": "http://legacy.example.test:8080",
            "web_img_url": "https://images.example.test",
            "cloud": {"base_url": ""},
        })

        self.assertEqual(
            api.api_base_url,
            "http://legacy.example.test:8080",
        )
        self.assertEqual(api.image_base_url, "https://images.example.test")

    def test_robot_api_keeps_separate_internal_base_url(self):
        config = {
            "cloud": {"base_url": "https://cloud.example.test"},
            "robot": {"base_url": "http://10.10.10.10:8080"},
        }
        robot_api = RobotApi.from_config(config)

        config["robot"]["base_url"] = "http://10.10.10.20:8080"

        self.assertEqual(
            robot_api.base_url,
            "http://10.10.10.10:8080",
        )


if __name__ == "__main__":
    unittest.main()
