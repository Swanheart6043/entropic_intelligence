#!/usr/bin/env python3
"""
FrameHub - 共享 RTSP 拉流, 为多个检测线程提供最新帧
设计:
  - 底层使用 HWDecodeCapture 硬件解码 (CPU≈0)
  - 存储 NV12 原始帧, BGR 按需懒转换
  - 只保留最新一帧, 各检测线程按自己的节奏取帧处理
  - 线程安全, 无帧拷贝(只读引用)
"""
import threading
import time
import logging
import cv2
import numpy as np
from common.hw_decode_capture import HWDecodeCapture

logger = logging.getLogger("FrameHub")

class FrameHub:
    """
    共享 RTSP 帧分发器 (硬件解码版)
    用法:
        hub = FrameHub("rtsp://...")
        hub.start()

        frame = hub.get_frame() # BGR, 兼容所有 Worker
        nv12  = hub.get_nv12() # NV12, 给 BPU 直推
        frame, ts, fid = hub.get_frame_with_info() # BGR + 时间戳 + 帧ID

        hub.stop()
    """
    def __init__(self, rtsp_url: str, dec_chn: int = 0):
        self._rtsp_url = rtsp_url
        self._dec_chn = dec_chn
        self._cap: HWDecodeCapture | None = None

        # 帧数据
        self._nv12: bytes | None = None
        self._bgr: np.ndarray | None = None
        self._bgr_dirty: bool = True
        self._frame_ts: float = 0.0
        self._frame_id: int = 0

        # 线程控制
        self._lock = threading.Lock()
        self._running = False
        self._thread: threading.Thread | None = None

    # ----------------------------------------------------------
    #  属性
    # ----------------------------------------------------------
    @property
    def width(self) -> int:
        return self._cap.width if self._cap else 0

    @property
    def height(self) -> int:
        return self._cap.height if self._cap else 0

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def frame_id(self) -> int:
        with self._lock:
            return self._frame_id

    # ----------------------------------------------------------
    #  启动 / 停止
    # ----------------------------------------------------------
    def start(self):
        if self._running:
            return

        # 初始化硬件解码采集 (内部自动启动 demux + 解码线程)
        self._cap = HWDecodeCapture(self._rtsp_url, dec_chn=self._dec_chn)

        self._running = True
        self._thread = threading.Thread(
            target=self._pull_loop, daemon=True, name="FrameHub-pull"
        )
        self._thread.start()
        logger.info(f"started: {self.width}x{self.height} (硬件解码)")

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None
        if self._cap:
            self._cap.release()
            self._cap = None
        logger.info("stopped")

    # ----------------------------------------------------------
    #  取帧接口
    # ----------------------------------------------------------
    def get_frame(self) -> np.ndarray | None:
        """获取最新帧 BGR (兼容所有现有 Worker, 懒转换)"""
        with self._lock:
            if self._nv12 is None:
                return None
            if self._bgr_dirty:
                self._bgr = cv2.cvtColor(
                    np.frombuffer(self._nv12, dtype=np.uint8).reshape(
                        self.height * 3 // 2, self.width
                    ),
                    cv2.COLOR_YUV2BGR_NV12,
                )
                self._bgr_dirty = False
            return self._bgr

    def get_nv12(self) -> bytes | None:
        """获取最新帧 NV12 原始数据 (给 BPU 直推的 Worker 用)"""
        with self._lock:
            return self._nv12

    def get_frame_with_info(self) -> tuple[np.ndarray | None, float, int]:
        """获取最新帧 BGR + 时间戳 + 帧ID"""
        with self._lock:
            if self._nv12 is None:
                return None, 0.0, 0
            if self._bgr_dirty:
                self._bgr = cv2.cvtColor(
                    np.frombuffer(self._nv12, dtype=np.uint8).reshape(
                        self.height * 3 // 2, self.width
                    ),
                    cv2.COLOR_YUV2BGR_NV12,
                )
                self._bgr_dirty = False
            return self._bgr, self._frame_ts, self._frame_id

    # ----------------------------------------------------------
    #  拉帧循环
    # ----------------------------------------------------------
    def _pull_loop(self):
        consecutive_failures = 0

        while self._running:
            ret, nv12 = self._cap.read()
            if not ret or nv12 is None:
                consecutive_failures += 1
                if consecutive_failures > 50:
                    logger.error("连续 50 次读取失败, 等待...")
                    consecutive_failures = 0
                    time.sleep(1)
                else:
                    time.sleep(0.01)
                continue

            consecutive_failures = 0
            with self._lock:
                self._nv12 = nv12
                self._bgr_dirty = True  # BGR 缓存失效, 下次 get_frame 时再转
                self._frame_ts = time.time()
                self._frame_id += 1