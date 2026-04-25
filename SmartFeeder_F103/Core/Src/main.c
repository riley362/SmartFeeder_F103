/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "rtc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bh1750.h"
#include "Lcd_Driver.h"
#include "GUI.h"
#include "dma.h"
#include "esp8266.h"
#include "mqtt.h"
#include "light.h"
#include "motor_28byj.h"
#include "key.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* atoi (为下行 feed 命令解 amount 数值) */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 自动补光阈值 (带迟滞, 防止临界抖动): 
 *   lux < LOW  时开灯; lux > HIGH 时关灯 
 *   中间区间保持当前状态, 避免 50±50 lux 闪灵时反复开关 */
#define AUTO_LIGHT_LUX_LOW   50.0f
#define AUTO_LIGHT_LUX_HIGH  150.0f

/* 自动喂食冷却时间 (ms): 触发一次后至少等这么久才允许再次自动投喂,
 * 防止食物刚投下还没被传感器检测到就再投一次 */
#define AUTO_FEED_COOLDOWN_MS  60000U   /* 60 秒 */
#define AUTO_FEED_GRAMS        30U      /* 每次自动投喂克数 */
#define PET_CONFIRM_COUNT      5U       /* 连续收到多少帧宠物识别才确认有宠物 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* K210 AI frame state machine: [0xAA, ID, 0x55] */
static uint8_t ai_rx_byte     = 0;
static uint8_t ai_rx_state    = 0;
static uint8_t ai_animal_id   = 0xFF;
static volatile uint8_t  ai_pet_detected  = 0;
static volatile uint8_t  ai_latched_id    = 0xFF; /* 中断中锁存的 ID, 防竞态 */
static uint8_t  ai_detect_count   = 0;   /* 连续识别计数 */
static uint8_t  ai_pet_confirmed  = 0;   /* 1=多帧确认有宠物 */

/* DMA 接收缓冲 + 空闲中断标志 (仿 sht_monitor_stm 方案) */
uint8_t  rx_buffer[256];
volatile uint8_t  rx_flag = 0;
volatile uint16_t rx_len  = 0;

/* Sensor readings */
static RTC_TimeTypeDef g_time;
static RTC_DateTypeDef g_date;
static uint16_t mq135_adc  = 0;
static float    mq135_volt = 0.0f;
static float    bh1750_lux = 0.0f;
static uint8_t  water_low  = 0;
static uint8_t  food_low   = 0;

