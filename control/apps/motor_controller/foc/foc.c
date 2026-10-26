/**
******************************************************************************
*  @brief   开环 FOC (I-F 强拖) 核心实现
*  算法内状态机：ALIGN(转子预定位) -> RUN(I-F开环强拖)。
*  校准、保护和外设生命周期由 motor_controller / motor_hw 负责。
******************************************************************************
*/
#include "foc.h"
#include "foc_config.h"
#include <math.h>
#include <string.h>
#define TWO_PI 6.2831853f
#define SQRT3_BY_2 0.8660254f

typedef enum { FOC_STATE_IDLE = 0, FOC_STATE_ALIGN, FOC_STATE_RUN } foc_state_t;

typedef struct {
    foc_state_t state;
    uint32_t state_tick;
    float iu, iv;
    float i_alpha, i_beta, id, iq;
    float vd, vq, v_alpha, v_beta;
    float theta, omega_e;
    float pi_d_int, pi_q_int;
    float target_omega_e, iq_ref;
} foc_t;

/* 私有单电机上下文；不向业务层暴露算法内部状态。 */
static foc_t s_foc;

/* -------------------------------------------------------------------------- */
/* PI 调节器（条件抗积分饱和）                                                  */
/* -------------------------------------------------------------------------- */
static float pi_run(float ref, float fb, float *integ, float dt_s, float limit)
{
    float err  = ref - fb;
    float i_new = *integ + PI_KI * err * dt_s;
    float out  = PI_KP * err + i_new;

    if (out > limit) {
        out = limit;                 /* 饱和且同向则不再积分 */
        if (err < 0.0f) *integ = i_new;
    } else if (out < -limit) {
        out = -limit;
        if (err > 0.0f) *integ = i_new;
    } else {
        *integ = i_new;
    }
    return out;
}

/* -------------------------------------------------------------------------- */
/* SVPWM（min-max 零序注入法，与七段式 SVPWM 等效）                             */
/* 输入电压 V，输出三相占空比；硬件层负责采样窗口限幅及寄存器写入              */
/* -------------------------------------------------------------------------- */
static void svpwm_output(float v_alpha, float v_beta, float vbus, motor_pwm_t *pwm)
{
    float va = v_alpha;
    float vb = -0.5f * v_alpha + SQRT3_BY_2 * v_beta;
    float vc = -0.5f * v_alpha - SQRT3_BY_2 * v_beta;

    float vmax = va, vmin = va;
    if (vb > vmax) vmax = vb;
    if (vb < vmin) vmin = vb;
    if (vc > vmax) vmax = vc;
    if (vc < vmin) vmin = vc;

    float voff = 0.5f * (vmax + vmin);      /* 零序分量（居中注入） */

    pwm->duty_u = 0.5f + (va - voff) / vbus;
    pwm->duty_v = 0.5f + (vb - voff) / vbus;
    pwm->duty_w = 0.5f + (vc - voff) / vbus;
}

/* -------------------------------------------------------------------------- */
/* 电流环一次迭代（20 kHz，JEOC 中断内）                                        */
/* -------------------------------------------------------------------------- */
static inline void foc_control_step(float id_ref, float iq_ref,
                                    const motor_sample_t *sample, motor_pwm_t *pwm)
{
    foc_t *f = &s_foc;

    /* Clarke */
    f->i_alpha = f->iu;
    f->i_beta  = (f->iu + 2.0f * f->iv) * 0.57735027f;  /* (iu + 2iv)/sqrt(3) */

    /* Park（theta 为强制开环角度） */
    float s = sinf(f->theta);
    float c = cosf(f->theta);
    f->id =  f->i_alpha * c + f->i_beta * s;
    f->iq = -f->i_alpha * s + f->i_beta * c;

    /* 电流环 PI */
    float limit = sample->vbus * 0.57735f;
    f->vd = pi_run(id_ref, f->id, &f->pi_d_int, sample->dt_s, limit);
    f->vq = pi_run(iq_ref, f->iq, &f->pi_q_int, sample->dt_s, limit);

    /* 反 Park */
    f->v_alpha = f->vd * c - f->vq * s;
    f->v_beta  = f->vd * s + f->vq * c;

    /* SVPWM 输出 */
    svpwm_output(f->v_alpha, f->v_beta, sample->vbus, pwm);
}

