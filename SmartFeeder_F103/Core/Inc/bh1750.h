/*
 * bh1750.h
 *
 *  Created on: 2025年12月11日
 *      Author: riley
 */

#ifndef __BH1750_H
#define __BH1750_H

#include "main.h"

// ADDR引脚接地时，地址为0x23，左移一位供HAL库使用 => 0x46
#define BH1750_ADDR (0x23 << 1)

void  BH1750_Init(void);
float BH1750_ReadLight(void);
void  BH1750_StartMeasure(void);
float BH1750_ReadResult(void);

#endif
