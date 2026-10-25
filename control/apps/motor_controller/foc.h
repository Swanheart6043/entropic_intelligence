/**
  ******************************************************************************
  * @file    foc.h
  * @brief   开环 FOC (I-F 强拖) 核心接口
  *
  *  控制链路（与经典 I-F 开环启动一致）：
  *    相电流采样 -> Clarke -> Park(theta强制斜坡) -> Id/Iq
  *    -> 两个 PI(给定 Id*=0, Iq*=设定) -> Vd/Vq
  *    -> 反Park -> SVPWM -> TIM1 -> 逆变器 -> PMSM
  *
  *  时序：TIM1 CH4 比较事件 -> ADC1 注入组转换 -> JEOC 中断里跑 FOC_ADC_ISR()
  *       （20 kHz）；FOC_Task() 在主循环里以 1ms 步进跑状态机/速度斜坡。
  ******************************************************************************
  */
#ifndef FOC_H
#define FOC_H

#include "stm32f1xx_hal.h"
#include "foc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FOC_STATE_IDLE = 0,   /* PWM 关闭 */
    FOC_STATE_CAL,        /* 电流零点校准 */
    FOC_STATE_ALIGN,      /* 转子预定位 */
    FOC_STATE_RUN,        /* I-F 开环运行 */
    FOC_STATE_FAULT       /* 故障（过流等），PWM 关闭 */
} foc_state_t;

typedef struct {
    /* 运行状态 */
    foc_state_t state;
    uint32_t    state_tick;     /* 当前状态已持续的 ms 数 */

    /* 电流采样（标幺化后） */
    float iu, iv, iw;           /* 三相电流 A */
    float offset_u, offset_v;   /* ADC 零点（counts） */

    /* 坐标变换量 */
    float i_alpha, i_beta;
    float id, iq;
    float vd, vq;
    float v_alpha, v_beta;

    /* 开环角度/速度 */
    float theta;                /* 强制电角度 rad, 0..2pi */
    float omega_e;              /* 当前电角速度 rad/s */

    /* 电流环 PI */
    float pi_d_int;             /* d 轴积分 */
    float pi_q_int;             /* q 轴积分 */

    /* 观测输出（调试用） */
    float duty_u, duty_v, duty_w;
    uint32_t fault_code;        /* 0=无, 1=U过流, 2=V过流, 3=W过流 */
} foc_t;

extern foc_t g_foc;

/* 初始化内部状态并绑定外设句柄（由 Motor_HW_Init 调用） */
void FOC_Init(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim);

/* 启动流程：零点校准 -> 预定位 -> I-F 强拖 */
void FOC_Start(void);

/* 立即停止 PWM 并回到 IDLE */
void FOC_Stop(void);

/* 20 kHz：在 ADC1 注入组完成中断中调用（热路径，直接读寄存器） */
void FOC_ADC_ISR(void);

/* 1 ms：主循环中调用，跑状态机时序和速度斜坡 */
void FOC_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* FOC_H */
