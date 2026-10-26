/**
******************************************************************************
*  @brief   开环 FOC (I-F 强拖) 核心实现
*  状态机：CAL(零点校准) -> ALIGN(转子预定位) -> RUN(I-F开环强拖)
*  电流环 20kHz 在 ADC1 注入组完成中断(JEOC)中执行。
******************************************************************************
*/
#include "foc.h"
#include <math.h>
#include <string.h>
#define TWO_PI 6.2831853f
#define SQRT3_BY_2 0.8660254f

foc_t g_foc;
static ADC_HandleTypeDef *s_hadc;
static TIM_HandleTypeDef *s_htim;

/* -------------------------------------------------------------------------- */
/* PI 调节器（条件抗积分饱和）                                                  */
/* -------------------------------------------------------------------------- */
static float pi_run(float ref, float fb, float *integ)
{
    float err  = ref - fb;
    float i_new = *integ + PI_KI * err * PWM_TS;
    float out  = PI_KP * err + i_new;

    if (out > PI_OUT_LIMIT) {
        out = PI_OUT_LIMIT;                 /* 饱和且同向则不再积分 */
        if (err < 0.0f) *integ = i_new;
    } else if (out < -PI_OUT_LIMIT) {
        out = -PI_OUT_LIMIT;
        if (err > 0.0f) *integ = i_new;
    } else {
        *integ = i_new;
    }
    return out;
}

/* -------------------------------------------------------------------------- */
/* SVPWM（min-max 零序注入法，与七段式 SVPWM 等效）                             */
/* 输入 v_alpha/v_beta 单位 V，输出写入 TIM1 CCR1..3                            */
/* -------------------------------------------------------------------------- */
static void svpwm_output(float v_alpha, float v_beta)
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

    float du = 0.5f + (va - voff) / VBUS_VOLTAGE;
    float dv = 0.5f + (vb - voff) / VBUS_VOLTAGE;
    float dw = 0.5f + (vc - voff) / VBUS_VOLTAGE;

    /* 占空比限幅，保证低边采样窗口 */
    if (du > DUTY_MAX) du = DUTY_MAX; else if (du < DUTY_MIN) du = DUTY_MIN;
    if (dv > DUTY_MAX) dv = DUTY_MAX; else if (dv < DUTY_MIN) dv = DUTY_MIN;
    if (dw > DUTY_MAX) dw = DUTY_MAX; else if (dw < DUTY_MIN) dw = DUTY_MIN;

    g_foc.duty_u = du;  g_foc.duty_v = dv;  g_foc.duty_w = dw;

    uint32_t arr1 = TIM1_ARR + 1U;
    s_htim->Instance->CCR1 = (uint32_t)(du * arr1);
    s_htim->Instance->CCR2 = (uint32_t)(dv * arr1);
    s_htim->Instance->CCR3 = (uint32_t)(dw * arr1);
}

/* -------------------------------------------------------------------------- */
/* 电流环一次迭代（20 kHz，JEOC 中断内）                                        */
/* -------------------------------------------------------------------------- */
static inline void foc_control_step(float id_ref, float iq_ref)
{
    foc_t *f = &g_foc;

    /* Clarke */
    f->i_alpha = f->iu;
    f->i_beta  = (f->iu + 2.0f * f->iv) * 0.57735027f;  /* (iu + 2iv)/sqrt(3) */

    /* Park（theta 为强制开环角度） */
    float s = sinf(f->theta);
    float c = cosf(f->theta);
    f->id =  f->i_alpha * c + f->i_beta * s;
    f->iq = -f->i_alpha * s + f->i_beta * c;

    /* 电流环 PI */
    f->vd = pi_run(id_ref, f->id, &f->pi_d_int);
    f->vq = pi_run(iq_ref, f->iq, &f->pi_q_int);

    /* 反 Park */
    f->v_alpha = f->vd * c - f->vq * s;
    f->v_beta  = f->vd * s + f->vq * c;

    /* SVPWM 输出 */
    svpwm_output(f->v_alpha, f->v_beta);
}

/* -------------------------------------------------------------------------- */
/* 对外接口                                                                     */
/* -------------------------------------------------------------------------- */
void foc_init(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim)
{
    memset(&g_foc, 0, sizeof(g_foc));
    s_hadc = hadc;
    s_htim = htim;
    g_foc.state = FOC_STATE_IDLE;
}

/* 零点校准：PWM 关断时用规则组软件触发采 512 次取平均 */
static void foc_calibrate_offsets(void)
{
    ADC_ChannelConfTypeDef cfg = {0};
    uint32_t acc_u = 0, acc_v = 0;

    for (uint32_t i = 0; i < CAL_SAMPLE_NUM; i++) {
        cfg.Channel      = ADC_CHANNEL_0;      /* PA0 -> U 相 */
        cfg.Rank         = ADC_REGULAR_RANK_1;
        cfg.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
        HAL_ADC_ConfigChannel(s_hadc, &cfg);
        HAL_ADC_Start(s_hadc);
        HAL_ADC_PollForConversion(s_hadc, 1);
        acc_u += HAL_ADC_GetValue(s_hadc);
        HAL_ADC_Stop(s_hadc);

        cfg.Channel      = ADC_CHANNEL_1;      /* PA1 -> V 相 */
        HAL_ADC_ConfigChannel(s_hadc, &cfg);
        HAL_ADC_Start(s_hadc);
        HAL_ADC_PollForConversion(s_hadc, 1);
        acc_v += HAL_ADC_GetValue(s_hadc);
        HAL_ADC_Stop(s_hadc);
    }
    g_foc.offset_u = (float)acc_u / (float)CAL_SAMPLE_NUM;
    g_foc.offset_v = (float)acc_v / (float)CAL_SAMPLE_NUM;
}

