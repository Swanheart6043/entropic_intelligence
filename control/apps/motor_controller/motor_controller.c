/** @brief 单电机控制器：算法调度、公共保护和控制事件生命周期。 */
#include "motor_controller.h"
#include "motor_algorithm.h"
#include "motor_config.h"
#include "motor_hw.h"
#include "foc/foc.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static const motor_algorithm_ops_t *volatile s_algorithm;
static volatile motor_status_t s_status;
static volatile bool s_hardware_active;
static uint32_t s_last_task_ms;

static const motor_algorithm_ops_t *find_algorithm(motor_algorithm_id_t id)
{
    switch (id) {
        case MOTOR_ALGORITHM_FOC: return &foc_algorithm;
        /* SIX_STEP 未实现，不能以空实现启动功率输出。 */
        default: return NULL;
    }
}

static void latch_fault(motor_fault_t fault)
{
    uint32_t key = motor_hw_critical_enter();
    motor_hw_emergency_stop();
    if (s_status.fault == MOTOR_FAULT_NONE) s_status.fault = fault;
    s_status.state = MOTOR_STATE_FAULT;
    s_status.pwm = (motor_pwm_t){0};
    motor_hw_critical_exit(key);
}

motor_result_t motor_controller_init(void)
{
    if (s_algorithm != NULL) return MOTOR_RESULT_BUSY;

    const motor_algorithm_ops_t *algorithm = find_algorithm(MOTOR_DEFAULT_ALGORITHM);
    if (algorithm == NULL) return MOTOR_RESULT_UNSUPPORTED;

    motor_hw_init();
    algorithm->init();
    s_status = (motor_status_t){
        .algorithm = MOTOR_DEFAULT_ALGORITHM,
        .state = MOTOR_STATE_IDLE
    };
    s_last_task_ms = motor_hw_now_ms();
    s_algorithm = algorithm;
    return MOTOR_RESULT_OK;
}

motor_result_t motor_controller_select_algorithm(motor_algorithm_id_t id)
{
    if (s_algorithm == NULL) return MOTOR_RESULT_NOT_INITIALIZED;
    if (s_status.state == MOTOR_STATE_FAULT) return MOTOR_RESULT_FAULT;
    if (s_status.state != MOTOR_STATE_IDLE) return MOTOR_RESULT_BUSY;

    const motor_algorithm_ops_t *algorithm = find_algorithm(id);
    if (algorithm == NULL) return MOTOR_RESULT_UNSUPPORTED;

    /* IDLE 保证控制事件已关闭，新算法从干净的私有状态开始。 */
    motor_hw_stop();
    s_algorithm->stop();
    algorithm->init();
    uint32_t key = motor_hw_critical_enter();
    s_algorithm = algorithm;
    s_status.algorithm = id;
    s_status.sample = (motor_sample_t){0};
    s_status.pwm = (motor_pwm_t){0};
    motor_hw_critical_exit(key);
    return MOTOR_RESULT_OK;
}

motor_result_t motor_controller_start(void)
{
    if (s_algorithm == NULL) return MOTOR_RESULT_NOT_INITIALIZED;
    if (s_status.state == MOTOR_STATE_FAULT) return MOTOR_RESULT_FAULT;
    if (s_status.state != MOTOR_STATE_IDLE) return MOTOR_RESULT_BUSY;

    motor_hw_stop();
    s_status.state = MOTOR_STATE_CALIBRATING;
    if (!motor_hw_calibrate_currents()) {
        latch_fault(MOTOR_FAULT_CURRENT_CALIBRATION);
        return MOTOR_RESULT_FAULT;
    }

    uint32_t now = motor_hw_now_ms();
    s_algorithm->start(now);
    s_last_task_ms = now;
    s_status.sample = (motor_sample_t){0};
    s_status.pwm = (motor_pwm_t){0};
    /* 先发布算法状态，再启动采样，避免首个 ISR 看到未准备好的算法。 */
    s_status.state = s_algorithm->get_state();
    s_hardware_active = true;
    if (!motor_hw_start()) {
        latch_fault(MOTOR_FAULT_HARDWARE_START);
        motor_hw_stop();
        s_hardware_active = false;
        return MOTOR_RESULT_FAULT;
    }
    return s_status.state == MOTOR_STATE_FAULT ? MOTOR_RESULT_FAULT : MOTOR_RESULT_OK;
}

