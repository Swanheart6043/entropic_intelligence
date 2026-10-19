# 热成像高温目标检测 (thermal)

纯 CV 方案,无模型。针对大华 DH-TPC-AT2201(固件 2.630,测温 HTTP API 不可用),
通过 RTSP 伪彩画面反推温度,实时检测 >70°C 目标。放在 RDK 感知层(有 VPU)。

## 原理
设备测温接口(RadiometryManager/TemperCorrection)在该固件返回 400 不可用,
事件订阅可用但配规则受阻(web 前端坏)。故走伪彩反推:
1. 模板匹配读 colorbar 上/下限数字 → 当前量程 T_max/T_min(应对自动量程)
2. 抠 colorbar 渐变条 → 颜色↔温度映射,建 3D LUT(RGB各量化64级)
3. 全画面查表 → 温度图;`(温度>阈值) AND (颜色距colorbar<容差) AND OSD屏蔽` → 高温mask
4. 连通域 + 面积过滤 → 高温目标框,命中抓图上报

**关键认知**:colorbar 只覆盖当前量程颜色,低温背景反推不出温度(无所谓,
本就 <阈值);高温必落在 colorbar 高温端,组合判据保证背景不误报、高温被捕获。
已在真实 50.1°C 帧验证。

## 性能(1280×960)
- 建 LUT:~5s(仅量程变化时重建)
- 每帧查表+检测:~50ms(~20fps),热成像监控足够

## 文件
- `config.py` — 所有坐标/阈值常量(已用真原图量准;换设备需重新核对)
- `calibrator.py` — colorbar 抠取 + 3D LUT + 布局核对工具
- `range_reader.py` — colorbar 温度数字模板匹配读数
- `thermal_detector.py` — 主检测
- `thermal_worker.py` — 接入 DetectionWorker 框架的 worker(生产用)
- `calibrate.py` — 离线标定工具(换设备/分辨率时跑;生产运行不需要)
- `thermal_digit_templates.npz` — 已自标定的数字模板(本设备字体)

## 用法

### 生产运行 (接 patrol_robot)
不单独跑本包。在主程序里:
```python
from common.frame_hub import FrameHub
from detection.fire_overheat.thermal_detector.thermal_thread import ThermalWorker

# 热成像独立 hub (dec_chn 必须和可见光 hub 错开!)
thermal_hub = FrameHub("rtsp://admin:heyn_888@10.10.10.41:554/cam/realmonitor?channel=1&subtype=0", dec_chn=1)
thermal_hub.start()

tw = ThermalWorker(thermal_hub, zmq_sender)
tw.warmup()    # 可选: 出帧后预热建LUT, 避免运行中首帧卡~5s
tw.start()
```

### 标定 (换设备/批次/分辨率时)
```bash
# 用一帧 VPU 原图(cv2.imwrite存的,非播放器截图) + 该帧肉眼读到的量程真值
python -m detection.fire_overheat.thermal_detector.calibrate --frame vpu_raw.png --tmax 50.1 --tmin 26.2
# 生成 thermal_digit_templates.npz(数字模板) 和 thermal_layout_check.png(坐标核对)
# 检查核对图: 绿框贴colorbar, Tmax/Tmin框套住数字, 红框盖住OSD文字
```

## 集成到 patrol_robot
放在 `core/src/detection/fire_overheat/thermal_detector/`，作为火焰/过热融合模块的热成像子模块。
取帧、`common/zmq_sender.py` 上报。协调层只收结构化报警+jpg,不碰视频/温度计算。

## 注意 / 局限
- **精度**:伪彩反推非真测温,绝对误差约 ±2-3°C(量化+渲染)。对 >70 阈值判断够用;
  生产可把阈值留 margin(如判 68 触发 + 多帧确认)。
- **量程读数是命门**:每帧靠模板匹配读 colorbar 数字。读失败有兜底(沿用旧值30帧),
  超时跳过本帧并告警(宁漏一帧不用错量程误报)。
- **OSD 白色文字**:时间戳/水印已固定屏蔽;画面内测温点标注(随测温点移动)靠面积过滤。
- 换分辨率/OSD布局/colormap:重跑 `--calib` 并核对 config 坐标。