void foc_start(void)
{
    if (g_foc.state != FOC_STATE_IDLE && g_foc.state != FOC_STATE_FAULT)
        return;

    g_foc.pi_d_int = 0.0f;
    g_foc.pi_q_int = 0.0f;
    g_foc.theta = 0.0f;
    g_foc.omega_e = 0.0f;
    g_foc.fault_code = 0;

    /* 1. 零点校准（此时 PWM 未使能，电流应为 0） */
    g_foc.state = FOC_STATE_CAL;
    foc_calibrate_offsets();

    /* 2. 使能 ADC 注入组：TIM1_CC4 外部触发 + JEOC 中断 */
    SET_BIT(s_hadc->Instance->CR2, ADC_CR2_JEXTTRIG);
    SET_BIT(s_hadc->Instance->CR1, ADC_CR1_JEOCIE);
    NVIC_SetPriority(ADC1_2_IRQn, 1);
    NVIC_EnableIRQ(ADC1_2_IRQn);

    /* 3. 清零占空比，启动 PWM（先 0 占空比，再开主输出） */
    s_htim->Instance->CCR1 = 0;
    s_htim->Instance->CCR2 = 0;
    s_htim->Instance->CCR3 = 0;
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_3);
    /* CH4 仅作 ADC 触发源（输出比较事件，不接引脚） */
    HAL_TIM_OC_Start(s_htim, TIM_CHANNEL_4);

    /* 4. 进入预定位 */
    g_foc.state = FOC_STATE_ALIGN;
    g_foc.state_tick = HAL_GetTick();
}

void foc_stop(void)
{
    __HAL_TIM_MOE_DISABLE(s_htim);
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_3);
    HAL_TIM_OC_Stop(s_htim, TIM_CHANNEL_4);
    NVIC_DisableIRQ(ADC1_2_IRQn);
    g_foc.state = FOC_STATE_IDLE;
}

/* 20 kHz 热路径：直接读注入数据寄存器，清标志后跑控制 */
void foc_adc_isr(void)
{
    if (!(ADC1->SR & ADC_SR_JEOC)) return;
    uint16_t raw_u = (uint16_t)ADC1->JDR1;
    uint16_t raw_v = (uint16_t)ADC1->JDR2;
    // 清 JEOC（写0清除，注意保留位）
    ADC1->SR = ~ADC_SR_JEOC;

    foc_t *f = &g_foc;

    // counts -> A
    f->iu = ((float)raw_u - f->offset_u) * ADC_TO_AMP * IU_SIGN;
    f->iv = ((float)raw_v - f->offset_v) * ADC_TO_AMP * IV_SIGN;
    f->iw = -(f->iu + f->iv);

    // 过流保护
    if (f->iu >  OVERCURRENT_A || f->iu < -OVERCURRENT_A) { 
        f->fault_code = 1; 
        goto fault; 
    }
    if (f->iv >  OVERCURRENT_A || f->iv < -OVERCURRENT_A) { 
        f->fault_code = 2; 
        goto fault; 
    }
    if (f->iw >  OVERCURRENT_A || f->iw < -OVERCURRENT_A) { 
        f->fault_code = 3; 
        goto fault; 
    }
    switch (f->state) {
        case FOC_STATE_ALIGN:
            /* theta 固定 0，d 轴注入电流把转子拉到 0 电角度 */
            foc_control_step(ALIGN_CURRENT_A, 0.0f);
            break;
        case FOC_STATE_RUN:
            /* 电角度强制斜坡 —— 这就是“开环”：角度不靠反馈，靠积分生成 */
            f->theta += f->omega_e * PWM_TS;
            if (f->theta >= TWO_PI) f->theta -= TWO_PI;
            foc_control_step(ID_REF_A, IQ_REF_A);
            break;
        default:
            break;
    }
    return;

    fault:
        __HAL_TIM_MOE_DISABLE(s_htim);
        f->state = FOC_STATE_FAULT;
}

/* 1 ms 慢任务：状态机时序 + 速度斜坡 */
void foc_task(void)
{
    foc_t *f = &g_foc;
    uint32_t now = HAL_GetTick();

    switch (f->state) {
        case FOC_STATE_ALIGN:
            if (now - f->state_tick >= ALIGN_TIME_MS) {
                /* 预定位完成，清零角度与积分，从 OMEGA_START 开始强拖 */
                f->theta = 0.0f;
                f->pi_d_int = 0.0f;
                f->pi_q_int = 0.0f;
                f->omega_e = OMEGA_START_RAD;
                f->state = FOC_STATE_RUN;
                f->state_tick = now;
            }
            break;
        case FOC_STATE_RUN: {
            /* 线性速度斜坡 */
            float step = (OMEGA_TARGET_RAD - OMEGA_START_RAD) / (float)OMEGA_RAMP_TIME_MS;
            if (f->omega_e < OMEGA_TARGET_RAD) {
                f->omega_e += step;
                if (f->omega_e > OMEGA_TARGET_RAD) f->omega_e = OMEGA_TARGET_RAD;
            } else if (f->omega_e > OMEGA_TARGET_RAD) {
                f->omega_e -= step;
                if (f->omega_e < OMEGA_TARGET_RAD) f->omega_e = OMEGA_TARGET_RAD;
            }
            break;
        }
        default:
            break;
    }
}
