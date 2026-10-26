/**
 * @file motor_hw.c
 * @brief 电机专用 TIM1/ADC1 初始化、采样和功率输出。
 */
#include "motor_hw.h"
#include "motor_hw_config.h"
#include "main.h"
#include <math.h>

static ADC_HandleTypeDef hadc1;
static TIM_HandleTypeDef htim1;
static float offset_u;
static float offset_v;
static bool s_initialized;
static bool s_offsets_valid;
static bool s_adc_ready;
static volatile bool s_running;

#define MOTOR_TIM_OUTPUTS (TIM_CCER_CC1E | TIM_CCER_CC1NE | \
                           TIM_CCER_CC2E | TIM_CCER_CC2NE | \
                           TIM_CCER_CC3E | TIM_CCER_CC3NE | TIM_CCER_CC4E)

// TIM1 初始化：中心对齐 PWM 20kHz，三对互补输出 + 死区，CH4 输出比较事件用作 ADC 注入组触发源
static void mx_tim1_init(void)
{
	TIM_OC_InitTypeDef sConfigOC = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

	htim1.Instance = TIM1;
	htim1.Init.Prescaler = 0;
	htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
	htim1.Init.Period = TIM1_ARR;
	htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim1.Init.RepetitionCounter = 0;
	htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
	{
		Error_Handler();
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = 0;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
	sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
	if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
	{
		Error_Handler();
	}

	// CH4：仅产生比较事件触发 ADC，不输出到引脚
	sConfigOC.OCMode = TIM_OCMODE_TIMING;
	sConfigOC.Pulse = TIM1_CCR4_TRIG;
	if (HAL_TIM_OC_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
	{
		Error_Handler();
	}

	sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
	sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
	sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
	sBreakDeadTimeConfig.DeadTime = TIM1_DEADTIME;
	sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
	sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
	sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
	if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
	{
		Error_Handler();
	}
}

// ADC1 初始化：注入组(IN0,IN1)由 TIM1_CH4 触发，规则组用于零点校准
static void mx_adc1_init(void)
{
	ADC_InjectionConfTypeDef sConfigInjected = {0};
	ADC_ChannelConfTypeDef sConfig = {0};

	hadc1.Instance = ADC1;
	hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
	hadc1.Init.ContinuousConvMode = DISABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion = 1;
	if (HAL_ADC_Init(&hadc1) != HAL_OK)
	{
		Error_Handler();
	}

	/* 规则组 rank1：供零点校准用（软件触发轮询） */
	sConfig.Channel = ADC_CHANNEL_0;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
	{
		Error_Handler();
	}

	/* 注入组：U相(IN0) -> rank1, V相(IN1) -> rank2，TIM1_CC4 外部触发 */
	sConfigInjected.InjectedChannel = ADC_CHANNEL_0;
	sConfigInjected.InjectedRank = ADC_INJECTED_RANK_1;
	sConfigInjected.InjectedNbrOfConversion = 2;
	sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_13CYCLES_5;
	sConfigInjected.ExternalTrigInjecConv = ADC_EXTERNALTRIGINJECCONV_T1_CC4;
	sConfigInjected.AutoInjectedConv = DISABLE;
	sConfigInjected.InjectedDiscontinuousConvMode = DISABLE;
	sConfigInjected.InjectedOffset = 0;
	if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected) != HAL_OK)
	{
		Error_Handler();
	}
	sConfigInjected.InjectedChannel = ADC_CHANNEL_1;
	sConfigInjected.InjectedRank = ADC_INJECTED_RANK_2;
	if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected) != HAL_OK)
	{
		Error_Handler();
	}

	/* F1 上电后必须做一次 ADC 自校准 */
	if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
	{
		Error_Handler();
	}
}

/* TIM1 PWM 底层：时钟 + GPIO (PA8/9/10 上桥, PB13/14/15 下桥) */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef* htim)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	if (htim->Instance == TIM1)
	{
		__HAL_RCC_TIM1_CLK_ENABLE();
		__HAL_RCC_GPIOA_CLK_ENABLE();
		__HAL_RCC_GPIOB_CLK_ENABLE();

		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
		HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	}
}

/* ADC1 底层：时钟 + GPIO (PA0/PA1 模拟输入) */
void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	if (hadc->Instance == ADC1)
	{
		__HAL_RCC_ADC1_CLK_ENABLE();
		__HAL_RCC_GPIOA_CLK_ENABLE();

		GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	}
}

void motor_hw_init(void)
{
    mx_tim1_init();
    mx_adc1_init();
    offset_u = 0.0f;
    offset_v = 0.0f;
    s_offsets_valid = false;
    s_initialized = true;
    motor_hw_stop();
}

uint32_t motor_hw_critical_enter(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return key;
}

void motor_hw_critical_exit(uint32_t key)
{
    __DMB();
    __set_PRIMASK(key);
    __ISB();
}

void motor_hw_emergency_stop(void)
{
    if (!s_initialized) return;

    /* 普通 MOE_DISABLE 宏受 CCER 条件限制，急停必须无条件关桥。 */
    __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim1);
    CLEAR_BIT(htim1.Instance->CR1, TIM_CR1_CEN);
    CLEAR_BIT(htim1.Instance->CCER, MOTOR_TIM_OUTPUTS);
    CLEAR_BIT(hadc1.Instance->CR1, ADC_CR1_JEOCIE);
    CLEAR_BIT(hadc1.Instance->CR2, ADC_CR2_JEXTTRIG | ADC_CR2_ADON);
    NVIC_DisableIRQ(ADC1_2_IRQn);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC | ADC_FLAG_JSTRT);
    NVIC_ClearPendingIRQ(ADC1_2_IRQn);
    s_running = false;
    __DSB();
}

