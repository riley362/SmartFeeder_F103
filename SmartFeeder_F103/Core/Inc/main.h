/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* PA2 / PA3 are used by USART2 (ESP8266), no longer GPIO */
#define LIGHT_LED_Pin GPIO_PIN_1
#define LIGHT_LED_GPIO_Port GPIOA
#define LCD_RES_Pin GPIO_PIN_4
#define LCD_RES_GPIO_Port GPIOA
#define LCD_DCA6_Pin GPIO_PIN_6
#define LCD_DCA6_GPIO_Port GPIOA
#define WATER_SENSOR_Pin GPIO_PIN_0
#define WATER_SENSOR_GPIO_Port GPIOB
#define FOOD_SENSOR_Pin GPIO_PIN_1
#define FOOD_SENSOR_GPIO_Port GPIOB
#define KEY_LIGHT_Pin GPIO_PIN_5
#define KEY_LIGHT_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_10
#define LCD_RST_GPIO_Port GPIOB
#define KEY_FEED_Pin GPIO_PIN_11
#define KEY_FEED_GPIO_Port GPIOB
#define MOTOR_IN1_Pin GPIO_PIN_12
#define MOTOR_IN1_GPIO_Port GPIOB
#define MOTOR_IN2_Pin GPIO_PIN_13
#define MOTOR_IN2_GPIO_Port GPIOB
#define MOTOR_IN3_Pin GPIO_PIN_14
#define MOTOR_IN3_GPIO_Port GPIOB
#define MOTOR_IN4_Pin GPIO_PIN_15
#define MOTOR_IN4_GPIO_Port GPIOB
#define SCL_Pin GPIO_PIN_6
#define SCL_GPIO_Port GPIOB
#define SDA_Pin GPIO_PIN_7
#define SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
