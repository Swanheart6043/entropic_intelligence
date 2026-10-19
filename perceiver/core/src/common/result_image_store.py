"""ZMQ 告警图片的本地兜底存储。"""

from __future__ import annotations

import logging
import os
import re
import threading
import uuid
from datetime import datetime
from pathlib import Path

from .paths import RESULT_DIR


logger = logging.getLogger("ResultImageStore")


class ResultImageStore:
    """将告警 JPEG 保存在本地，并仅保留最新的有限数量。"""

    def __init__(self, directory: str | Path = RESULT_DIR, max_images: int = 30):
        if max_images < 1:
            raise ValueError("max_images must be at least 1")
        self.directory = Path(directory)
        self.max_images = max_images
        self._lock = threading.Lock()

    def save_jpeg(
        self,
        jpeg_bytes: bytes,
        *,
        detection_type: str = "",
        frame_id: int = 0,
    ) -> Path:
        """原子写入一张 JPEG，并删除超出上限的最旧图片。"""
        if not jpeg_bytes:
            raise ValueError("jpeg_bytes must not be empty")

        with self._lock:
            self.directory.mkdir(parents=True, exist_ok=True)
            output_path = self.directory / self._build_filename(
                detection_type,
                frame_id,
            )
            temp_path = self.directory / (
                f".{output_path.name}.{uuid.uuid4().hex}.tmp"
            )
            try:
                temp_path.write_bytes(jpeg_bytes)
                os.replace(temp_path, output_path)
            finally:
                temp_path.unlink(missing_ok=True)

            self._prune_oldest()
            return output_path

    @staticmethod
    def _build_filename(detection_type: str, frame_id: int) -> str:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        safe_type = re.sub(r"[^A-Za-z0-9_-]+", "_", detection_type).strip("_")
        safe_type = safe_type[:48] or "detection"
        suffix = uuid.uuid4().hex[:8]
        return f"{timestamp}_{safe_type}_{int(frame_id)}_{suffix}.jpg"

    def _prune_oldest(self) -> None:
        images = sorted(
            self.directory.glob("*.jpg"),
            key=lambda path: (path.stat().st_mtime_ns, path.name),
        )
        for old_path in images[: max(0, len(images) - self.max_images)]:
            try:
                old_path.unlink()
            except OSError as exc:
                logger.warning("删除旧告警图片失败: %s: %s", old_path, exc)
