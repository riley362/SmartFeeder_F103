#include "soft_iic.h"

// 简单的微秒延时，控制I2C速度
// 数字越小速度越快，ADS1115 这种慢速设备建议稍微大一点
static void IIC_Delay(void)
{
    volatile int i = 10; 
    while (i--);
}

// 初始化IIC
void IIC_Init(void)
{
    // GPIO已经在 main.c 的 MX_GPIO_Init 中初始化过了
    // 这里只需要确保总线空闲（拉高）
    IIC_SCL(1);
    IIC_SDA(1);
}

// 产生IIC起始信号
void IIC_Start(void)
{
    IIC_SDA(1);
    IIC_SCL(1);
    IIC_Delay();
    IIC_SDA(0); // SCL高电平时，SDA由高变低 = START
    IIC_Delay();
    IIC_SCL(0); // 钳住I2C总线，准备发送或接收数据
}

// 产生IIC停止信号
void IIC_Stop(void)
{
    IIC_SCL(0);
    IIC_SDA(0);
    IIC_Delay();
    IIC_SCL(1);
    IIC_SDA(1); // SCL高电平时，SDA由低变高 = STOP
    IIC_Delay();
}

// 等待应答信号到来
// 返回值：1，接收应答失败；0，接收应答成功
uint8_t IIC_Wait_Ack(void)
{
    uint8_t ucErrTime = 0;
    IIC_SDA(1); // 释放SDA，让从机去拉低
    IIC_Delay();
    IIC_SCL(1);
    IIC_Delay();
    while (READ_SDA)
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            IIC_Stop();
            return 1;
        }
    }
    IIC_SCL(0); // 时钟输出0
    return 0;
}

// 产生ACK应答
void IIC_Ack(void)
{
    IIC_SCL(0);
    IIC_SDA(0);
    IIC_Delay();
    IIC_SCL(1);
    IIC_Delay();
    IIC_SCL(0);
}

// 不产生ACK应答
void IIC_NAck(void)
{
    IIC_SCL(0);
    IIC_SDA(1);
    IIC_Delay();
    IIC_SCL(1);
    IIC_Delay();
    IIC_SCL(0);
}

// IIC发送一个字节
void IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;
    IIC_SCL(0); // 拉低时钟开始数据传输
    for (t = 0; t < 8; t++)
    {
        IIC_SDA((txd & 0x80) >> 7);
        txd <<= 1;
        IIC_Delay();
        IIC_SCL(1);
        IIC_Delay();
        IIC_SCL(0);
        IIC_Delay();
    }
}

// 读1个字节，ack=1时，发送ACK，ack=0，发送nACK
uint8_t IIC_Read_Byte(uint8_t ack)
{
    unsigned char i, receive = 0;
    IIC_SDA(1); // 释放SDA总线，设置为输入模式（因为是开漏输出，写1即等同于输入）
    for (i = 0; i < 8; i++)
    {
        IIC_SCL(0);
        IIC_Delay();
        IIC_SCL(1);
        receive <<= 1;
        if (READ_SDA) receive++;
        IIC_Delay();
    }
    if (!ack)
        IIC_NAck();
    else
        IIC_Ack();
    return receive;
}

// --- ADS1115 具体读取逻辑 ---
// 假设 ADDR 引脚接地，地址为 0x48 (写=0x90, 读=0x91)
#define ADS_ADDR_W 0x90
#define ADS_ADDR_R 0x91

float ADS1115_Read_Voltage(void)
{
    uint16_t result;
    uint8_t config_H = 0xC3; // 你的配置: OS=1, A0单端, 4.096V量程, 单次模式
    uint8_t config_L = 0x83; // 128SPS, 禁用比较器

    // 1. 写配置寄存器
    IIC_Start();
    IIC_Send_Byte(ADS_ADDR_W);
    if(IIC_Wait_Ack()) { 
        IIC_Stop();
        printf("Error: ADS1115 No ACK (Write Addr)\r\n");
        return -1.0f; 
    } // 如果没应答，返回0
    IIC_Send_Byte(0x01); // 指向配置寄存器
    IIC_Wait_Ack();
    IIC_Send_Byte(config_H);
    IIC_Wait_Ack();
    IIC_Send_Byte(config_L);
    IIC_Wait_Ack();
    IIC_Stop();

    // 2. 延时等待转换 (128SPS 需要约 8ms)
    HAL_Delay(10); 

    // 3. 指向转换结果寄存器
    IIC_Start();
    IIC_Send_Byte(ADS_ADDR_W);
    IIC_Wait_Ack();
    IIC_Send_Byte(0x00); // 指向转换结果寄存器
    IIC_Wait_Ack();
    IIC_Stop();

    // 4. 读取数据
    IIC_Start();
    IIC_Send_Byte(ADS_ADDR_R); // 读模式
    IIC_Wait_Ack();
    result = IIC_Read_Byte(1) << 8; // 读高8位，发送ACK
    result |= IIC_Read_Byte(0);     // 读低8位，发送NACK
    IIC_Stop();

    // 5. 计算电压
    if(result > 32767) result = 0; // 简单的负数归零处理
    
    // 4.096V量程下，1 bit = 0.125mV
    return (float)result * 0.000125f; 
}
