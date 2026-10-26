/* Host regression tests: real controller + FOC, deterministic hardware port. */
#include "motor_controller.h"
#include "motor_hw.h"
#include "motor_config.h"
#include "foc/foc.h"
#include "foc/foc_config.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static unsigned assertions;
#define CHECK(expression) do { \
    assertions++; \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expression); \
        return false; \
    } \
} while (0)

typedef struct {
    uint32_t now_ms;
    uint32_t critical_depth;
    unsigned init_calls, stop_calls, calibration_calls, start_calls;
    unsigned emergency_calls, pwm_calls;
    bool calibration_ok, start_ok, active, sample_ready, fault_on_start;
    bool bad_start_order;
    motor_sample_t next_sample;
    motor_pwm_t last_pwm;
} fake_hw_t;

static fake_hw_t hw;

void motor_hw_init(void) { hw.init_calls++; }
bool motor_hw_calibrate_currents(void)
{
    hw.calibration_calls++;
    if (hw.active || hw.critical_depth != 0U) hw.bad_start_order = true;
    return hw.calibration_ok;
}
bool motor_hw_start(void)
{
    motor_status_t status;
    motor_controller_get_status(&status);
    hw.start_calls++;
    if (status.state != MOTOR_STATE_STARTING || hw.critical_depth != 0U)
        hw.bad_start_order = true;
    hw.active = hw.start_ok;
    /* A JEOC can preempt start before it returns to the ordinary task. */
    if (hw.active && hw.fault_on_start) {
        hw.next_sample = (motor_sample_t){ .iu = 4.0f, .vbus = 12.0f, .dt_s = 0.00005f };
        hw.sample_ready = true;
        motor_controller_adc_isr();
    }
    return hw.start_ok;
}
void motor_hw_stop(void)
{
    hw.stop_calls++;
    hw.active = false;
    hw.sample_ready = false;
}
void motor_hw_emergency_stop(void)
{
    hw.emergency_calls++;
    hw.active = false;
    hw.sample_ready = false;
}
bool motor_hw_read_sample(motor_sample_t *sample)
{
    if (!hw.sample_ready) return false;
    *sample = hw.next_sample;
    hw.sample_ready = false;
    return true;
}
static float clamp_duty(float value)
{
    return fminf(0.72f, fmaxf(0.02f, value));
}
void motor_hw_write_pwm(motor_pwm_t *pwm)
{
    hw.pwm_calls++;
    pwm->duty_u = clamp_duty(pwm->duty_u);
    pwm->duty_v = clamp_duty(pwm->duty_v);
    pwm->duty_w = clamp_duty(pwm->duty_w);
    hw.last_pwm = *pwm;
}
uint32_t motor_hw_now_ms(void) { return hw.now_ms; }
uint32_t motor_hw_critical_enter(void) { return hw.critical_depth++; }
void motor_hw_critical_exit(uint32_t key) { hw.critical_depth = key; }

static void reset_mock(void)
{
    memset(&hw, 0, sizeof(hw));
    hw.calibration_ok = true;
    hw.start_ok = true;
}
static motor_status_t get_status(void)
{
    motor_status_t status;
    motor_controller_get_status(&status);
    return status;
}
static void sample_currents(float iu, float iv, float iw)
{
    hw.next_sample = (motor_sample_t){
        .iu = iu, .iv = iv, .iw = iw, .vbus = 12.0f, .dt_s = 0.00005f
    };
    hw.sample_ready = true;
    motor_controller_adc_isr();
}
static bool prepare_idle(void)
{
    motor_controller_stop();
    CHECK(motor_controller_clear_fault() == MOTOR_RESULT_OK);
    CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_FOC) == MOTOR_RESULT_OK);
    CHECK(hw.critical_depth == 0U);
    reset_mock();
    return true;
}

static bool test_initialization_and_selection(void)
{
    reset_mock();
    CHECK(motor_controller_start() == MOTOR_RESULT_NOT_INITIALIZED);
    CHECK(motor_controller_clear_fault() == MOTOR_RESULT_NOT_INITIALIZED);
    CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_FOC) == MOTOR_RESULT_NOT_INITIALIZED);
    CHECK(motor_controller_init() == MOTOR_RESULT_OK);
    CHECK(hw.init_calls == 1U);
    CHECK(get_status().state == MOTOR_STATE_IDLE);
    CHECK(motor_controller_init() == MOTOR_RESULT_BUSY);
    CHECK(hw.init_calls == 1U);

    unsigned stop_calls = hw.stop_calls;
    CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_SIX_STEP) == MOTOR_RESULT_UNSUPPORTED);
    CHECK(motor_controller_select_algorithm((motor_algorithm_id_t)99) == MOTOR_RESULT_UNSUPPORTED);
    CHECK(get_status().algorithm == MOTOR_ALGORITHM_FOC);
    CHECK(get_status().state == MOTOR_STATE_IDLE);
    CHECK(hw.stop_calls == stop_calls);
    CHECK(hw.start_calls == 0U);
    return true;
}

