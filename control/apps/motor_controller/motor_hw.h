/**
 * @file motor_hw.h
 * @brief 电机硬件端口：采样、PWM 和控制事件生命周期。
 */
#pragma once
#include <stdbool.h>
#include "motor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化外设；不依赖具体算法、不启动功率输出。 */
void motor_hw_init(void);
/* 普通上下文调用；校准前必须停止控制，失败不得启动输出。 */
bool motor_hw_calibrate_currents(void);
bool motor_hw_start(void);
void motor_hw_stop(void);
/* ISR 可调用：立即关输出和控制事件，不调用阻塞 HAL 操作。 */
void motor_hw_emergency_stop(void);
/* 仅在本次 ADC 注入转换完成时返回 true，同时确认中断标志。 */
bool motor_hw_read_sample(motor_sample_t *sample);
/* 三相互补 PWM 输出；就地限幅后写入硬件，反馈实际提交的占空比。 */
void motor_hw_write_pwm(motor_pwm_t *pwm);
uint32_t motor_hw_now_ms(void);
uint32_t motor_hw_critical_enter(void);
void motor_hw_critical_exit(uint32_t key);

#ifdef __cplusplus
}
#endif
