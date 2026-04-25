# 基于STM32的智能宠物喂食系统——核心代码总结

---

## 一、系统总体架构

```
┌─────────────────────────────────────────────────────────┐
│                    微信小程序 (云端)                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ 状态纵览  │  │ 远程控制  │  │ 定时喂食  │  │时间同步  │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
│       │             │             │              │       │
│       └──────┬──────┴─────────────┴──────────────┘       │
│              │  MQTT over WebSocket (wss://bemfa.com)    │
└──────────────┼──────────────────────────────────────────┘
               │
        ┌──────┴──────┐  巴法云 MQTT Broker
        │  bemfa.com  │  feeder003 (上行)
        │  :9501 TCP  │  feederctrl003 (下行)
        └──────┬──────┘
               │
┌──────────────┼──────────────────────────────────────────┐
│   STM32F103  │  ESP8266 (UART2, TCP透传)                 │
│              │                                           │
│  ┌───────────┴───────────┐                               │
│  │      主控制循环 main.c │                               │
│  │  ┌─────┐ ┌─────┐     │  ┌─────────┐  ┌───────────┐  │
│  │  │传感器│ │执行器│     │  │ K210 AI  │  │ 1.44"LCD  │  │
│  │  │采集  │ │控制  │     │  │ 宠物识别 │  │ 状态显示  │  │
│  │  └─────┘ └─────┘     │  └─────────┘  └───────────┘  │
│  └───────────────────────┘                               │
└─────────────────────────────────────────────────────────┘
```

### 硬件模块清单

| 模块 | 型号/引脚 | 通信方式 | 功能 |
|------|----------|---------|------|
| 主控芯片 | STM32F103C8T6 | — | 系统核心控制器 |
| WiFi模块 | ESP8266 (UART2) | UART+TCP透传 | 网络通信 |
| AI摄像头 | K210 (UART1) | UART中断 | 宠物识别 |
| 光照传感器 | BH1750 (PB6/PB7) | 软件I2C | 环境光检测 |
| 空气传感器 | MQ-135 (PA0) | ADC | 空气质量检测 |
| 水位传感器 | 水位开关 (PB0) | GPIO | 饮水余量检测 |
| 食物传感器 | 红外对管 (PB1) | GPIO | 食物余量检测 |
| 步进电机 | 28BYJ-48+ULN2003 (PB12~15) | GPIO | 投喂执行器 |
| 补光灯 | LED (PA1) | GPIO | 环境补光 |
| 显示屏 | 1.44" TFT LCD (SPI1) | SPI | 状态显示 |
| 实时时钟 | STM32内置RTC | — | 时间计时 |
| 物理按键 | KEY1(PB5) KEY2(PB11) | GPIO | 本地手动操作 |

---

## 二、各模块核心代码

---

### 2.1 ESP8266 WiFi模块初始化 (`esp8266.c`)

ESP8266通过AT指令集配置为Station模式，连接WiFi后建立TCP透传连接至巴法云MQTT服务器。

```c
/**
 * @brief  发送AT指令并等待期望的回复
 * @param  cmd:     要发送的AT指令
 * @param  ack:     期望收到的回复 (如 "OK")
 * @param  wait_ms: 等待超时时间 (毫秒)
 * @return 1: 成功, 0: 失败
 */
uint8_t ESP_Send_Cmd(char* cmd, char* ack, uint32_t wait_ms)
{
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
    HAL_UART_Transmit(&huart2, (uint8_t*)cmd, strlen(cmd), 100);
    HAL_UART_Receive(&huart2, (uint8_t*)esp_rx_buf,
                     sizeof(esp_rx_buf)-1, wait_ms);
    if (strstr(esp_rx_buf, ack))
        return 1;
    return 0;
}

/**
 * @brief  ESP8266初始化流程 (TCP透传模式)
 */
void ESP8266_Init(void)
{
    // 退出可能存在的透传模式
    HAL_UART_Transmit(&huart2, (uint8_t*)"+++", 3, 100);
    HAL_Delay(1000);

    ESP_Send_Cmd("AT\r\n", "OK", 1000);                  // AT测试
    ESP_Send_Cmd("AT+CWMODE=1\r\n", "OK", 1000);         // Station模式
    sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n",
            WIFI_SSID, WIFI_PASS);
    ESP_Send_Cmd(cmd_buf, "OK", 10000);                   // 连接WiFi
    sprintf(cmd_buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",
            SERVER_IP, SERVER_PORT);
    ESP_Send_Cmd(cmd_buf, "OK", 5000);                    // TCP连接
    ESP_Send_Cmd("AT+CIPMODE=1\r\n", "OK", 1000);        // 透传模式
    ESP_Send_Cmd("AT+CIPSEND\r\n", ">", 1000);           // 开始透传
}
```