static bool test_lifecycle_and_sample_dispatch(void)
{
    CHECK(prepare_idle());
    for (unsigned restart = 0; restart < 3U; restart++) {
        CHECK(motor_controller_start() == MOTOR_RESULT_OK);
        CHECK(get_status().state == MOTOR_STATE_STARTING);
        CHECK(hw.active);
        CHECK(!hw.bad_start_order);
        CHECK(motor_controller_start() == MOTOR_RESULT_BUSY);
        CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_FOC) == MOTOR_RESULT_BUSY);
        CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_SIX_STEP) == MOTOR_RESULT_BUSY);
        CHECK(motor_controller_clear_fault() == MOTOR_RESULT_BUSY);

        unsigned pwm_calls = hw.pwm_calls;
        motor_controller_adc_isr();
        CHECK(hw.pwm_calls == pwm_calls); /* No conversion event. */
        sample_currents(0.1f, -0.05f, -0.05f);
        CHECK(hw.pwm_calls == pwm_calls + 1U);
        CHECK(isfinite(hw.last_pwm.duty_u));
        CHECK(isfinite(hw.last_pwm.duty_v));
        CHECK(isfinite(hw.last_pwm.duty_w));
        CHECK(get_status().sample.iu == 0.1f);
        CHECK(get_status().pwm.duty_u == hw.last_pwm.duty_u);

        hw.now_ms += ALIGN_TIME_MS;
        motor_controller_task();
        CHECK(get_status().state == MOTOR_STATE_RUNNING);
        motor_controller_stop();
        motor_controller_stop();
        CHECK(!hw.active);
        CHECK(get_status().state == MOTOR_STATE_IDLE);
        CHECK(get_status().pwm.duty_u == 0.0f);
        CHECK(hw.critical_depth == 0U);
    }
    CHECK(hw.start_calls == 3U);
    CHECK(hw.calibration_calls == 3U);
    return true;
}

static bool test_fault_latching_and_recovery(void)
{
    const motor_sample_t currents[] = {
        { .iu = 3.1f }, { .iu = -3.1f },
        { .iv = 3.1f }, { .iv = -3.1f },
        { .iw = 3.1f }, { .iw = -3.1f }
    };
    const motor_fault_t expected[] = {
        MOTOR_FAULT_OVERCURRENT_U, MOTOR_FAULT_OVERCURRENT_U,
        MOTOR_FAULT_OVERCURRENT_V, MOTOR_FAULT_OVERCURRENT_V,
        MOTOR_FAULT_OVERCURRENT_W, MOTOR_FAULT_OVERCURRENT_W
    };
    for (unsigned i = 0; i < sizeof(currents) / sizeof(currents[0]); i++) {
        CHECK(prepare_idle());
        CHECK(motor_controller_start() == MOTOR_RESULT_OK);
        sample_currents(currents[i].iu, currents[i].iv, currents[i].iw);
        CHECK(hw.emergency_calls == 1U);
        CHECK(hw.pwm_calls == 0U);
        CHECK(!hw.active);
        CHECK(get_status().state == MOTOR_STATE_FAULT);
        CHECK(get_status().fault == expected[i]);

        /* Delayed ALIGN->RUN processing must not overwrite an ISR fault. */
        hw.now_ms = ALIGN_TIME_MS + 10U;
        motor_controller_task();
        CHECK(get_status().state == MOTOR_STATE_FAULT);
        motor_controller_stop();
        CHECK(get_status().fault == expected[i]);
        CHECK(motor_controller_start() == MOTOR_RESULT_FAULT);
        CHECK(motor_controller_select_algorithm(MOTOR_ALGORITHM_FOC) == MOTOR_RESULT_FAULT);
        motor_command_t command = { MOTOR_COMMAND_OPEN_LOOP_SPEED, 50.0f };
        CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_FAULT);

        CHECK(motor_controller_clear_fault() == MOTOR_RESULT_OK);
        CHECK(get_status().state == MOTOR_STATE_IDLE);
        CHECK(get_status().fault == MOTOR_FAULT_NONE);
        CHECK(hw.start_calls == 1U); /* Clearing a fault never starts hardware. */
        CHECK(motor_controller_start() == MOTOR_RESULT_OK);
        CHECK(hw.active);
        CHECK(hw.critical_depth == 0U);
    }
    return true;
}

static bool test_start_failures(void)
{
    CHECK(prepare_idle());
    hw.calibration_ok = false;
    CHECK(motor_controller_start() == MOTOR_RESULT_FAULT);
    CHECK(get_status().fault == MOTOR_FAULT_CURRENT_CALIBRATION);
    CHECK(hw.start_calls == 0U);
    CHECK(!hw.active);

    CHECK(prepare_idle());
    hw.start_ok = false;
    CHECK(motor_controller_start() == MOTOR_RESULT_FAULT);
    CHECK(get_status().fault == MOTOR_FAULT_HARDWARE_START);
    CHECK(hw.start_calls == 1U);
    CHECK(!hw.active);
    CHECK(hw.stop_calls >= 2U);

    CHECK(prepare_idle());
    hw.fault_on_start = true;
    CHECK(motor_controller_start() == MOTOR_RESULT_FAULT);
    CHECK(get_status().state == MOTOR_STATE_FAULT);
    CHECK(get_status().fault == MOTOR_FAULT_OVERCURRENT_U);
    CHECK(!hw.active);
    CHECK(hw.critical_depth == 0U);
    return true;
}

