# 电机控制模块

当前为单电机实例，默认注册 FOC（电流闭环、电角度开环的 I-F 强拖）。
`six_step/six_step.c` 是尚未实现的接入位置，选择 `MOTOR_ALGORITHM_SIX_STEP`
会返回 `MOTOR_RESULT_UNSUPPORTED`，不会替换当前算法或启动输出。

## 职责

| 文件 | 职责 |
| --- | --- |
| `motor_controller.c/.h` | 对外启停、算法选择、命令、状态快照和公共故障保护 |
| `motor_algorithm.h` | 算法适配接口：初始化、启停、快速计算、慢任务、命令及状态 |
| `motor_types.h` | 不依赖 HAL 的采样、输出、状态和命令类型 |
| `motor_hw.c/.h` | TIM1/ADC1、零点校准、采样换算、PWM 写入、急停和事件清理 |
| `motor_hw_config.h` | 当前板的 PWM 时序、ADC 电路参数和采样窗口限制 |
| `motor_config.h` | 电机参数、过流阈值和默认算法 |
| `foc/` | Clarke/Park、PI、反 Park、SVPWM 和 ALIGN/RUN 算法状态 |

业务层只包含 `motor_controller.h`。`motor_hw` 不引用任何算法。
FOC 不包含 HAL、不读取 ADC、不写 TIM，也不管理硬件启停。

## 调用与时序

1. 系统时钟初始化后调用 `motor_controller_init()`，再调用 `motor_controller_start()`。
2. 启动时保持 PWM 关闭，完成电流零点校准；成功后准备算法，重新使能 ADC，再开启 TIM 输出。
3. `ADC1_2_IRQHandler()` 调用 `motor_controller_adc_isr()`：确认 ADC 事件、读取采样、检查过流、计算并提交 PWM。
4. 主循环尽量每 1ms 调用 `motor_controller_task()`；慢任务使用实际经过的毫秒数，重复调用同一 tick 不会推进斜坡。
5. 停止时先无条件关闭主输出、计数器和控制事件，再完成 ADC HAL 状态及待处理中断清理。

对外状态为 `IDLE → CALIBRATING → STARTING → RUNNING`。FOC 的 `STARTING`
对应转子预定位。当前维持 20kHz 快环、1s 预定位和默认 3s 速度斜坡。
母线电压仍采用板级配置值，尚未增加电压传感器。

除 ADC ISR 入口外，所有公共函数都从同一个主循环或控制任务调用。
主循环与 ISR 共享的算法更新使用短临界区，校准和 ADC 启停等阻塞操作在临界区外执行。
`motor_controller_get_status()` 返回一致的状态快照，不暴露可写算法上下文。

## 命令

```c
motor_command_t speed = {
    .type = MOTOR_COMMAND_OPEN_LOOP_SPEED,
    .value = 100.0f  /* 强制电角速度 rad/s，不是机械 rpm 或速度反馈闭环 */
};
motor_result_t result = motor_controller_set_command(&speed);
```

当前 FOC 支持：

- `MOTOR_COMMAND_OPEN_LOOP_SPEED`：`0..OMEGA_TARGET_RAD` 电角速度 rad/s，按配置斜率接近目标。
- `MOTOR_COMMAND_Q_CURRENT`：q 轴电流 A，绝对值必须小于 `OVERCURRENT_A`；d 轴给定仍由 FOC 配置。

命令可在 IDLE 或运行时更新。普通停止保留给定；重新选择算法和清除故障会恢复算法默认给定。
无效数值返回 `INVALID_ARGUMENT`，未支持的命令返回 `UNSUPPORTED`。

## 故障与算法切换

过流、校准失败、硬件启动失败或非有限输出都会进入 `FAULT`。
ISR 快路立即关断输出，阻塞式 ADC 清理由普通上下文完成。
`motor_controller_stop()` 保持故障锁存；必须显式调用
`motor_controller_clear_fault()`，随后才能再次启动。清除故障本身不会启动输出。

算法只允许在无故障的 IDLE 状态切换：

```c
motor_controller_stop();
motor_result_t result = motor_controller_select_algorithm(MOTOR_ALGORITHM_FOC);
if (result == MOTOR_RESULT_OK) {
    result = motor_controller_start();
}
```

这表示控制事件已经停止；重新启动前，调用者仍需确认转子处于算法允许的启动条件。
第一版不提供运行中的平滑切换。

## 接入新算法或框架

1. 在独立目录实现 `motor_algorithm_ops_t`。算法内部管理自身计算状态，硬件操作留在端口层。
2. 在 `motor_controller.c` 的 `find_algorithm()` 注册，并在工程 CMake 中加入源文件。
3. 定义支持的命令、单位、反馈需求和启动条件。按需修改 `MOTOR_DEFAULT_ALGORITHM`。
4. 当前快速输出合同是三相互补 PWM。实现六步时，应一起扩展输出类型和 `motor_hw`，明确逐相悬空、导通和换相状态；不能用三相 duty 模拟所有相态。
5. 如果算法需要霍尔边沿、换相定时器或不同 ADC/PWM 时序，增加相应硬件事件和配置适配，避免把算法直接绑定到某个芯片中断。

有感/无感 FOC 可以继续在 `foc/` 内拆分角度来源；电流、速度、位置控制模式也应与算法体系分别组织。
接入其他控制框架时，使用适配器处理其生命周期、外设和调度要求。