---

### 2.2 MQTT协议实现 (`mqtt.c`)

在TCP透传基础上，手动构造MQTT协议报文实现CONNECT、PUBLISH、SUBSCRIBE和PINGREQ四种核心操作。

```c
// MQTT CONNECT报文构造
void MQTT_Connect(char* client_id)
{
    uint8_t packet[200] = {0};
    uint16_t id_len = strlen(client_id);
    uint16_t total_len = 10 + 2 + id_len;   // 可变头(10) + ID长度(2) + ID

    packet[0] = 0x10;          // Fixed Header: CONNECT类型
    packet[1] = total_len;     // 剩余长度

    // Variable Header: 协议名"MQTT" + 协议级别 + 连接标志 + 心跳间隔
    uint8_t var_header[] = {
        0x00, 0x04, 'M', 'Q', 'T', 'T',   // Protocol Name
        0x04,                                // Protocol Level (3.1.1)
        0x02,                                // Flags (Clean Session)
        0x00, 0x3C                           // Keep Alive (60秒)
    };
    memcpy(&packet[2], var_header, 10);

    // Payload: Client ID
    packet[12] = (id_len >> 8) & 0xFF;
    packet[13] = id_len & 0xFF;
    memcpy(&packet[14], client_id, id_len);

    UART_SendBytes(packet, total_len + 2);
}

// MQTT PUBLISH报文构造
void MQTT_Publish(char* topic, char* payload)
{
    uint8_t packet[256] = {0};
    uint16_t topic_len = strlen(topic);
    uint16_t payload_len = strlen(payload);
    uint16_t total_len = 2 + topic_len + payload_len;

    packet[0] = 0x30;             // PUBLISH类型
    packet[1] = total_len;
    packet[2] = (topic_len >> 8) & 0xFF;
    packet[3] = topic_len & 0xFF;
    memcpy(&packet[4], topic, topic_len);
    memcpy(&packet[4 + topic_len], payload, payload_len);

    UART_SendBytes(packet, total_len + 2);
}

// MQTT SUBSCRIBE报文构造
void MQTT_Subscribe(char* topic)
{
    uint8_t packet[128] = {0};
    uint16_t topic_len = strlen(topic);
    uint16_t total_len = 2 + 2 + topic_len + 1; // 报文标识符+主题长度+主题+QoS

    packet[0] = 0x82;   // SUBSCRIBE类型
    packet[1] = total_len;
    packet[2] = 0x00;   packet[3] = 0x0A;  // 报文标识符
    packet[4] = (topic_len >> 8) & 0xFF;
    packet[5] = topic_len & 0xFF;
    memcpy(&packet[6], topic, topic_len);
    packet[6 + topic_len] = 0x00;           // QoS 0

    UART_SendBytes(packet, total_len + 2);
}

// MQTT PINGREQ 心跳保活
void MQTT_PingREQ(void)
{
    uint8_t packet[2] = {0xC0, 0x00};
    UART_SendBytes(packet, 2);
}
```

---

### 2.3 BH1750光照传感器驱动 (`bh1750.c`)

采用软件I2C通信，将测量拆分为"启动测量"和"读取结果"两阶段，实现非阻塞采集，180ms等待期可执行其他任务。

