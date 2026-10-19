import time
import cv2
import logging
import numpy as np
from common.cloud_api import CloudApi
from common.robot_api import RobotApi
from ..detection_worker import DetectionWorker
from .components.engine import FaceEngine

logger = logging.getLogger("FaceWorker")

DEFAULT_FACE_CONFIG = {
    "det_model": "/home/heyn/model/buffalo_s/det_500m.onnx",
    "rec_model": "/home/heyn/model/buffalo_s/w600k_mbf.onnx",
    "gallery_path": "/home/heyn/data/faces/face_gallery.json",
    # 检测参数
    "det_size": [320, 320],
    "match_thresh": 0.4,
    "detect_interval": 5.0,
    "cooldown": 10.0,
}

class FaceWorker(DetectionWorker):
    """人脸识别工作线程, 检测到已注册人脸时触发 callback, 带per-identity cooldown"""
    def __init__(
        self,
        hub,
        basic_info,
        cloud_api: CloudApi | None = None,
        robot_api: RobotApi | None = None,
        config: dict | None = None,
    ):
        cfg = dict(DEFAULT_FACE_CONFIG)
        if config:
            cfg.update(config)
        self.cfg = cfg

        super().__init__(
            name="FaceWorker",
            hub=hub,
            interval=cfg["detect_interval"],
            callback=None,
        )

        # ── 基础信息 ──
        self.basic_info = basic_info
        config_snapshot = basic_info.get_all()
        self.cloud_api = cloud_api or CloudApi.from_config(config_snapshot)
        self.robot_api = robot_api or RobotApi.from_config(config_snapshot)

        # ── 初始化人脸引擎 ──
        self.engine = FaceEngine(
            det_model=cfg["det_model"],
            rec_model=cfg["rec_model"],
            det_size=tuple(cfg["det_size"]),
            match_thresh=cfg["match_thresh"],
        )

        # ── 加载人脸库 ──
        gallery_path = cfg.get("gallery_path")
        if gallery_path:
            self.engine.load_gallery(gallery_path)

        # ── 其他 ──
        self._cooldown = cfg["cooldown"]
        self._last_seen: dict[str, float] = {}
        logger.info(f"FaceEngine loaded, gallery={self.engine.get_gallery_size()} 人, "f"cooldown={self._cooldown}s")

    def process(self, frame: np.ndarray):
        """调用 FaceEngine.recognize() 返回值: dict with faces, 或 None(无已注册人脸)"""
        results = self.engine.recognize(frame)

        if not results:
            return None
        now = time.time()
        triggered = []

        for r in results:
            identity = r.identity or "unknown"
            last = self._last_seen.get(identity, 0)
            if now - last < self._cooldown:
                continue
            self._last_seen[identity] = now
            triggered.append(r)
            logger.info(f"识别: {identity} (sim={r.similarity:.2f})")

        if not triggered:
            return None
        capture_time = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now))

        # 整帧背景图只上传一次
        bg_url = self._upload_frame(frame, filename="background.jpg")
        for r in triggered:
            face_url = self._upload_face(frame, r.bbox.astype(int).tolist())
            self._report(r, face_url, bg_url, capture_time)
        return {
            "type": "face_recognized",
            "faces": triggered,
            "timestamp": now,
        }

    def _report(self, r, face_url: str, bg_url: str, capture_time: str):
        if not self.cloud_api.configured:
            logger.warning("cloud.base_url 未配置，跳过上报")
            return
        meta = self.engine.get_metadata(r.identity) or {}
        fields = {
            "faceId": str(meta.get("id", "")) if r.identity else "",
            "facePicUrl": face_url,
            "faceBackgroundPicUrl": bg_url,
            "similarity": str(round(r.similarity * 100)),
            "captureTime": capture_time,
            "sn": self.basic_info.get_sn(),
            "mapName": self._get_current_map(),
        }
        try:
            response_text = self.cloud_api.report_face(fields)
            logger.info(
                f"上报成功 faceId={fields.get('faceId','?')} "
                f"resp={response_text[:200]}"
            )
            logger.info(f"face_url={face_url}, bg_url={bg_url}")
        except Exception as e:
            logger.error(f"上报失败: {e}")

    # ------------------------------------------------------------------ #
    #  图片上传
    # ------------------------------------------------------------------ #
    def _upload_face(self, frame: np.ndarray, bbox: list) -> str:
        """裁剪人脸区域（带padding）后上传"""
        x1, y1, x2, y2 = bbox
        h, w = frame.shape[:2]
        pad_x = int((x2 - x1) * 0.2)
        pad_y = int((y2 - y1) * 0.2)
        crop = frame[
            max(0, y1 - pad_y): min(h, y2 + pad_y),
            max(0, x1 - pad_x): min(w, x2 + pad_x),
        ]
        return self._upload_frame(crop, filename="face.jpg")

    def _upload_frame(self, frame: np.ndarray, filename: str = "alarm.jpg") -> str:
        """编码为 JPEG 并上传，返回 picUrl，失败返回空字符串"""
        ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 85])
        if not ok:
            logger.error("cv2.imencode 失败")
            return ""
        return self._upload_jpeg(buf.tobytes(), filename)

    def _upload_jpeg(self, jpeg_bytes: bytes, filename: str) -> str:
        if not self.cloud_api.configured:
            logger.warning("cloud.base_url 未配置，跳过上传")
            return ""
        try:
            url = self.cloud_api.upload_image(
                jpeg_bytes,
                filename=filename,
                image_type="14",
                sn=self.basic_info.get_sn(),
            )
            logger.info(f"上传成功: {url}")
            return url
        except Exception as e:
            logger.error(f"上传失败: {e}")
            return ""

    def _get_current_map(self) -> str:
        try:
            current_map = self.robot_api.get_current_map()
            if not current_map:
                logger.warning("获取当前地图成功，但 currentMap 为空")
                return ""
            logger.info(f"获取当前地图成功: {current_map}")
            return current_map
        except Exception as e:
            logger.error(f"获取当前地图失败: {e}")
        return ""
