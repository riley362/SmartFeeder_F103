/**
 * @file    mqtt_parser.c
 * @brief   MQTT PUBLISH 报文解析 + 命令分发, 详见头文件说明
 */
#include "mqtt_parser.h"
#include "light.h"
#include "motor_28byj.h"
#include "esp8266.h"    /* TOPIC_SUB */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 共享给 main.c 的全局 (main.c 里定义) */
extern volatile uint32_t feed_count_today;
extern volatile uint8_t  g_manual_mode;

/* ============================================================
 *  环形缓冲 (ISR 写 / 主循环读, size 必须是 2 的幂, 以便用位掩码)
 * ============================================================ */
#define RB_SIZE   512u
#define RB_MASK   (RB_SIZE - 1u)

static volatile uint8_t  rb[RB_SIZE];
static volatile uint16_t rb_head = 0;   /* writer index (ISR)  */
static volatile uint16_t rb_tail = 0;   /* reader index (main) */

void MQTT_RX_Feed(uint8_t byte)
{
    uint16_t next = (uint16_t)((rb_head + 1u) & RB_MASK);
    if (next != rb_tail) {
        rb[rb_head] = byte;
        rb_head = next;
    }
    /* 缓冲满时直接丢弃本字节, 不阻塞 ISR */
}

static uint16_t rb_count(void)
{
    return (uint16_t)((rb_head - rb_tail) & RB_MASK);
}

static uint8_t rb_peek(uint16_t offset)
{
    return rb[(rb_tail + offset) & RB_MASK];
}

static void rb_advance(uint16_t n)
{
    rb_tail = (uint16_t)((rb_tail + n) & RB_MASK);
}

/* ============================================================
 *  命令分发
 * ============================================================ */
static void dispatch_command(const char *p)
{
    /* --- 补光灯 --- */
    if (strstr(p, "\"cmd\":\"light\"")) {
        if (strstr(p, "\"value\":\"on\"")) {
            LIGHT_On();
            printf("[CMD ] LIGHT -> ON\r\n");
        } else if (strstr(p, "\"value\":\"off\"")) {
            LIGHT_Off();
            printf("[CMD ] LIGHT -> OFF\r\n");
        }
        return;
    }

    /* --- 投喂 --- */
    if (strstr(p, "\"cmd\":\"feed\"")) {
        /* 从 JSON 里抽 amount 数值 */
        uint16_t grams = 30;   /* 默认值, 防止字段缺失 */
        const char *a = strstr(p, "\"amount\"");
        if (a) {
            a = strchr(a, ':');
            if (a) {
                a++;
                while (*a == ' ' || *a == '"') a++;
                int v = atoi(a);
                if (v > 0 && v <= 500) grams = (uint16_t)v;
            }
        }
        if (!Motor_IsBusy()) {
            Motor_StartFeed(grams);
            feed_count_today++;
            printf("[CMD ] FEED -> %u g (total=%lu)\r\n",
                   (unsigned)grams, (unsigned long)feed_count_today);
        } else {
            printf("[CMD ] FEED ignored: motor busy (%lu steps left)\r\n",
                   (unsigned long)Motor_GetRemainingSteps());
        }
        return;
    }

    /* --- 运行模式 --- */
    if (strstr(p, "\"cmd\":\"mode\"")) {
        if (strstr(p, "\"value\":\"auto\"")) {
            g_manual_mode = 0;
            printf("[CMD ] MODE -> AUTO\r\n");
        } else if (strstr(p, "\"value\":\"manual\"")) {
            g_manual_mode = 1;
            printf("[CMD ] MODE -> MANUAL\r\n");
        }
        return;
    }

    /* --- 兼容 Bemfa 控制台手动发的裸字符串 --- */
    if (strcmp(p, "on") == 0) {
        LIGHT_On();
        printf("[CMD ] LIGHT -> ON (raw)\r\n");
    } else if (strcmp(p, "off") == 0) {
        LIGHT_Off();
        printf("[CMD ] LIGHT -> OFF (raw)\r\n");
    }
}