```c
// 发送测量指令 (不等待, 调用后需外部等待 >=180ms 再读结果)
void BH1750_StartMeasure(void)
{
    IIC_Start();
    IIC_Send_Byte(BH1750_ADDR);
    if (IIC_Wait_Ack()) { IIC_Stop(); return; }
    IIC_Send_Byte(0x10);        // 连续高分辨率模式
    IIC_Wait_Ack();
    IIC_Stop();
}

// 读取测量结果 (需在StartMeasure后等待>=180ms再调用)
float BH1750_ReadResult(void)
{
    uint16_t result = 0;
    IIC_Start();
    IIC_Send_Byte(BH1750_ADDR | 0x01);   // 读模式
    if (IIC_Wait_Ack()) { IIC_Stop(); return 0.0f; }

    result  = IIC_Read_Byte(1);           // 高8位, ACK
    result <<= 8;
    result |= IIC_Read_Byte(0);           // 低8位, NACK
    IIC_Stop();

    return (float)(result / 1.2f);        // 转换为Lux
}
```

主循环中非阻塞调用方式：

```c
// 启动BH1750测量 → 利用180ms等待期执行电机步进 → 读取结果
BH1750_StartMeasure();
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < 180) {
        Motor_Update();    // 180ms内持续驱动电机步进
    }
}
bh1750_lux = BH1750_ReadResult();
```

---

### 2.4 28BYJ-48步进电机非阻塞驱动 (`motor_28byj.c`)

采用半步8相序驱动，通过定时轮询方式实现非阻塞运转，不占用定时器资源。

```c
/* 半步8相序 (顺时针), bit0~bit3 对应 IN1~IN4 */
static const uint8_t s_half_step_seq[8] = {
    0x01, 0x03, 0x02, 0x06,
    0x04, 0x0C, 0x08, 0x09,
};

static volatile uint32_t s_remaining_steps = 0;
static uint32_t s_last_step_tick = 0;
static uint8_t  s_seq_idx       = 0;

/* 输出相序到GPIO */
static void motor_write_phase(uint8_t pattern)
{
    HAL_GPIO_WritePin(MOTOR_IN1_GPIO_Port, MOTOR_IN1_Pin,
                      (pattern & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_GPIO_Port, MOTOR_IN2_Pin,
                      (pattern & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN3_GPIO_Port, MOTOR_IN3_Pin,
                      (pattern & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN4_GPIO_Port, MOTOR_IN4_Pin,
                      (pattern & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* 启动一次投喂, 非阻塞: 立即返回, 后台由Motor_Update()推进 */
void Motor_StartFeed(uint16_t grams)
{
    if (grams == 0 || s_remaining_steps != 0) return;
    s_remaining_steps = (uint32_t)grams * MOTOR_STEPS_PER_GRAM;
    s_last_step_tick  = HAL_GetTick();
}

/* 主循环频繁调用, 每2ms推进一步 */
void Motor_Update(void)
{
    if (s_remaining_steps == 0) return;

    uint32_t now = HAL_GetTick();
    if ((now - s_last_step_tick) < HALF_STEP_INTERVAL_MS) return;
    s_last_step_tick = now;

    motor_write_phase(s_half_step_seq[s_seq_idx]);
    s_seq_idx = (s_seq_idx + 1U) & 0x07U;
    s_remaining_steps--;

    if (s_remaining_steps == 0) {
        motor_write_phase(0x00);   // 投喂结束, 断电防发热
    }
}
```

---

### 2.5 物理按键去抖 (`key.c`)

基于状态机的非阻塞按键去抖，20ms稳定确认后产生单次按下事件。

