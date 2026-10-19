import os
import json
import numpy as np
import cv2
import logging
import time
import tempfile
import threading
from .detector import FaceDetector
from .extraction import FaceRecognizer

logger = logging.getLogger("FaceEngine")

class FaceResult:
    """单张人脸结果"""
    __slots__ = ("bbox", "score", "landmarks", "embedding", "identity", "similarity")

    def __init__(self, bbox, score, landmarks, embedding=None, identity=None, similarity=0.0):
        self.bbox = bbox              # np.array (4,) [x1,y1,x2,y2]
        self.score = score            # float, 检测置信度
        self.landmarks = landmarks    # np.array (5,2)
        self.embedding = embedding    # np.array (512,) or None
        self.identity = identity      # str or None
        self.similarity = similarity  # float, 匹配相似度

    def __repr__(self):
        ident = self.identity or "unknown"
        return (
            f"Face({ident}, sim={self.similarity:.2f}, "
            f"det={self.score:.2f}, bbox={self.bbox.astype(int).tolist()})"
        )

class FaceEngine:
    """
    人脸识别引擎
    用法:
        engine = FaceEngine(det_model="det_10g.onnx", rec_model="w600k_r50.onnx")

        # 注册人脸
        engine.register("张三", img_zhangsan)

        # 识别
        results = engine.recognize(frame)
        for r in results:
            print(r.identity, r.similarity)
    """
    def __init__(self, det_model: str, rec_model: str, det_conf=0.5, det_nms=0.4, det_size=(640, 640), match_thresh=0.4):
        self.detector = FaceDetector(
            det_model, conf_thresh=det_conf,
            nms_thresh=det_nms, det_size=det_size
        )
        self.recognizer = FaceRecognizer(rec_model)
        self.match_thresh = match_thresh

        # 人脸库: {name: np.array(512,)}
        self._gallery_names: list[str] = []
        self._gallery_embeddings: np.ndarray | None = None  # (N, 512)
        self._metadata: dict[str, dict] = {}
        self._inference_lock = threading.Lock()
        self._gallery_lock = threading.RLock()

        logger.info(f"FaceEngine ready, match_thresh={match_thresh}")

    # ------------------------------------------------------------------
    # 检测 (不提特征, 速度快)
    # ------------------------------------------------------------------
    def detect(self, img: np.ndarray) -> list[FaceResult]:
        """只做人脸检测, 不提取 embedding"""
        with self._inference_lock:
            faces = self.detector.detect(img)
        return [
            FaceResult(f["bbox"], f["score"], f["landmarks"])
            for f in faces
        ]

    # ------------------------------------------------------------------
    # 识别 (检测 + 特征提取 + 比对)
    # ------------------------------------------------------------------
    def recognize(self, img: np.ndarray) -> list[FaceResult]:
        """检测 + 特征提取 + 人脸库比对"""
        results = []
        with self._inference_lock:
            faces = self.detector.detect(img)
            for f in faces:
                emb = self.recognizer.get_embedding(img, f["landmarks"])
                identity, similarity = self._match(emb)
                results.append(FaceResult(
                    bbox=f["bbox"],
                    score=f["score"],
                    landmarks=f["landmarks"],
                    embedding=emb,
                    identity=identity,
                    similarity=similarity,
                ))
        return results

    # ------------------------------------------------------------------
    # 人脸库管理
    # ------------------------------------------------------------------
    def register(self, name: str, img: np.ndarray) -> bool:
        """
        从图片注册人脸到库中 (取置信度最高的一张脸)
        Returns:
            True: 注册成功, False: 未检测到人脸
        """
        embedding = self.extract_embedding(img)
        if embedding is None:
            logger.warning(f"注册失败: 图片中未检测到人脸 ({name})")
            return False
        self._add_to_gallery(name, embedding)
        logger.info(f"注册成功: {name}")
        return True

    def extract_embedding(self, img: np.ndarray) -> np.ndarray | None:
        """从图片中串行提取最高置信度人脸的特征，不修改图库。"""
        with self._inference_lock:
            faces = self.detector.detect(img)
            if not faces:
                return None
            best = max(faces, key=lambda f: f["score"])
            return self.recognizer.get_embedding(img, best["landmarks"])

    def register_embedding(self, name: str, embedding: np.ndarray):
        """直接用 embedding 注册"""
        emb = embedding / (np.linalg.norm(embedding) + 1e-8)
        self._add_to_gallery(name, emb)

    def get_gallery_size(self) -> int:
        with self._gallery_lock:
            return len(self._gallery_names)

    def get_gallery_names(self) -> list[str]:
        with self._gallery_lock:
            return list(self._gallery_names)

    def set_metadata(self, name: str, meta: dict):
        with self._gallery_lock:
            self._metadata[name] = dict(meta)

    def get_metadata(self, name: str) -> dict | None:
        with self._gallery_lock:
            meta = self._metadata.get(name)
            return dict(meta) if meta is not None else None

    def remove(self, name: str) -> bool:
        """从人脸库删除"""
        with self._gallery_lock:
            if name not in self._gallery_names:
                return False
            idx = self._gallery_names.index(name)
            self._gallery_names.pop(idx)
            self._metadata.pop(name, None)
            if self._gallery_embeddings is not None:
                self._gallery_embeddings = np.delete(
                    self._gallery_embeddings, idx, axis=0
                )
                if len(self._gallery_names) == 0:
                    self._gallery_embeddings = None
        logger.info(f"已删除: {name}")
        return True

    # ------------------------------------------------------------------
    # 人脸库持久化
    # ------------------------------------------------------------------
    def save_gallery(self, path: str):
        with self._gallery_lock:
            data = self._serialize_gallery(
                self._gallery_names,
                self._gallery_embeddings,
                self._metadata,
            )
        self._write_gallery(path, data)
        logger.info(f"人脸库已保存: {path} ({len(data)} 人)")

    def replace_gallery(
        self,
        registrations: list[tuple[str, np.ndarray, dict]],
        path: str | None = None,
    ) -> int:
        """持久化新图库后一次性切换，识别线程不会看到中间状态。"""
        names: list[str] = []
        embeddings_by_name: dict[str, np.ndarray] = {}
        metadata: dict[str, dict] = {}
        for name, embedding, meta in registrations:
            if name not in embeddings_by_name:
                names.append(name)
            embeddings_by_name[name] = np.asarray(embedding, dtype=np.float32)
            metadata[name] = dict(meta)

        embeddings = (
            np.vstack([embeddings_by_name[name] for name in names])
            if names else None
        )
        data = self._serialize_gallery(names, embeddings, metadata)
        if path:
            self._write_gallery(path, data)

        with self._gallery_lock:
            self._gallery_names = names
            self._gallery_embeddings = embeddings
            self._metadata = metadata
        logger.info(f"人脸库已替换: {len(names)} 人")
        return len(names)

    def load_gallery(self, path: str):
        """从文件加载人脸库"""
        if not os.path.exists(path):
            logger.info(f"人脸库文件不存在, 创建空库: {path}")
            with open(path, "w") as f:
                json.dump([], f)
            return
        with open(path, "r") as f:
            data = json.load(f)
        names = []
        metadata = {}
        embeddings = []
        for entry in data:
            name = entry["name"]
            names.append(name)
            embeddings.append(entry["embedding"])
            meta = {k: v for k, v in entry.items() if k not in ("name", "embedding")}
            if meta:
                metadata[name] = meta
        gallery_embeddings = (
            np.array(embeddings, dtype=np.float32) if embeddings else None
        )
        with self._gallery_lock:
            self._gallery_names = names
            self._metadata = metadata
            self._gallery_embeddings = gallery_embeddings
        logger.info(f"人脸库已加载: {path} ({len(names)} 人)")

    # ------------------------------------------------------------------
    # internal
    # ------------------------------------------------------------------
    def _add_to_gallery(self, name: str, embedding: np.ndarray):
        """添加到人脸库 (同名覆盖)"""
        with self._gallery_lock:
            if name in self._gallery_names:
                idx = self._gallery_names.index(name)
                self._gallery_embeddings[idx] = embedding
            else:
                self._gallery_names.append(name)
                if self._gallery_embeddings is None:
                    self._gallery_embeddings = embedding.reshape(1, -1)
                else:
                    self._gallery_embeddings = np.vstack(
                        [self._gallery_embeddings, embedding.reshape(1, -1)]
                    )

    def _match(self, embedding: np.ndarray) -> tuple[str | None, float]:
        """在人脸库中匹配"""
        with self._gallery_lock:
            if self._gallery_embeddings is None or len(self._gallery_names) == 0:
                return None, 0.0
            best_idx, best_score = self.recognizer.compare_1_to_n(
                embedding, self._gallery_embeddings
            )
            if best_score >= self.match_thresh:
                return self._gallery_names[best_idx], best_score
            return None, best_score

    @staticmethod
    def _serialize_gallery(
        names: list[str],
        embeddings: np.ndarray | None,
        metadata: dict[str, dict],
    ) -> list[dict]:
        data = []
        for i, name in enumerate(names):
            entry = {"name": name}
            if embeddings is not None:
                entry["embedding"] = embeddings[i].tolist()
            entry.update(metadata.get(name, {}))
            data.append(entry)
        return data

    @staticmethod
    def _write_gallery(path: str, data: list[dict]) -> None:
        directory = os.path.dirname(os.path.abspath(path))
        os.makedirs(directory, exist_ok=True)
        fd, tmp_path = tempfile.mkstemp(dir=directory, suffix=".tmp")
        try:
            with os.fdopen(fd, "w") as f:
                json.dump(data, f, ensure_ascii=False)
            os.replace(tmp_path, path)
        except Exception:
            if os.path.exists(tmp_path):
                os.unlink(tmp_path)
            raise