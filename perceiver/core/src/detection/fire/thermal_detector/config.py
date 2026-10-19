"""
热成像测温检测配置 (大华 DH-TPC-AT2201, 1280x960 VPU 原图坐标系)

坐标已用真实 VPU 原图 (1280x960) 量准。若更换设备/分辨率/OSD布局, 用
calibrator.py 的 dump_layout() 重新核对。
"""

# ---- 画面尺寸 (VPU 原图, read_bgr 输出) ----
FRAME_W = 1280
FRAME_H = 960

# ---- colorbar 渐变条 (抠"颜色->温度"映射用) ----
# 实测: 顶端 y=234 (最热, 黄白), 底端 y=700 (最冷, 紫). 取中心列平均降噪。
COLORBAR_X0 = 1196
COLORBAR_X1 = 1205          # 取 [X0,X1) 列平均
COLORBAR_Y_TOP = 234        # 对应 T_max
COLORBAR_Y_BOTTOM = 700     # 对应 T_min

# ---- colorbar 上/下限温度数字裁剪框 (模板匹配读数, 应对自动量程) ----
# 形如 "50.1℃" / "26.2℃"。实测上限数字白像素 x∈[1119,1193] y∈[194,212]。
# 框略放大留余量 (x, y, w, h)
TMAX_TEXT_BOX = (1105, 190, 110, 26)   # colorbar 上方 上限数字
TMIN_TEXT_BOX = (1105, 700, 110, 26)   # colorbar 下方 下限数字 (y按对称估, 首次运行用dump核对)

# ---- 量程读数兜底 ----
RANGE_READ_MAX_STALE_FRAMES = 30   # 连续读不到量程, 沿用旧值的最大帧数
RANGE_SANITY_MIN = -40.0           # 读出值超此范围视为错误丢弃
RANGE_SANITY_MAX = 600.0
RANGE_MIN_SPAN = 2.0               # T_max - T_min 至少这么大, 否则视为读数异常

# ---- OSD 屏蔽区 (叠加文字/图标, 非真实温度, 检测前置 0) ----
# (x, y, w, h)。白色高温文字最易误报, 框宁大勿漏。
OSD_MASK_BOXES = [
    (860, 60, 380, 80),      # 右上 时间戳
    (20, 865, 160, 70),      # 左下 "热成像" 水印
    (1085, 180, 160, 540),   # 右侧 colorbar 整条 + 上下限数字
]
# 画面内测温点标注 ("◇ 50.1℃" 白字, 位置随设备测温点移动, 无法固定框):
# 不固定屏蔽, 靠 MIN_BLOB_AREA + 白色文字形态过滤 (见 detector)。

# ---- 检测参数 ----
TEMP_THRESHOLD = 60.0      # 高温阈值 °C
MIN_BLOB_AREA = 120        # 连通域最小面积(像素), 滤文字/噪点
DILATE_KERNEL = 7          # mask 膨胀核, 连接断裂高温块(调大可合并碎框)
NN_MATCH_TIE_TOL = 6.0     # 最近邻匹配色距阈值: 超过则该像素判为"非colormap色"(OSD残留等), 不计温度

# ---- 标定缓存 ----
LUT_CACHE_PATH = "thermal_colorbar_lut.npz"

# ---- 3D LUT 量化级数 (每通道) ----
# 64级: 建表~5s, 查表~50ms@1280x960, 精度足够。RDK可调小到48提速。
LUT_QUANT = 64

# ---- 高温有效判据容差 ----
# 像素到 colorbar 的颜色距离 < 此值, 该像素温度才可信。
# 实测 20 时高温区捕获干净、背景冷区不误报。
HOT_DIST_TOL = 20.0

# ---- 框合并 ----
# 连通域得到的框之间间距 <= MERGE_GAP 像素就合并 (同一热源被切块时粘回一个)。
# 调大 -> 合并更激进, 框更少更大; 调小 -> 保留更多独立小框。
MERGE_GAP = 25