/**
 * @file    key.h
 * @brief   物理按键轮询去抖模块
 *
 * 硬件:
 *   KEY_LIGHT (补光) → PB5,  按下=低, 释放=高 (内部上拉)
 *   KEY_FEED  (喂食) → PB11, 按下=低, 释放=高 (内部上拉)
 *
 * 用法:
 *   1) 上电:     KEY_Init();
 *   2) 主循环:   KEY_Event_t ev = KEY_Scan();
 *                switch (ev) { case KEY_EVT_LIGHT_PRESS: ... }
 *
 * 算法: 20ms 去抖, 只在"稳定释放 → 稳定按下"的下降沿返回事件.
 */
#ifndef __KEY_H
#define __KEY_H

#include "main.h"

typedef enum {
    KEY_EVT_NONE = 0,        /* 无事件 */
    KEY_EVT_LIGHT_PRESS,     /* KEY_LIGHT 被按下 (一次短按) */
    KEY_EVT_FEED_PRESS,      /* KEY_FEED  被按下 (一次短按) */
} KEY_Event_t;

void         KEY_Init(void);
KEY_Event_t  KEY_Scan(void);

#endif /* __KEY_H */
