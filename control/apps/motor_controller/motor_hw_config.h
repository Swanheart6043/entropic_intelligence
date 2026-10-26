/**
 * @brief 当前 STM32F103 板的 PWM 时序及双电阻采样参数。
 * TIM1: PA8/9/10 + PB13/14/15；ADC1: PA0(U)、PA1(V)。
 * 修改时序时应同时核对 ADC 触发点、采样窗口和占空比上限。
 */
#pragma once

#define MCU_CLOCK_HZ        72000000U
#define PWM_FREQ_HZ         20000U
#define TIM1_ARR            ((MCU_CLOCK_HZ / (2U * PWM_FREQ_HZ)) - 1U)
#define PWM_TS              (1.0f / (float)PWM_FREQ_HZ)

/* TIM1 CC4 在顶峰前触发 ADC 注入组；预留双通道转换时间。 */
#define TIM1_TRIG_OFFSET    450U
#define TIM1_CCR4_TRIG      (TIM1_ARR - TIM1_TRIG_OFFSET)
#define TIM1_DEADTIME       72U
#define DUTY_MAX            0.72f
#define DUTY_MIN            0.02f

/* 当前母线电压为配置值，后续可在硬件层替换成实际采样。 */
#define VBUS_VOLTAGE        12.0f
#define ADC_VREF            3.3f
#define ADC_FULL_SCALE      4095.0f
#define SHUNT_RES_OHM       0.010f
#define AMP_GAIN            10.0f
#define ADC_TO_AMP          (ADC_VREF / ADC_FULL_SCALE / (SHUNT_RES_OHM * AMP_GAIN))
#define IU_SIGN             (+1.0f)
#define IV_SIGN             (+1.0f)
#define CAL_SAMPLE_NUM      512U
