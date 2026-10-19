#!/usr/bin/env python3
"""
thermal_detector.py - 热成像高温目标检测主模块

每帧:
  read_range -> T_max,T_min (自动量程, 模板匹配, 带兜底)
  量程变化时重建 3D LUT; 否则复用
  apply LUT -> temp_map + dist_map
  hot = (temp_map>阈值) AND (dist<容差) AND OSD屏蔽
  连通域 + 面积过滤 -> 高温目标框
接口对齐 HWDecodeCapture.read_bgr() 的 BGR 输出。
"""
import logging
import numpy as np
import cv2

from . import config as C
from . import calibrator as calib
from .range_reader import RangeReader

logger = logging.getLogger("thermal.detect")


def _boxes_close(a, b, gap):
    """两个 (x,y,w,h) 框是否重叠或间距 <= gap (按膨胀后相交判断)。"""
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax > bx + bw + gap or bx > ax + aw + gap or
                ay > by + bh + gap or by > ay + ah + gap)


def _union_box(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    x0 = min(ax, bx); y0 = min(ay, by)
    x1 = max(ax + aw, bx + bw); y1 = max(ay + ah, by + bh)
    return (x0, y0, x1 - x0, y1 - y0)


def _merge_detections(dets, gap):
    """迭代合并重叠/邻近的 Detection。合并框温度取最高、面积累加、质心取最热那个。"""
    if len(dets) <= 1:
        return dets
    items = [{"bbox": d.bbox, "max_temp": d.max_temp, "area": d.area,
              "centroid": d.centroid} for d in dets]
    changed = True
    while changed:
        changed = False
        out = []
        while items:
            cur = items.pop()
            merged = True
            while merged:
                merged = False
                rest = []
                for it in items:
                    if _boxes_close(cur["bbox"], it["bbox"], gap):
                        cur = {
                            "bbox": _union_box(cur["bbox"], it["bbox"]),
                            "max_temp": max(cur["max_temp"], it["max_temp"]),
                            "area": cur["area"] + it["area"],
                            "centroid": cur["centroid"] if cur["max_temp"] >= it["max_temp"] else it["centroid"],
                        }
                        merged = True; changed = True
                    else:
                        rest.append(it)
                items = rest
            out.append(cur)
        items = out
    return [Detection(it["bbox"], it["max_temp"], it["area"], it["centroid"])
            for it in items]


class Detection:
    __slots__ = ("bbox", "max_temp", "area", "centroid")
    def __init__(self, bbox, max_temp, area, centroid):
        self.bbox = bbox
        self.max_temp = max_temp
        self.area = area
        self.centroid = centroid
    def __repr__(self):
        return f"<Det bbox={self.bbox} max={self.max_temp:.1f}C area={self.area}>"


class ThermalDetector:
    def __init__(self):
        self.reader = RangeReader()
        self.lut = calib.ColorbarLUT()
        self._osd_mask = np.ones((C.FRAME_H, C.FRAME_W), dtype=bool)
        for (x, y, w, h) in C.OSD_MASK_BOXES:
            self._osd_mask[y:y+h, x:x+w] = False
        self._last_tmax = None
        self._last_tmin = None
        self._stale = 0
        self._dk = cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (C.DILATE_KERNEL, C.DILATE_KERNEL))

    def _get_range(self, frame):
        tmax, tmin = self.reader.read_range(frame)
        ok = (tmax is not None and tmin is not None
              and C.RANGE_SANITY_MIN <= tmin < tmax <= C.RANGE_SANITY_MAX
              and (tmax - tmin) >= C.RANGE_MIN_SPAN)
        if ok:
            self._last_tmax, self._last_tmin = tmax, tmin
            self._stale = 0
            return tmax, tmin
        self._stale += 1
        if self._last_tmax is not None and self._stale <= C.RANGE_READ_MAX_STALE_FRAMES:
            return self._last_tmax, self._last_tmin
        return None, None

    def prepare_lut(self, frame_bgr):
        """Build the LUT for the current range, regardless of alarm threshold."""
        if frame_bgr is None or frame_bgr.shape[:2] != (C.FRAME_H, C.FRAME_W):
            return {
                "ok": False,
                "reason": "frame None or wrong size",
                "rebuilt": False,
            }

        t_max, t_min = self._get_range(frame_bgr)
        if t_max is None:
            return {
                "ok": False,
                "reason": "range read failed",
                "rebuilt": False,
            }

        rebuilt = self.lut.need_rebuild(t_max, t_min)
        if rebuilt:
            bar = calib.extract_colorbar(frame_bgr)
            self.lut.build(bar, t_max, t_min)

        return {
            "ok": True,
            "rebuilt": rebuilt,
            "t_max": t_max,
            "t_min": t_min,
        }

    def detect(self, frame_bgr):
        if frame_bgr is None or frame_bgr.shape[:2] != (C.FRAME_H, C.FRAME_W):
            return {"ok": False, "reason": "frame None or wrong size", "detections": []}

        t_max, t_min = self._get_range(frame_bgr)
        if t_max is None:
            return {"ok": False, "reason": "range read failed", "detections": []}

        # 自动量程: 上限都不到阈值, 说明全场低温, 不可能有>阈值目标
        if t_max < C.TEMP_THRESHOLD:
            return {"ok": True, "t_max": t_max, "t_min": t_min,
                    "frame_max_temp": t_max, "detections": []}

        # colorbar + LUT (量程变了才重建)
        if self.lut.need_rebuild(t_max, t_min):
            bar = calib.extract_colorbar(frame_bgr)
            self.lut.build(bar, t_max, t_min)

        temp_map, dist_map = self.lut.apply(frame_bgr)

        hot = ((temp_map > C.TEMP_THRESHOLD) &
               (dist_map < C.HOT_DIST_TOL) &
               self._osd_mask).astype(np.uint8)

        # 全帧最高可信温度(供上报参考)
        trust = (dist_map < C.HOT_DIST_TOL) & self._osd_mask
        frame_max = float(temp_map[trust].max()) if trust.any() else t_min

        if hot.sum() == 0:
            return {"ok": True, "t_max": t_max, "t_min": t_min,
                    "frame_max_temp": frame_max, "detections": []}

        hot = cv2.dilate(hot, self._dk, iterations=1)
        n, labels, stats, cents = cv2.connectedComponentsWithStats(hot, connectivity=8)

        dets = []
        for i in range(1, n):
            area = int(stats[i, cv2.CC_STAT_AREA])
            if area < C.MIN_BLOB_AREA:
                continue
            x = int(stats[i, cv2.CC_STAT_LEFT]); y = int(stats[i, cv2.CC_STAT_TOP])
            w = int(stats[i, cv2.CC_STAT_WIDTH]); h = int(stats[i, cv2.CC_STAT_HEIGHT])
            sub = labels[y:y+h, x:x+w] == i
            region = temp_map[y:y+h, x:x+w][sub]
            mx = float(region.max()) if region.size else frame_max
            dets.append(Detection((x, y, w, h), mx, area,
                                  (float(cents[i][0]), float(cents[i][1]))))

        # 合并相互重叠/邻近的框 (同一热源被连通域切成多块时, 粘回一个)
        dets = _merge_detections(dets, gap=C.MERGE_GAP)

        return {"ok": True, "t_max": t_max, "t_min": t_min,
                "frame_max_temp": frame_max, "detections": dets}

    @staticmethod
    def draw(frame_bgr, result):
        vis = frame_bgr.copy()
        for d in result.get("detections", []):
            x, y, w, h = d.bbox
            cv2.rectangle(vis, (x, y), (x+w, y+h), (0, 0, 255), 2)
            cv2.putText(vis, f"{d.max_temp:.1f}C", (x, max(0, y-6)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
        return vis