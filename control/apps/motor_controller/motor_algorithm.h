/** @brief 控制算法适配接口。仅由 motor_controller 调度，不直接操作外设。 */
#pragma once
#include "motor_types.h"

typedef struct {
    void (*init)(void);
    void (*start)(uint32_t now_ms);
    void (*stop)(void);
    void (*fast_step)(const motor_sample_t *sample, motor_pwm_t *pwm);
    void (*task)(uint32_t now_ms, uint32_t dt_ms);
    motor_state_t (*get_state)(void);
    motor_result_t (*set_command)(const motor_command_t *command);
} motor_algorithm_ops_t;
