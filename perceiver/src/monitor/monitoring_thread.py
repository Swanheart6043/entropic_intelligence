"""服务运行状态、内存和检测 Worker 统计监控。"""
import gc
import logging
import os
import threading
import time
from typing import TYPE_CHECKING
import psutil
if TYPE_CHECKING:
    from detection.detection_manager import DetectionManager

logger = logging.getLogger("MonitoringThread")

class MonitoringThread:
    """定期执行 GC、检查内存并输出检测 Worker 心跳。"""
    def __init__(
        self,
        detection_manager: "DetectionManager",
        heartbeat_interval: float = 300.0,
        gc_interval: float = 300.0,
        memory_warning_mb: float = 500.0,
        poll_interval: float = 1.0,
        join_timeout: float = 5.0,
    ):
        positive_values = {
            "heartbeat_interval": heartbeat_interval,
            "gc_interval": gc_interval,
            "memory_warning_mb": memory_warning_mb,
            "poll_interval": poll_interval,
            "join_timeout": join_timeout,
        }
        for name, value in positive_values.items():
            if value <= 0:
                raise ValueError(f"{name} must be greater than zero")

        self._detection_manager = detection_manager
        self._heartbeat_interval = heartbeat_interval
        self._gc_interval = gc_interval
        self._memory_warning_mb = memory_warning_mb
        self._poll_interval = poll_interval
        self._join_timeout = join_timeout

        self._stop_event = threading.Event()
        self._lifecycle_lock = threading.RLock()
        self._thread: threading.Thread | None = None
        self._started_at: float | None = None
        self._stopped_at: float | None = None

    @property
    def is_running(self) -> bool:
        with self._lifecycle_lock:
            return self._thread is not None and self._thread.is_alive()

    @property
    def uptime_seconds(self) -> float:
        with self._lifecycle_lock:
            if self._started_at is None:
                return 0.0
            end = self._stopped_at or time.monotonic()
            return max(0.0, end - self._started_at)

    def start(self) -> bool:
        with self._lifecycle_lock:
            if self._thread is not None and self._thread.is_alive():
                return False
            self._stop_event.clear()
            self._started_at = time.monotonic()
            self._stopped_at = None
            thread = threading.Thread(
                target=self._run,
                daemon=True,
                name="MonitoringThread",
            )
            self._thread = thread
            try:
                thread.start()
            except Exception:
                self._thread = None
                self._started_at = None
                self._stopped_at = None
                self._stop_event.set()
                raise

        logger.info(
            "监控线程已启动: heartbeat=%.0fs, gc=%.0fs",
            self._heartbeat_interval,
            self._gc_interval,
        )
        return True

    def stop(self) -> bool:
        with self._lifecycle_lock:
            thread = self._thread
            if thread is None:
                return False
            self._stop_event.set()

        if thread is not threading.current_thread():
            thread.join(timeout=self._join_timeout)

        if thread.is_alive():
            logger.error("监控线程停止超时")
            return False

        with self._lifecycle_lock:
            if self._thread is thread:
                self._thread = None
            if self._stopped_at is None:
                self._stopped_at = time.monotonic()

        logger.info("监控线程已停止")
        return True

    def join(self, timeout: float | None = None) -> bool:
        """等待监控线程结束；返回线程是否已经退出。"""
        with self._lifecycle_lock:
            thread = self._thread
        if thread is None:
            return True
        if thread is threading.current_thread():
            return False
        thread.join(timeout=timeout)
        return not thread.is_alive()

    def _run(self) -> None:
        last_heartbeat = time.monotonic()
        last_gc = last_heartbeat

        try:
            while not self._stop_event.wait(self._poll_interval):
                now = time.monotonic()
                try:
                    if now - last_gc >= self._gc_interval:
                        self._collect_and_check_memory()
                        last_gc = now

                    if now - last_heartbeat >= self._heartbeat_interval:
                        self._print_heartbeat()
                        last_heartbeat = now
                except Exception:
                    logger.exception("监控任务执行异常")
        finally:
            with self._lifecycle_lock:
                self._stopped_at = time.monotonic()

    def _collect_and_check_memory(self) -> None:
        gc.collect()
        memory_mb = self._get_memory_mb()
        if memory_mb > self._memory_warning_mb:
            logger.warning(
                "内存使用过高: %.1fMB, 强制 GC",
                memory_mb,
            )
            gc.collect()

    @staticmethod
    def _get_memory_mb() -> float:
        try:
            return psutil.Process(os.getpid()).memory_info().rss / 1024 / 1024
        except Exception:
            return 0.0

    def _print_heartbeat(self) -> None:
        elapsed = self.uptime_seconds
        hours = int(elapsed // 3600)
        minutes = int((elapsed % 3600) // 60)
        memory_mb = self._get_memory_mb()

        worker_stats = []
        for worker in self._detection_manager.workers_snapshot():
            try:
                total_ms = float(getattr(worker, "_stats_total_ms", 0.0))
                count = max(0, int(getattr(worker, "_stats_count", 0)))
                name = getattr(worker, "name", worker.__class__.__name__)
                average_ms = total_ms / count if count else 0.0
                worker_stats.append(f"{name}: {count}帧/{average_ms:.0f}ms")
            except Exception:
                worker_stats.append(
                    f"{getattr(worker, 'name', worker.__class__.__name__)}: stat err"
                )

        message = f"[心跳] {hours}h{minutes}m | 内存: {memory_mb:.1f}MB"
        if worker_stats:
            message += " | " + " | ".join(worker_stats)
        logger.info(message)