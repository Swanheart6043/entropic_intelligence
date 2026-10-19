import sys
import threading
import time
import unittest
from pathlib import Path
from unittest import mock


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from monitor.monitoring_thread import MonitoringThread
from patrol_robot_h1 import PatrolAIService


class _DetectionManager:
    def __init__(self, workers=None):
        self._workers = list(workers or [])

    def workers_snapshot(self):
        return list(self._workers)


class _Worker:
    def __init__(self, name, count, total_ms):
        self.name = name
        self._stats_count = count
        self._stats_total_ms = total_ms


class MonitoringThreadLifecycleTest(unittest.TestCase):
    def test_start_is_idempotent_and_stop_wakes_thread(self):
        monitor = MonitoringThread(
            _DetectionManager(),
            heartbeat_interval=0.02,
            gc_interval=10.0,
            poll_interval=0.005,
        )
        heartbeat = threading.Event()

        with mock.patch.object(
            monitor,
            "_print_heartbeat",
            side_effect=heartbeat.set,
        ):
            self.assertTrue(monitor.start())
            self.assertFalse(monitor.start())
            self.assertTrue(heartbeat.wait(timeout=1.0))
            self.assertTrue(monitor.is_running)
            self.assertTrue(monitor.stop())

        self.assertFalse(monitor.is_running)
        self.assertTrue(monitor.join(timeout=0.01))
        self.assertGreater(monitor.uptime_seconds, 0.0)

    def test_invalid_intervals_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "poll_interval"):
            MonitoringThread(_DetectionManager(), poll_interval=0)

    def test_start_failure_clears_lifecycle_state(self):
        monitor = MonitoringThread(_DetectionManager())

        with (
            mock.patch(
                "monitor.monitoring_thread.threading.Thread.start",
                side_effect=RuntimeError("thread unavailable"),
            ),
            self.assertRaisesRegex(RuntimeError, "thread unavailable"),
        ):
            monitor.start()

        self.assertFalse(monitor.is_running)
        self.assertEqual(monitor.uptime_seconds, 0.0)


class MonitoringThreadMetricsTest(unittest.TestCase):
    def test_heartbeat_reports_uptime_memory_and_worker_average(self):
        worker = _Worker("face", count=4, total_ms=100.0)
        monitor = MonitoringThread(_DetectionManager([worker]))
        monitor._started_at = time.monotonic() - 3665

        with (
            mock.patch.object(monitor, "_get_memory_mb", return_value=123.4),
            self.assertLogs("MonitoringThread", level="INFO") as logs,
        ):
            monitor._print_heartbeat()

        message = "\n".join(logs.output)
        self.assertIn("1h1m", message)
        self.assertIn("内存: 123.4MB", message)
        self.assertIn("face: 4帧/25ms", message)

    def test_high_memory_triggers_second_gc(self):
        monitor = MonitoringThread(
            _DetectionManager(),
            memory_warning_mb=500.0,
        )

        with (
            mock.patch(
                "monitor.monitoring_thread.gc.collect",
                return_value=0,
            ) as collect,
            mock.patch.object(monitor, "_get_memory_mb", return_value=600.0),
            self.assertLogs("MonitoringThread", level="WARNING"),
        ):
            monitor._collect_and_check_memory()

        self.assertEqual(collect.call_count, 2)


class MonitoringServiceIntegrationTest(unittest.TestCase):
    def test_service_stops_monitor_before_other_components(self):
        stop_order = []

        class Component:
            def __init__(self, name):
                self.name = name

            def stop(self):
                stop_order.append(self.name)

        class Monitor(Component):
            uptime_seconds = 10.0

        class DetectionManager(Component):
            def stop_all(self):
                stop_order.append(self.name)

        class Sender:
            def close(self):
                stop_order.append("zmq")

        service = PatrolAIService.__new__(PatrolAIService)
        service.monitoring_thread = Monitor("monitoring")
        service.command_controller = Component("command")
        service.detection_manager = DetectionManager("detection")
        service.high_definition_hub = Component("high_definition_hub")
        service.infrared_thermal_hub = Component("infrared_thermal_hub")
        service.zmq_sender = Sender()

        service.stop()

        self.assertEqual(
            stop_order,
            [
                "monitoring",
                "command",
                "detection",
                "high_definition_hub",
                "infrared_thermal_hub",
                "zmq",
            ],
        )


if __name__ == "__main__":
    unittest.main()
