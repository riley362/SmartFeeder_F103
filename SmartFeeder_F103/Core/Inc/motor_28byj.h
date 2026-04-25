/**
 * @file    motor_28byj.h
 * @brief   28BYJ-48 + ULN2003 步进电机非阻塞驱动
 *
 * 引脚 (全部 GPIOB):
 *   IN1=PB12, IN2=PB13, IN3=PB14, IN4=PB15
 *
 * 电机参数:
 *   28BYJ-48 内部 1:64 减速齿轮;
 *   半步 (half-step) 模式:   4096 步/圈;
 *   全步 (full-step) 模式:   2048 步/圈;
 *   本驱动采用半步, 扭矩最大且运转最平滑.
 *
 * 时序:
 *   每半步间隔 2ms → ~500 步/秒 → 约 8.2 秒/圈.
 *   用 HAL_GetTick() 计时, 完全非阻塞, 不影响主循环上报.
 *
 * 喂食量标定 (需按实际螺旋推料结构微调):
 *   DEFAULT_STEPS_PER_GRAM = 20 → 50g ≈ 1000 步 ≈ 2 秒.
 *
 * 调用方式:
 *   1) 上电:     Motor_Init();
 *   2) 主循环:   Motor_Update();        // 每次循环都要调, 越频繁越准
 *   3) 触发:     Motor_StartFeed(50);   // 喂 50g, 立即返回, 不阻塞
 *   4) 查询:     while (Motor_IsBusy()) { ... }
 */
#ifndef __MOTOR_28BYJ_H
#define __MOTOR_28BYJ_H

#include "main.h"

/* 每克对应的步数 (半步), 按实际机械结构标定 */
#ifndef MOTOR_STEPS_PER_GRAM
#define MOTOR_STEPS_PER_GRAM    20U
#endif

/* 按键触发的默认单次喂食量 (克) */
#ifndef MOTOR_KEY_FEED_GRAMS
#define MOTOR_KEY_FEED_GRAMS    30U
#endif

/* 初始化: 所有线圈断电, 清空状态 */
void     Motor_Init(void);

/* 启动一次投喂. 非阻塞: 立即返回, 后台由 Motor_Update() 推进 */
/*  - grams: 要投喂的克数, 传 0 或正忙时被忽略 */
void     Motor_StartFeed(uint16_t grams);

/* 按拍执行电机推进, 必须在主循环频繁调用 */
void     Motor_Update(void);

/* 是否正在投喂中 */
uint8_t  Motor_IsBusy(void);

/* 剩余步数 (诊断用) */
uint32_t Motor_GetRemainingSteps(void);

#endif /* __MOTOR_28BYJ_H */