```c
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       stable_level;     // 上次稳定电平 (1=释放, 0=按下)
    uint8_t       last_raw_level;   // 上次采样电平
    uint32_t      edge_tick;        // 最近一次电平变化的tick
} key_state_t;

/* 返回1表示该按键产生"释放→按下"的下降沿事件 */
static uint8_t key_update_one(key_state_t *k, uint32_t now)
{
    uint8_t raw = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_SET)
                  ? 1U : 0U;

    if (raw != k->last_raw_level) {
        k->last_raw_level = raw;      // 电平变化, 开始计时
        k->edge_tick      = now;
        return 0;
    }

    // 电平保持一致且超过去抖时间, 且与稳定值不同
    if (raw != k->stable_level &&
        (now - k->edge_tick) >= KEY_DEBOUNCE_MS) {
        uint8_t prev = k->stable_level;
        k->stable_level = raw;
        if (prev == 1U && raw == 0U)
            return 1;   // 稳定释放 → 稳定按下
    }
    return 0;
}

KEY_Event_t KEY_Scan(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last_scan_tick) < KEY_SCAN_MIN_MS) return KEY_EVT_NONE;
    s_last_scan_tick = now;

    if (key_update_one(&s_keys[0], now)) return KEY_EVT_LIGHT_PRESS;
    if (key_update_one(&s_keys[1], now)) return KEY_EVT_FEED_PRESS;
    return KEY_EVT_NONE;
}
```

---

### 2.6 K210 AI宠物识别帧解析 (UART1中断回调)

K210发送固定3字节帧 `[0xAA, ID, 0x55]`，在UART接收中断中通过状态机解析，锁存识别结果。

```c
/* K210 AI帧状态机变量 */
static uint8_t  ai_rx_byte    = 0;
static uint8_t  ai_rx_state   = 0;
static uint8_t  ai_animal_id  = 0xFF;       // 当前帧ID (随时被覆写)
static volatile uint8_t ai_latched_id  = 0xFF; // 完整帧锁存ID (防竞态)
static volatile uint8_t ai_pet_detected = 0;   // 新帧标志

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        switch (ai_rx_state) {
        case 0:   // 等待帧头
            if (ai_rx_byte == 0xAA) ai_rx_state = 1;
            break;
        case 1:   // 接收动物ID
            ai_animal_id = ai_rx_byte;
            ai_rx_state  = 2;
            break;
        case 2:   // 验证帧尾
            if (ai_rx_byte == 0x55) {
                ai_latched_id   = ai_animal_id;  // 原子锁存
                ai_pet_detected = 1;              // 通知主循环
            }
            ai_rx_state = 0;
            break;
        }
        HAL_UART_Receive_IT(&huart1, &ai_rx_byte, 1);  // 继续接收
    }
}
```

主循环中的防抖逻辑（衰减式计数器）：

```c
#define PET_CONFIRM_COUNT  5U    // 连续多帧确认阈值

/* 衰减式防抖: 识别到宠物 +1, 未识别到 -1 (不清零) */
if (ai_pet_detected) {
    ai_pet_detected = 0;
    uint8_t id = ai_latched_id;
    if (id != 0xFF) {
        if (ai_detect_count < 255) ai_detect_count++;
    } else {
        if (ai_detect_count > 0) ai_detect_count--;
    }
    ai_pet_confirmed = (ai_detect_count >= PET_CONFIRM_COUNT) ? 1 : 0;
}
```

---

### 2.7 DMA+空闲中断接收云端指令

利用STM32 HAL库的DMA接收配合UART空闲线检测，实现不定长MQTT报文的高效接收。

```c
/* 初始化: 启动DMA空闲中断接收 */
HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
__HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);  // 禁用半传输中断

/* 空闲中断回调: 一帧数据接收完成 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        rx_len  = Size;
        rx_flag = 1;     // 通知主循环处理
    }
}

/* 主循环中处理接收到的数据 */
if (rx_flag) {
    rx_flag = 0;
    uint16_t len = rx_len;
    if (len >= 10) {
        char cmd_buf[256];
        uint16_t copy_len = (len < sizeof(cmd_buf)-1) ? len : (sizeof(cmd_buf)-1);
        memcpy(cmd_buf, rx_buffer, copy_len);
        cmd_buf[copy_len] = '\0';
        // 将内嵌的0x00替换为空格, 使strstr能搜索JSON
        for (uint16_t i = 0; i < copy_len; i++) {
            if (cmd_buf[i] == '\0') cmd_buf[i] = ' ';
        }
        Process_Cloud_Command(cmd_buf);   // 统一命令处理
    }
    // 重新启动DMA接收
    memset(rx_buffer, 0, sizeof(rx_buffer));
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
    __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
}
```

