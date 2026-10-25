/**
 * @file motor_hw.h
 * @brief 电机硬件初始化接口。
 */
#ifndef MOTOR_HW_H
#define MOTOR_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化电机专用外设并绑定 FOC；不启动 PWM，随后调用 FOC_Start。 */
void Motor_HW_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_HW_H */
