#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"

/* ======================================================================
 * User configuration
 * ----------------------------------------------------------------------
 * WIFI_SSID / WIFI_PASS : your Wi-Fi credentials (no Chinese chars)
 * SERVER_IP / SERVER_PORT : MQTT broker (default: bemfa.com / 9501)
 * BEMFA_UID : your 32-char UID from https://cloud.bemfa.com/
 * TOPIC_PUB : topic we publish sensor telemetry to
 * TOPIC_SUB : topic we subscribe to for down-link commands
 * ====================================================================== */
#define WIFI_SSID     "qiao"
#define WIFI_PASS     "qzj315605"
#define SERVER_IP     "bemfa.com"
#define SERVER_PORT   9501

#define BEMFA_UID     "e1cf97bfce8b4c0dbae0d0e26fc05941"
#define TOPIC_PUB     "feeder003"
#define TOPIC_SUB     "feederctrl003"

void ESP8266_Init(void);

#endif