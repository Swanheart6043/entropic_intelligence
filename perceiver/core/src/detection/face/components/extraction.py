import numpy as np
import onnxruntime as ort
import cv2
import logging

logger = logging.getLogger("FaceRecognizer")

# ArcFace 标准 5 点对齐模板 (112x112 目标空间)
ARCFACE_REF_LANDMARKS = np.array([
    [38.2946, 51.6963],   # 左眼
    [73.5318, 51.5014],   # 右眼
    [56.0252, 71.7366],   # 鼻尖
    [41.5493, 92.3655],   # 左嘴角
    [70.7299, 92.2041],   # 右嘴角
], dtype=np.float32)

class FaceRecognizer:
    """ArcFace 人脸特征提取 (w600k_r50.onnx)"""

    INPUT_SIZE = (112, 112)   # ArcFace 固定输入尺寸

    def __init__(self, model_path: str):
        self.session = ort.InferenceSession(
            model_path, providers=["CPUExecutionProvider"]
        )
        self.input_name = self.session.get_inputs()[0].name
        logger.info(f"FaceRecognizer loaded: {model_path}")

    def get_embedding(self, img: np.ndarray, landmarks: np.ndarray) -> np.ndarray:
        """
        从原图 + 5 点关键点 → 512 维 L2 归一化特征向量

        Parameters:
            img:       BGR uint8 原图
            landmarks: np.array (5, 2), 来自 FaceDetector

        Returns:
            np.array (512,), L2 归一化后的 embedding
        """
        aligned = self._align(img, landmarks)
        blob = self._preprocess(aligned)
        embedding = self.session.run(None, {self.input_name: blob})[0][0]
        # L2 归一化
        embedding = embedding / (np.linalg.norm(embedding) + 1e-8)
        return embedding

    def get_embeddings_batch(self, img: np.ndarray,
                             faces: list[dict]) -> list[np.ndarray]:
        """批量提取多张脸的 embedding"""
        return [self.get_embedding(img, f["landmarks"]) for f in faces]

    @staticmethod
    def compare(emb1: np.ndarray, emb2: np.ndarray) -> float:
        """余弦相似度 (两个 embedding 都已 L2 归一化, dot 即 cosine)"""
        return float(np.dot(emb1, emb2))

    @staticmethod
    def compare_1_to_n(query: np.ndarray,
                       gallery: np.ndarray) -> tuple[int, float]:
        """
        1:N 检索

        Parameters:
            query:   np.array (512,)
            gallery: np.array (N, 512), 人脸库

        Returns:
            (best_index, best_score)
        """
        scores = gallery @ query          # (N,)
        best_idx = int(np.argmax(scores))
        return best_idx, float(scores[best_idx])

    # ------------------------------------------------------------------
    # internal
    # ------------------------------------------------------------------
    @staticmethod
    def _align(img: np.ndarray, landmarks: np.ndarray) -> np.ndarray:
        """5 点仿射对齐到 112x112"""
        M = cv2.estimateAffinePartial2D(
            landmarks.astype(np.float32),
            ARCFACE_REF_LANDMARKS,
        )[0]
        aligned = cv2.warpAffine(img, M, (112, 112), borderValue=(0, 0, 0))
        return aligned

    @staticmethod
    def _preprocess(face_img: np.ndarray) -> np.ndarray:
        """112x112 BGR uint8 → (1, 3, 112, 112) float32, 归一化到 [-1, 1]"""
        blob = face_img.astype(np.float32) / 255.0
        blob = (blob - 0.5) / 0.5
        blob = blob.transpose(2, 0, 1)[np.newaxis, ...]
        return blob
