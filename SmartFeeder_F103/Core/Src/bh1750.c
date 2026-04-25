/*
 * bh1750.c
 *
 * Created on: 2025年12月11日
 * Author: riley
 */
#include "bh1750.h"
#include "soft_iic.h" // 引入你的软件IIC头文件
#include <stdio.h>

// 初始化 BH1750
void BH1750_Init(void) {
    IIC_Start();
    IIC_Send_Byte(BH1750_ADDR); // 发送设备地址 (写方向, 默认通常是 0x46)
    
    if (IIC_Wait_Ack()) {
        IIC_Stop();
        printf("[BH1750] Error: No ACK on Init!\r\n"); // 如果没接好，串口会报警
        return; 
    }
    
    IIC_Send_Byte(0x01); // Power On 指令
    IIC_Wait_Ack();
    IIC_Stop();
    
    HAL_Delay(10); // 等待电源稳定
}

// 发送测量指令 (不等待, 调用后需外部等待 >=180ms 再读结果)
void BH1750_StartMeasure(void) {
    IIC_Start();
    IIC_Send_Byte(BH1750_ADDR);
    if (IIC_Wait_Ack()) {
        IIC_Stop();
        return;
    }
    IIC_Send_Byte(0x10); 
    IIC_Wait_Ack();
    IIC_Stop();
}

// 读取测量结果 (需在 StartMeasure 后等待 >=180ms 再调用)
float BH1750_ReadResult(void) {
    uint16_t result = 0;

    IIC_Start();
    IIC_Send_Byte(BH1750_ADDR | 0x01); 
    if (IIC_Wait_Ack()) {
        IIC_Stop();
        return 0.0f; 
    }

    result = IIC_Read_Byte(1); 
    result <<= 8;
    result |= IIC_Read_Byte(0); 
    IIC_Stop();

    return (float)(result / 1.2f);
}

// 读取 BH1750 光照强度 (阻塞版, 兼容旧调用)
float BH1750_ReadLight(void) {
    BH1750_StartMeasure();
    HAL_Delay(180);
    return BH1750_ReadResult();
}