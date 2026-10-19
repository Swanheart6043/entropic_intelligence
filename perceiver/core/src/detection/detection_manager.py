"""检测 Worker 的装配、注册与生命周期管理。"""
import logging
import threading
from collections.abc import Iterable

logger = logging.getLogger("DetectionManager")

class DetectionManager:
    """根据配置创建检测 Worker，并统一管理其生命周期。"""
    _OBJECT_DETECTION_DEPENDENTS = frozenset({
        "smoking",
        "safety_helmet",
        "no_safety_helmet",
        "reflective_vest",
        "no_reflective_vest",
        "water",
        "breaking_into_penalty_area",
        "cats_and_dogs",
    })
    _INITIALIZERS = (
        ("face", "_init_face", "人脸"),
        ("plate", "_init_plate", "车牌"),
        ("smoking", "_init_smoking", "吸烟"),
        ("fire_high_temperature", "_init_fire_high_temperature", "火焰/过热"),
        ("safety_helmet", "_init_safety_helmet", "安全帽"),
        ("no_safety_helmet", "_init_no_safety_helmet", "未佩戴安全帽"),
        ("reflective_vest", "_init_reflective_vest", "反光衣"),
        ("no_reflective_vest", "_init_no_reflective_vest", "未穿反光衣"),
        ("door", "_init_door", "门状态"),
        ("water", "_init_water", "地面积水"),
        ("breaking_into_penalty_area", "_init_breaking_into_penalty_area", "禁区闯入"),
        ("cats_and_dogs", "_init_cats_and_dogs", "猫狗"),
        ("rubbish", "_init_rubbish", "垃圾"),
        ("body", "_init_body", "人体二合一")
    )

    def __init__(
        self,
        high_definition_hub,
        infrared_thermal_hub,
        zmq_sender,
        config_provider,
        cloud_api=None,
        robot_api=None,
    ):
        self.high_definition_hub = high_definition_hub
        self.infrared_thermal_hub = infrared_thermal_hub
        self.zmq_sender = zmq_sender
        self.config_provider = config_provider
        self.cloud_api = cloud_api
        self.robot_api = robot_api

        self._workers: list[object] = []
        self._worker_map: dict[str, object] = {}
        self._workers_by_name: dict[str, object] = {}
        self._registered_names: set[str] = set()
        self._object_detection_worker = None
        self._worker_lock = threading.RLock()

    @property
    def worker_map(self) -> dict[str, object]:
        """供命令处理器访问需要对外暴露的 Worker。"""
        return self._worker_map

    @property
    def workers_by_name(self) -> dict[str, object]:
        """供动态启停命令按检测类型查询 Worker。"""
        return self._workers_by_name

    @property
    def worker_lock(self):
        return self._worker_lock

    @property
    def object_detection_worker(self):
        return self._object_detection_worker

    def __len__(self) -> int:
        with self._worker_lock:
            return len(self._workers)

    def workers_snapshot(self) -> list[object]:
        with self._worker_lock:
            return list(self._workers)

    def initialize(self, enabled: Iterable[str]) -> int:
        """初始化启用的检测器；单个检测器失败不影响其他检测器。"""
        enabled_set = set(enabled)

        with self._worker_lock:
            if self._workers:
                raise RuntimeError("detection workers have already been initialized")

        if enabled_set & self._OBJECT_DETECTION_DEPENDENTS:
            try:
                self._object_detection_worker = self._init_object_detection()
                self._register_worker("object_detection", self._object_detection_worker, expose=False, controllable=False)
                logger.info("✅ 通用目标检测线程初始化完成")
            except Exception as exc:
                self._object_detection_worker = None
                logger.error("❌ 通用目标检测线程初始化失败: %s", exc, exc_info=True)
        for name, initializer_name, display_name in self._INITIALIZERS:
            if name not in enabled_set:
                continue
            try:
                worker = getattr(self, initializer_name)()
                self._register_worker(name, worker)
                logger.info("✅ %s检测线程初始化完成", display_name)
            except Exception as exc:
                logger.error("❌ %s检测线程初始化失败: %s", display_name, exc, exc_info=True)
        return len(self)

    def start_all(self) -> bool:
        """启动全部 Worker；失败时按相反顺序回滚已启动项。"""
        started_workers = []
        try:
            for worker in self.workers_snapshot():
                if worker.start() is False:
                    name = getattr(worker, "name", worker.__class__.__name__)
                    raise RuntimeError(f"worker refused to start: {name}")
                started_workers.append(worker)
        except Exception as exc:
            logger.error("检测线程启动异常: %s", exc, exc_info=True)
            for worker in reversed(started_workers):
                try:
                    worker.stop()
                except Exception:
                    logger.exception("回滚停止检测线程失败")
            return False
        return True

    def stop_all(self) -> None:
        """停止全部 Worker，并清空注册表以释放模型资源。"""
        for worker in reversed(self.workers_snapshot()):
            try:
                worker.stop()
            except Exception:
                logger.exception("停止检测线程失败: %s", getattr(worker, "name", repr(worker)))
        with self._worker_lock:
            self._workers.clear()
            self._workers_by_name.clear()
            self._worker_map.clear()
            self._registered_names.clear()
            self._object_detection_worker = None

    def _register_worker(self, name: str, worker: object, expose: bool = True, controllable: bool = True) -> None:
        with self._worker_lock:
            if name in self._registered_names:
                raise ValueError(f"duplicate detection worker: {name}")
            self._registered_names.add(name)
            self._workers.append(worker)
            if controllable:
                self._workers_by_name[name] = worker
            if expose and controllable:
                self._worker_map[name] = worker

    def _require_object_detection_worker(self):
        if self._object_detection_worker is None:
            raise RuntimeError("shared ObjectDetectionWorker is not initialized")
        return self._object_detection_worker

    def _init_object_detection(self):
        from .object_detection.object_detection_worker import ObjectDetectionWorker
        return ObjectDetectionWorker(self.high_definition_hub)

    # 人相关检测任务
    def _init_smoking(self):
        from .smoking.smoking_thread import SmokingWorker
        config = {}
        get_all = getattr(self.config_provider, "get_all", None)
        if callable(get_all):
            configured = get_all().get("smoking", {})
            if isinstance(configured, dict):
                config = configured
        return SmokingWorker(
            self.high_definition_hub, zmq_sender=self.zmq_sender,
            object_detection_worker=self._require_object_detection_worker(), config=config,
        )

    def _init_face(self):
        from common.cloud_api import CloudApi
        from common.robot_api import RobotApi
        from .face.face_thread import FaceWorker
        config = self.config_provider.get_all()
        cloud_api = self.cloud_api or CloudApi.from_config(config)
        robot_api = self.robot_api or RobotApi.from_config(config)
        return FaceWorker(
            self.high_definition_hub,
            basic_info=self.config_provider,
            cloud_api=cloud_api,
            robot_api=robot_api,
        )

    def _init_safety_helmet(self):
        from .safety_helmet.safety_helmet_thread import SafetyHelmetWorker
        return SafetyHelmetWorker(self.high_definition_hub, zmq_sender=self.zmq_sender, object_detection_worker=self._require_object_detection_worker())

    def _init_no_safety_helmet(self):
        from .no_safety_helmet.safety_helmet_thread import SafetyHelmetWorker
        return SafetyHelmetWorker(self.high_definition_hub, zmq_sender=self.zmq_sender, object_detection_worker=self._require_object_detection_worker())

    def _init_reflective_vest(self):
        from .reflective_vest.reflective_vest_thread import ReflectiveVestWorker
        return ReflectiveVestWorker(self.high_definition_hub, zmq_sender=self.zmq_sender, object_detection_worker=self._require_object_detection_worker())

    def _init_no_reflective_vest(self):
        from .no_reflective_vest.reflective_vest_thread import ReflectiveVestWorker
        return ReflectiveVestWorker(self.high_definition_hub, zmq_sender=self.zmq_sender, object_detection_worker=self._require_object_detection_worker())

    # 车相关检测任务
    def _init_plate(self):
        from common.cloud_api import CloudApi
        from .plate.plate_thread import PlateWorker
        cloud_api = self.cloud_api or CloudApi.from_config(
            self.config_provider.get_all()
        )
        return PlateWorker(
            self.high_definition_hub,
            basic_info=self.config_provider,
            cloud_api=cloud_api,
        )

    # 热成像相关检测任务
    def _init_fire_high_temperature(self):
        from .fire_high_temperature.fire_overheat_thread import FireOverheatWorker
        return FireOverheatWorker(rgb_hub=self.high_definition_hub, thermal_hub=self.infrared_thermal_hub, zmq_sender=self.zmq_sender)

    # 其他检测任务
    def _init_door(self):
        from .door.door_thread import DoorDetectionWorker
        return DoorDetectionWorker(self.high_definition_hub, zmq_sender=self.zmq_sender)

    def _init_water(self):
        from .water.water_thread import WaterWorker
        return WaterWorker(self.high_definition_hub, self.zmq_sender, self._require_object_detection_worker())

    def _init_breaking_into_penalty_area(self):
        from .breaking_into_penalty_area import BreakingIntoPenaltyAreaWorker

        config = {}
        get_all = getattr(self.config_provider, "get_all", None)
        if callable(get_all):
            configured = get_all().get("breaking_into_penalty_area", {})
            if isinstance(configured, dict):
                config = configured
        return BreakingIntoPenaltyAreaWorker(
            self.high_definition_hub,
            self.zmq_sender,
            self._require_object_detection_worker(),
            config=config,
        )

    def _init_cats_and_dogs(self):
        from .cats_and_dogs import CatsAndDogsWorker

        config = {}
        get_all = getattr(self.config_provider, "get_all", None)
        if callable(get_all):
            configured = get_all().get("cats_and_dogs", {})
            if isinstance(configured, dict):
                config = configured
        return CatsAndDogsWorker(
            self.high_definition_hub,
            self.zmq_sender,
            self._require_object_detection_worker(),
            config=config,
        )

    def _init_rubbish(self):
        from .rubbish.rubbish_thread import RubbishWorker
        return RubbishWorker(self.high_definition_hub, zmq_sender=self.zmq_sender)

    def _init_body(self):
        from .body.body_thread import BodyWorker
        return BodyWorker(self.high_definition_hub, zmq_sender=self.zmq_sender)
