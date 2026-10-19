#!/usr/bin/env python3
"""适配 RDK BPU 的通用 YOLO 目标检测器。"""
import logging
import cv2
import numpy as np
try:
    from scipy.special import softmax
except ImportError:
    def softmax(x, axis=None):
        e_x = np.exp(x - np.max(x, axis=axis, keepdims=True))
        return e_x / np.sum(e_x, axis=axis, keepdims=True)
try:
    try:
        from hobot_dnn import pyeasy_dnn as dnn
    except Exception:
        from hobot_dnn_rdkx5 import pyeasy_dnn as dnn
except ImportError:
    dnn = None

logger = logging.getLogger("ObjectDetector")

class ObjectDetector:
    def __init__(
        self,
        model_path,
        classes_num,
        score_thres=0.20,
        nms_thres=0.45,
        reg=16,
        strides=(8, 16, 32),
        model_name="目标检测",
    ):
        if dnn is None:
            raise ImportError("未找到 hobot_dnn / hobot_dnn_rdkx5，确认当前运行环境为RDK开发板并安装BPU依赖")

        self.CLASSES_NUM = int(classes_num)
        self.SCORE_THRESHOLD = float(score_thres)
        self.NMS_THRESHOLD = float(nms_thres)
        self.REG = int(reg)
        self.strides = list(strides)
        self.CONF_THRES_RAW = -np.log(1.0 / self.SCORE_THRESHOLD - 1.0)

        logger.info("加载%s模型: %s", model_name, model_path)
        from time import time
        t0 = time()
        self.quantize_model = dnn.load(model_path)
        logger.info(f"模型加载耗时: {(time() - t0) * 1000:.1f} ms")

        in_shape = self.quantize_model[0].inputs[0].properties.shape
        if in_shape[1] in (1, 3):
            self.input_H, self.input_W = int(in_shape[2]), int(in_shape[3])
        else:
            self.input_H, self.input_W = int(in_shape[1]), int(in_shape[2])
        logger.info(f"模型输入分辨率: {self.input_W} x {self.input_H}")

        self.num_outputs = len(self.quantize_model[0].outputs)
        out0_shape = self.quantize_model[0].outputs[0].properties.shape
        self.output_format = "bbox_first" if out0_shape[-1] == self.REG * 4 else "cls_first"
        logger.info(f"输出层数: {self.num_outputs}, 输出格式: {self.output_format}")
        logger.info(f"置信阈值: {self.SCORE_THRESHOLD}, 原始阈值raw: {self.CONF_THRES_RAW:.4f}")

        self.weights_static = np.arange(self.REG, dtype=np.float32)[np.newaxis, np.newaxis, :]

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

        self.img_h = self.img_w = 0
        self.scale = 1.0
        self.x_shift = self.y_shift = 0

    def preprocess(self, img):
        self.img_h, self.img_w = img.shape[:2]
        self.scale = min(self.input_H / self.img_h, self.input_W / self.img_w)
        new_w = int(self.img_w * self.scale)
        new_h = int(self.img_h * self.scale)
        self.x_shift = (self.input_W - new_w) // 2
        self.y_shift = (self.input_H - new_h) // 2

        img_resized = cv2.resize(img, (new_w, new_h))
        img_padded = cv2.copyMakeBorder(
            img_resized,
            self.y_shift, self.input_H - new_h - self.y_shift,
            self.x_shift, self.input_W - new_w - self.x_shift,
            cv2.BORDER_CONSTANT, value=[127, 127, 127],
        )
        return self._bgr2nv12(img_padded)

    @staticmethod
    def _bgr2nv12(bgr_img):
        h, w = bgr_img.shape[:2]
        area = h * w
        yuv420p = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2YUV_I420).reshape(area * 3 // 2)
        y = yuv420p[:area]
        uv_planar = yuv420p[area:].reshape(2, area // 4)
        uv_packed = uv_planar.transpose(1, 0).reshape(area // 2)
        nv12 = np.zeros_like(yuv420p)
        nv12[:area] = y
        nv12[area:] = uv_packed
        return nv12

    def inference(self, input_tensor):
        outputs = self.quantize_model[0].forward(input_tensor)
        return [out.buffer for out in outputs]

    def postprocess(self, outputs):
        if not outputs:
            return []
        n = len(outputs)
        
        if n == 1:
            result = self._postprocess_single(outputs[0])
        elif n == 2:
            result = self._postprocess_multi(outputs, stride_indices=[0])
        elif n >= 6:
            result = self._postprocess_multi(outputs, stride_indices=[0, 1, 2])
        else:
            logger.error(f"不支持的模型输出层数: {n}")
            return []
        
        return result

    def _postprocess_single(self, raw_output):
        output = raw_output.squeeze()
        if output.shape[0] == self.CLASSES_NUM + 4:
            output = output.T

        bbox_pred = output[:, :4]
        cls_pred = output[:, 4:]

        max_scores = np.max(cls_pred, axis=1)
        keep = max_scores >= self.SCORE_THRESHOLD
        
        if not np.any(keep):
            return []

        bbox_sel = bbox_pred[keep]
        scores = max_scores[keep]
        class_ids = np.argmax(cls_pred[keep], axis=1)

        cx, cy, w, h = bbox_sel[:, 0], bbox_sel[:, 1], bbox_sel[:, 2], bbox_sel[:, 3]
        x1 = cx - w / 2
        y1 = cy - h / 2
        x2 = cx + w / 2
        y2 = cy + h / 2
        xyxy = np.stack([x1, y1, x2, y2], axis=1)

        return self._nms_per_class(xyxy, scores, class_ids)

    def _postprocess_multi(self, outputs, stride_indices):
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
        
        for layer_idx, (cls_arr, bbox_arr, stride, grid) in enumerate(zip(clses, bboxes, strides_use, grids_use)):
            max_raw = np.max(cls_arr, axis=1)
            keep = np.flatnonzero(max_raw >= self.CONF_THRES_RAW)
            if len(keep) == 0:
                continue

            all_ids.append(np.argmax(cls_arr[keep], axis=1))
            all_scores.append(1.0 / (1.0 + np.exp(-max_raw[keep])))

            ltrb = np.sum(
                softmax(bbox_arr[keep].reshape(-1, 4, self.REG), axis=2) * self.weights_static,
                axis=2,
            )
            grid_sel = grid[keep]
            xyxy = np.hstack([grid_sel - ltrb[:, :2], grid_sel + ltrb[:, 2:]]) * float(stride)
            all_xyxy.append(xyxy)

        if not all_xyxy:
            return []
        xyxy = np.concatenate(all_xyxy, axis=0)
        scores = np.concatenate(all_scores, axis=0)
        ids = np.concatenate(all_ids, axis=0)
        return self._nms_per_class(xyxy, scores, ids)

    def _nms_per_class(self, xyxy, scores, class_ids):
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
            
            indices = np.array(indices).flatten()
            pos = np.flatnonzero(mask)
            
            for idx in indices:
                i = pos[int(idx)]
                bx1, by1, bx2, by2 = xyxy[i]
                bx1 = int(max(0, min((bx1 - self.x_shift) / self.scale, self.img_w)))
                by1 = int(max(0, min((by1 - self.y_shift) / self.scale, self.img_h)))
                bx2 = int(max(0, min((bx2 - self.x_shift) / self.scale, self.img_w)))
                by2 = int(max(0, min((by2 - self.y_shift) / self.scale, self.img_h)))
                if bx2 <= bx1 or by2 <= by1:
                    continue
                results.append((int(cid), float(scores[i]), bx1, by1, bx2, by2))
        return results

    def detect(self, img):
        inp = self.preprocess(img)
        outs = self.inference(inp)
        return self.postprocess(outs)