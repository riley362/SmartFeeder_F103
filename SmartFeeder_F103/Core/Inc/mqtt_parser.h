/**
 * @file    mqtt_parser.h
 * @brief   MQTT 下行报文解析器 + 命令分发
 *
 * 数据流:
 *     ESP8266 (透传模式) --UART2 RX--> STM32
 *        -> UART2 RxCpltCallback 调 MQTT_RX_Feed 逐字节入环形缓冲
 *        -> 主循环调 MQTT_RX_Process 解析 MQTT PUBLISH 报文
 *        -> 识别 topic == TOPIC_SUB 后, 解析 JSON 并分发到
 *           LIGHT_On / LIGHT_Off / Motor_StartFeed 等
 *
 * 小程序 dashboard.js 的下行 JSON 协议:
 *     {"cmd":"light","value":"on"}       -> 开补光灯
 *     {"cmd":"light","value":"off"}      -> 关补光灯
 *     {"cmd":"feed","amount":50}         -> 投喂 50g
 *     {"cmd":"mode","value":"auto"}      -> 切自动模式
 *     {"cmd":"mode","value":"manual"}    -> 切手动模式
 *
 * 兼容: Bemfa 控制台手工测试时直接发 "on" / "off" 也能工作.
 */
#ifndef __MQTT_PARSER_H
#define __MQTT_PARSER_H

#include <stdint.h>

/**
 * @brief  从 UART2 RX 中断里调用, 把刚收到的一字节塞入解析器的环形缓冲
 *         必须是非阻塞的, 禁止在里面做 printf / strcmp 等耗时操作
 */
void MQTT_RX_Feed(uint8_t byte);

/**
 * @brief  主循环定期调用, 消费环形缓冲, 抽取完整的 MQTT PUBLISH 报文,
 *         解析 topic/payload 并分发命令.  单次最多处理缓冲中现有数据,
 *         不会阻塞
 */
void MQTT_RX_Process(void);

#endif /* __MQTT_PARSER_H */
