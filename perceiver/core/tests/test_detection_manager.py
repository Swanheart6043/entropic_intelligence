import sys
import unittest
from pathlib import Path
from unittest import mock


SRC_ROOT = Path(__file__).resolve().parents[1] / "src"
if str(SRC_ROOT) not in sys.path:
    sys.path.insert(0, str(SRC_ROOT))

from detection.detection_manager import DetectionManager


class _Worker:
    def __init__(self, name: str, start_result=True):
        self.name = name
        self.start_result = start_result
        self.start_calls = 0
        self.stop_calls = 0

    def start(self):
        self.start_calls += 1
        return self.start_result

    def stop(self):
        self.stop_calls += 1
        return True


def _manager() -> DetectionManager:
    return DetectionManager(
        high_definition_hub=object(),
        infrared_thermal_hub=object(),
        zmq_sender=object(),
        config_provider=object(),
    )


class DetectionManagerInitializationTest(unittest.TestCase):
    def test_initializes_only_enabled_workers_in_dependency_order(self):
        manager = _manager()
        object_detection = _Worker("object_detection")
        face = _Worker("face")
        smoking = _Worker("smoking")
        water = _Worker("water")

        with (
            mock.patch.object(
                manager,
                "_init_object_detection",
                return_value=object_detection,
            ),
            mock.patch.object(manager, "_init_face", return_value=face),
            mock.patch.object(manager, "_init_smoking", return_value=smoking),
            mock.patch.object(manager, "_init_water", return_value=water),
        ):
            count = manager.initialize(["water", "face", "smoking"])

        self.assertEqual(count, 4)
        self.assertEqual(
            manager.workers_snapshot(),
            [object_detection, face, smoking, water],
        )
        self.assertEqual(
            list(manager.workers_by_name),
            ["face", "smoking", "water"],
        )
        self.assertNotIn("object_detection", manager.worker_map)
        self.assertIs(manager.object_detection_worker, object_detection)

    def test_initializer_failure_does_not_block_other_workers(self):
        manager = _manager()
        plate = _Worker("plate")

        with (
            mock.patch.object(
                manager,
                "_init_face",
                side_effect=RuntimeError("face unavailable"),
            ),
            mock.patch.object(manager, "_init_plate", return_value=plate),
            self.assertLogs("DetectionManager", level="ERROR"),
        ):
            count = manager.initialize(["face", "plate"])

        self.assertEqual(count, 1)
        self.assertEqual(manager.workers_by_name, {"plate": plate})

    def test_water_requires_object_detection_worker(self):
        manager = _manager()

        with self.assertRaisesRegex(RuntimeError, "ObjectDetectionWorker"):
            manager._init_water()

    def test_penalty_area_requires_object_detection_worker(self):
        manager = _manager()

        with self.assertRaisesRegex(RuntimeError, "ObjectDetectionWorker"):
            manager._init_breaking_into_penalty_area()

    def test_cats_and_dogs_requires_object_detection_worker(self):
        manager = _manager()

        with self.assertRaisesRegex(RuntimeError, "ObjectDetectionWorker"):
            manager._init_cats_and_dogs()

    def test_cats_and_dogs_is_a_shared_detection_consumer(self):
        manager = _manager()
        object_detection = _Worker("object_detection")
        cats_and_dogs = _Worker("cats_and_dogs")

        with (
            mock.patch.object(
                manager,
                "_init_object_detection",
                return_value=object_detection,
            ),
            mock.patch.object(
                manager,
                "_init_cats_and_dogs",
                return_value=cats_and_dogs,
            ),
        ):
            count = manager.initialize(["cats_and_dogs"])

        self.assertEqual(count, 2)
        self.assertEqual(
            manager.workers_snapshot(),
            [object_detection, cats_and_dogs],
        )
        self.assertIs(
            manager.workers_by_name["cats_and_dogs"],
            cats_and_dogs,
        )

    def test_penalty_area_initialization_forwards_config(self):
        manager = _manager()
        manager._object_detection_worker = object_detection = object()
        manager.config_provider = mock.Mock()
        manager.config_provider.get_all.return_value = {
            "breaking_into_penalty_area": {"side": "right"},
        }

        with mock.patch(
            "detection.breaking_into_penalty_area."
            "BreakingIntoPenaltyAreaWorker"
        ) as worker_class:
            manager._init_breaking_into_penalty_area()

        worker_class.assert_called_once_with(
            manager.high_definition_hub,
            manager.zmq_sender,
            object_detection,
            config={"side": "right"},
        )

    def test_penalty_area_is_registered_as_dynamic_control_target(self):
        manager = _manager()
        object_detection = _Worker("object_detection")
        penalty_area = _Worker("breaking_into_penalty_area")

        with (
            mock.patch.object(
                manager,
                "_init_object_detection",
                return_value=object_detection,
            ),
            mock.patch.object(
                manager,
                "_init_breaking_into_penalty_area",
                return_value=penalty_area,
            ),
        ):
            manager.initialize(["breaking_into_penalty_area"])

        self.assertIs(
            manager.workers_by_name["breaking_into_penalty_area"],
            penalty_area,
        )
        self.assertNotIn("object_detection", manager.workers_by_name)

    def test_negative_equipment_workers_are_independent_control_targets(self):
        manager = _manager()
        object_detection = _Worker("object_detection")
        no_safety_helmet = _Worker("no_safety_helmet")
        no_reflective_vest = _Worker("no_reflective_vest")

        with (
            mock.patch.object(
                manager,
                "_init_object_detection",
                return_value=object_detection,
            ),
            mock.patch.object(
                manager,
                "_init_no_safety_helmet",
                return_value=no_safety_helmet,
            ),
            mock.patch.object(
                manager,
                "_init_no_reflective_vest",
                return_value=no_reflective_vest,
            ),
        ):
            count = manager.initialize([
                "no_safety_helmet",
                "no_reflective_vest",
            ])

        self.assertEqual(count, 3)
        self.assertEqual(
            manager.workers_snapshot(),
            [object_detection, no_safety_helmet, no_reflective_vest],
        )
        self.assertEqual(
            list(manager.workers_by_name),
            ["no_safety_helmet", "no_reflective_vest"],
        )
        self.assertNotIn("object_detection", manager.worker_map)

    def test_unrelated_workers_do_not_initialize_object_detection(self):
        manager = _manager()
        face = _Worker("face")

        with (
            mock.patch.object(manager, "_init_face", return_value=face),
            mock.patch.object(manager, "_init_object_detection") as initializer,
        ):
            manager.initialize(["face"])

        initializer.assert_not_called()
        self.assertEqual(manager.workers_snapshot(), [face])


