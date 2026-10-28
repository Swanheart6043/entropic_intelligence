// FOC 算法适配器；硬件和业务层通过 motor_controller 调度
#pragma once
#include "motor_algorithm.h"

extern const motor_algorithm_ops_t foc_algorithm;
