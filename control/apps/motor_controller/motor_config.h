/** @brief 电机参数和跨算法共用的保护设置。 */
#pragma once
#include "motor_types.h"

#ifndef MOTOR_DEFAULT_ALGORITHM
#define MOTOR_DEFAULT_ALGORITHM MOTOR_ALGORITHM_FOC
#endif

#define MOTOR_POLE_PAIRS    7U
#define MOTOR_RS_OHM        2.0f
#define MOTOR_LS_H          0.001f
#define OVERCURRENT_A      3.0f