---

### 2.8 云端JSON命令解析与执行 (`Process_Cloud_Command`)

统一处理来自小程序的四类下行指令：补光灯控制、立即投喂、模式切换、时间校准。

```c
/* 从JSON中提取整数值, 如 "h":17 → 返回17 */
static int json_get_int(const char *buf, const char *key, int def)
{
    const char *p = strstr(buf, key);
    if (!p) return def;
    p = strchr(p, ':');
    if (!p) return def;
    p++;
    while (*p == ' ' || *p == '"') p++;
    return atoi(p);
}

static void Process_Cloud_Command(const char *cmd_buf)
{
    /* 时间校准: {"cmd":"time","h":17,"m":30,"s":0,"Y":2026,"M":4,"D":24} */
    if (strstr(cmd_buf, "\"cmd\":\"time\"")) {
        int h = json_get_int(cmd_buf, "\"h\"", -1);
        int m = json_get_int(cmd_buf, "\"m\"", -1);
        int s = json_get_int(cmd_buf, "\"s\"",  0);
        int Y = json_get_int(cmd_buf, "\"Y\"", -1);
        int M = json_get_int(cmd_buf, "\"M\"", -1);
        int D = json_get_int(cmd_buf, "\"D\"", -1);
        if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            RTC_TimeTypeDef sTime = {0};
            sTime.Hours = h;  sTime.Minutes = m;  sTime.Seconds = s;
            HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
            if (Y > 2000 && M >= 1 && M <= 12 && D >= 1 && D <= 31) {
                RTC_DateTypeDef sDate = {0};
                sDate.Year = Y - 2000;  sDate.Month = M;  sDate.Date = D;
                HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
            }
        }
    }
    /* 补光灯: {"cmd":"light","value":"on"/"off"} */
    else if (strstr(cmd_buf, "\"cmd\":\"light\"")) {
        if (strstr(cmd_buf, "\"value\":\"on\""))  LIGHT_On();
        else if (strstr(cmd_buf, "\"value\":\"off\"")) LIGHT_Off();
    }
    /* 立即投喂: {"cmd":"feed","amount":30} */
    else if (strstr(cmd_buf, "\"cmd\":\"feed\"")) {
        int v = json_get_int(cmd_buf, "\"amount\"", 30);
        uint16_t grams = (v > 0 && v <= 500) ? (uint16_t)v : 30;
        if (!Motor_IsBusy()) {
            Motor_StartFeed(grams);
            feed_count_today++;
        }
    }
    /* 模式切换: {"cmd":"mode","value":"auto"/"manual"} */
    else if (strstr(cmd_buf, "\"cmd\":\"mode\"")) {
        if (strstr(cmd_buf, "\"value\":\"auto\""))    g_manual_mode = 0;
        else if (strstr(cmd_buf, "\"value\":\"manual\"")) g_manual_mode = 1;
    }
}
```

---

### 2.9 传感器数据上报 (`Bemfa_Upload_Data`)

每5秒将所有传感器数据打包为JSON，通过MQTT发布至云端。

```c
static void Bemfa_Upload_Data(void)
{
    char json[160];
    int n = snprintf(json, sizeof(json),
        "{\"air\":%u,\"lux\":%.1f,\"water\":%u,\"food\":%u,"
        "\"pet\":%u,\"light\":%u,\"feed_cnt\":%lu}",
        (unsigned)mq135_adc,
        bh1750_lux,
        (unsigned)(water_low  ? 1 : 0),
        (unsigned)(food_low   ? 1 : 0),
        (unsigned)ai_animal_id,
        (unsigned)(LIGHT_IsOn() ? 1 : 0),
        (unsigned long)feed_count_today);
    if (n > 0 && n < (int)sizeof(json)) {
        MQTT_Publish(TOPIC_PUB, json);
    }
}
```

---

## 三、系统核心控制逻辑 (`main.c` 主循环)