void motor_controller_stop(void)
{
    if (s_algorithm == NULL) return;
    motor_hw_stop();
    s_hardware_active = false;
    uint32_t key = motor_hw_critical_enter();
    s_algorithm->stop();
    s_status.pwm = (motor_pwm_t){0};
    s_status.state = s_status.fault == MOTOR_FAULT_NONE ? MOTOR_STATE_IDLE : MOTOR_STATE_FAULT;
    motor_hw_critical_exit(key);
}

motor_result_t motor_controller_clear_fault(void)
{
    if (s_algorithm == NULL) return MOTOR_RESULT_NOT_INITIALIZED;
    if (s_status.state != MOTOR_STATE_FAULT) {
        return s_status.state == MOTOR_STATE_IDLE ? MOTOR_RESULT_OK : MOTOR_RESULT_BUSY;
    }
    motor_controller_stop();
    uint32_t key = motor_hw_critical_enter();
    s_algorithm->init();
    s_status.fault = MOTOR_FAULT_NONE;
    s_status.state = MOTOR_STATE_IDLE;
    motor_hw_critical_exit(key);
    return MOTOR_RESULT_OK;
}

motor_result_t motor_controller_set_command(const motor_command_t *command)
{
    if (s_algorithm == NULL) return MOTOR_RESULT_NOT_INITIALIZED;
    if (command == NULL || !isfinite(command->value)) return MOTOR_RESULT_INVALID_ARGUMENT;
    uint32_t key = motor_hw_critical_enter();
    motor_result_t result;
    if (s_status.state == MOTOR_STATE_FAULT) result = MOTOR_RESULT_FAULT;
    else result = s_algorithm->set_command(command);
    motor_hw_critical_exit(key);
    return result;
}

void motor_controller_get_status(motor_status_t *status)
{
    if (status == NULL) return;
    uint32_t key = motor_hw_critical_enter();
    *status = s_status;
    motor_hw_critical_exit(key);
}

void motor_controller_task(void)
{
    if (s_algorithm == NULL) return;
    /* 故障快路已关断输出；阻塞式 ADC 清理留在普通上下文。 */
    if (s_status.state == MOTOR_STATE_FAULT) {
        if (s_hardware_active) {
            motor_hw_stop();
            s_hardware_active = false;
        }
        return;
    }

    uint32_t now = motor_hw_now_ms();
    uint32_t dt_ms = now - s_last_task_ms;
    if (dt_ms == 0U) return;
    s_last_task_ms = now;

    /* 只有这段短算法更新与 ADC ISR 共享状态；不能在此调用阻塞 HAL。 */
    uint32_t key = motor_hw_critical_enter();
    if (s_status.state == MOTOR_STATE_STARTING || s_status.state == MOTOR_STATE_RUNNING) {
        s_algorithm->task(now, dt_ms);
        s_status.state = s_algorithm->get_state();
    }
    motor_hw_critical_exit(key);
}

void motor_controller_adc_isr(void)
{
    motor_sample_t sample;
    if (!motor_hw_read_sample(&sample)) return;
    if (s_algorithm == NULL ||
        (s_status.state != MOTOR_STATE_STARTING && s_status.state != MOTOR_STATE_RUNNING)) return;

    s_status.sample = sample;
    if (fabsf(sample.iu) > OVERCURRENT_A) { latch_fault(MOTOR_FAULT_OVERCURRENT_U); return; }
    if (fabsf(sample.iv) > OVERCURRENT_A) { latch_fault(MOTOR_FAULT_OVERCURRENT_V); return; }
    if (fabsf(sample.iw) > OVERCURRENT_A) { latch_fault(MOTOR_FAULT_OVERCURRENT_W); return; }

    motor_pwm_t pwm;
    s_algorithm->fast_step(&sample, &pwm);
    if (!isfinite(pwm.duty_u) || !isfinite(pwm.duty_v) || !isfinite(pwm.duty_w)) {
        latch_fault(MOTOR_FAULT_INVALID_OUTPUT);
        return;
    }
    motor_hw_write_pwm(&pwm);
    s_status.pwm = pwm;
}
