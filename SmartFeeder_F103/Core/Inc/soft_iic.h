#ifndef __SOFT_IIC_H
#define __SOFT_IIC_H

#include "main.h"

// --- 引脚定义 (对应 PB6, PB7) ---
// 如果你改了引脚，只需要改这里
#define IIC_SCL_PORT    GPIOB
#define IIC_SCL_PIN     GPIO_PIN_6
#define IIC_SDA_PORT    GPIOB
#define IIC_SDA_PIN     GPIO_PIN_7

// --- IO操作宏 (为了速度和可读性) ---
#define IIC_SCL(x)      HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SCL_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define IIC_SDA(x)      HAL_GPIO_WritePin(IIC_SDA_PORT, IIC_SDA_PIN, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define READ_SDA        HAL_GPIO_ReadPin(IIC_SDA_PORT, IIC_SDA_PIN)

// --- 函数声明 ---
void IIC_Init(void);                // 初始化
void IIC_Start(void);               // 发送开始信号
void IIC_Stop(void);                // 发送停止信号
void IIC_Send_Byte(uint8_t txd);    // 发送一个字节
uint8_t IIC_Read_Byte(uint8_t ack); // 读取一个字节
uint8_t IIC_Wait_Ack(void);         // 等待ACK
void IIC_Ack(void);                 // 发送ACK
void IIC_NAck(void);                // 发送NACK

// --- ADS1115 相关 ---
float ADS1115_Read_Voltage(void);   // 顶层读取函数

#endif