/* ============================================================
 *  MQTT PUBLISH 报文解析
 *
 *  QoS 0 PUBLISH 报文格式:
 *      byte 0              0x30  (type=3, flags=0)
 *      byte 1..(1+vi-1)    remaining length (MQTT varint, 最多 4 字节)
 *      byte ...            topic length MSB/LSB (2B)
 *      byte ...            topic string
 *      byte ...            payload (剩余字节)
 *
 *  本项目不处理 QoS>0 / DUP / RETAIN, 小程序和硬件都用 QoS 0
 * ============================================================ */
void MQTT_RX_Process(void)
{
    while (rb_count() > 0) {
        /* 1. 同步: 首字节必须是 0x30, 否则丢 1 字节重试
         *    CONNACK(0x20) / SUBACK(0x90) / PINGRESP(0xD0) 等通过这种
         *    方式被扫过, 我们只关心 PUBLISH */
        if (rb_peek(0) != 0x30) {
            rb_advance(1);
            continue;
        }

        /* 2. 需要至少 2 字节 (header + 首字节 varint) */
        if (rb_count() < 2) return;

        /* 3. 解码 remaining length varint (MQTT 规范 1~4 字节) */
        uint32_t remaining = 0;
        uint32_t mult      = 1;
        uint8_t  vi_len    = 0;
        uint8_t  vi_ok     = 0;
        for (uint8_t i = 0; i < 4; i++) {
            if (rb_count() < (uint16_t)(1u + i + 1u)) {
                /* varint 还没收齐, 等下一轮 */
                return;
            }
            uint8_t c = rb_peek((uint16_t)(1u + i));
            remaining += (uint32_t)(c & 0x7Fu) * mult;
            vi_len++;
            if ((c & 0x80u) == 0) { vi_ok = 1; break; }
            mult <<= 7;
        }
        if (!vi_ok) {
            /* varint 格式非法, 跳过这个 0x30 */
            rb_advance(1);
            continue;
        }

        uint32_t total_len = 1u + vi_len + remaining;

        /* 单报文过大, 我们的缓冲也装不下, 丢弃同步字节 */
        if (total_len > RB_SIZE) {
            rb_advance(1);
            continue;
        }

        /* 4. 整个报文还没到齐, 先返回, 下次再处理 */
        if (rb_count() < total_len) return;

        /* 5. 拆 topic length / topic / payload */
        uint16_t topic_len = (uint16_t)(
            ((uint16_t)rb_peek((uint16_t)(1u + vi_len)) << 8) |
             (uint16_t)rb_peek((uint16_t)(1u + vi_len + 1u))
        );

        if ((uint32_t)(topic_len + 2u) > remaining) {
            /* 长度字段异常, 丢同步字节重试 */
            rb_advance(1);
            continue;
        }

        static char topic  [32];
        static char payload[192];

        uint16_t topic_start   = (uint16_t)(1u + vi_len + 2u);
        uint16_t payload_start = (uint16_t)(topic_start + topic_len);
        uint32_t payload_len   = remaining - 2u - topic_len;

        uint16_t t_copy = (topic_len < (uint16_t)(sizeof(topic) - 1))
                          ? topic_len : (uint16_t)(sizeof(topic) - 1);
        for (uint16_t i = 0; i < t_copy; i++) {
            topic[i] = (char)rb_peek((uint16_t)(topic_start + i));
        }
        topic[t_copy] = 0;

        uint16_t p_copy = (payload_len < (uint32_t)(sizeof(payload) - 1))
                          ? (uint16_t)payload_len : (uint16_t)(sizeof(payload) - 1);
        for (uint16_t i = 0; i < p_copy; i++) {
            payload[i] = (char)rb_peek((uint16_t)(payload_start + i));
        }
        payload[p_copy] = 0;

        /* 6. 消费整个报文 */
        rb_advance((uint16_t)total_len);

        printf("[MQTT RX] topic=%s payload=%s\r\n", topic, payload);

        /* 7. 只接受我们订阅的下行主题 */
        if (strcmp(topic, TOPIC_SUB) == 0) {
            dispatch_command(payload);
        }
    }
}