### 3.1 系统初始化流程

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    // 外设初始化
    MX_GPIO_Init();
    MX_USART1_UART_Init();     // K210 AI通信
    MX_DMA_Init();
    MX_USART2_UART_Init();     // ESP8266通信
    MX_ADC1_Init();             // MQ-135空气传感器
    MX_SPI1_Init();             // LCD显示屏
    MX_RTC_Init();              // 实时时钟

    HAL_UART_Receive_IT(&huart1, &ai_rx_byte, 1);  // 启动K210中断接收
    HAL_ADCEx_Calibration_Start(&hadc1);             // ADC自校准

    // 传感器 & 执行器初始化
    BH1750_Init();
    LIGHT_Init();
    Motor_Init();
    KEY_Init();
    Lcd_Init();

    // 网络连接
    ESP8266_Init();              // WiFi连接
    MQTT_Connect(BEMFA_UID);     // MQTT连接
    MQTT_Subscribe(TOPIC_SUB);   // 订阅控制主题

    // 启动DMA接收
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));

    // 开机时间同步: 向小程序请求时间 + 上报当前模式
    snprintf(sync_buf, sizeof(sync_buf),
             "{\"cmd\":\"get_time\",\"mode\":\"%s\"}",
             g_manual_mode ? "manual" : "auto");
    MQTT_Publish(TOPIC_PUB, sync_buf);
    // 等待小程序回复时间, 最多3秒 ...
```

### 3.2 主循环核心逻辑

```
while (1) {
    ┌────────────────────────────────────────────────────┐
    │  1. 高频非阻塞任务 (每次循环必须执行)                 │
    │     Motor_Update();        // 步进电机推进            │
    │     KEY_Scan();            // 按键扫描                │
    ├────────────────────────────────────────────────────┤
    │  2. 定时网络任务                                     │
    │     每5秒: Bemfa_Upload_Data()  // 上报传感器数据     │
    │     每30秒: MQTT_PingREQ()      // MQTT心跳保活      │
    ├────────────────────────────────────────────────────┤
    │  3. 云端下行命令处理 (DMA+空闲中断)                   │
    │     rx_flag → Process_Cloud_Command()               │
    ├────────────────────────────────────────────────────┤
    │  4. 每秒执行一次的传感器采集 (RTC秒跳触发)             │
    │     ├─ MQ-135 ADC采集 (16次平均)                     │
    │     ├─ BH1750 光照采集 (非阻塞分步)                   │
    │     ├─ 水位/食物传感器 GPIO读取                       │
    │     ├─ AI宠物识别防抖 (衰减式计数器)                   │
    │     ├─ 自动补光判断 (迟滞阈值 50/150 Lux)             │
    │     ├─ 自动喂食判断 (5个条件同时满足)                  │
    │     └─ LCD状态刷新                                   │
    └────────────────────────────────────────────────────┘
}
```

### 3.3 自动补光逻辑

```c
#define AUTO_LIGHT_LUX_LOW   50.0f    // 低于此值开灯
#define AUTO_LIGHT_LUX_HIGH  150.0f   // 高于此值关灯 (迟滞区间防抖动)

if (g_manual_mode == 0) {   // 仅AUTO模式
    if (bh1750_lux < AUTO_LIGHT_LUX_LOW && !LIGHT_IsOn()) {
        LIGHT_On();
    } else if (bh1750_lux > AUTO_LIGHT_LUX_HIGH && LIGHT_IsOn()) {
        LIGHT_Off();
    }
    // 50~150 Lux之间保持当前状态, 避免临界值反复开关
}
```

### 3.4 自动喂食逻辑

```c
#define AUTO_FEED_COOLDOWN_MS  60000U   // 冷却60秒
#define AUTO_FEED_GRAMS        30U      // 每次30克
#define PET_CONFIRM_COUNT      5U       // 连续5帧确认

/* 五重条件全部满足才触发自动投喂:
 * ① AUTO模式  ② 食盆空  ③ AI确认有宠物  ④ 电机空闲  ⑤ 冷却期已过 */
