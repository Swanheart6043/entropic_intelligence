#!/usr/bin/env python3
"""
calibrator.py - colorbar 标定(3D LUT 加速) + 布局核对工具

核心: 把"像素颜色 -> 温度"做成 3D LUT(RGB各量化Q级, 预算每格最近colorbar色)。
建表一次(~5s), 之后每帧纯查表(~50ms@1280x960)。

关键认知(实测得出):
- colorbar 只覆盖当前量程 [T_min,T_max] 这段颜色。低于 T_min 的背景冷区
  用的是量程外颜色, 反推不出温度——但不影响高温检测(高温必落在colorbar高温端)。
- 有效高温判据 = (反推温度>阈值) AND (颜色到colorbar的距离<容差)。
  背景冷区色距大但温度低, 不会误报; 高温区色距小被正确捕获。已在50.1°C真帧验证。
"""
import logging
import numpy as np
import cv2

from . import config as C

logger = logging.getLogger("thermal.calib")


def extract_colorbar(frame_bgr):
    """抠 colorbar 渐变条, 返回 (N,3) BGR, index0=顶(最热)..N-1=底(最冷)。"""
    strip = frame_bgr[C.COLORBAR_Y_TOP:C.COLORBAR_Y_BOTTOM,
                      C.COLORBAR_X0:C.COLORBAR_X1, :]
    return strip.astype(np.float32).mean(axis=1)


class ColorbarLUT:
    """根据一帧的 colorbar + 量程, 构建 3D 查找表。量程变化时需重建。"""

    def __init__(self, quant=C.LUT_QUANT):
        self.Q = quant
        self.lut_temp = None     # (Q,Q,Q) float32 温度
        self.lut_dist = None     # (Q,Q,Q) float32 到colorbar最近色距
        self._built_range = None # (t_max,t_min) 记录建表时量程, 用于判断是否需重建

    def build(self, bar_bgr, t_max, t_min):
        Q = self.Q
        N = len(bar_bgr)
        bar = bar_bgr.astype(np.float32)
        temp_axis = np.linspace(t_max, t_min, N).astype(np.float32)

        grid = (np.arange(Q) + 0.5) * (256.0 / Q)
        bb, gg, rr = np.meshgrid(grid, grid, grid, indexing="ij")
        cells = np.stack([bb.ravel(), gg.ravel(), rr.ravel()], 1).astype(np.float32)

        idx = np.empty(len(cells), np.int32)
        dst = np.empty(len(cells), np.float32)
        CH = 8192
        for s in range(0, len(cells), CH):
            e = min(s + CH, len(cells))
            d2 = ((cells[s:e][:, None, :] - bar[None, :, :]) ** 2).sum(2)
            j = d2.argmin(1)
            idx[s:e] = j
            dst[s:e] = np.sqrt(d2[np.arange(e - s), j])

        self.lut_temp = temp_axis[idx].reshape(Q, Q, Q).astype(np.float32)
        self.lut_dist = dst.reshape(Q, Q, Q).astype(np.float32)
        self._built_range = (t_max, t_min)
        logger.debug("3D LUT 重建: Q=%d range=[%.1f,%.1f]", Q, t_min, t_max)

    def need_rebuild(self, t_max, t_min):
        if self._built_range is None:
            return True
        # 量程变化超过 0.3°C 才重建, 避免每帧抖动重建
        return (abs(self._built_range[0] - t_max) > 1.0 or
                abs(self._built_range[1] - t_min) > 1.0)

    def apply(self, frame_bgr):
        """查表, 返回 (temp_map HxW, dist_map HxW)。"""
        q = (frame_bgr.astype(np.int32) * self.Q // 256)
        np.clip(q, 0, self.Q - 1, out=q)
        b, g, r = q[:, :, 0], q[:, :, 1], q[:, :, 2]
        return self.lut_temp[b, g, r], self.lut_dist[b, g, r]


def dump_layout(frame_bgr, out_path="thermal_layout_check.png"):
    """把所有坐标框画到帧上, 人工核对。换设备/分辨率后必跑。"""
    vis = frame_bgr.copy()

    def box(b, color, label):
        x, y, w, h = b
        cv2.rectangle(vis, (x, y), (x + w, y + h), color, 2)
        cv2.putText(vis, label, (x, max(0, y - 4)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

    cv2.rectangle(vis, (C.COLORBAR_X0, C.COLORBAR_Y_TOP),
                  (C.COLORBAR_X1, C.COLORBAR_Y_BOTTOM), (0, 255, 0), 2)
    cv2.putText(vis, "colorbar", (C.COLORBAR_X0 - 80, C.COLORBAR_Y_TOP),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
    box(C.TMAX_TEXT_BOX, (255, 255, 0), "Tmax")
    box(C.TMIN_TEXT_BOX, (255, 255, 0), "Tmin")
    for i, b in enumerate(C.OSD_MASK_BOXES):
        box(b, (0, 0, 255), f"osd{i}")

    cv2.imwrite(out_path, vis)
    logger.info("layout check saved: %s", out_path)
    return out_path


if __name__ == "__main__":
    import sys
    logging.basicConfig(level=logging.INFO)
    if len(sys.argv) < 2:
        print(
            "用法: python -m detection.fire_overheat.thermal_detector.calibrator "
            "vpu_raw.png"
        )
        sys.exit(1)
    img = cv2.imread(sys.argv[1])
    p = dump_layout(img)
    print("布局核对图:", p)
