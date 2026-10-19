#!/usr/bin/env python3
"""
range_reader.py - 读 colorbar 上/下限温度数字 (应对自动量程)

方案: 二值化 -> 列投影切字符 -> 与数字模板(0-9)逐一匹配。
零重依赖(只用 numpy/cv2), 确定性强, RDK 上飞快。

模板来源: 首次运行时用一帧"已知读数"的画面自标定(build_templates),
把切出的字符与你提供的真值字符串对齐, 存成模板库 npz。之后直接加载匹配。
"""
import logging
import os
import numpy as np
import cv2
from . import config as C
from common.paths import THERMAL_DIGIT_TEMPLATES

logger = logging.getLogger("thermal.range")

TEMPLATE_PATH = THERMAL_DIGIT_TEMPLATES
_BIN_THRESH = 180          # 数字二值化阈值(白字)
_CHAR_NORM = (16, 12)      # 字符归一化尺寸 (h,w) 用于模板比对

def _binarize(crop_bgr):
    g = crop_bgr.mean(2)
    return (g > _BIN_THRESH).astype(np.uint8)

def _segment_chars(binimg):
    """列投影切字符, 返回 [(x0,x1), ...] 仅保留数字宽度的段(滤掉 ℃)。"""
    colsum = binimg.sum(0)
    segs = []
    ins = False; st = 0
    for i, c in enumerate(colsum):
        if c > 0 and not ins:
            ins = True; st = i
        elif c == 0 and ins:
            ins = False; segs.append((st, i))
    if ins:
        segs.append((st, len(colsum)))
    # 数字段宽度大致 2(小数点)~12; ℃ 段更宽(>14)且在最右, 丢弃
    out = []
    for s, e in segs:
        w = e - s
        if w <= 13:
            out.append((s, e))
    return out


def _norm_char(binimg, x0, x1):
    sub = binimg[:, x0:x1]
    # 去掉全空行
    rows = np.where(sub.sum(1) > 0)[0]
    if len(rows):
        sub = sub[rows.min():rows.max() + 1, :]
    return cv2.resize(sub.astype(np.uint8) * 255, (_CHAR_NORM[1], _CHAR_NORM[0]),
                      interpolation=cv2.INTER_NEAREST)


class RangeReader:
    def __init__(self, template_path=TEMPLATE_PATH):
        self.templates = None      # dict: char -> norm img (含 '0'..'9' 和 '.')
        if os.path.exists(template_path):
            data = np.load(template_path, allow_pickle=True)
            self.templates = {k: data[k] for k in data.files}
            logger.info("数字模板已加载: %s (%d 个)", template_path, len(self.templates))
        else:
            logger.warning("未找到数字模板 %s, 需先调用 build_templates() 自标定", template_path)

    # ---------------- 自标定: 用一帧已知读数建模板 ----------------
    def build_templates(self, frame_bgr, tmax_truth: str, tmin_truth: str,
                        save_path=TEMPLATE_PATH):
        """
        frame_bgr: 一帧画面
        tmax_truth/tmin_truth: 你肉眼读到的真值, 如 "50.1" "26.2" (不含 ℃)
        把上/下限框切出的字符与真值字符对齐, 收集模板。
        """
        templates = {}
        for box, truth in [(C.TMAX_TEXT_BOX, tmax_truth),
                           (C.TMIN_TEXT_BOX, tmin_truth)]:
            x, y, w, h = box
            crop = frame_bgr[y:y + h, x:x + w]
            b = _binarize(crop)
            segs = _segment_chars(b)
            chars = list(truth)  # e.g. ['5','0','.','1']
            if len(segs) != len(chars):
                logger.warning("自标定: 框%s 切出%d段 但真值%d字符, 跳过 (核对TMAX/TMIN_TEXT_BOX)",
                               box, len(segs), len(chars))
                continue
            for (x0, x1), ch in zip(segs, chars):
                templates[ch] = _norm_char(b, x0, x1)
        if templates:
            np.savez(save_path, **templates)
            self.templates = templates
            logger.info("数字模板已建立并保存: %s (%s)", save_path, "".join(sorted(templates)))
        else:
            logger.error("自标定失败, 未生成模板")
        return templates

    # ---------------- 读一个数字框 ----------------
    def _read_box(self, frame_bgr, box):
        if not self.templates:
            return None
        x, y, w, h = box
        crop = frame_bgr[y:y + h, x:x + w]
        b = _binarize(crop)
        segs = _segment_chars(b)
        if not segs:
            return None
        s = ""
        for x0, x1 in segs:
            ch = _norm_char(b, x0, x1)
            # 与每个模板比, 取最相似(归一化后逐像素一致率最高)
            best, best_score = None, -1.0
            for k, tmpl in self.templates.items():
                score = (ch == tmpl).mean()
                if score > best_score:
                    best_score, best = score, k
            if best_score < 0.6:   # 太不像, 读数不可信
                return None
            s += best
        try:
            return float(s)
        except ValueError:
            return None

    def read_range(self, frame_bgr):
        """返回 (t_max, t_min) 或 (None, None)。"""
        return (self._read_box(frame_bgr, C.TMAX_TEXT_BOX),
                self._read_box(frame_bgr, C.TMIN_TEXT_BOX))