/* -------------------------------------------------------------------------- */
/* 对外接口                                                                     */
/* -------------------------------------------------------------------------- */
static void foc_init(void)
{
    memset(&s_foc, 0, sizeof(s_foc));
    s_foc.target_omega_e = OMEGA_TARGET_RAD;
    s_foc.iq_ref = IQ_REF_A;
}

static void foc_start(uint32_t now_ms)
{
    s_foc.pi_d_int = 0.0f;
    s_foc.pi_q_int = 0.0f;
    s_foc.theta = 0.0f;
    s_foc.omega_e = 0.0f;
    s_foc.state_tick = now_ms;
    s_foc.state = FOC_STATE_ALIGN;
}

static void foc_stop(void)
{
    s_foc.state = FOC_STATE_IDLE;
}

/* 快速控制纯计算：不读取寄存器，不负责硬件保护和 PWM 启停。 */
static void foc_fast_step(const motor_sample_t *sample, motor_pwm_t *pwm)
{
    foc_t *f = &s_foc;
    f->iu = sample->iu;
    f->iv = sample->iv;
    *pwm = (motor_pwm_t){0};
    switch (f->state) {
        case FOC_STATE_ALIGN:
            /* theta 固定 0，d 轴注入电流把转子拉到 0 电角度 */
            foc_control_step(ALIGN_CURRENT_A, 0.0f, sample, pwm);
            break;
        case FOC_STATE_RUN:
            /* 电角度强制斜坡 —— 这就是“开环”：角度不靠反馈，靠积分生成 */
            f->theta += f->omega_e * sample->dt_s;
            if (f->theta >= TWO_PI) f->theta -= TWO_PI;
            foc_control_step(ID_REF_A, f->iq_ref, sample, pwm);
            break;
        default:
            break;
    }
}

/* 慢任务：按真实经过时间更新速度斜坡，不能依赖 HAL_Delay(1) 的调用次数。 */
static void foc_task(uint32_t now, uint32_t dt_ms)
{
    foc_t *f = &s_foc;

    switch (f->state) {
        case FOC_STATE_ALIGN:
            if (now - f->state_tick >= ALIGN_TIME_MS) {
                /* 预定位完成，清零角度与积分，从 OMEGA_START 开始强拖 */
                f->theta = 0.0f;
                f->pi_d_int = 0.0f;
                f->pi_q_int = 0.0f;
                f->omega_e = fminf(OMEGA_START_RAD, f->target_omega_e);
                f->state = FOC_STATE_RUN;
                f->state_tick = now;
            }
            break;
        case FOC_STATE_RUN: {
            /* 线性速度斜坡 */
            float step = fabsf(OMEGA_RAMP_RAD_S2) * ((float)dt_ms * 0.001f);
            if (f->omega_e < f->target_omega_e)
                f->omega_e = fminf(f->omega_e + step, f->target_omega_e);
            else if (f->omega_e > f->target_omega_e)
                f->omega_e = fmaxf(f->omega_e - step, f->target_omega_e);
            break;
        }
        default:
            break;
    }
}

static motor_state_t foc_get_state(void)
{
    switch (s_foc.state) {
        case FOC_STATE_ALIGN: return MOTOR_STATE_STARTING;
        case FOC_STATE_RUN: return MOTOR_STATE_RUNNING;
        default: return MOTOR_STATE_IDLE;
    }
}

static motor_result_t foc_set_command(const motor_command_t *command)
{
    if (!isfinite(command->value)) return MOTOR_RESULT_INVALID_ARGUMENT;
    switch (command->type) {
        case MOTOR_COMMAND_OPEN_LOOP_SPEED:
            /* 第一版保持原有单向强拖工作范围，不扩展到未经验证的转速。 */
            if (command->value < 0.0f || command->value > OMEGA_TARGET_RAD)
                return MOTOR_RESULT_INVALID_ARGUMENT;
            s_foc.target_omega_e = command->value;
            break;
        case MOTOR_COMMAND_Q_CURRENT:
            if (fabsf(command->value) >= OVERCURRENT_A) return MOTOR_RESULT_INVALID_ARGUMENT;
            s_foc.iq_ref = command->value;
            break;
        default: return MOTOR_RESULT_UNSUPPORTED;
    }
    return MOTOR_RESULT_OK;
}

const motor_algorithm_ops_t foc_algorithm = {
    .init = foc_init,
    .start = foc_start,
    .stop = foc_stop,
    .fast_step = foc_fast_step,
    .task = foc_task,
    .get_state = foc_get_state,
    .set_command = foc_set_command
};
