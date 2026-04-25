#ifndef __MQTT_H__
#define __MQTT_H__

#include "main.h"  

// --- 函数声明 (对外开放的接口) ---

/**
 * @brief 连接 MQTT 服务器
 * @param client_id 客户端ID (唯一标识)
 */
void MQTT_Connect(char* client_id);

/**
 * @brief 发布消息
 * @param topic 主题
 * @param payload 消息内容
 */
void MQTT_Publish(char* topic, char* payload);
void MQTT_Subscribe(char* topic); 
void MQTT_PingREQ(void);
#endif /* __MQTT_H__ */