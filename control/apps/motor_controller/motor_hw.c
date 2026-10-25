/**
 * @file motor_hw.c
 * @brief 电机专用 TIM1/ADC1 外设初始化及 FOC 句柄绑定。
 */
#include "motor_hw.h"
#include "foc.h"
#include "main.h"

static ADC_HandleTypeDef hadc1;
static TIM_HandleTypeDef htim1;

/**
  * @brief TIM1 初始化：中心对齐 PWM 20kHz，三对互补输出 + 死区，
  *        CH4 输出比较事件用作 ADC 注入组触发源
  */
static void MX_TIM1_Init(void)
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
	HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig);

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

	/* CH4：仅产生比较事件触发 ADC，不输出到引脚 */
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

/**
  * @brief ADC1 初始化：注入组(IN0,IN1)由 TIM1_CH4 触发，规则组用于零点校准
  */
static void MX_ADC1_Init(void)
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

void Motor_HW_Init(void)
{
    MX_TIM1_Init();
    MX_ADC1_Init();
    FOC_Init(&hadc1, &htim1);
}
