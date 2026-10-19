import numpy as np
import onnxruntime as ort
import cv2
import logging

opts = ort.SessionOptions()
opts.log_severity_level = 3  # 0=verbose, 1=info, 2=warning, 3=error

logger = logging.getLogger("FaceDetector")

class FaceDetector:
    """RetinaFace 人脸检测 (det_10g.onnx)"""
    # 模型固定参数
    FMC = 3                          # feature map count
    STRIDES = [8, 16, 32]
    NUM_ANCHORS = 2                  # anchors per position

    def __init__(self, model_path: str, conf_thresh=0.5, nms_thresh=0.4, det_size=(640, 640)):
        self.session = ort.InferenceSession(model_path, sess_options=opts, providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name
        self.conf_thresh = conf_thresh
        self.nms_thresh = nms_thresh
        self.det_size = det_size       # (W, H)
        self._anchor_cache = {}
        logger.info(f"FaceDetector loaded: {model_path}, det_size={det_size}")

    # ------------------------------------------------------------------
    # public
    # ------------------------------------------------------------------
    def detect(self, img: np.ndarray) -> list[dict]:
        """
        检测人脸

        Parameters:
            img: BGR uint8 图像

        Returns:
            list of {
                'bbox':      np.array [x1, y1, x2, y2]  (原图坐标),
                'score':     float,
                'landmarks': np.array (5, 2)  (原图坐标, 左眼/右眼/鼻尖/左嘴角/右嘴角)
            }
        """
        det_w, det_h = self.det_size
        h, w = img.shape[:2]

        # --- 预处理: letterbox resize + normalize ---
        ratio = min(det_w / w, det_h / h)
        new_w, new_h = int(w * ratio), int(h * ratio)
        resized = cv2.resize(img, (new_w, new_h))

        padded = np.zeros((det_h, det_w, 3), dtype=np.uint8)
        padded[:new_h, :new_w, :] = resized

        blob = (padded.astype(np.float32) - 127.5) / 128.0
        blob = blob.transpose(2, 0, 1)[np.newaxis, ...]  # (1,3,H,W)

        # --- 推理 ---
        outputs = self.session.run(None, {self.input_name: blob})

        # --- 解码 ---
        # 输出顺序: [score_s8, score_s16, score_s32,
        #            bbox_s8,  bbox_s16,  bbox_s32,
        #            kps_s8,   kps_s16,   kps_s32]
        all_scores, all_bboxes, all_kps = [], [], []

        for idx, stride in enumerate(self.STRIDES):
            scores = outputs[idx].flatten()                     # (N,)
            bbox_preds = outputs[idx + self.FMC]                # (N, 4)
            kps_preds = outputs[idx + self.FMC * 2]             # (N, 10)

            anchors = self._get_anchors(det_h, det_w, stride)   # (N, 2)

            # bbox: distance (left, top, right, bottom) * stride
            bbox_preds = bbox_preds * stride
            bboxes = np.column_stack([
                anchors[:, 0] - bbox_preds[:, 0],
                anchors[:, 1] - bbox_preds[:, 1],
                anchors[:, 0] + bbox_preds[:, 2],
                anchors[:, 1] + bbox_preds[:, 3],
            ])

            # landmarks: offset * stride
            kps_preds = kps_preds * stride
            kps = np.zeros_like(kps_preds)
            for k in range(5):
                kps[:, k * 2] = anchors[:, 0] + kps_preds[:, k * 2]
                kps[:, k * 2 + 1] = anchors[:, 1] + kps_preds[:, k * 2 + 1]

            all_scores.append(scores)
            all_bboxes.append(bboxes)
            all_kps.append(kps)

        scores = np.concatenate(all_scores)
        bboxes = np.concatenate(all_bboxes)
        kps = np.concatenate(all_kps)

        # --- 过滤 + NMS ---
        mask = scores > self.conf_thresh
        scores, bboxes, kps = scores[mask], bboxes[mask], kps[mask]

        if len(scores) == 0:
            return []

        keep = self._nms(bboxes, scores, self.nms_thresh)

        # --- 映射回原图坐标 ---
        results = []
        for i in keep:
            results.append({
                "bbox": bboxes[i] / ratio,
                "score": float(scores[i]),
                "landmarks": kps[i].reshape(5, 2) / ratio,
            })
        return results

    # ------------------------------------------------------------------
    # internal
    # ------------------------------------------------------------------
    def _get_anchors(self, height: int, width: int, stride: int) -> np.ndarray:
        """生成 anchor center 坐标, 带缓存"""
        key = (height, width, stride)
        if key not in self._anchor_cache:
            h, w = height // stride, width // stride
            y, x = np.mgrid[:h, :w]
            centers = np.stack([x, y], axis=-1).astype(np.float32).reshape(-1, 2)
            centers = centers * stride
            centers = np.repeat(centers, self.NUM_ANCHORS, axis=0)
            self._anchor_cache[key] = centers
        return self._anchor_cache[key]

    @staticmethod
    def _nms(bboxes: np.ndarray, scores: np.ndarray, thresh: float) -> list[int]:
        """标准 NMS"""
        x1, y1, x2, y2 = bboxes[:, 0], bboxes[:, 1], bboxes[:, 2], bboxes[:, 3]
        areas = (x2 - x1) * (y2 - y1)
        order = scores.argsort()[::-1]

        keep = []
        while order.size > 0:
            i = order[0]
            keep.append(int(i))
            if order.size == 1:
                break
            xx1 = np.maximum(x1[i], x1[order[1:]])
            yy1 = np.maximum(y1[i], y1[order[1:]])
            xx2 = np.minimum(x2[i], x2[order[1:]])
            yy2 = np.minimum(y2[i], y2[order[1:]])
            inter = np.maximum(0.0, xx2 - xx1) * np.maximum(0.0, yy2 - yy1)
            iou = inter / (areas[i] + areas[order[1:]] - inter)
            inds = np.where(iou <= thresh)[0]
            order = order[inds + 1]
        return keep
