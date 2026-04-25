#ifndef __LCD_DRIVER_H
#define __LCD_DRIVER_H

#include "main.h" // 引入 HAL 库核心头文件

// 兼容老代码的类型定义
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

// 屏幕分辨率定义 (如果你的 LCD_Config.h 里没有，请打开注释)
#define X_MAX_PIXEL	128
#define Y_MAX_PIXEL	160

// 常用颜色定义
#define RED  	0xf800
#define GREEN	0x07e0
#define BLUE 	0x001f
#define WHITE	0xffff
#define BLACK	0x0000
#define YELLOW  0xFFE0
#define GRAY0   0xEF7D   
#define GRAY1   0x8410      
#define GRAY2   0x4208      

// ========================================================
// 核心修改区：替换为 HAL 库的引脚控制宏 (对齐之前的硬件分配)
// ========================================================

// CS: PA4
#define LCD_CS_CLR  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define LCD_CS_SET  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

// RS (DC): PA6 
#define LCD_RS_CLR  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET)
#define LCD_RS_SET  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET)

// RST: PB10
#define LCD_RST_CLR HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET)
#define LCD_RST_SET HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET)

// ========================================================

// 底层驱动函数声明
void Lcd_WriteIndex(u8 Index);
void Lcd_WriteData(u8 Data);
void Lcd_WriteReg(u8 Index,u8 Data);
void Lcd_Reset(void);
void Lcd_Init(void);
void Lcd_Clear(u16 Color);
void Lcd_SetXY(u16 x,u16 y);
void Gui_DrawPoint(u16 x,u16 y,u16 Data);
void Lcd_SetRegion(u16 x_start,u16 y_start,u16 x_end,u16 y_end);
void LCD_WriteData_16Bit(u16 Data);

#endif