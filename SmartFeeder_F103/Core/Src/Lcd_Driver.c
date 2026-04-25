#include "Lcd_Driver.h"
#include "spi.h" // 引入硬件 SPI 句柄

// 引入 CubeMX 自动生成的硬件 SPI1 句柄
extern SPI_HandleTypeDef hspi1; 

// ========================================================
// 核心修改：抛弃缓慢的软件模拟 SPI，换成极速的硬件 SPI 发送
// ========================================================
void SPI_WriteData(u8 Data)
{
    // 调用 HAL 库硬件发送，超时时间设为 10ms
    HAL_SPI_Transmit(&hspi1, &Data, 1, 10);
}

// 向液晶屏写一个8位指令
void Lcd_WriteIndex(u8 Index)
{
    LCD_CS_CLR;
    LCD_RS_CLR; // 写指令时 RS 拉低
    SPI_WriteData(Index);
    LCD_CS_SET;
}

// 向液晶屏写一个8位数据
void Lcd_WriteData(u8 Data)
{
    LCD_CS_CLR;
    LCD_RS_SET; // 写数据时 RS 拉高
    SPI_WriteData(Data);
    LCD_CS_SET; 
}

// 向液晶屏写一个16位数据
void LCD_WriteData_16Bit(u16 Data)
{
    LCD_CS_CLR;
    LCD_RS_SET;
    SPI_WriteData(Data >> 8);   // 写入高8位数据
    SPI_WriteData(Data & 0xFF); // 写入低8位数据
    LCD_CS_SET; 
}

void Lcd_WriteReg(u8 Index, u8 Data)
{
    Lcd_WriteIndex(Index);
    Lcd_WriteData(Data);
}

void Lcd_Reset(void)
{
    /* ST7735S spec: RST low >= 10us, RST release -> internal ready >= 120 ms.
     * 实测:  RST 释放后的延时如果不够 (旧代码只等 50ms), LCD 内部尚未启动
     *        就开始接收命令, 初始化会静默失败, 整屏白色.
     * 这里给足 150 ms, 余量充足也只多等 0.1 秒. */
    LCD_RST_CLR;
    HAL_Delay(20);
    LCD_RST_SET;
    HAL_Delay(150);
}

// ST7735S init sequence (ported from vendor demo that works on this panel)
void Lcd_Init(void)
{
    Lcd_Reset();

    Lcd_WriteIndex(0x11);             // Sleep out
    HAL_Delay(120);

    /* Frame rate control */
    Lcd_WriteIndex(0xB1);
    Lcd_WriteData(0x05); Lcd_WriteData(0x3C); Lcd_WriteData(0x3C);
    Lcd_WriteIndex(0xB2);
    Lcd_WriteData(0x05); Lcd_WriteData(0x3C); Lcd_WriteData(0x3C);
    Lcd_WriteIndex(0xB3);
    Lcd_WriteData(0x05); Lcd_WriteData(0x3C); Lcd_WriteData(0x3C);
    Lcd_WriteData(0x05); Lcd_WriteData(0x3C); Lcd_WriteData(0x3C);

    /* Dot inversion */
    Lcd_WriteIndex(0xB4);
    Lcd_WriteData(0x03);

    /* Power sequence */
    Lcd_WriteIndex(0xC0);
    Lcd_WriteData(0x28); Lcd_WriteData(0x08); Lcd_WriteData(0x04);
    Lcd_WriteIndex(0xC1);
    Lcd_WriteData(0xC0);
    Lcd_WriteIndex(0xC2);
    Lcd_WriteData(0x0D); Lcd_WriteData(0x00);
    Lcd_WriteIndex(0xC3);
    Lcd_WriteData(0x8D); Lcd_WriteData(0x2A);
    Lcd_WriteIndex(0xC4);
    Lcd_WriteData(0x8D); Lcd_WriteData(0xEE);

    /* VCOM */
    Lcd_WriteIndex(0xC5);
    Lcd_WriteData(0x1A);

    /* Memory access: MX, MY, RGB */
    Lcd_WriteIndex(0x36);
    Lcd_WriteData(0xC0);

    /* Gamma */
    Lcd_WriteIndex(0xE0);
    Lcd_WriteData(0x04); Lcd_WriteData(0x22); Lcd_WriteData(0x07); Lcd_WriteData(0x0A);
    Lcd_WriteData(0x2E); Lcd_WriteData(0x30); Lcd_WriteData(0x25); Lcd_WriteData(0x2A);
    Lcd_WriteData(0x28); Lcd_WriteData(0x26); Lcd_WriteData(0x2E); Lcd_WriteData(0x3A);
    Lcd_WriteData(0x00); Lcd_WriteData(0x01); Lcd_WriteData(0x03); Lcd_WriteData(0x13);
    Lcd_WriteIndex(0xE1);
    Lcd_WriteData(0x04); Lcd_WriteData(0x16); Lcd_WriteData(0x06); Lcd_WriteData(0x0D);
    Lcd_WriteData(0x2D); Lcd_WriteData(0x26); Lcd_WriteData(0x23); Lcd_WriteData(0x27);
    Lcd_WriteData(0x27); Lcd_WriteData(0x25); Lcd_WriteData(0x2D); Lcd_WriteData(0x3B);
    Lcd_WriteData(0x00); Lcd_WriteData(0x01); Lcd_WriteData(0x04); Lcd_WriteData(0x13);

    /* 16-bit color (65k) */
    Lcd_WriteIndex(0x3A);
    Lcd_WriteData(0x05);

    /* Display on */
    Lcd_WriteIndex(0x29);
}

/* ST7735S 1.8" panel has a 2-pixel X offset and 1-pixel Y offset (USE_HORIZONTAL=1) */
void Lcd_SetRegion(u16 x_start, u16 y_start, u16 x_end, u16 y_end)
{
    Lcd_WriteIndex(0x2A);
    Lcd_WriteData(0x00); Lcd_WriteData(x_start + 2);
    Lcd_WriteData(0x00); Lcd_WriteData(x_end   + 2);

    Lcd_WriteIndex(0x2B);
    Lcd_WriteData(0x00); Lcd_WriteData(y_start + 1);
    Lcd_WriteData(0x00); Lcd_WriteData(y_end   + 1);

    Lcd_WriteIndex(0x2C);
}

void Lcd_SetXY(u16 x, u16 y)
{
    Lcd_SetRegion(x, y, x, y);
}

void Gui_DrawPoint(u16 x, u16 y, u16 Data)
{
    Lcd_SetRegion(x, y, x + 1, y + 1);
    LCD_WriteData_16Bit(Data);
}    

void Lcd_Clear(u16 Color)
{
    unsigned int i, m;
    Lcd_SetRegion(0, 0, X_MAX_PIXEL - 1, Y_MAX_PIXEL - 1);
    for (i = 0; i < X_MAX_PIXEL; i++)
    {
        for (m = 0; m < Y_MAX_PIXEL; m++)
        {
            LCD_WriteData_16Bit(Color);
        }
    }
}