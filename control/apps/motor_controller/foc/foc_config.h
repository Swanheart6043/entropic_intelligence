/** @brief FOC 电流环及 I-F 强拖参数，板级配置见 motor_hw_config.h。 */
#pragma once
#include "motor_config.h"

/* 连续域电流环参数；离散积分使用每次采样提供的 dt_s。 */
#define CURR_LOOP_BW        800.0f
#define PI_KP               (MOTOR_LS_H * CURR_LOOP_BW)
#define PI_KI               (MOTOR_RS_OHM * CURR_LOOP_BW)

#define ALIGN_CURRENT_A     0.4f
#define ALIGN_TIME_MS       1000U
#define OMEGA_START_RAD     20.0f
#define OMEGA_TARGET_RAD    150.0f
#define OMEGA_RAMP_TIME_MS  3000U
#define OMEGA_RAMP_RAD_S2   ((OMEGA_TARGET_RAD - OMEGA_START_RAD) * 1000.0f / (float)OMEGA_RAMP_TIME_MS)
#define ID_REF_A            0.0f
#define IQ_REF_A            0.6f
