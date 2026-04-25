/**
 * @file    motor_28byj.c
 * @brief   28BYJ-48 + ULN2003 半步 8 相序非阻塞驱动
 */
#include "motor_28byj.h"

/* 每半步的间隔 (ms). 低于 2ms 28BYJ 易失步. */
#define HALF_STEP_INTERVAL_MS   2U

/**
 * 半步 8 相序 (顺时针):
 *   bit0=IN1 (PB12), bit1=IN2 (PB13), bit2=IN3 (PB14), bit3=IN4 (PB15)
 *
 *   拍次:   1    2    3    4    5    6    7    8
 *   IN1 :   1    1    0    0    0    0    0    1
 *   IN2 :   0    1    1    1    0    0    0    0
 *   IN3 :   0    0    0    1    1    1    0    0
 *   IN4 :   0    0    0    0    0    1    1    1
 */
static const uint8_t s_half_step_seq[8] = {
    0x01,  /* IN1 */
    0x03,  /* IN1 + IN2 */
    0x02,  /* IN2 */
    0x06,  /* IN2 + IN3 */
    0x04,  /* IN3 */
    0x0C,  /* IN3 + IN4 */
    0x08,  /* IN4 */
    0x09,  /* IN4 + IN1 */
};

static volatile uint32_t s_remaining_steps = 0;
static uint32_t s_last_step_tick = 0;
static uint8_t  s_seq_idx        = 0;

static void motor_write_phase(uint8_t pattern)
{
    HAL_GPIO_WritePin(MOTOR_IN1_GPIO_Port, MOTOR_IN1_Pin,
                      (pattern & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_GPIO_Port, MOTOR_IN2_Pin,
                      (pattern & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN3_GPIO_Port, MOTOR_IN3_Pin,
                      (pattern & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN4_GPIO_Port, MOTOR_IN4_Pin,
                      (pattern & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Motor_Init(void)
{
    motor_write_phase(0x00);   /* 四相全断电, 线圈冷却 */
    s_remaining_steps = 0;
    s_seq_idx         = 0;
    s_last_step_tick  = HAL_GetTick();
}

void Motor_StartFeed(uint16_t grams)
{
    if (grams == 0)                return;
    if (s_remaining_steps != 0)    return;   /* 已在投喂, 丢弃重复请求 */

    s_remaining_steps = (uint32_t)grams * MOTOR_STEPS_PER_GRAM;
    s_last_step_tick  = HAL_GetTick();
    /* 相序保持上次位置, 保证连续两次投喂不抖动 */
}

void Motor_Update(void)
{
    if (s_remaining_steps == 0) return;

    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_step_tick) < HALF_STEP_INTERVAL_MS) return;
    s_last_step_tick = now;

    motor_write_phase(s_half_step_seq[s_seq_idx]);
    s_seq_idx = (s_seq_idx + 1U) & 0x07U;
    s_remaining_steps--;

    if (s_remaining_steps == 0) {
        /* 投喂结束: 立刻断电, 避免长时间通电发热 */
        motor_write_phase(0x00);
    }
}

uint8_t Motor_IsBusy(void)
{
    return (s_remaining_steps != 0) ? 1U : 0U;
}

uint32_t Motor_GetRemainingSteps(void)
{
    return s_remaining_steps;
}
