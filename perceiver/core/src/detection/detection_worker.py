#!/usr/bin/env python3
"""
DetectionWorker 基类
每个 Worker 是独立线程, 按自己的间隔从 FrameHub 取最新帧处理
"""
import threading
import time
import logging
from abc import ABC, abstractmethod
from typing import Any, Callable
import numpy as np

logger = logging.getLogger("DetectionWorker")

class DetectionWorker(ABC):
    """检测工作线程基类"""
    def __init__(self, name: str, hub, interval: float = 2.0, callback: Callable[[str, Any], None] | None = None):
        """
        Parameters:
            name:     工作线程名称
            hub:      FrameHub 实例
            interval: 处理间隔(秒)
            callback: 结果回调 fn(worker_name, results)
        """
        self.name = name
        self.hub = hub
        self.interval = interval
        self.callback = callback

        self._running = False
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._enabled_event = threading.Event()
        self._enabled_event.set()
        self._last_frame_id: int = -1
        self._stats_count = 0
        self._stats_total_ms = 0.0
        self._logger = logging.getLogger(name)

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def is_enabled(self) -> bool:
        """是否允许执行推理；线程本身可以仍处于运行状态。"""
        return self._enabled_event.is_set()

    def enable(self) -> bool:
        """恢复推理，不重新创建线程或加载模型。"""
        changed = not self._enabled_event.is_set()
        self._enabled_event.set()
        if changed:
            self._logger.info("inference enabled")
        return changed

    def disable(self) -> bool:
        """暂停推理，保留线程和模型实例。"""
        changed = self._enabled_event.is_set()
        self._enabled_event.clear()
        if changed:
            self._logger.info("inference disabled")
        return changed

    def start(self):
        if self._running:
            return False
        if self._thread is not None and self._thread.is_alive():
            self._logger.error("cannot start: previous thread is still exiting")
            return False
        self._stop_event.clear()
        self._running = True
        self._thread = threading.Thread(
            target=self._work_loop, daemon=True, name=self.name
        )
        self._thread.start()
        self._logger.info(f"started, interval={self.interval}s")
        return True

    def stop(self):
        self._running = False
        self._stop_event.set()
        # 唤醒被暂停的线程，使其能够观察到退出请求。
        self._enabled_event.set()
        if self._thread:
            self._thread.join(timeout=5)
            if self._thread.is_alive():
                self._logger.error("stop timed out: worker thread is still alive")
                return False
            self._thread = None
        self._enabled_event.clear()
        avg = self._stats_total_ms / max(1, self._stats_count)
        self._logger.info(f"stopped, processed {self._stats_count} frames, avg={avg:.0f}ms")
        return True

    @abstractmethod
    def process(self, frame: np.ndarray) -> Any:
        """子类实现: 处理一帧, 返回结果 (None 表示无告警)"""
        ...

    def process_frame(self, frame: np.ndarray, timestamp: float, frame_id: int) -> Any:
        """带帧元数据的扩展入口；普通 Worker 默认仍调用 process。"""
        return self.process(frame)

    def _work_loop(self):
        while not self._stop_event.is_set():
            # 关闭检测时阻塞在这里，不取帧、不执行 process()。
            self._enabled_event.wait()
            if self._stop_event.is_set():
                break
            frame, ts, fid = self.hub.get_frame_with_info()
            if frame is None or fid == self._last_frame_id:
                self._stop_event.wait(0.05)
                continue
            self._last_frame_id = fid
            try:
                t0 = time.time()
                results = self.process_frame(frame, ts, fid)
                elapsed_ms = (time.time() - t0) * 1000

                self._stats_count += 1
                self._stats_total_ms += elapsed_ms

                if self.callback and results is not None:
                    self.callback(self.name, results)
            except Exception as e:
                self._logger.error(f"处理异常: {e}", exc_info=True)
            self._stop_event.wait(self.interval)