if (g_manual_mode == 0
    && food_low
    && ai_pet_confirmed
    && !Motor_IsBusy()
    && (HAL_GetTick() - last_auto_feed_tick >= AUTO_FEED_COOLDOWN_MS))
{
    Motor_StartFeed(AUTO_FEED_GRAMS);
    feed_count_today++;
    last_auto_feed_tick = HAL_GetTick();
}
```

### 3.5 开机时间同步流程

```c
/* STM32发布同步请求 (含当前模式) */
MQTT_Publish(TOPIC_PUB, "{\"cmd\":\"get_time\",\"mode\":\"auto\"}");

/* 轮询等待小程序回复, 最多3秒 */
while (HAL_GetTick() - sync_t0 < 3000) {
    if (rx_flag) {
        rx_flag = 0;
        // 解析收到的数据
        Process_Cloud_Command(cmd_buf);
        if (strstr(cmd_buf, "\"cmd\":\"time\"")) {
            time_synced = 1;   // RTC已由Process_Cloud_Command校准
            break;
        }
    }
}
```

---

## 四、微信小程序核心代码 (`dashboard.js`)

### 4.1 MQTT通信与遥测数据处理

```javascript
const mqtt = require('../../utils/mqtt.min.js')
const MQTT_CFG = require('../../utils/mqtt-config.js')

// MQTT连接 (WebSocket Secure)
this._mqttClient = mqtt.connect(MQTT_CFG.BROKER_URL, {
    clientId: MQTT_CFG.UID,
    keepalive: 60,
    clean: true,
    protocolVersion: 4
})

// 订阅硬件上报主题
this._mqttClient.subscribe(MQTT_CFG.TOPIC_UP, { qos: 0 })

// 消息处理: 遥测数据 + 开机同步
this._mqttClient.on('message', (topic, payload) => {
    const data = JSON.parse(payload.toString())

    // 硬件开机同步请求: 回复时间 + 同步模式
    if (data.cmd === 'get_time') {
        this._replyTimeSync()
        if (data.mode === 'auto' || data.mode === 'manual') {
            this.setData({ isAutoMode: data.mode === 'auto' })
        }
        return
    }

    // 常规遥测数据映射到UI
    this._applyTelemetry(data)
})
```

### 4.2 遥测数据映射

```javascript
_applyTelemetry(data) {
    const patch = {}

    // 空气质量 (MQ-135 ADC → 三档)
    const air = Number(data.air)
    const AIR_LEVELS = [
        { max: 1500, label: '优', level: 'good' },
        { max: 2500, label: '良', level: 'moderate' },
        { max: 4096, label: '差', level: 'bad' }
    ]
    const hit = AIR_LEVELS.find(l => air < l.max)
    patch.airQuality = hit.label

    // 光照强度
    patch.lightIntensity = Math.round(Number(data.lux))

    // 水位/食物 (0=OK, 1=LOW → 百分比映射)
    patch.waterLevel = Number(data.water) === 1 ? 15 : 85
    patch.foodLevel  = Number(data.food)  === 1 ? 12 : 80

    // AI宠物识别
    const PET_MAP = { 0: '猫', 1: '狗', 2: '兔子' }
    patch.petType = PET_MAP[Number(data.pet)] || ''

    // 设备在线心跳
    patch.deviceOnline = true
    this._lastMsgTs = Date.now()

    this.setData(patch)
}
```

### 4.3 下行控制指令

```javascript
// 发布控制指令 (JSON → MQTT)
_publishCommand(obj) {
    const json = JSON.stringify(obj)
    this._mqttClient.publish(MQTT_CFG.TOPIC_DOWN, json, { qos: 0 })
}

// 切换模式
onSwitchMode(e) {
    const mode = e.currentTarget.dataset.mode
    this._publishCommand({ cmd: 'mode', value: mode })
}

// 补光灯开关
onToggleLight() {
    const next = !this.data.lightOn
    this._publishCommand({ cmd: 'light', value: next ? 'on' : 'off' })
}

