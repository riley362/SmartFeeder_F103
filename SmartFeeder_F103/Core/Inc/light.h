/**
 * @file    light.h
 * @brief   补光灯模块 (PA1, 高电平点亮)
 *          硬件接法:
 *            - 小功率 LED: PA1 → 220Ω 限流 → LED(+) → LED(-) → GND
 *            - 大功率 LED/灯条: PA1 → 1kΩ → NPN(S8050) 基极, 集电极驱动负载
 */
#ifndef __LIGHT_H
#define __LIGHT_H

#include "main.h"

/* 初始化: 强制关灯 */
void    LIGHT_Init(void);

/* 开/关/翻转 */
void    LIGHT_On(void);
void    LIGHT_Off(void);
void    LIGHT_Toggle(void);

/* 查询当前状态: 0=关, 1=开 */
uint8_t LIGHT_IsOn(void);

#endif /* __LIGHT_H */
