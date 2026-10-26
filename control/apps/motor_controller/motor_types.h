/** @brief 算法、控制器和硬件层共用的数据类型，不依赖 STM32 HAL。 */
#pragma once
#include <stdint.h>

typedef enum {
    MOTOR_ALGORITHM_FOC = 0,
    MOTOR_ALGORITHM_SIX_STEP
} motor_algorithm_id_t;

typedef enum {
    MOTOR_STATE_UNINITIALIZED = 0,
    MOTOR_STATE_IDLE,
    MOTOR_STATE_CALIBRATING,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_FAULT
} motor_state_t;

typedef enum {
    MOTOR_FAULT_NONE = 0,
    MOTOR_FAULT_OVERCURRENT_U,
    MOTOR_FAULT_OVERCURRENT_V,
    MOTOR_FAULT_OVERCURRENT_W,
    MOTOR_FAULT_CURRENT_CALIBRATION,
    MOTOR_FAULT_HARDWARE_START,
    MOTOR_FAULT_INVALID_OUTPUT
} motor_fault_t;

typedef enum {
    MOTOR_RESULT_OK = 0,
    MOTOR_RESULT_NOT_INITIALIZED,
    MOTOR_RESULT_BUSY,
    MOTOR_RESULT_UNSUPPORTED,
    MOTOR_RESULT_INVALID_ARGUMENT,
    MOTOR_RESULT_FAULT
} motor_result_t;

typedef struct {
    float iu, iv, iw;          /* A */
    float vbus;                /* V */
    float dt_s;                /* 实际配置的快速控制周期，s */
} motor_sample_t;

typedef struct {
    float duty_u, duty_v, duty_w; /* 0..1 */
} motor_pwm_t;

typedef enum {
    MOTOR_COMMAND_OPEN_LOOP_SPEED = 0, /* 强制电角速度，rad/s；非转速反馈闭环 */
    MOTOR_COMMAND_Q_CURRENT            /* q 轴电流给定，A */
} motor_command_type_t;

typedef struct {
    motor_command_type_t type;
    float value;
} motor_command_t;

typedef struct {
    motor_algorithm_id_t algorithm;
    motor_state_t state;
    motor_fault_t fault;
    motor_sample_t sample;
    motor_pwm_t pwm;
} motor_status_t;