// 立即投喂
onFeedNow() {
    const amount = Number(this.data.feedAmount)
    this._publishCommand({ cmd: 'feed', amount })
}
```

### 4.4 时间同步回复

```javascript
_replyTimeSync() {
    const now = new Date()
    this._publishCommand({
        cmd: 'time',
        h: now.getHours(),
        m: now.getMinutes(),
        s: now.getSeconds(),
        Y: now.getFullYear(),
        M: now.getMonth() + 1,
        D: now.getDate()
    })
}
```

### 4.5 定时喂食

```javascript
// 每秒检查是否到达定时喂食时间
_checkSchedules() {
    const now = new Date()
    const hm = String(now.getHours()).padStart(2, '0') + ':' +
               String(now.getMinutes()).padStart(2, '0')
    const sec = now.getSeconds()

    // 每天 00:00:00 重置已执行标志
    if (hm === '00:00' && sec === 0) {
        this.data.schedules.forEach(s => { s.firedToday = false })
    }

    // 匹配到定时计划 → 下发喂食指令
    this.data.schedules.forEach(s => {
        if (s.enabled && !s.firedToday && s.time === hm && sec === 0) {
            this._publishCommand({ cmd: 'feed', amount: s.amount })
            s.firedToday = true
        }
    })
}
```

---

## 五、系统通信协议汇总

### 5.1 MQTT主题定义

| 主题 | 方向 | 用途 |
|------|------|------|
| `feeder003` | STM32 → 小程序 | 传感器遥测上报 + 开机同步请求 |
| `feederctrl003` | 小程序 → STM32 | 控制指令下发 |

### 5.2 上行JSON格式 (每5秒)

```json
{
    "air": 1800,       // MQ-135 ADC原始值 (0~4095)
    "lux": 254.2,      // BH1750 光照强度 (Lux)
    "water": 0,        // 水位 (0=正常, 1=不足)
    "food": 0,         // 食物 (0=正常, 1=不足)
    "pet": 0,          // AI识别 (0=猫, 1=狗, 2=兔, 255=未识别)
    "light": 1,        // 补光灯状态 (0=关, 1=开)
    "feed_cnt": 3      // 今日投喂次数
}
```

### 5.3 下行指令格式

| 指令 | JSON格式 | 说明 |
|------|---------|------|
| 补光灯 | `{"cmd":"light","value":"on"}` | on/off |
| 投喂 | `{"cmd":"feed","amount":30}` | 克数 |
| 模式 | `{"cmd":"mode","value":"auto"}` | auto/manual |
| 时间 | `{"cmd":"time","h":17,"m":30,"s":0,"Y":2026,"M":4,"D":24}` | RTC校准 |

### 5.4 K210 AI帧格式

```
 字节0    字节1     字节2
┌──────┬─────────┬──────┐
│ 0xAA │ 动物ID  │ 0x55 │
└──────┴─────────┴──────┘
  帧头   0x00=猫    帧尾
         0x01=狗
         0x02=兔
         0xFF=未识别
```

---

## 六、关键设计要点

1. **非阻塞架构**: 所有外设驱动均采用非阻塞设计，主循环通过`HAL_GetTick()`时间片轮询调度，保证步进电机2ms步进精度不被其他任务阻塞。

2. **迟滞阈值防抖**: 自动补光采用50/150 Lux双阈值迟滞设计，避免临界光照值导致灯光频繁开关。

3. **衰减式宠物检测防抖**: K210识别结果采用衰减式计数器（检测到+1，未检测到-1），相比清零式更能容忍偶发的识别失败，有效防止误触发。

4. **中断锁存防竞态**: K210帧解析在UART中断中完成时锁存动物ID至`ai_latched_id`，避免主循环读取时被新帧覆盖导致的竞态条件。

5. **DMA+空闲中断**: MQTT数据接收采用DMA传输配合UART空闲线检测，实现不定长报文的零拷贝高效接收。

6. **开机时间同步**: 设备启动后主动向小程序请求时间，小程序回复后校准RTC，同时上报当前工作模式实现双向状态同步。

7. **定时喂食持久化**: 小程序端定时计划存储于`wx.setStorageSync`，关闭重开不丢失；每日00:00自动重置执行标志。
