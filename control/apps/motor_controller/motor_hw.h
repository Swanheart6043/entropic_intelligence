/**
 * @file motor_hw.h
 * @brief 电机硬件初始化接口。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化电机专用外设并绑定 FOC；不启动 PWM，随后调用 foc_start。 */
void motor_hw_init(void);

#ifdef __cplusplus
}
#endif
