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
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"
#define SERVER_IP     "bemfa.com"
#define SERVER_PORT   9501

#define BEMFA_UID     "YOUR_BEMFA_UID"
#define TOPIC_PUB     "feeder003"
#define TOPIC_SUB     "feederctrl003"

void ESP8266_Init(void);

#endif