/**
 * @brief 电机控制统一入口。当前为单电机实例。
 * 除 adc_isr 外，接口都从同一个主循环/任务调用，不支持任务间并发调用。
 */
#pragma once
#include "motor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

motor_result_t motor_controller_init(void);
/* 只允许 IDLE 时切换；未实现的算法返回 UNSUPPORTED，保留当前选择。 */
motor_result_t motor_controller_select_algorithm(motor_algorithm_id_t algorithm);
motor_result_t motor_controller_start(void);
/* 停止输出及控制事件；故障仍锁存。 */
void motor_controller_stop(void);
/* 显式完成停机清理并清除故障；不会自动启动。 */
motor_result_t motor_controller_clear_fault(void);
motor_result_t motor_controller_set_command(const motor_command_t *command);
void motor_controller_get_status(motor_status_t *status);
/* 尽量每 1ms 调用，内部按真实经过的毫秒数调度。 */
void motor_controller_task(void);
/* ADC 硬件事件入口，采样后调度当前算法。 */
void motor_controller_adc_isr(void);

#ifdef __cplusplus
}
#endif
