"""基于 urllib 的轻量同步 HTTP 客户端。"""

from __future__ import annotations

import json
import ssl
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Mapping
from urllib.parse import urlsplit


class HttpClientError(RuntimeError):
    """HTTP 请求失败，保留请求上下文便于业务层记录日志。"""

    def __init__(
        self,
        message: str,
        *,
        method: str,
        url: str,
        status: int | None = None,
        response_body: bytes = b"",
    ):
        super().__init__(message)
        self.method = method
        self.url = url
        self.status = status
        self.response_body = response_body


@dataclass(frozen=True)
class HttpResponse:
    status: int
    headers: Mapping[str, str]
    body: bytes

    def text(self, encoding: str = "utf-8") -> str:
        return self.body.decode(encoding)

    def json(self) -> Any:
        return json.loads(self.text())


class HttpClient:
    """缓存基础地址和 TLS 策略的同步 HTTP 客户端。

    配置只在构造时读取。运行中修改配置文件不会改变该实例；重启进程并
    重新创建实例后才会生效。
    """

    def __init__(
        self,
        base_url: str = "",
        *,
        timeout: float = 10.0,
        verify_tls: bool = True,
        user_agent: str = "PatrolRobot/1.0",
        opener=None,
    ):
        self._base_url = self._normalize_base_url(base_url)
        self._timeout = self._normalize_timeout(timeout)
        self._verify_tls = bool(verify_tls)
        self._user_agent = user_agent
        self._opener = opener or self._build_opener()

    @property
    def base_url(self) -> str:
        return self._base_url

    @property
    def configured(self) -> bool:
        return bool(self._base_url)

    @property
    def verify_tls(self) -> bool:
        return self._verify_tls

    def build_url(self, path_or_url: str) -> str:
        target = str(path_or_url or "").strip()
        parsed = urlsplit(target)
        if parsed.scheme:
            self._validate_url(target)
            return target
        if not self._base_url:
            raise HttpClientError(
                "HTTP base_url 未配置",
                method="",
                url=target,
            )
        return f"{self._base_url}/{target.lstrip('/')}"

    def request(
        self,
        method: str,
        path_or_url: str,
        *,
        data: bytes | None = None,
        headers: Mapping[str, str] | None = None,
        timeout: float | None = None,
    ) -> HttpResponse:
        request_method = method.upper()
        url = self.build_url(path_or_url)
        request_headers = {"User-Agent": self._user_agent}
        if headers:
            request_headers.update(headers)
        request = urllib.request.Request(
            url=url,
            data=data,
            headers=request_headers,
            method=request_method,
        )
        request_timeout = (
            self._timeout
            if timeout is None
            else self._normalize_timeout(timeout)
        )

        try:
            with self._opener.open(request, timeout=request_timeout) as response:
                return HttpResponse(
                    status=response.getcode(),
                    headers=dict(response.headers.items()),
                    body=response.read(),
                )
        except urllib.error.HTTPError as exc:
            try:
                response_body = exc.read()
            except Exception:
                response_body = b""
            raise HttpClientError(
                f"HTTP {exc.code}: {exc.reason}",
                method=request_method,
                url=url,
                status=exc.code,
                response_body=response_body,
            ) from exc
        except urllib.error.URLError as exc:
            raise HttpClientError(
                f"HTTP 请求失败: {exc.reason}",
                method=request_method,
                url=url,
            ) from exc
        except (TimeoutError, OSError) as exc:
            raise HttpClientError(
                f"HTTP 请求失败: {exc}",
                method=request_method,
                url=url,
            ) from exc

    def get(
        self,
        path_or_url: str,
        *,
        headers: Mapping[str, str] | None = None,
        timeout: float | None = None,
    ) -> HttpResponse:
        return self.request(
            "GET",
            path_or_url,
            headers=headers,
            timeout=timeout,
        )

    def post(
        self,
        path_or_url: str,
        *,
        data: bytes | None = None,
        headers: Mapping[str, str] | None = None,
        timeout: float | None = None,
    ) -> HttpResponse:
        return self.request(
            "POST",
            path_or_url,
            data=data,
            headers=headers,
            timeout=timeout,
        )

    def _build_opener(self):
        if self._verify_tls:
            ssl_context = ssl.create_default_context()
        else:
            ssl_context = ssl._create_unverified_context()
        return urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ssl_context),
        )

    @staticmethod
    def _normalize_base_url(base_url: str) -> str:
        normalized = str(base_url or "").strip().rstrip("/")
        if normalized:
            HttpClient._validate_url(normalized)
        return normalized

    @staticmethod
    def _validate_url(url: str) -> None:
        parsed = urlsplit(url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise ValueError(f"仅支持完整的 HTTP/HTTPS URL: {url!r}")

    @staticmethod
    def _normalize_timeout(timeout: float) -> float:
        value = float(timeout)
        if value <= 0:
            raise ValueError("HTTP timeout 必须大于 0")
        return value
