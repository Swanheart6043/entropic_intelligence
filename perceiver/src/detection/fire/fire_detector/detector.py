import logging
from typing import List, Optional, Tuple
import cv2
import numpy as np
try:
    from scipy.special import softmax
except ImportError:
    def softmax(values, axis=None):
        shifted = values - np.max(values, axis=axis, keepdims=True)
        exp_values = np.exp(shifted)
        return exp_values / np.maximum(
            np.sum(exp_values, axis=axis, keepdims=True), np.finfo(np.float32).tiny
        )
try:
    try:
        from hobot_dnn import pyeasy_dnn as dnn
    except Exception:
        from hobot_dnn_rdkx5 import pyeasy_dnn as dnn
except ImportError:
    dnn = None

logger = logging.getLogger("RDK_FIRE")

DEFAULT_CLASS_NAMES = ["Fire", "Smoke"]
CLS_FIRE = 0
CLS_SMOKE = 1

# ====================================================================== #
#  事件
# ====================================================================== #
class AlarmEvent:
    def __init__(self, class_id: int, bbox: list, conf: float, frame_id: int):
        self.class_id = int(class_id)
        self.bbox = bbox
        self.conf = conf
        self.frame_id = frame_id

    @property
    def class_name(self) -> str:
        if 0 <= self.class_id < len(DEFAULT_CLASS_NAMES):
            return DEFAULT_CLASS_NAMES[self.class_id]
        return f"class_{self.class_id}"


# ====================================================================== #
#  火焰/烟雾追踪状态（按类别和归一化中心匹配）
# ====================================================================== #
class _TargetState:
    def __init__(self, class_id: int, cx: float, cy: float):
        self.class_id = class_id
        self.cx = cx
        self.cy = cy
        self.streak = 0

    def distance(self, cx: float, cy: float) -> float:
        return ((self.cx - cx) ** 2 + (self.cy - cy) ** 2) ** 0.5

