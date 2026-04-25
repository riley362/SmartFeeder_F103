#include "esp8266.h"
#include <string.h>
#include <stdio.h>
#include "usart.h" // 引用 huart2

// 定义一个足够大的接收缓冲区
char esp_rx_buf[256];

/**
 * @brief  发送 AT 指令并等待期望的回复
 * @param  cmd: 要发送的 AT 指令
 * @param  ack: 期望收到的回复 (如 "OK")
 * @param  wait_ms: 等待超时时间 (毫秒)
 * @return 1: 成功, 0: 失败
 */
uint8_t ESP_Send_Cmd(char* cmd, char* ack, uint32_t wait_ms)
{
    // 1. 清空接收缓存
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
    
    // 2. 发送指令 (通过 UART2)
    HAL_UART_Transmit(&huart2, (uint8_t*)cmd, strlen(cmd), 100);
    
    // 3. 阻塞接收回复 (为了简单，这里使用阻塞接收)
    // 注意：实际项目中建议用 DMA+空闲中断，但初始化阶段阻塞是可以的
    HAL_UART_Receive(&huart2, (uint8_t*)esp_rx_buf, sizeof(esp_rx_buf)-1, wait_ms);
    
    // 4. 检查是否包含期望的回复字符串
    if(strstr(esp_rx_buf, ack))
    {
        return 1; // 成功
    }
    return 0; // 失败
}

/**
 * @brief  ESP8266 初始化流程 (TCP 透传模式)
 */
void ESP8266_Init(void)
{
    char cmd_buf[128];
    
    printf("ESP8266: Initializing...\r\n");

    // 0. 退出透传 (防止之前已经在透传模式，发AT指令不理人)
    HAL_UART_Transmit(&huart2, (uint8_t*)"+++", 3, 100);
    HAL_Delay(1000);

    // 1. 测试 AT
    // 只要有回复 OK 就说明模块活了
    if(ESP_Send_Cmd("AT\r\n", "OK", 1000))
    {
        printf("ESP8266: AT Check OK\r\n");
    }
    else
    {
        printf("ESP8266: No Response! Check Wiring!\r\n");
        // 如果这里死循环，说明线没接对或者 EN 没拉高
        // return; 
    }

    // 2. 恢复出厂设置 (可选，防止之前的设置干扰)
    // ESP_Send_Cmd("AT+RESTORE\r\n", "ready", 3000); 

    // 3. 设置为 Station 模式
    ESP_Send_Cmd("AT+CWMODE=1\r\n", "OK", 1000);

    // 4. 连接 WiFi
    // 这一步时间比较长，给它 10 秒
    sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    printf("ESP8266: Connecting to WiFi...\r\n");
    if(ESP_Send_Cmd(cmd_buf, "OK", 10000)) // 等待 10秒
    {
        printf("ESP8266: WiFi Connected!\r\n");
    }
    else
    {
        printf("ESP8266: WiFi Connect Failed!\r\n");
    }

    // 5. 建立 TCP 连接
    sprintf(cmd_buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", SERVER_IP, SERVER_PORT);
    printf("ESP8266: Connecting to Server...\r\n");
    if(ESP_Send_Cmd(cmd_buf, "OK", 5000))
    {
        printf("ESP8266: Server Connected!\r\n");
    }
    else
    {
        printf("ESP8266: Server Connect Failed!\r\n");
    }

    // 6. 开启透传模式
    ESP_Send_Cmd("AT+CIPMODE=1\r\n", "OK", 1000);

    // 7. 开始发送数据
    // 发送这条指令后，模块回复 ">"，之后串口发什么，它就转得什么
    if(ESP_Send_Cmd("AT+CIPSEND\r\n", ">", 1000))
    {
        printf("ESP8266: Passthrough Mode Enabled. Ready to send!\r\n");
    }
}