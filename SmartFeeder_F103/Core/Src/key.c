/**
 * @file    key.c
 * @brief   PB5/PB11 物理按键轮询去抖
 *          单按键状态机: 稳定释放 → 候选边沿 → 20ms 保持 → 稳定按下
 *          只在"按下瞬间"返回一次事件, 长按不会重复触发.
 */
#include "key.h"

#define KEY_DEBOUNCE_MS     20U
#define KEY_SCAN_MIN_MS     5U       /* 轮询节流, 避免主循环空转过快 */

/* 单个按键状态 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       stable_level;      /* 上次稳定电平 (1=释放, 0=按下) */
    uint8_t       last_raw_level;    /* 上次采样电平 */
    uint32_t      edge_tick;         /* 最近一次电平变化的 tick */
} key_state_t;

static key_state_t s_keys[2] = {
    { KEY_LIGHT_GPIO_Port, KEY_LIGHT_Pin, 1, 1, 0 },
    { KEY_FEED_GPIO_Port,  KEY_FEED_Pin,  1, 1, 0 },
};

static uint32_t s_last_scan_tick = 0;

void KEY_Init(void)
{
    uint32_t now = HAL_GetTick();
    s_last_scan_tick = now;
    for (uint8_t i = 0; i < 2; i++) {
        s_keys[i].stable_level   = 1;
        s_keys[i].last_raw_level = 1;
        s_keys[i].edge_tick      = now;
    }
}

/* 返回 1 表示该按键产生"释放→按下"的下降沿事件 */
static uint8_t key_update_one(key_state_t *k, uint32_t now)
{
    uint8_t raw = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_SET) ? 1U : 0U;

    if (raw != k->last_raw_level) {
        /* 电平变化了, 开始计时 */
        k->last_raw_level = raw;
        k->edge_tick      = now;
        return 0;
    }

    /* 电平和上次采样一致; 检查是否已保持足够久, 以及与上次"稳定值"是否不同 */
    if (raw != k->stable_level &&
        (uint32_t)(now - k->edge_tick) >= KEY_DEBOUNCE_MS) {
        uint8_t prev = k->stable_level;
        k->stable_level = raw;
        if (prev == 1U && raw == 0U) {
            return 1;   /* 稳定释放 → 稳定按下 */
        }
    }
    return 0;
}

KEY_Event_t KEY_Scan(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_scan_tick) < KEY_SCAN_MIN_MS) return KEY_EVT_NONE;
    s_last_scan_tick = now;

    if (key_update_one(&s_keys[0], now)) return KEY_EVT_LIGHT_PRESS;
    if (key_update_one(&s_keys[1], now)) return KEY_EVT_FEED_PRESS;
    return KEY_EVT_NONE;
}
