from typing import Dict, List, Optional, Sequence, Tuple
import base64
import logging
import threading
import time
import cv2
import numpy as np
from .thermal_detector import config as THERMAL_DETECTOR_CONFIG
from .thermal_detector.thermal_detector import ThermalDetector
from .thermal_detector.thermal_thread import CONFIG as THERMAL_WORKER_CONFIG
from .fire_detector.detector import (AlarmEvent, CLS_FIRE, CLS_SMOKE, DEFAULT_CLASS_NAMES, FireDetector)

logger = logging.getLogger("FireOverheatWorker")

RISK_CONFIRMED_FIRE = "confirmed_fire"
RISK_SMOKE_WITH_HEAT = "smoke_with_heat"
RISK_VISUAL_FIRE = "visual_fire"
RISK_SMOKE_WARNING = "smoke_warning"
RISK_THERMAL = "thermal_risk"
RISK_LEVELS = {
    RISK_CONFIRMED_FIRE: "critical",
    RISK_SMOKE_WITH_HEAT: "high",
    RISK_VISUAL_FIRE: "high",
    RISK_SMOKE_WARNING: "medium",
    RISK_THERMAL: "medium",
}
RISK_LABELS = {
    RISK_CONFIRMED_FIRE: "CONFIRMED FIRE",
    RISK_SMOKE_WITH_HEAT: "SMOKE WITH HEAT",
    RISK_VISUAL_FIRE: "VISUAL FIRE",
    RISK_SMOKE_WARNING: "SMOKE WARNING",
    RISK_THERMAL: "THERMAL RISK",
}
CONFIG = {
    "detect_interval": 1.0,
    "fire_model_path": "/home/heyn/model/fire_rdk.bin",
    "fire_score_thres": 0.81,
    "fire_nms_thres": 0.45,
    "fire_confirm_frames": 2,
    "thermal_confirm_frames": int(THERMAL_WORKER_CONFIG["confirm_frames"]),
    "max_frame_skew": 0.50,
    "max_frame_age": 2.0,
    "visual_event_hold": 3.0,
    "thermal_box_expand": 0.15,
    "match_iou_thres": 0.05,
    "composite_max_width": 1280,
    "composite_max_height": 720,
    "jpeg_quality": 80,
    "thermal_to_rgb_homography": None,
    "cooldowns": {
        RISK_CONFIRMED_FIRE: 10.0,
        RISK_SMOKE_WITH_HEAT: 10.0,
        RISK_VISUAL_FIRE: 10.0,
        RISK_SMOKE_WARNING: 15.0,
        RISK_THERMAL: float(THERMAL_WORKER_CONFIG["alarm_cooldown"]),
    },
}

