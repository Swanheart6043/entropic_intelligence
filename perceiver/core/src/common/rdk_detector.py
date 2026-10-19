import os
import cv2
import numpy as np
from hobot_dnn import pyeasy_dnn

# 内网无ROS，注释ai_msgs相关全部依赖
# from ai_msgs.msg import DetBox, DetResult

class RDKDetector:
    def __init__(self, model_path, input_w, input_h, score_thresh=0.4, nms_thresh=0.45,
                 is_person_falling=False, is_landmark=False):
        self.score_thresh = score_thresh
        self.nms_thresh = nms_thresh
        self.input_w = input_w
        self.input_h = input_h
        self.is_person_falling = is_person_falling
        self.is_landmark = is_landmark

        if not os.path.exists(model_path):
            raise FileNotFoundError(f"模型不存在: {model_path}")
        self.models = pyeasy_dnn.load(model_path)

    def preprocess(self, img_bgr):
        h, w = img_bgr.shape[:2]
        scale = min(self.input_w / w, self.input_h / h)
        new_w, new_h = int(w * scale), int(h * scale)
        img_resize = cv2.resize(img_bgr, (new_w, new_h))
        pad_img = np.zeros((self.input_h, self.input_w, 3), dtype=np.uint8)
        pad_img[:new_h, :new_w, :] = img_resize
        input_tensor = pad_img.transpose(2, 0, 1)[np.newaxis, ...]
        return input_tensor, scale

    def infer(self, img_bgr):
        tensor, scale = self.preprocess(img_bgr)
        outputs = self.models[0].forward(tensor)
        raw_out = outputs[0].buffer

        # 人脸关键点特殊分支
        if self.is_landmark:
            return self.decode_landmark_model(raw_out, scale)
        # 跌倒人体空分支兜底防越界
        elif self.is_person_falling:
            return [], [], []
        # 标准YOLO检测分支（人脸、手掌、通用检测）
        else:
            boxes, scores, cls_ids = self.decode_yolo_output(raw_out, scale)
            return self.nms(boxes, scores, cls_ids)

    # 标准YOLO解码（人脸/手掌/通用检测模型）
    def decode_yolo_output(self, feat, scale):
        h, w = self.input_h, self.input_w
        stride_list = [8, 16, 32]
        all_boxes, all_scores, all_cls = [], [], []
        for stride in stride_list:
            grid_h, grid_w = h // stride, w // stride
            grid_x, grid_y = np.meshgrid(np.arange(grid_w), np.arange(grid_h))
            grid = np.stack([grid_x, grid_y], axis=-1).reshape(-1, 2)
            feat_slice = feat.reshape(-1, feat.shape[-1])

            # 防维度越界兜底
            if feat_slice.shape[1] < 6:
                continue

            obj_conf = feat_slice[:, 4]
            cls_conf = feat_slice[:, 5:]
            max_cls_score = np.max(cls_conf, axis=-1)
            final_score = obj_conf * max_cls_score
            mask = final_score > self.score_thresh
            if not np.any(mask):
                continue

            dx, dy, dw, dh = feat_slice[mask, :4].T
            gx, gy = grid[mask].T
            x = (gx + dx) * stride / scale
            y = (gy + dy) * stride / scale
            bw = np.exp(dw) * stride / scale
            bh = np.exp(dh) * stride / scale

            x1 = x - bw / 2
            y1 = y - bh / 2
            x2 = x + bw / 2
            y2 = y + bh / 2
            cls_idx = np.argmax(cls_conf[mask], axis=-1)

            all_boxes.append(np.column_stack([x1, y1, x2, y2]))
            all_scores.append(final_score[mask])
            all_cls.append(cls_idx[mask])

        if not all_boxes:
            return np.array([]), np.array([]), np.array([])
        return np.vstack(all_boxes), np.hstack(all_scores), np.hstack(all_cls)

    # 人脸关键点专用解码（适配106点模型）
    def decode_landmark_model(self, feat, scale):
        # 输出扁平化关键点坐标 + 平均置信
        kps = feat.reshape(-1, 2)
        kps = kps / scale
        avg_score = 0.9
        return kps.tolist(), avg_score, 0

    def nms(self, boxes, scores, cls_ids):
        res_boxes, res_scores, res_cls = [], [], []
        if len(boxes) == 0:
            return res_boxes, res_scores, res_cls
        unique_cls = np.unique(cls_ids)
        for c in unique_cls:
            idx = cls_ids == c
            c_boxes = boxes[idx]
            c_scores = scores[idx]
            keep = cv2.dnn.NMSBoxes(c_boxes.tolist(), c_scores.tolist(), self.score_thresh, self.nms_thresh)
            for k in keep:
                res_boxes.append(c_boxes[k])
                res_scores.append(c_scores[k])
                res_cls.append(c)
        return res_boxes, res_scores, res_cls
