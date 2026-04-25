/**
 * @file    light.c
 * @brief   补光灯 on/off 控制 —— PA1
 */
#include "light.h"

static uint8_t s_light_state = 0;

void LIGHT_Init(void)
{
    LIGHT_Off();
}

void LIGHT_On(void)
{
    HAL_GPIO_WritePin(LIGHT_LED_GPIO_Port, LIGHT_LED_Pin, GPIO_PIN_SET);
    s_light_state = 1;
}

void LIGHT_Off(void)
{
    HAL_GPIO_WritePin(LIGHT_LED_GPIO_Port, LIGHT_LED_Pin, GPIO_PIN_RESET);
    s_light_state = 0;
}

void LIGHT_Toggle(void)
{
    if (s_light_state) LIGHT_Off();
    else               LIGHT_On();
}

uint8_t LIGHT_IsOn(void)
{
    return s_light_state;
}
