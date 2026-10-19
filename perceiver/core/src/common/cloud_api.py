"""云端接口适配层。

检测业务只依赖这里的稳定方法，云端路径、multipart 细节和返回结构集中
在本模块适配。
"""

from __future__ import annotations

import uuid
from typing import Mapping

from .http_client import HttpClient


class CloudApi:
    DEFAULT_ENDPOINTS = {
        "upload_image": "/robotboot/device/device/uploadImage",
        "report_face": "/robotboot/face/comparefaceById",
    }
    DEFAULT_TIMEOUTS = {
        "default": 10.0,
        "upload_image": 10.0,
        "report_face": 5.0,
        "download_image": 10.0,
    }

    def __init__(
        self,
        api_http: HttpClient,
        image_http: HttpClient,
        *,
        endpoints: Mapping[str, str] | None = None,
        timeouts: Mapping[str, float] | None = None,
    ):
        self._api_http = api_http
        self._image_http = image_http
        self._endpoints = dict(self.DEFAULT_ENDPOINTS)
        if endpoints:
            self._endpoints.update(endpoints)
        self._timeouts = dict(self.DEFAULT_TIMEOUTS)
        if timeouts:
            self._timeouts.update({key: float(value) for key, value in timeouts.items()})

    @classmethod
    def from_config(cls, config: Mapping) -> "CloudApi":
        """从配置快照创建实例，后续不再读取原配置对象。"""
        cloud = config.get("cloud", {})
        if not isinstance(cloud, Mapping):
            cloud = {}

        base_url = str(
            cloud.get("base_url")
            or config.get("web_http_url")
            or ""
        )
        image_base_url = str(
            cloud.get("image_base_url")
            or config.get("web_img_url")
            or base_url
        )
        verify_tls = cloud.get("verify_tls", True)
        image_verify_tls = cloud.get("image_verify_tls", verify_tls)
        timeouts = cloud.get("timeouts", {})
        if not isinstance(timeouts, Mapping):
            timeouts = {}
        default_timeout = float(
            timeouts.get("default", cls.DEFAULT_TIMEOUTS["default"])
        )
        endpoints = cloud.get("endpoints", {})
        if not isinstance(endpoints, Mapping):
            endpoints = {}

        return cls(
            HttpClient(
                base_url,
                timeout=default_timeout,
                verify_tls=verify_tls,
            ),
            HttpClient(
                image_base_url,
                timeout=default_timeout,
                verify_tls=image_verify_tls,
            ),
            endpoints=dict(endpoints),
            timeouts=dict(timeouts),
        )

    @property
    def configured(self) -> bool:
        return self._api_http.configured

    @property
    def api_base_url(self) -> str:
        return self._api_http.base_url

    @property
    def image_base_url(self) -> str:
        return self._image_http.base_url

    def upload_image(
        self,
        image: bytes,
        *,
        filename: str,
        image_type: str,
        sn: str,
    ) -> str:
        body, content_type = self._build_multipart(
            fields={"type": image_type, "sn": sn},
            files={"file": (filename, image, "image/jpeg")},
        )
        response = self._api_http.post(
            self._endpoint("upload_image"),
            data=body,
            headers={"Content-Type": content_type},
            timeout=self._timeout("upload_image"),
        )
        payload = response.json()
        try:
            pic_url = payload["result"]["picUrl"]
        except (KeyError, TypeError) as exc:
            raise ValueError(f"云端上传响应缺少 result.picUrl: {payload!r}") from exc
        if not isinstance(pic_url, str):
            raise ValueError(f"云端 picUrl 类型错误: {type(pic_url).__name__}")
        return pic_url

    def report_face(self, fields: Mapping[str, str]) -> str:
        body, content_type = self._build_multipart(fields=fields)
        response = self._api_http.post(
            self._endpoint("report_face"),
            data=body,
            headers={"Content-Type": content_type},
            timeout=self._timeout("report_face"),
        )
        return response.text()

    def upload_plate(
        self,
        *,
        plate_image: bytes,
        background_image: bytes,
        fields: Mapping[str, str],
    ):
        body, content_type = self._build_multipart(
            fields=fields,
            files={
                "file": ("plate.jpg", plate_image, "image/jpeg"),
                "plateBg": ("background.jpg", background_image, "image/jpeg"),
            },
        )
        response = self._api_http.post(
            self._endpoint("upload_image"),
            data=body,
            headers={"Content-Type": content_type},
            timeout=self._timeout("upload_image"),
        )
        return response.json()

    def download_image(self, path_or_url: str) -> bytes:
        response = self._image_http.get(
            path_or_url,
            timeout=self._timeout("download_image"),
        )
        return response.body

    def _endpoint(self, name: str) -> str:
        endpoint = str(self._endpoints.get(name, "")).strip()
        if not endpoint:
            raise ValueError(f"云端接口路径未配置: {name}")
        return endpoint

    def _timeout(self, name: str) -> float:
        return float(self._timeouts.get(name, self._timeouts["default"]))

    @staticmethod
    def _build_multipart(
        *,
        fields: Mapping[str, object],
        files: Mapping[str, tuple[str, bytes, str]] | None = None,
    ) -> tuple[bytes, str]:
        boundary = f"----PatrolRobot{uuid.uuid4().hex}"
        parts: list[bytes] = []

        for name, value in fields.items():
            parts.append(
                (
                    f"--{boundary}\r\n"
                    f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                    f"{value}\r\n"
                ).encode("utf-8")
            )
        for name, (filename, content, content_type) in (files or {}).items():
            parts.append(
                (
                    f"--{boundary}\r\n"
                    f'Content-Disposition: form-data; name="{name}"; '
                    f'filename="{filename}"\r\n'
                    f"Content-Type: {content_type}\r\n\r\n"
                ).encode("utf-8")
                + content
                + b"\r\n"
            )
        parts.append(f"--{boundary}--\r\n".encode("ascii"))
        return b"".join(parts), f"multipart/form-data; boundary={boundary}"