class FireOverheatWorker:
    def __init__(self, rgb_hub, thermal_hub, zmq_sender):
        self.name = "FireOverheatWorker"
        self.rgb_hub = rgb_hub
        self.thermal_hub = thermal_hub
        self.zmq_sender = zmq_sender
        self.cfg = dict(CONFIG)
        self.interval = self.cfg["detect_interval"]
        self.fire_detector = FireDetector(
            model_path=self.cfg["fire_model_path"],
            classes_num=len(DEFAULT_CLASS_NAMES),
            score_thres=self.cfg["fire_score_thres"],
            nms_thres=self.cfg["fire_nms_thres"],
            confirm_frames=self.cfg["fire_confirm_frames"],
        )
        self.thermal_detector = ThermalDetector()
        homography = self.cfg["thermal_to_rgb_homography"]
        self._homography: Optional[np.ndarray] = None
        if homography is not None:
            matrix = np.asarray(homography, dtype=np.float32)
            if matrix.shape != (3, 3):
                raise ValueError("thermal_to_rgb_homography must be a 3x3 matrix")
            self._homography = matrix
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._enabled_event = threading.Event()
        self._enabled_event.set()
        self._stats_count = 0
        self._stats_total_ms = 0.0
        self._sequence = 0
        self._last_rgb_frame_id = -1
        self._last_thermal_frame_id = -1
        self._latest_rgb_frame = None
        self._latest_rgb_ts = 0.0
        self._latest_rgb_frame_id = 0
        self._latest_rgb_detections = []
        self._latest_visual_events: List[AlarmEvent] = []
        self._latest_visual_event_ts = 0.0
        self._latest_thermal_frame = None
        self._latest_thermal_ts = 0.0
        self._latest_thermal_frame_id = 0
        self._latest_thermal_result = {"ok": False, "reason": "not processed", "detections": []}
        self._thermal_hit_streak = 0
        self._last_alarm_times: Dict[str, float] = {}

        if (thermal_hub.width and (thermal_hub.width != THERMAL_DETECTOR_CONFIG.FRAME_W or thermal_hub.height != THERMAL_DETECTOR_CONFIG.FRAME_H)):
            logger.warning(
                "热成像 hub 尺寸 %dx%d 与标定尺寸 %dx%d 不一致",
                thermal_hub.width,
                thermal_hub.height,
                THERMAL_DETECTOR_CONFIG.FRAME_W,
                THERMAL_DETECTOR_CONFIG.FRAME_H,
            )
        logger.info(
            "融合参数: fire_score=%.2f, fire_confirm=%d, thermal_confirm=%d, "
            "max_skew=%.2fs, spatial=%s",
            self.cfg["fire_score_thres"],
            self.cfg["fire_confirm_frames"],
            self.cfg["thermal_confirm_frames"],
            self.cfg["max_frame_skew"],
            "homography" if self._homography is not None else "temporal_only",
        )

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def is_enabled(self) -> bool:
        return self._enabled_event.is_set()

    def enable(self) -> bool:
        changed = not self._enabled_event.is_set()
        self._enabled_event.set()
        if changed:
            logger.info("inference enabled")
        return changed

    def disable(self) -> bool:
        changed = self._enabled_event.is_set()
        self._enabled_event.clear()
        if changed:
            logger.info("inference disabled")
        return changed

    def _warmup_lut(self, timeout: float = 10.0) -> bool:
        """Build the thermal LUT inside this worker thread."""
        started_at = time.time()
        last_reason = "无热成像帧"
        logger.info("热成像 LUT 后台预热开始")

        while self._running and time.time() - started_at < timeout:
            frame, _, _ = self.thermal_hub.get_frame_with_info()
            if frame is None:
                time.sleep(0.2)
                continue
            try:
                result = self.thermal_detector.prepare_lut(frame)
            except Exception as exc:
                last_reason = str(exc)
                logger.warning("热成像 LUT 预热异常，将重试: %s", exc)
                time.sleep(0.2)
                continue
            if result.get("ok"):
                logger.info(
                    "热成像 LUT %s: range=[%.1f,%.1f], elapsed=%.1fs",
                    "建立完成" if result.get("rebuilt") else "已就绪",
                    result["t_min"],
                    result["t_max"],
                    time.time() - started_at,
                )
                return True
            last_reason = result.get("reason", "unknown")
            time.sleep(0.2)

        logger.warning("热成像 LUT 后台预热未完成: %s；将在检测时重试", last_reason)
        return False

    def start(self):
        if self._running:
            return False
        if self._thread is not None and self._thread.is_alive():
            logger.error("cannot start: previous thread is still exiting")
            return False
        self._stop_event.clear()
        self._running = True
        self._thread = threading.Thread(target=self._work_loop, daemon=True, name=self.name)
        self._thread.start()
        logger.info("started, interval=%.1fs", self.interval)
        return True

    def stop(self):
        self._running = False
        self._stop_event.set()
        self._enabled_event.set()
        if self._thread:
            self._thread.join(timeout=5)
            if self._thread.is_alive():
                logger.error("stop timed out: worker thread is still alive")
                return False
            self._thread = None
        self._enabled_event.clear()
        average = self._stats_total_ms / max(1, self._stats_count)
        logger.info("stopped, processed %d cycles, avg=%.0fms", self._stats_count, average)
        return True

    def _work_loop(self):
        self._warmup_lut()

        while not self._stop_event.is_set():
            self._enabled_event.wait()
            if self._stop_event.is_set():
                break

            rgb_frame, rgb_ts, rgb_fid = self.rgb_hub.get_frame_with_info()
            thermal_frame, thermal_ts, thermal_fid = (
                self.thermal_hub.get_frame_with_info()
            )

            rgb_new = rgb_frame is not None and rgb_fid != self._last_rgb_frame_id
            thermal_new = (
                thermal_frame is not None
                and thermal_fid != self._last_thermal_frame_id
            )
            if not rgb_new and not thermal_new:
                self._stop_event.wait(0.05)
                continue

            if rgb_new:
                self._last_rgb_frame_id = rgb_fid
            if thermal_new:
                self._last_thermal_frame_id = thermal_fid

            started_at = time.time()
            try:
                result = self.process_pair(
                    rgb_frame,
                    rgb_ts,
                    rgb_fid,
                    rgb_new,
                    thermal_frame,
                    thermal_ts,
                    thermal_fid,
                    thermal_new,
                )
                if result is not None:
                    self.alert(result)
            except Exception as exc:
                logger.error("融合处理异常: %s", exc, exc_info=True)

            elapsed_ms = (time.time() - started_at) * 1000
            self._stats_count += 1
            self._stats_total_ms += elapsed_ms
            self._stop_event.wait(self.interval)

    def process_pair(self, rgb_frame, rgb_ts: float, rgb_fid: int, rgb_new: bool, thermal_frame, thermal_ts: float, thermal_fid: int, thermal_new: bool):
        now = time.time()
        if rgb_frame is not None:
            self._latest_rgb_frame = rgb_frame
            self._latest_rgb_ts = rgb_ts
            self._latest_rgb_frame_id = rgb_fid
        if thermal_frame is not None:
            self._latest_thermal_frame = thermal_frame
            self._latest_thermal_ts = thermal_ts
            self._latest_thermal_frame_id = thermal_fid

        if rgb_new:
            try:
                detections, events = self.fire_detector.detect(
                    rgb_frame, rgb_fid
                )
                self._latest_rgb_detections = detections
                if events:
                    self._latest_visual_events = events
                    self._latest_visual_event_ts = rgb_ts or now
                elif not detections:
                    self._latest_visual_events = []
                    self._latest_visual_event_ts = 0.0
            except Exception as exc:
                logger.error("可见光火焰检测异常: %s", exc, exc_info=True)
                self._latest_rgb_detections = []

        if thermal_new:
            try:
                thermal_result = self.thermal_detector.detect(thermal_frame)
            except Exception as exc:
                logger.error("热成像高温检测异常: %s", exc, exc_info=True)
                thermal_result = {
                    "ok": False,
                    "reason": str(exc),
                    "detections": [],
                }
            self._latest_thermal_result = thermal_result
            if thermal_result.get("ok") and thermal_result.get("detections"):
                self._thermal_hit_streak = min(
                    self._thermal_hit_streak + 1,
                    self.cfg["thermal_confirm_frames"],
                )
            else:
                self._thermal_hit_streak = 0

        if (
            self._latest_visual_events
            and now - self._latest_visual_event_ts
            > self.cfg["visual_event_hold"]
        ):
            self._latest_visual_events = []

        rgb_fresh = (
            self._latest_rgb_frame is not None
            and now - self._latest_rgb_ts <= self.cfg["max_frame_age"]
        )
        thermal_fresh = (
            self._latest_thermal_frame is not None
            and now - self._latest_thermal_ts <= self.cfg["max_frame_age"]
            and self._latest_thermal_result.get("ok", False)
        )
        frame_skew = (
            abs(self._latest_rgb_ts - self._latest_thermal_ts)
            if rgb_fresh and thermal_fresh
            else None
        )
        synchronized = (
            frame_skew is not None
            and frame_skew <= self.cfg["max_frame_skew"]
        )

        thermal_detections = (
            self._latest_thermal_result.get("detections", [])
            if thermal_fresh
            else []
        )
        thermal_confirmed = (
            bool(thermal_detections)
            and self._thermal_hit_streak
            >= self.cfg["thermal_confirm_frames"]
        )
        visual_events = (
            list(self._latest_visual_events) if rgb_fresh else []
        )

        risk_type, secondary_risks, match_mode = self._select_risk(
            visual_events,
            thermal_detections,
            thermal_confirmed,
            synchronized,
        )
        if risk_type is None:
            return None

        cooldown = self.cfg["cooldowns"][risk_type]
        if now - self._last_alarm_times.get(risk_type, 0.0) < cooldown:
            return None
        self._last_alarm_times[risk_type] = now
        self._sequence += 1

        max_temp = (
            max(det.max_temp for det in thermal_detections)
            if thermal_detections
            else None
        )
        composite = self._compose_image(risk_type)
        rgb_alarm_detections = [
            (
                event.class_id,
                event.conf,
                event.bbox[0],
                event.bbox[1],
                event.bbox[2],
                event.bbox[3],
            )
            for event in visual_events
        ]

        result = {
            "type": "fire_overheat_fusion",
            "risk_type": risk_type,
            "risk_level": RISK_LEVELS[risk_type],
            "secondary_risks": secondary_risks,
            "frame": composite,
            "frame_id": self._sequence,
            "rgb_detections": rgb_alarm_detections,
            "rgb_raw_detections": list(self._latest_rgb_detections),
            "thermal_targets": self._thermal_targets(thermal_detections),
            "max_temperature": max_temp,
            "frame_max_temperature": self._latest_thermal_result.get("frame_max_temp"),
            "temperature_range": self._temperature_range(),
            "rgb_frame_id": self._latest_rgb_frame_id,
            "thermal_frame_id": self._latest_thermal_frame_id,
            "time_delta_ms": (round(frame_skew * 1000, 1) if frame_skew is not None else None),
            "rgb_available": rgb_fresh,
            "thermal_available": thermal_fresh,
            "match_mode": match_mode,
            "synchronized": synchronized,
            "thermal_confirmed": thermal_confirmed,
            "timestamp": now,
        }
        self._log_alarm(result)
        return result

    def _select_risk(self, visual_events: Sequence[AlarmEvent], thermal_detections, thermal_confirmed: bool, synchronized: bool) -> Tuple[Optional[str], List[str], str]:
        fire_events = [event for event in visual_events if event.class_id == CLS_FIRE]
        smoke_events = [event for event in visual_events if event.class_id == CLS_SMOKE]
        match_mode = ("homography" if self._homography is not None else "temporal_only")
        fire_heat = (thermal_confirmed and synchronized and self._has_matching_heat(fire_events, thermal_detections))
        smoke_heat = (thermal_confirmed and synchronized and self._has_matching_heat(smoke_events, thermal_detections))

        secondary = []
        if fire_heat:
            risk = RISK_CONFIRMED_FIRE
        elif smoke_heat:
            risk = RISK_SMOKE_WITH_HEAT
        elif fire_events:
            risk = RISK_VISUAL_FIRE
        elif smoke_events:
            risk = RISK_SMOKE_WARNING
        elif thermal_confirmed:
            risk = RISK_THERMAL
        else:
            return None, [], match_mode
        if (thermal_confirmed and risk not in (RISK_CONFIRMED_FIRE, RISK_SMOKE_WITH_HEAT, RISK_THERMAL)):
            secondary.append(RISK_THERMAL)
        return risk, secondary, match_mode

    def _has_matching_heat(self, visual_events: Sequence[AlarmEvent], thermal_detections) -> bool:
        if not visual_events or not thermal_detections:
            return False
        if self._homography is None:
            return True

        rgb_h, rgb_w = self._latest_rgb_frame.shape[:2]
        mapped_boxes = [
            self._map_thermal_bbox(det.bbox, rgb_w, rgb_h)
            for det in thermal_detections
        ]
        for event in visual_events:
            for thermal_box in mapped_boxes:
                if self._boxes_match(event.bbox, thermal_box):
                    return True
        return False

    def _map_thermal_bbox(self, bbox: Tuple[int, int, int, int], rgb_w: int, rgb_h: int) -> List[int]:
        x, y, width, height = bbox
        corners = np.array(
            [
                [x, y],
                [x + width, y],
                [x + width, y + height],
                [x, y + height],
            ],
            dtype=np.float32,
        ).reshape(-1, 1, 2)
        mapped = cv2.perspectiveTransform(corners, self._homography).reshape(-1, 2)
        x1, y1 = mapped.min(axis=0)
        x2, y2 = mapped.max(axis=0)

        expand_x = (x2 - x1) * self.cfg["thermal_box_expand"]
        expand_y = (y2 - y1) * self.cfg["thermal_box_expand"]
        return [
            int(np.clip(x1 - expand_x, 0, rgb_w)),
            int(np.clip(y1 - expand_y, 0, rgb_h)),
            int(np.clip(x2 + expand_x, 0, rgb_w)),
            int(np.clip(y2 + expand_y, 0, rgb_h)),
        ]

    def _boxes_match(self, visual_box, thermal_box) -> bool:
        vx1, vy1, vx2, vy2 = visual_box
        tx1, ty1, tx2, ty2 = thermal_box
        ix1, iy1 = max(vx1, tx1), max(vy1, ty1)
        ix2, iy2 = min(vx2, tx2), min(vy2, ty2)
        intersection = max(0, ix2 - ix1) * max(0, iy2 - iy1)
        visual_area = max(1, (vx2 - vx1) * (vy2 - vy1))
        thermal_area = max(1, (tx2 - tx1) * (ty2 - ty1))
        union = visual_area + thermal_area - intersection
        iou = intersection / max(1, union)
        visual_center = ((vx1 + vx2) / 2, (vy1 + vy2) / 2)
        thermal_center = ((tx1 + tx2) / 2, (ty1 + ty2) / 2)
        center_inside = (
            tx1 <= visual_center[0] <= tx2
            and ty1 <= visual_center[1] <= ty2
        ) or (
            vx1 <= thermal_center[0] <= vx2
            and vy1 <= thermal_center[1] <= vy2
        )
        return iou >= self.cfg["match_iou_thres"] or center_inside

    @staticmethod
    def _thermal_targets(detections) -> List[dict]:
        targets = []
        for detection in detections:
            x, y, width, height = detection.bbox
            targets.append(
                {
                    "max_temperature": round(float(detection.max_temp), 1),
                    "area": int(detection.area),
                    "centroid": [
                        round(float(detection.centroid[0]), 1),
                        round(float(detection.centroid[1]), 1),
                    ],
                    "bbox": {
                        "x1": int(x),
                        "y1": int(y),
                        "x2": int(x + width),
                        "y2": int(y + height),
                        "width": int(width),
                        "height": int(height),
                    },
                }
            )
        return targets

    def _temperature_range(self):
        t_min = self._latest_thermal_result.get("t_min")
        t_max = self._latest_thermal_result.get("t_max")
        if t_min is None or t_max is None:
            return None
        return [round(float(t_min), 1), round(float(t_max), 1)]

    def _draw_rgb(self):
        if self._latest_rgb_frame is None:
            return self._placeholder("RGB UNAVAILABLE")
        image = self._latest_rgb_frame.copy()
        for class_id, score, x1, y1, x2, y2 in self._latest_rgb_detections:
            color = (0, 0, 255) if class_id == CLS_FIRE else (0, 180, 255)
            cv2.rectangle(image, (x1, y1), (x2, y2), color, 3)
            class_name = (
                DEFAULT_CLASS_NAMES[class_id]
                if 0 <= class_id < len(DEFAULT_CLASS_NAMES)
                else str(class_id)
            )
            label = f"{class_name}: {score:.2f}"
            cv2.putText(
                image,
                label,
                (x1, max(24, y1 - 8)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                color,
                2,
            )
        return image

    def _draw_thermal(self):
        if self._latest_thermal_frame is None:
            return self._placeholder("THERMAL UNAVAILABLE")
        return self.thermal_detector.draw(
            self._latest_thermal_frame,
            self._latest_thermal_result,
        )

    @staticmethod
    def _placeholder(text: str) -> np.ndarray:
        image = np.zeros((480, 640, 3), dtype=np.uint8)
        cv2.putText(image, text, (40, 240), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (180, 180, 180), 2)
        return image

    def _compose_image(self, risk_type: str) -> np.ndarray:
        rgb_image = self._draw_rgb()
        thermal_image = self._draw_thermal()

        rgb_ratio = rgb_image.shape[1] / float(rgb_image.shape[0])
        thermal_ratio = thermal_image.shape[1] / float(thermal_image.shape[0])
        separator_width = 4
        target_height = int((self.cfg["composite_max_width"] - separator_width) / (rgb_ratio + thermal_ratio))
        target_height = max(240, min(target_height, self.cfg["composite_max_height"]))
        rgb_width = int(target_height * rgb_ratio)
        thermal_width = int(target_height * thermal_ratio)

        rgb_image = cv2.resize(rgb_image, (rgb_width, target_height), interpolation=cv2.INTER_AREA)
        thermal_image = cv2.resize(thermal_image, (thermal_width, target_height), interpolation=cv2.INTER_AREA)
        separator = np.full((target_height, separator_width, 3), 220, dtype=np.uint8)
        if risk_type == RISK_THERMAL:
            return np.hstack((thermal_image, separator, rgb_image))
        return np.hstack((rgb_image, separator, thermal_image))

    def _log_alarm(self, result: dict):
        logger.warning(
            "%s level=%s, visual=%d, thermal=%d, max_temp=%s, dt=%sms, "
            "mode=%s",
            RISK_LABELS[result["risk_type"]],
            result["risk_level"],
            len(result["rgb_detections"]),
            len(result["thermal_targets"]),
            (f'{result["max_temperature"]:.1f}C' if result["max_temperature"] is not None else "N/A"),
            result["time_delta_ms"],
            result["match_mode"],
        )

    def alert(self, result: dict):
        try:
            metadata={
                "secondary_risks": result["secondary_risks"],
                "max_temperature": result["max_temperature"],
                "frame_max_temperature": result["frame_max_temperature"],
                "temperature_range": result["temperature_range"],
                "rgb_frame_id": result["rgb_frame_id"],
                "thermal_frame_id": result["thermal_frame_id"],
                "time_delta_ms": result["time_delta_ms"],
                "rgb_available": result["rgb_available"],
                "thermal_available": result["thermal_available"],
                "match_mode": result["match_mode"],
                "synchronized": result["synchronized"],
                "thermal_confirmed": result["thermal_confirmed"],
            }
            detections = []
            for class_id, score, x1, y1, x2, y2 in result["rgb_detections"]:
                class_name = (
                    DEFAULT_CLASS_NAMES[class_id]
                    if 0 <= class_id < len(DEFAULT_CLASS_NAMES)
                    else f"class_{class_id}"
                )
                detections.append(
                    {
                        "class_id": int(class_id),
                        "class_name": class_name,
                        "confidence": round(float(score), 2),
                        "coordinate_space": "rgb_source",
                        "bbox": {
                            "x1": int(x1),
                            "y1": int(y1),
                            "x2": int(x2),
                            "y2": int(y2),
                            "width": int(x2 - x1),
                            "height": int(y2 - y1),
                        },
                    }
                )

            if not detections and result["risk_type"] == RISK_THERMAL:
                for target in result["thermal_targets"]:
                    bbox = target["bbox"]
                    detections.append(
                        {
                            "class_id": 0,
                            "class_name": "OverTemp",
                            # Preserve the legacy thermal payload convention.
                            "confidence": round(
                                float(target["max_temperature"]), 2
                            ),
                            "coordinate_space": "thermal_source",
                            "bbox": {
                                "x1": int(bbox["x1"]),
                                "y1": int(bbox["y1"]),
                                "x2": int(bbox["x2"]),
                                "y2": int(bbox["y2"]),
                                "width": int(bbox["width"]),
                                "height": int(bbox["height"]),
                            },
                        }
                    )

            encode_ok, img_encoded = cv2.imencode(".jpg", result["frame"], [cv2.IMWRITE_JPEG_QUALITY, int(self.cfg["jpeg_quality"])])
            if not encode_ok:
                logger.error("Fusion alert JPEG encoding failed")
                return False

            confidences = [item["confidence"] for item in detections]
            detection_type = (
                "thermal_overtemp"
                if result["risk_type"] == RISK_THERMAL
                else "fire"
            )
            message = {
                "type": "detection_result",
                "detection_type": detection_type,
                "frame_id": int(result["frame_id"]),
                "timestamp": time.time(),
                "image_width": int(result["frame"].shape[1]),
                "image_height": int(result["frame"].shape[0]),
                "detections": detections,
                "detection_count": len(detections),
                "avg_confidence": (
                    round(sum(confidences) / len(confidences), 2)
                    if confidences
                    else 0.0
                ),
                "max_confidence": max(confidences) if confidences else 0.0,
                "all_confidences": (",".join(f"{value:.2f}" for value in confidences) if confidences else ""),
                "image_base64": base64.b64encode(img_encoded.tobytes()).decode("utf-8"),
                "fusion": {
                    "enabled": True,
                    "risk_type": result["risk_type"],
                    "risk_level": result["risk_level"],
                    "thermal_targets": result["thermal_targets"],
                    "thermal_target_count": len(result["thermal_targets"]),
                    **metadata,
                },
            }
            sent = self.zmq_sender.send_detection_message(
                message,
                img_encoded.tobytes(),
            )
            if sent:
                logger.info(
                    "已发送火焰/过热告警: frame_id=%s, risk=%s, rgb=%d, thermal=%d",
                    result["frame_id"],
                    result["risk_type"],
                    len(result["rgb_detections"]),
                    len(result["thermal_targets"]),
                )
            return sent
        except Exception as exc:
            logger.error("ZMQ火焰/过热告警失败: %s", exc, exc_info=True)
            return False