static bool test_command_validation(void)
{
    CHECK(prepare_idle());
    CHECK(motor_controller_set_command(NULL) == MOTOR_RESULT_INVALID_ARGUMENT);
    motor_command_t command = { MOTOR_COMMAND_OPEN_LOOP_SPEED, NAN };
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = INFINITY;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = -1.0f;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = OMEGA_TARGET_RAD + 1.0f;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = 50.0f;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_OK);
    command.type = MOTOR_COMMAND_Q_CURRENT;
    command.value = OVERCURRENT_A;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = -OVERCURRENT_A;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_INVALID_ARGUMENT);
    command.value = 0.3f;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_OK);
    command.type = (motor_command_type_t)99;
    CHECK(motor_controller_set_command(&command) == MOTOR_RESULT_UNSUPPORTED);
    CHECK(hw.critical_depth == 0U);
    return true;
}

static motor_pwm_t run_foc_ramp(bool split_elapsed_time)
{
    foc_algorithm.init();
    foc_algorithm.start(0U);
    foc_algorithm.task(ALIGN_TIME_MS, ALIGN_TIME_MS);
    if (split_elapsed_time) {
        for (uint32_t i = 1U; i <= 500U; i++)
            foc_algorithm.task(ALIGN_TIME_MS + i, 1U);
    } else {
        foc_algorithm.task(ALIGN_TIME_MS + 500U, 500U);
    }
    motor_sample_t sample = { .vbus = 12.0f, .dt_s = 0.00005f };
    motor_pwm_t pwm = {0};
    for (unsigned i = 0U; i < 100U; i++) foc_algorithm.fast_step(&sample, &pwm);
    return pwm;
}
static bool test_ramp_uses_elapsed_time(void)
{
    CHECK(prepare_idle());
    motor_pwm_t one_task = run_foc_ramp(false);
    motor_pwm_t many_tasks = run_foc_ramp(true);
    CHECK(isfinite(one_task.duty_u));
    CHECK(isfinite(one_task.duty_v));
    CHECK(isfinite(one_task.duty_w));
    CHECK(fabsf(one_task.duty_u - many_tasks.duty_u) < 0.00001f);
    CHECK(fabsf(one_task.duty_v - many_tasks.duty_v) < 0.00001f);
    CHECK(fabsf(one_task.duty_w - many_tasks.duty_w) < 0.00001f);
    return true;
}

static bool controller_output_with_same_tick_tasks(unsigned repeat_tasks, motor_pwm_t *pwm)
{
    CHECK(prepare_idle());
    CHECK(motor_controller_start() == MOTOR_RESULT_OK);
    hw.now_ms = ALIGN_TIME_MS;
    motor_controller_task();
    hw.now_ms += 500U;
    motor_controller_task();
    for (unsigned i = 0; i < repeat_tasks; i++) motor_controller_task();
    for (unsigned i = 0; i < 100U; i++) sample_currents(0.0f, 0.0f, 0.0f);
    *pwm = hw.last_pwm;
    return true;
}
static bool test_same_tick_task_is_noop(void)
{
    motor_pwm_t once, repeated;
    CHECK(controller_output_with_same_tick_tasks(0U, &once));
    CHECK(controller_output_with_same_tick_tasks(100U, &repeated));
    CHECK(once.duty_u == repeated.duty_u);
    CHECK(once.duty_v == repeated.duty_v);
    CHECK(once.duty_w == repeated.duty_w);
    return true;
}

static bool test_tick_wraparound(void)
{
    CHECK(prepare_idle());
    hw.now_ms = UINT32_MAX - 99U;
    CHECK(motor_controller_start() == MOTOR_RESULT_OK);
    hw.now_ms += ALIGN_TIME_MS;
    motor_controller_task();
    CHECK(get_status().state == MOTOR_STATE_RUNNING);
    sample_currents(0.0f, 0.0f, 0.0f);
    CHECK(hw.pwm_calls == 1U);
    CHECK(isfinite(hw.last_pwm.duty_u));
    return true;
}

int main(void)
{
    bool (*tests[])(void) = {
        test_initialization_and_selection,
        test_lifecycle_and_sample_dispatch,
        test_fault_latching_and_recovery,
        test_start_failures,
        test_command_validation,
        test_ramp_uses_elapsed_time,
        test_same_tick_task_is_noop,
        test_tick_wraparound
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (!tests[i]()) return 1;
    }
    if (hw.critical_depth != 0U) return 1;
    printf("PASS: %zu regression groups, %u assertions\n",
           sizeof(tests) / sizeof(tests[0]), assertions);
    return 0;
}
