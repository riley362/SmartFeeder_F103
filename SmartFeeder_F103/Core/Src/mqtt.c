#include "usart.h"
#include <string.h>
#include <stdio.h>
#include "mqtt.h"

// 发送二进制数据 (辅助函数)
void UART_SendBytes(uint8_t* data, uint16_t len) {
    HAL_UART_Transmit(&huart2, data, len, 100);
}

// 1. 发送 MQTT 连接包 (CONNECT)
void MQTT_Connect(char* client_id) {
    uint8_t packet[200] = {0};
    uint16_t id_len = strlen(client_id);

    // 1. 计算剩余长度 
    // Variable Header (10字节) + Payload (ID长度2字节 + ID字符串)
    uint16_t total_len = 10 + 2 + id_len;

    // 2. Fixed Header (固定头)
    packet[0] = 0x10; // CONNECT 类型
    packet[1] = total_len; // 剩余长度

    // 3. Variable Header (可变头，共10字节)
    // 包含: Protocol Name(6) + Level(1) + Flags(1) + KeepAlive(2)
    uint8_t var_header[] = {
        0x00, 0x04, 'M', 'Q', 'T', 'T', // Protocol Name "MQTT"
        0x04,                           // Level 3.1.1
        0x02,                           // Flags (Clean Session)
        0x00, 0x3C                      // Keep Alive (60秒)
    };
    
    // 【核心修正 A】这里必须拷贝 10 个字节！之前写成 8 导致缺了心跳
    memcpy(&packet[2], var_header, 10);

    // 4. Payload (负载)
    // 【核心修正 B】ID长度应该从第 12 个字节开始 (2 + 10)
    packet[12] = (id_len >> 8) & 0xFF; // 长度高8位
    packet[13] = id_len & 0xFF;        // 长度低8位

    // 【核心修正 C】ID字符串应该从第 14 个字节开始
    memcpy(&packet[14], client_id, id_len);

    // 5. 发送
    // 总长度 = 固定头(2) + 剩余长度(total_len)
    UART_SendBytes(packet, total_len + 2);
    
    HAL_Delay(500); // 稍微等待服务器响应
}

// PUBLISH
void MQTT_Publish(char* topic, char* payload) {
    uint8_t packet[256] = {0};
    uint16_t topic_len = strlen(topic);
    uint16_t payload_len = strlen(payload);
    uint16_t total_len = 2 + topic_len + payload_len;

    // Fixed Header (0x30 = PUBLISH)
    packet[0] = 0x30;
    // 剩余长度 (这里假设长度小于127字节，简单处理)
    packet[1] = total_len; 

    // Variable Header (Topic Length + Topic Name)
    packet[2] = (topic_len >> 8) & 0xFF;
    packet[3] = topic_len & 0xFF;
    memcpy(&packet[4], topic, topic_len);

    // Payload (消息内容)
    memcpy(&packet[4 + topic_len], payload, payload_len);

    // 发送
    UART_SendBytes(packet, total_len + 2);
}
// SUBSCRIBE
void MQTT_Subscribe(char* topic) {
    uint8_t packet[128] = {0};
    uint16_t topic_len = strlen(topic);
    
    // 剩余长度 = 报文标识符(2) + 主题长度(2) + 主题字符串 + QoS(1)
    uint16_t total_len = 2 + 2 + topic_len + 1;

    packet[0] = 0x82; // SUBSCRIBE 类型 (QoS 1)
    packet[1] = total_len;

    // 报文标识符 (随便填个非零数，比如 0x00 0x0A)
    packet[2] = 0x00;
    packet[3] = 0x0A;

    // 主题长度和内容
    packet[4] = (topic_len >> 8) & 0xFF;
    packet[5] = topic_len & 0xFF;
    memcpy(&packet[6], topic, topic_len);

    // QoS级别 (0)
    packet[6 + topic_len] = 0x00;

    // 发送
    UART_SendBytes(packet, total_len + 2);
    HAL_Delay(200);
}

// PINGREQ
void MQTT_PingREQ(void) {
    uint8_t packet[2] = {0xC0, 0x00}; // PINGREQ 报文固定就这两个字节
    UART_SendBytes(packet, 2);
}