/* Actuator / user-interaction state */
static uint32_t feed_count_today = 0;   /* 今日已投喂次数 (上电清零) */
static uint8_t  g_manual_mode    = 0;   /* 0=AUTO, 1=MANUAL (小程序下发切换) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Bemfa_Upload_Data(void);
static void Process_Cloud_Command(const char *cmd_buf);
static int  json_get_int(const char *buf, const char *key, int def);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\n==== Smart Feeder Sensor Test ====\r\n");
  HAL_Delay(200);

  HAL_UART_Receive_IT(&huart1, &ai_rx_byte, 1);

  /* STM32F1 ADC requires a one-shot self-calibration after init */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
    printf("[ERR ] ADC1 calibration failed\r\n");
  } else {
    printf("[INIT] ADC1 self-calibration done\r\n");
  }

  BH1750_Init();
  printf("[INIT] BH1750 on PB6(SCL)/PB7(SDA)\r\n");
  printf("[INIT] MQ-135 on PA0 (ADC1_IN0)\r\n");
  printf("[INIT] Water sensor on PB0, Food sensor on PB1\r\n");

  /* --- Actuators & UI --- */
  LIGHT_Init();
  Motor_Init();
  KEY_Init();
  printf("[INIT] Light LED on PA1\r\n");
  printf("[INIT] Motor 28BYJ-48 (ULN2003) on PB12~PB15\r\n");
  printf("[INIT] Keys: KEY_LIGHT=PB5, KEY_FEED=PB11\r\n");

  /* LCD init + static UI frame (4 plain white edges, no 3D box) */
  Lcd_Init();
  Lcd_Clear(BLACK);
  Gui_DrawLine(2,   2, 125,   2, WHITE);   /* top    */
  Gui_DrawLine(2, 157, 125, 157, WHITE);   /* bottom */
  Gui_DrawLine(2,   2,   2, 157, WHITE);   /* left   */
  Gui_DrawLine(125, 2, 125, 157, WHITE);   /* right  */
  Gui_DrawFont_GBK16(5, 5, YELLOW, BLACK, (uint8_t*)"Smart Feeder");
  Gui_DrawLine(2, 25, 125, 25, WHITE);     /* title divider */
  printf("[INIT] LCD ready\r\n");

  /* --- ESP8266 + MQTT (Bemfa) --- */
  /* 字符串控制在 14 个 ASCII 字符内 (14*8=112px), 从 x=5 画到 x=116,
   * 不会覆盖 x=125 处的右侧白边框 */
  Gui_DrawFont_GBK16(5, 135, YELLOW, BLACK, (uint8_t*)"Net: connect..");
  ESP8266_Init();
  HAL_Delay(1000);
  MQTT_Connect(BEMFA_UID);
  HAL_Delay(500);
  MQTT_Subscribe(TOPIC_SUB);
  Gui_DrawFont_GBK16(5, 135, GREEN,  BLACK, (uint8_t*)"Net: MQTT OK  ");
  printf("[INIT] MQTT connected & subscribed\r\n");

  /* Start DMA + UART idle line detection receive (reference: sht_monitor_stm) */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
  __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
  printf("[INIT] UART2 DMA+Idle RX started\r\n");

  /* --- 开机同步: 向小程序请求时间 + 上报当前模式 --- */
  {
    char sync_buf[80];
    snprintf(sync_buf, sizeof(sync_buf),
             "{\"cmd\":\"get_time\",\"mode\":\"%s\"}",
             g_manual_mode ? "manual" : "auto");
    MQTT_Publish(TOPIC_PUB, sync_buf);
    printf("[SYNC] Published: %s\r\n", sync_buf);

    /* 等待小程序回复时间, 最多 3 秒 */
    Gui_DrawFont_GBK16(5, 135, YELLOW, BLACK, (uint8_t*)"Net: TimSync..");
    uint32_t sync_t0 = HAL_GetTick();
    uint8_t  time_synced = 0;
    while (HAL_GetTick() - sync_t0 < 3000) {
      if (rx_flag) {
        rx_flag = 0;
        uint16_t len = rx_len;
        if (len >= 10) {
          char cmd_buf[256];
          uint16_t copy_len = (len < sizeof(cmd_buf) - 1) ? len : (sizeof(cmd_buf) - 1);
          memcpy(cmd_buf, rx_buffer, copy_len);
          cmd_buf[copy_len] = '\0';
          for (uint16_t i = 0; i < copy_len; i++) {
            if (cmd_buf[i] == '\0') cmd_buf[i] = ' ';
          }
          printf("[SYNC RX] %s\r\n", cmd_buf);
          Process_Cloud_Command(cmd_buf);
          if (strstr(cmd_buf, "\"cmd\":\"time\"")) {
            time_synced = 1;
          }
        }
        memset(rx_buffer, 0, sizeof(rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
        __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
        if (time_synced) break;
      }
    }
    if (time_synced) {
      printf("[SYNC] Time synchronized OK\r\n");
    } else {
      printf("[SYNC] No time response, using default RTC\r\n");
    }
    Gui_DrawFont_GBK16(5, 135, GREEN, BLACK, (uint8_t*)"Net: WiFi OK  ");
  }

  printf("[INIT] Ready. Sampling every 1s, uploading every 5s.\r\n\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t timer_upload = 0;
  uint32_t timer_ping   = 0;
  uint8_t  motor_was_busy = 0;
  uint32_t motor_dbg_tick = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* --- 非阻塞任务: 电机步进 + 按键扫描 (必须每轮都跑) --- */
    Motor_Update();

    /* 电机诊断: 每秒打印剩余步数, 结束时打印 DONE */
    if (Motor_IsBusy()) {
      motor_was_busy = 1;
      if (HAL_GetTick() - motor_dbg_tick >= 1000) {
        motor_dbg_tick = HAL_GetTick();
        printf("[MOTOR DBG] remaining=%lu\r\n", (unsigned long)Motor_GetRemainingSteps());
      }
    } else if (motor_was_busy) {
      motor_was_busy = 0;
      printf("[MOTOR DBG] DONE\r\n");
    }

    KEY_Event_t key_ev = KEY_Scan();
    if (key_ev == KEY_EVT_LIGHT_PRESS) {
      LIGHT_Toggle();
      printf("[KEY ] LIGHT toggled -> %s\r\n", LIGHT_IsOn() ? "ON" : "OFF");
    } else if (key_ev == KEY_EVT_FEED_PRESS) {
      if (!Motor_IsBusy()) {
        Motor_StartFeed(MOTOR_KEY_FEED_GRAMS);
        feed_count_today++;
        printf("[KEY ] FEED pressed -> dispense %ug (total=%lu)\r\n",
               (unsigned)MOTOR_KEY_FEED_GRAMS, (unsigned long)feed_count_today);
      } else {
        printf("[KEY ] FEED ignored: motor busy (%lu steps left)\r\n",
               (unsigned long)Motor_GetRemainingSteps());
      }
    }

    /* --- MQTT: publish telemetry every 5 s --- */
    if (HAL_GetTick() - timer_upload >= 5000) {
      timer_upload = HAL_GetTick();
      Bemfa_Upload_Data();
    }

    /* --- MQTT: keep TCP alive every 30 s --- */
    if (HAL_GetTick() - timer_ping >= 30000) {
      timer_ping = HAL_GetTick();
      MQTT_PingREQ();
    }

    /* ============================================================
     *  DMA + UART 空闲中断接收处理 (仿 sht_monitor_stm 方案)
     *  rx_flag 由 HAL_UARTEx_RxEventCallback 在空闲检测时置 1
     * ============================================================ */
    if (rx_flag)
    {
      rx_flag = 0;
      uint16_t len = rx_len;

      /* 收到任何字节都打印 hex, 便于诊断 */
      printf("[CLOUD RX] %u B hex: ", (unsigned)len);
      for (uint16_t i = 0; i < len && i < 80; i++) {
        printf("%02X ", rx_buffer[i]);
      }
      printf("\r\n");

      /* 太短 (<10B) 的基本是 PINGRESP(0xD0 0x00)或噪声, 不走命令分支 */
      if (len >= 10)
      {
        /* 在本地副本上操作, 把 0x00 替成空格让 strstr 能搜到 JSON */
        char cmd_buf[256];
        uint16_t copy_len = (len < sizeof(cmd_buf) - 1) ? len : (sizeof(cmd_buf) - 1);
        memcpy(cmd_buf, rx_buffer, copy_len);
        cmd_buf[copy_len] = '\0';
        for (uint16_t i = 0; i < copy_len; i++) {
          if (cmd_buf[i] == '\0') cmd_buf[i] = ' ';
        }

        printf("[CLOUD RX] text: %s\r\n", cmd_buf);
        Process_Cloud_Command(cmd_buf);
      }

      /* 重新启动 DMA 接收, 等待下一帧 */
      memset(rx_buffer, 0, sizeof(rx_buffer));
      HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
      __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
    }

    /* Refresh only when RTC second changes, keeps display in sync with real time */
    static uint8_t last_sec = 0xFF;
    HAL_RTC_GetTime(&hrtc, &g_time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &g_date, RTC_FORMAT_BIN);
    if (g_time.Seconds == last_sec) {
      if (!Motor_IsBusy()) HAL_Delay(10);
      continue;
    }
    last_sec = g_time.Seconds;

    /* Average 16 samples to suppress residual noise */
    uint32_t adc_sum = 0;
    for (uint8_t i = 0; i < 16; i++) {
      HAL_ADC_Start(&hadc1);
      HAL_ADC_PollForConversion(&hadc1, 10);
      adc_sum += HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);
    }
    mq135_adc  = (uint16_t)(adc_sum >> 4);
    mq135_volt = (float)mq135_adc * 3.3f / 4095.0f;

    /* BH1750: 启动测量 → 利用 180ms 等待期跑 Motor_Update → 读结果 */
    BH1750_StartMeasure();
    {
      uint32_t t0 = HAL_GetTick();
      while (HAL_GetTick() - t0 < 180) {
        Motor_Update();
      }
    }
    bh1750_lux = BH1750_ReadResult();

    /* ================================================
     *  本地自动补光逻辑 (主机时钟粒度: 1s, 由上面 RTC 秒跳触发)
     *    条件: 仅在 AUTO 模式下执行; MANUAL 模式完全听小程序与按键.
     *    阈值迟滞: lux<50 开; lux>150 关; 50~150 之间保持不动.
     * ================================================ */
    if (g_manual_mode == 0) {
      if (bh1750_lux < AUTO_LIGHT_LUX_LOW && !LIGHT_IsOn()) {
        LIGHT_On();
        printf("[AUTO] LIGHT ON  (lux=%.1f < %.1f)\r\n",
               bh1750_lux, AUTO_LIGHT_LUX_LOW);
      } else if (bh1750_lux > AUTO_LIGHT_LUX_HIGH && LIGHT_IsOn()) {
        LIGHT_Off();
        printf("[AUTO] LIGHT OFF (lux=%.1f > %.1f)\r\n",
               bh1750_lux, AUTO_LIGHT_LUX_HIGH);
      }
    }

    water_low = HAL_GPIO_ReadPin(WATER_SENSOR_GPIO_Port, WATER_SENSOR_Pin);
    food_low  = HAL_GPIO_ReadPin(FOOD_SENSOR_GPIO_Port,  FOOD_SENSOR_Pin);

    /* ================================================
     *  AI 宠物识别防抖动 (先于自动喂食判定, 保证同一秒生效)
     *    衰减式: 每识别到宠物 +1, 未识别到 -1 (不清零)
     * ================================================ */
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

    /* ================================================
     *  本地自动喂食逻辑
     *    条件: AUTO 模式 + 食盆空 + K210 检测到宠物 + 电机空闲 + 冷却期已过
     * ================================================ */
    {
      static uint32_t last_auto_feed_tick = 0;
      if (g_manual_mode == 0
          && food_low
          && ai_pet_confirmed
          && !Motor_IsBusy()
          && (HAL_GetTick() - last_auto_feed_tick >= AUTO_FEED_COOLDOWN_MS))
      {
        Motor_StartFeed(AUTO_FEED_GRAMS);
        feed_count_today++;
        last_auto_feed_tick = HAL_GetTick();
        printf("[AUTO] FEED %ug (food=LOW, pet=%u, cnt=%u, total=%lu)\r\n",
               (unsigned)AUTO_FEED_GRAMS, (unsigned)ai_latched_id,
               (unsigned)ai_detect_count, (unsigned long)feed_count_today);
      }
    }

    /* --- Serial log --- */
    printf("[%02d:%02d:%02d] MQ135=%4u(%.2fV) | Lux=%7.1f | Water=%s | Food=%s",
           g_time.Hours, g_time.Minutes, g_time.Seconds,
           mq135_adc, mq135_volt, bh1750_lux,
           water_low ? "LOW " : "OK  ",
           food_low  ? "LOW " : "OK  ");

    /* --- LCD update (trailing spaces overwrite previous longer strings) --- */
    char lcd_buf[24];

    snprintf(lcd_buf, sizeof(lcd_buf), "Time %02d:%02d:%02d", g_time.Hours, g_time.Minutes, g_time.Seconds);
    Gui_DrawFont_GBK16(5, 30, WHITE, BLACK, (uint8_t*)lcd_buf);
    Motor_Update();

    snprintf(lcd_buf, sizeof(lcd_buf), "Lux  %7.1f ", bh1750_lux);
    Gui_DrawFont_GBK16(5, 50, GREEN, BLACK, (uint8_t*)lcd_buf);
    Motor_Update();

    snprintf(lcd_buf, sizeof(lcd_buf), "Air  %5u    ", mq135_adc);
    Gui_DrawFont_GBK16(5, 70, GREEN, BLACK, (uint8_t*)lcd_buf);
    Motor_Update();

    /* Water + Food 合并成一行, 左右两段分别按状态着色 */
    Gui_DrawFont_GBK16( 5, 90, water_low ? RED : GREEN, BLACK,
                       (uint8_t*)(water_low ? "W:LO" : "W:OK"));
    Gui_DrawFont_GBK16(70, 90, food_low  ? RED : GREEN, BLACK,
                       (uint8_t*)(food_low  ? "F:LO" : "F:OK"));
    Motor_Update();

    /* AI: K210 识别结果持续显示在原 Food 位置(y=110).
     * 尾部补空格是为了覆盖掉上一轮更长字符串的残留像素. */
    const char *ai_name = (ai_animal_id == 0)    ? "Cat       " :
                          (ai_animal_id == 1)    ? "Dog       " :
                          (ai_animal_id == 2)    ? "Rabbit    " :
                          (ai_animal_id == 0xFF) ? "---       " :
                                                   "Unknown   ";
    snprintf(lcd_buf, sizeof(lcd_buf), "AI: %s", ai_name);
    Gui_DrawFont_GBK16(5, 110, YELLOW, BLACK, (uint8_t*)lcd_buf);
    Motor_Update();

    /* AI 识别状态日志 */
    printf(" | AI=%s(%u%s)",
           ai_name, (unsigned)ai_detect_count,
           ai_pet_confirmed ? "*" : "");
    printf("\r\n");
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Extract an integer value from a JSON key like "h":17
  * @param  buf:  null-terminated string to search
  * @param  key:  JSON key including quotes, e.g. "\"h\""
  * @param  def:  default value if key not found
  * @retval parsed integer or def
  */
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

/**
  * @brief  Process a cloud command string (shared by boot sync & main loop).
  *         Supported commands:
  *           {"cmd":"light","value":"on"/"off"}
  *           {"cmd":"feed","amount":30}
  *           {"cmd":"mode","value":"auto"/"manual"}
  *           {"cmd":"time","h":17,"m":30,"s":0,"Y":2026,"M":4,"D":24}
  */
static void Process_Cloud_Command(const char *cmd_buf)
{
  /* --- 时间校准 --- */
  if (strstr(cmd_buf, "\"cmd\":\"time\"")) {
    int h = json_get_int(cmd_buf, "\"h\"", -1);
    int m = json_get_int(cmd_buf, "\"m\"", -1);
    int s = json_get_int(cmd_buf, "\"s\"",  0);
    int Y = json_get_int(cmd_buf, "\"Y\"", -1);
    int M = json_get_int(cmd_buf, "\"M\"", -1);
    int D = json_get_int(cmd_buf, "\"D\"", -1);

    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
      RTC_TimeTypeDef sTime = {0};
      sTime.Hours   = (uint8_t)h;
      sTime.Minutes = (uint8_t)m;
      sTime.Seconds = (uint8_t)s;
      HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
      printf("[CLOUD] TIME -> %02d:%02d:%02d\r\n", h, m, s);

      if (Y > 2000 && M >= 1 && M <= 12 && D >= 1 && D <= 31) {
        RTC_DateTypeDef sDate = {0};
        sDate.Year    = (uint8_t)(Y - 2000);
        sDate.Month   = (uint8_t)M;
        sDate.Date    = (uint8_t)D;
        HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
        printf("[CLOUD] DATE -> %04d-%02d-%02d\r\n", Y, M, D);
      }
    }
  }
  /* --- 补光灯 --- */
  else if (strstr(cmd_buf, "\"cmd\":\"light\"")) {
    if (strstr(cmd_buf, "\"value\":\"on\"")) {
      LIGHT_On();
      printf("[CLOUD] LIGHT -> ON\r\n");
    } else if (strstr(cmd_buf, "\"value\":\"off\"")) {
      LIGHT_Off();
      printf("[CLOUD] LIGHT -> OFF\r\n");
    }
  }
  /* --- 立即投喂 --- */
  else if (strstr(cmd_buf, "\"cmd\":\"feed\"")) {
    uint16_t grams = 30;
    int v = json_get_int(cmd_buf, "\"amount\"", 30);
    if (v > 0 && v <= 500) grams = (uint16_t)v;
    if (!Motor_IsBusy()) {
      Motor_StartFeed(grams);
      feed_count_today++;
      printf("[CLOUD] FEED -> %u g (total=%lu)\r\n",
             (unsigned)grams, (unsigned long)feed_count_today);
    } else {
      printf("[CLOUD] FEED ignored: motor busy (%lu steps left)\r\n",
             (unsigned long)Motor_GetRemainingSteps());
    }
  }
  /* --- 运行模式 --- */
  else if (strstr(cmd_buf, "\"cmd\":\"mode\"")) {
    if (strstr(cmd_buf, "\"value\":\"auto\"")) {
      g_manual_mode = 0;
      printf("[CLOUD] MODE -> AUTO\r\n");
    } else if (strstr(cmd_buf, "\"value\":\"manual\"")) {
      g_manual_mode = 1;
      printf("[CLOUD] MODE -> MANUAL\r\n");
    }
  }
}

/**
  * @brief  Build a compact JSON snapshot and publish to Bemfa MQTT.
  *         Payload example:
  *           {"air":1800,"lux":254.2,"water":0,"food":0,"pet":255,"light":1,"feed_cnt":3}
  */
static void Bemfa_Upload_Data(void)
{
  char json[160];
  int n = snprintf(json, sizeof(json),
      "{\"air\":%u,\"lux\":%.1f,\"water\":%u,\"food\":%u,\"pet\":%u,"
      "\"light\":%u,\"feed_cnt\":%lu}",
      (unsigned)mq135_adc,
      bh1750_lux,
      (unsigned)(water_low ? 1 : 0),
      (unsigned)(food_low  ? 1 : 0),
      (unsigned)ai_animal_id,
      (unsigned)(LIGHT_IsOn() ? 1 : 0),
      (unsigned long)feed_count_today);
  if (n > 0 && n < (int)sizeof(json)) {
    MQTT_Publish(TOPIC_PUB, json);
    printf("[MQTT TX] %s\r\n", json);
  }
}

/**
  * @brief  UART1 RX callback: parse K210 AI frame [0xAA, ID, 0x55].
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    /* K210 AI 帧解析: [0xAA, ID, 0x55] */
    switch (ai_rx_state)
    {
      case 0:
        if (ai_rx_byte == 0xAA) ai_rx_state = 1;
        break;
      case 1:
        ai_animal_id = ai_rx_byte;
        ai_rx_state  = 2;
        break;
      case 2:
        if (ai_rx_byte == 0x55) {
          ai_latched_id   = ai_animal_id;
          ai_pet_detected = 1;
        }
        ai_rx_state = 0;
        break;
      default:
        ai_rx_state = 0;
        break;
    }
    HAL_UART_Receive_IT(&huart1, &ai_rx_byte, 1);
  }
}

/**
  * @brief  UART RxEvent callback (DMA + idle line detection).
  *         Called when UART idle is detected after DMA reception.
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART2)
  {
    rx_len  = Size;
    rx_flag = 1;
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