void motor_hw_stop(void)
{
    if (!s_initialized) return;

    motor_hw_emergency_stop();
    /* 急停只操作寄存器；普通上下文补齐 ADC HAL 状态，供下次启动。 */
    HAL_StatusTypeDef status;
    if ((hadc1.State & HAL_ADC_STATE_REG_BUSY) != 0U) {
        status = HAL_ADC_Stop(&hadc1);
    } else {
        status = HAL_ADCEx_InjectedStop_IT(&hadc1);
    }
    s_adc_ready = (status == HAL_OK);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC | ADC_FLAG_JSTRT);
    NVIC_ClearPendingIRQ(ADC1_2_IRQn);
}

static bool read_calibration_channel(uint32_t channel, uint16_t *raw)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel = channel;
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) return false;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc1, 1U) != HAL_OK) return false;
    *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    return HAL_ADC_Stop(&hadc1) == HAL_OK;
}

bool motor_hw_calibrate_currents(void)
{
    if (!s_initialized) return false;

    motor_hw_stop();
    s_offsets_valid = false;
    if (!s_adc_ready) return false;

    uint32_t acc_u = 0U;
    uint32_t acc_v = 0U;
    for (uint32_t i = 0U; i < CAL_SAMPLE_NUM; ++i) {
        uint16_t raw_u;
        uint16_t raw_v;
        if (!read_calibration_channel(ADC_CHANNEL_0, &raw_u) ||
            !read_calibration_channel(ADC_CHANNEL_1, &raw_v)) {
            motor_hw_stop();
            return false;
        }
        acc_u += raw_u;
        acc_v += raw_v;
    }

    offset_u = (float)acc_u / (float)CAL_SAMPLE_NUM;
    offset_v = (float)acc_v / (float)CAL_SAMPLE_NUM;
    s_offsets_valid = true;
    return true;
}

bool motor_hw_start(void)
{
    if (!s_initialized || !s_offsets_valid) return false;
    if (s_running) return true;

    motor_hw_stop();
    if (!s_adc_ready) return false;

    htim1.Instance->CCR1 = 0U;
    htim1.Instance->CCR2 = 0U;
    htim1.Instance->CCR3 = 0U;
    htim1.Instance->CNT = 0U;
    /* PWM 通道有预装载，先更新影子寄存器再打开功率输出。 */
    htim1.Instance->EGR = TIM_EGR_UG;
    htim1.Instance->SR = 0U;

    /* HAL 负责 ADON 和稳定时间；此时计数器仍关闭，无 ADC 触发。 */
    if (HAL_ADCEx_InjectedStart_IT(&hadc1) != HAL_OK) {
        motor_hw_stop();
        return false;
    }

    uint32_t key = motor_hw_critical_enter();
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC | ADC_FLAG_JSTRT);
    NVIC_ClearPendingIRQ(ADC1_2_IRQn);
    NVIC_SetPriority(ADC1_2_IRQn, 1U);
    SET_BIT(htim1.Instance->CCER, MOTOR_TIM_OUTPUTS);
    s_running = true;
    NVIC_EnableIRQ(ADC1_2_IRQn);
    /* 运行期统一管理 TIM 寄存器，避免 HAL Start 逐通道提前打开 MOE。 */
    SET_BIT(htim1.Instance->CR1, TIM_CR1_CEN);
    __HAL_TIM_MOE_ENABLE(&htim1);
    motor_hw_critical_exit(key);
    return true;
}

bool motor_hw_read_sample(motor_sample_t *sample)
{
    if (!s_initialized || sample == NULL ||
        (hadc1.Instance->SR & ADC_SR_JEOC) == 0U) return false;

    uint16_t raw_u = (uint16_t)hadc1.Instance->JDR1;
    uint16_t raw_v = (uint16_t)hadc1.Instance->JDR2;
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC);
    if (!s_running) return false;

    sample->iu = ((float)raw_u - offset_u) * ADC_TO_AMP * IU_SIGN;
    sample->iv = ((float)raw_v - offset_v) * ADC_TO_AMP * IV_SIGN;
    sample->iw = -(sample->iu + sample->iv);
    sample->vbus = VBUS_VOLTAGE;
    sample->dt_s = PWM_TS;
    return true;
}

static float clamp_duty(float duty)
{
    if (duty > DUTY_MAX) return DUTY_MAX;
    if (duty < DUTY_MIN) return DUTY_MIN;
    return duty;
}

void motor_hw_write_pwm(motor_pwm_t *pwm)
{
    if (!s_initialized || pwm == NULL) return;
    if (!isfinite(pwm->duty_u) || !isfinite(pwm->duty_v) ||
        !isfinite(pwm->duty_w)) {
        motor_hw_emergency_stop();
        return;
    }

    pwm->duty_u = clamp_duty(pwm->duty_u);
    pwm->duty_v = clamp_duty(pwm->duty_v);
    pwm->duty_w = clamp_duty(pwm->duty_w);
    uint32_t arr1 = htim1.Instance->ARR + 1U;
    htim1.Instance->CCR1 = (uint32_t)(pwm->duty_u * (float)arr1);
    htim1.Instance->CCR2 = (uint32_t)(pwm->duty_v * (float)arr1);
    htim1.Instance->CCR3 = (uint32_t)(pwm->duty_w * (float)arr1);
}

uint32_t motor_hw_now_ms(void)
{
    return HAL_GetTick();
}