# ====================================================================== #
#  检测器
# ====================================================================== #
class FireDetector:
    """
    火焰/烟雾检测器 (RDK X5 BPU)
    2-class YOLOv8: Fire(0), Smoke(1)
    支持三种模型输出格式:
      - 单输出 (n==1): YOLOv8/v11 合并格式
      - 双输出 (n==2): 单尺度 cls + bbox
      - 六输出 (n>=6): 三尺度 cls + bbox (DFL 编码)
    """
    def __init__(
        self,
        model_path: str,
        classes_num: int = 2,
        score_thres: float = 0.60,
        nms_thres: float = 0.45,
        reg: int = 16,
        strides: tuple = (8, 16, 32),
        confirm_frames: int = 3,
        match_thres: float = 0.12,
    ):
        if dnn is None:
            raise ImportError(
                "未找到 hobot_dnn 或 hobot_dnn_rdkx5，请在 RDK X5 BPU 环境运行"
            )
        if int(classes_num) != len(DEFAULT_CLASS_NAMES):
            raise ValueError(
                f"classes_num 必须为 {len(DEFAULT_CLASS_NAMES)}，"
                f"与训练类别 {DEFAULT_CLASS_NAMES} 一致"
            )
        if not 0.0 < float(score_thres) < 1.0:
            raise ValueError("score_thres 必须在 (0, 1) 范围内")
        if int(confirm_frames) < 1:
            raise ValueError("confirm_frames 必须大于 0")

        # ── 推理参数 ──
        self.CLASSES_NUM = int(classes_num)
        self.SCORE_THRESHOLD = float(score_thres)
        self.NMS_THRESHOLD = float(nms_thres)
        self.REG = int(reg)
        self.strides = list(strides)
        self.CONF_THRES_RAW = -np.log(1.0 / self.SCORE_THRESHOLD - 1.0)

        # ── 确认参数 ──
        self.confirm_frames = int(confirm_frames)
        self.match_thres = float(match_thres)

        # ── 加载模型 ──
        logger.info(f"加载火焰/烟雾模型: {model_path}")
        from time import time
        t0 = time()
        self.quantize_model = dnn.load(model_path)
        logger.info(f"模型加载耗时: {(time() - t0) * 1000:.1f} ms")

        # 获取输入尺寸 — 兼容 NCHW / NHWC
        in_shape = self.quantize_model[0].inputs[0].properties.shape
        if in_shape[1] in (1, 3):
            self.input_H, self.input_W = int(in_shape[2]), int(in_shape[3])
        else:
            self.input_H, self.input_W = int(in_shape[1]), int(in_shape[2])
        logger.info(f"模型输入: {self.input_W}x{self.input_H}")

        # 检测输出格式
        self.num_outputs = len(self.quantize_model[0].outputs)
        out0_shape = self.quantize_model[0].outputs[0].properties.shape
        self.output_format = (
            "bbox_first" if out0_shape[-1] == self.REG * 4 else "cls_first"
        )
        logger.info(
            f"输出层数: {self.num_outputs}, 输出格式: {self.output_format}, "
            f"out0_shape: {out0_shape}"
        )

        # DFL 权重 (多输出格式用)
        self.weights_static = np.arange(self.REG, dtype=np.float32)[
            np.newaxis, np.newaxis, :
        ]

        # 预生成 grid (多输出格式用)
        self.grids = []
        for stride in self.strides:
            grid_h = self.input_H // stride
            grid_w = self.input_W // stride
            gy, gx = np.meshgrid(
                np.arange(grid_h, dtype=np.float32) + 0.5,
                np.arange(grid_w, dtype=np.float32) + 0.5,
                indexing="ij",
            )
            self.grids.append(np.stack([gx, gy], axis=-1).reshape(-1, 2))

        # 预处理状态
        self.img_h = self.img_w = 0
        self.scale = 1.0
        self.x_shift = self.y_shift = 0

        # 火焰/烟雾追踪状态
        self._targets: List[_TargetState] = []

    # ================================================================== #
    #  预处理
    # ================================================================== #
    def preprocess(self, img: np.ndarray):
        self.img_h, self.img_w = img.shape[:2]
        self.scale = min(self.input_H / self.img_h, self.input_W / self.img_w)
        new_w = int(self.img_w * self.scale)
        new_h = int(self.img_h * self.scale)
        self.x_shift = (self.input_W - new_w) // 2
        self.y_shift = (self.input_H - new_h) // 2

        img_resized = cv2.resize(img, (new_w, new_h))
        img_padded = cv2.copyMakeBorder(
            img_resized,
            self.y_shift,
            self.input_H - new_h - self.y_shift,
            self.x_shift,
            self.input_W - new_w - self.x_shift,
            cv2.BORDER_CONSTANT,
            value=[127, 127, 127],
        )
        return self._bgr2nv12(img_padded)

    @staticmethod
    def _bgr2nv12(bgr_img: np.ndarray):
        h, w = bgr_img.shape[:2]
        area = h * w
        yuv420p = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2YUV_I420).reshape(
            area * 3 // 2
        )
        y = yuv420p[:area]
        uv_planar = yuv420p[area:].reshape(2, area // 4)
        uv_packed = uv_planar.transpose(1, 0).reshape(area // 2)
        nv12 = np.zeros_like(yuv420p)
        nv12[:area] = y
        nv12[area:] = uv_packed
        return nv12

    # ================================================================== #
    #  推理
    # ================================================================== #
    def inference(self, input_tensor):
        outputs = self.quantize_model[0].forward(input_tensor)
        return [out.buffer for out in outputs]

    # ================================================================== #
    #  后处理 — 原始检测
    # ================================================================== #
    def postprocess(self, outputs):
        """返回所有原始检测: [(class_id, score, x1, y1, x2, y2), ...]"""
        if not outputs:
            return []

        n = len(outputs)
        if n == 1:
            return self._postprocess_single(outputs[0])
        elif n == 2:
            return self._postprocess_multi(outputs, stride_indices=[0])
        elif n >= 6:
            return self._postprocess_multi(outputs, stride_indices=[0, 1, 2])
        else:
            logger.error(f"不支持的输出层数: {n}")
            return []

    def _postprocess_single(self, raw_output):
        """单输出 YOLOv8/v11 合并格式: (1, 4+C, N, 1)"""
        output = raw_output.squeeze()

        # 统一为 (N, 4+C)
        if output.shape[0] == self.CLASSES_NUM + 4:
            output = output.T

        bbox_pred = output[:, :4]
        cls_pred = output[:, 4:]

        # ===== 诊断 =====
        max_scores = np.max(cls_pred, axis=1)
        top5_idx = np.argsort(max_scores)[-5:]
        for i in top5_idx:
            raw_val = max_scores[i]
            sigmoid_val = 1.0 / (1.0 + np.exp(-np.clip(raw_val, -50, 50)))
            cls_id = np.argmax(cls_pred[i])
            cls_name = DEFAULT_CLASS_NAMES[cls_id] if cls_id < len(DEFAULT_CLASS_NAMES) else str(cls_id)
            logger.debug(
                f"[诊断] top候选: raw={raw_val:.4f}, sigmoid={sigmoid_val:.4f}, "
                f"cls={cls_id}({cls_name}), bbox={bbox_pred[i].tolist()}"
            )
        logger.debug(
            f"[诊断] 总候选={len(max_scores)}, "
            f"max_raw={np.max(max_scores):.4f}, "
            f"阈值={self.SCORE_THRESHOLD}, "
            f"通过数={np.sum(max_scores >= self.SCORE_THRESHOLD)}"
        )
        # ===== 诊断结束 =====

        keep = max_scores >= self.SCORE_THRESHOLD
        if not np.any(keep):
            return []

        bbox_sel = bbox_pred[keep]
        scores = max_scores[keep]
        class_ids = np.argmax(cls_pred[keep], axis=1)

        # xywh → xyxy
        cx, cy, w, h = (
            bbox_sel[:, 0],
            bbox_sel[:, 1],
            bbox_sel[:, 2],
            bbox_sel[:, 3],
        )
        x1 = cx - w / 2
        y1 = cy - h / 2
        x2 = cx + w / 2
        y2 = cy + h / 2
        xyxy = np.stack([x1, y1, x2, y2], axis=1)
        return self._nms_per_class(xyxy, scores, class_ids)

    def _postprocess_multi(self, outputs, stride_indices):
        """双输出 / 六输出 DFL 格式"""
        if len(stride_indices) == 1:
            if self.output_format == "cls_first":
                clses = [outputs[0].reshape(-1, self.CLASSES_NUM)]
                bboxes = [outputs[1].reshape(-1, self.REG * 4)]
            else:
                bboxes = [outputs[0].reshape(-1, self.REG * 4)]
                clses = [outputs[1].reshape(-1, self.CLASSES_NUM)]
        else:
            even, odd = [0, 2, 4], [1, 3, 5]
            if self.output_format == "cls_first":
                clses = [outputs[i].reshape(-1, self.CLASSES_NUM) for i in even]
                bboxes = [outputs[i].reshape(-1, self.REG * 4) for i in odd]
            else:
                bboxes = [outputs[i].reshape(-1, self.REG * 4) for i in even]
                clses = [outputs[i].reshape(-1, self.CLASSES_NUM) for i in odd]

        strides_use = [self.strides[i] for i in stride_indices]
        grids_use = [self.grids[i] for i in stride_indices]

        all_xyxy, all_scores, all_ids = [], [], []
        for cls_arr, bbox_arr, stride, grid in zip(
            clses, bboxes, strides_use, grids_use
        ):
            max_raw = np.max(cls_arr, axis=1)
            keep = np.flatnonzero(max_raw >= self.CONF_THRES_RAW)
            if len(keep) == 0:
                continue

            all_ids.append(np.argmax(cls_arr[keep], axis=1))
            all_scores.append(1.0 / (1.0 + np.exp(-max_raw[keep])))

            ltrb = np.sum(
                softmax(
                    bbox_arr[keep].reshape(-1, 4, self.REG), axis=2
                )
                * self.weights_static,
                axis=2,
            )
            grid_sel = grid[keep]
            xyxy = (
                np.hstack([grid_sel - ltrb[:, :2], grid_sel + ltrb[:, 2:]])
                * float(stride)
            )
            all_xyxy.append(xyxy)

        if not all_xyxy:
            return []

        xyxy = np.concatenate(all_xyxy, axis=0)
        scores = np.concatenate(all_scores, axis=0)
        ids = np.concatenate(all_ids, axis=0)

        return self._nms_per_class(xyxy, scores, ids)

    def _nms_per_class(self, xyxy, scores, class_ids):
        """按类别 NMS，并将坐标映射回原图"""
        results = []
        xywh_nms = np.hstack([xyxy[:, :2], xyxy[:, 2:4] - xyxy[:, 0:2]])

        for cid in range(self.CLASSES_NUM):
            mask = class_ids == cid
            if not np.any(mask):
                continue

            indices = cv2.dnn.NMSBoxes(
                xywh_nms[mask].tolist(),
                scores[mask].tolist(),
                self.SCORE_THRESHOLD,
                self.NMS_THRESHOLD,
            )
            if len(indices) == 0:
                continue

            pos = np.flatnonzero(mask)
            for idx in np.array(indices).reshape(-1):
                i = pos[int(idx)]
                bx1, by1, bx2, by2 = xyxy[i]
                bx1 = int(
                    max(0, min((bx1 - self.x_shift) / self.scale, self.img_w))
                )
                by1 = int(
                    max(0, min((by1 - self.y_shift) / self.scale, self.img_h))
                )
                bx2 = int(
                    max(0, min((bx2 - self.x_shift) / self.scale, self.img_w))
                )
                by2 = int(
                    max(0, min((by2 - self.y_shift) / self.scale, self.img_h))
                )
                if bx2 <= bx1 or by2 <= by1:
                    continue
                results.append(
                    (int(cid), float(scores[i]), bx1, by1, bx2, by2)
                )
        return results

    # ================================================================== #
    #  火焰/烟雾追踪 + 连续帧确认
    # ================================================================== #
    def _match_or_create(
        self,
        class_id: int,
        cx: float,
        cy: float,
        used_keys: set,
    ) -> _TargetState:
        best: Optional[_TargetState] = None
        best_d = self.match_thres
        for ts in self._targets:
            if ts.class_id != class_id or id(ts) in used_keys:
                continue
            d = ts.distance(cx, cy)
            if d < best_d:
                best_d = d
                best = ts
        if best is None:
            best = _TargetState(class_id, cx, cy)
            self._targets.append(best)
        else:
            best.cx = cx
            best.cy = cy
        return best

    def get_fire_events(self, dets: list, frame_id: int = 0) -> List[AlarmEvent]:
        """
        从原始检测中提取火焰/烟雾事件。
        Fire(0) 和 Smoke(1) 都需要连续帧确认后报警。

        返回: [AlarmEvent, ...]
        """
        # 每个候选保留类别，避免火焰轨迹与烟雾轨迹互相匹配。
        candidates: List[Tuple[int, list, float]] = []

        for cid, score, x1, y1, x2, y2 in dets:
            w, h = x2 - x1, y2 - y1
            if w <= 0 or h <= 0:
                continue
            if cid in (CLS_FIRE, CLS_SMOKE):
                candidates.append((cid, [x1, y1, x2, y2], score))
        logger.debug(f"[诊断] 火焰/烟雾候选数: {len(candidates)}")


        # ── 按类别追踪每个候选 ──
        active_keys: set = set()
        alarms: List[AlarmEvent] = []
        used_keys: set = set()

        for class_id, bbox, conf in sorted(
            candidates, key=lambda item: item[2], reverse=True
        ):
            cx = ((bbox[0] + bbox[2]) / 2) / max(self.img_w, 1)
            cy = ((bbox[1] + bbox[3]) / 2) / max(self.img_h, 1)
            state = self._match_or_create(class_id, cx, cy, used_keys)
            used_keys.add(id(state))
            active_keys.add(id(state))
            state.streak += 1

            logger.debug(
                f"[诊断] {DEFAULT_CLASS_NAMES[class_id]} bbox={bbox}, "
                f"conf={conf:.4f}, streak={state.streak}/{self.confirm_frames}"
            )

            if state.streak >= self.confirm_frames:
                state.streak = 0
                alarms.append(
                    AlarmEvent(
                        class_id=class_id,
                        bbox=bbox,
                        conf=conf,
                        frame_id=frame_id,
                    )
                )
        # 缺失一帧即中断连续确认，清理本帧未匹配的轨迹。
        self._targets = [ts for ts in self._targets if id(ts) in active_keys]
        return alarms

    # ================================================================== #
    #  对外接口
    # ================================================================== #
    def detect(
        self, img: np.ndarray, frame_id: int = 0
    ) -> Tuple[list, List[AlarmEvent]]:
        """
        完整检测流程
        返回:
          dets:   [(class_id, score, x1, y1, x2, y2), ...]  所有原始检测
          alarms: [AlarmEvent, ...]  确认的火焰/烟雾事件
        """
        inp = self.preprocess(img)
        outs = self.inference(inp)
        dets = self.postprocess(outs)
        alarms = self.get_fire_events(dets, frame_id)
        return dets, alarms
