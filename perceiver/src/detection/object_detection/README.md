# 共享通用目标检测

`ObjectDetectionWorker` 在高清 `FrameHub` 上统一运行通用 YOLO，并发布只保留最新一帧的 `ObjectDetectionSnapshot`。

快照同时包含 `frame_id`、时间戳、原始帧和目标框，业务 Worker 必须使用快照中的帧处理对应检测框，避免机器人运动时跨帧 ROI 错位。

当前消费者：

- `SmokingWorker`
- `ReflectiveVestWorker`
- `SafetyHelmetWorker`
- `WaterWorker`
- `BreakingIntoPenaltyAreaWorker`
- `CatsAndDogsWorker`

`ObjectDetectionWorker` 属于内部基础设施，会随任一消费者初始化，但不会注册到动态控制表。关闭某个业务检测只暂停对应二阶段 Worker，不会影响其他消费者。第一版在消费者全部关闭后仍保持通用检测运行；后续如需进一步节省算力，可在 `DetectionManager` 中增加依赖引用计数。

通用快照保留全部 COCO 类别，可供后续车辆、违停、猫狗和车牌 ROI 等任务复用。`track_id` 当前为空，后续接入共享多目标跟踪时可以直接填充。