class DetectionManagerLifecycleTest(unittest.TestCase):
    def test_start_failure_rolls_back_started_workers(self):
        manager = _manager()
        first = _Worker("first")
        failing = _Worker("failing", start_result=False)
        untouched = _Worker("untouched")
        manager._register_worker("first", first)
        manager._register_worker("failing", failing)
        manager._register_worker("untouched", untouched)

        with self.assertLogs("DetectionManager", level="ERROR"):
            self.assertFalse(manager.start_all())

        self.assertEqual(first.stop_calls, 1)
        self.assertEqual(failing.start_calls, 1)
        self.assertEqual(untouched.start_calls, 0)

    def test_stop_runs_in_reverse_order_and_clears_registries(self):
        stop_order = []

        class OrderedWorker(_Worker):
            def stop(self):
                stop_order.append(self.name)
                return super().stop()

        manager = _manager()
        first = OrderedWorker("first")
        second = OrderedWorker("second")
        manager._register_worker("first", first)
        manager._register_worker("second", second)

        manager.stop_all()

        self.assertEqual(stop_order, ["second", "first"])
        self.assertEqual(manager.workers_snapshot(), [])
        self.assertEqual(manager.worker_map, {})
        self.assertEqual(manager.workers_by_name, {})


if __name__ == "__main__":
    unittest.main()
