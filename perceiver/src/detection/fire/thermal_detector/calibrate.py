#!/usr/bin/env python3
"""
calibrate.py - 热成像标定工具 (换设备/批次/分辨率时跑)

生产运行不需要本文件 (那是 ThermalWorker + 主程序的事)。
本文件只做两件离线标定的事:
  1. 自标定 colorbar 温度数字模板 -> thermal_digit_templates.npz
  2. 把 colorbar/OSD 坐标框画到帧上 -> thermal_layout_check.png 供人工核对

用法:
  # 用一帧 VPU 原图 (cv2.imwrite 存的, 非播放器截图) + 该帧肉眼读到的量程真值
  python -m detection.fire_overheat.thermal_detector.calibrate --frame vpu_raw.png --tmax 50.1 --tmin 26.2

  # 只核对布局, 不重建模板
  python -m detection.fire_overheat.thermal_detector.calibrate --frame vpu_raw.png --layout-only
"""
import argparse
import logging
import cv2

from .range_reader import RangeReader
from . import calibrator

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log = logging.getLogger("calibrate")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", required=True, help="VPU 原图 (cv2.imwrite 存的, 1280x960)")
    ap.add_argument("--tmax", help="该帧 colorbar 上限真值, 如 50.1")
    ap.add_argument("--tmin", help="该帧 colorbar 下限真值, 如 26.2")
    ap.add_argument("--layout-only", action="store_true", help="只画布局核对图, 不重建数字模板")
    a = ap.parse_args()

    frame = cv2.imread(a.frame)
    if frame is None:
        log.error("读图失败: %s", a.frame)
        return

    # 布局核对图 (总是生成)
    p = calibrator.dump_layout(frame)
    log.info("布局核对图: %s -> 检查绿框是否贴住colorbar, Tmax/Tmin框是否套住数字, 红框是否盖住OSD文字", p)

    if a.layout_only:
        return

    if not (a.tmax and a.tmin):
        log.error("重建数字模板需要 --tmax 和 --tmin (该帧肉眼读到的量程真值)")
        return

    r = RangeReader()
    r.build_templates(frame, a.tmax, a.tmin)

    # 自检: 用刚建的模板回读, 确认能读对
    r2 = RangeReader()
    tmax, tmin = r2.read_range(frame)
    if tmax == float(a.tmax) and tmin == float(a.tmin):
        log.info("✅ 模板自检通过: 回读 tmax=%s tmin=%s 与真值一致", tmax, tmin)
    else:
        log.warning("⚠️ 模板自检不符: 回读 tmax=%s tmin=%s, 真值 %s/%s. "
                    "核对 TMAX/TMIN_TEXT_BOX 坐标或字体是否变化", tmax, tmin, a.tmax, a.tmin)


if __name__ == "__main__":
    main()
