/**
  ******************************************************************************
  * @file    foc_config.h
  * @brief   FOC 开环（I-F 强拖）演示参数配置
  *
  *          硬件拓扑假设（典型学习板 / 三相逆变桥）：
  *            - TIM1 CH1/CH2/CH3  -> PA8/PA9/PA10   上桥臂
  *            - TIM1 CH1N/CH2N/CH3N -> PB13/PB14/PB15 下桥臂（互补+死区）
  *            - U 相低边采样电阻放大输出 -> PA0 (ADC1_IN0)
  *            - V 相低边采样电阻放大输出 -> PA1 (ADC1_IN1)
  *            - W 相电流由 iW = -(iU + iV) 重构（双电阻采样）
  *
  *          所有硬件相关数值都在本文件，换板子时只需改这里。
  ******************************************************************************
  */
#pragma once

/* -------------------------------------------------------------------------- */
/* 系统 / PWM                                                                  */
/* -------------------------------------------------------------------------- */
#define MCU_CLOCK_HZ        72000000U   /* 系统时钟 72 MHz (HSE 8M x PLL9)   */
#define PWM_FREQ_HZ         20000U      /* PWM / 电流环频率 20 kHz           */
/* 中心对齐模式：一个完整PWM周期 = 2*(ARR+1) 个时钟 */
#define TIM1_ARR            ((MCU_CLOCK_HZ / (2U * PWM_FREQ_HZ)) - 1U)  /* =1799 */
#define PWM_TS              (1.0f / (float)PWM_FREQ_HZ)   /* 控制周期 50us */

/* ADC 注入组由 TIM1_CH4 触发，触发点设在计数顶峰(ARR)之前，
 * 保证三相下桥臂同时导通窗口内完成采样。
 * 采样2个通道约需 4.3us(12MHz ADC, 13.5周期采样) -> 提前约 450 个计数 */
#define TIM1_TRIG_OFFSET    450U
#define TIM1_CCR4_TRIG      (TIM1_ARR - TIM1_TRIG_OFFSET)

/* 死区时间：72MHz 下 DTG=72 -> 约 1.0us，按驱动器需求调整 */
#define TIM1_DEADTIME       72U

/* 最大占空比限制：必须 < (ARR - TRIG_OFFSET)/ARR，否则采样窗口不够 */
#define DUTY_MAX            0.72f
#define DUTY_MIN            0.02f

/* -------------------------------------------------------------------------- */
/* 电流采样换算                                                                 */
/* -------------------------------------------------------------------------- */
#define VBUS_VOLTAGE        12.0f       /* 母线电压 V（按实际电源改）        */
#define ADC_VREF            3.3f
#define ADC_FULL_SCALE      4095.0f
#define SHUNT_RES_OHM       0.010f      /* 采样电阻 10 mΩ                    */
#define AMP_GAIN            10.0f       /* 电流放大器增益                    */
/* 电流(A) = (adc - offset) * ADC_VREF/4095 / (R*G) */
#define ADC_TO_AMP          (ADC_VREF / ADC_FULL_SCALE / (SHUNT_RES_OHM * AMP_GAIN))
/* 若电流方向接反，把对应符号改为 -1.0f */
#define IU_SIGN             (+1.0f)
#define IV_SIGN             (+1.0f)

/* -------------------------------------------------------------------------- */
/* 电机参数（按实际电机修改）                                                    */
/* -------------------------------------------------------------------------- */
#define MOTOR_POLE_PAIRS    7U          /* 极对数                            */
#define MOTOR_RS_OHM        2.0f        /* 相电阻，用于估算PI                */
#define MOTOR_LS_H          0.001f      /* 相电感，用于估算PI                */

/* -------------------------------------------------------------------------- */
/* 电流环 PI                                                                   */
/*  经典整定：Kp = Ls*BW, Ki = Rs*BW（连续域），BW 为带宽(rad/s)               */
/* -------------------------------------------------------------------------- */
#define CURR_LOOP_BW        800.0f      /* 电流环带宽 rad/s                  */
#define PI_KP               (MOTOR_LS_H * CURR_LOOP_BW)
#define PI_KI               (MOTOR_RS_OHM * CURR_LOOP_BW)
#define PI_OUT_LIMIT        (VBUS_VOLTAGE * 0.57735f)   /* Vbus/sqrt(3) */

/* -------------------------------------------------------------------------- */
/* 开环 I-F 运行参数                                                            */
/* -------------------------------------------------------------------------- */
/* 1. 预定位：theta=0，d轴注入 I_ALIGN，把转子拉到已知角度 */
#define ALIGN_CURRENT_A     0.4f        /* 预定位电流 A                      */
#define ALIGN_TIME_MS       1000U       /* 预定位时间 ms                     */

/* 2. 强拖：电角度斜坡。电角速度从 OMEGA_START 爬到 OMEGA_TARGET (rad/s)
 *    机械转速(rpm) = 电角速度 / (2*pi*极对数) * 60                        */
#define OMEGA_START_RAD     20.0f       /* 起步电角速度 rad/s                */
#define OMEGA_TARGET_RAD    150.0f      /* 目标电角速度 rad/s (7对极≈204rpm) */
#define OMEGA_RAMP_TIME_MS  3000U       /* 加速时间 ms                       */

/* 3. 运行电流给定（开环强拖时 Iq 要大于负载转矩所需，否则失步） */
#define ID_REF_A            0.0f
#define IQ_REF_A            0.6f

/* -------------------------------------------------------------------------- */
/* 保护                                                                        */
/* -------------------------------------------------------------------------- */
#define OVERCURRENT_A       3.0f        /* 过流保护阈值 A                    */

/* 零点校准采样次数（PWM 关断状态下进行） */
#define CAL_SAMPLE_NUM      512U
