/*
 * ZoneWar - Wi-Fi 模块
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    ZW_WIFI_DISCONNECTED = 0,
    ZW_WIFI_CONNECTING,
    ZW_WIFI_CONNECTED,
    ZW_WIFI_FAILED
} zw_wifi_state_t;

esp_err_t zw_wifi_init(void);
esp_err_t zw_wifi_connect(const char *ssid, const char *password);
// 在 APSTA 模式下连接 WiFi（不切换模式，保持 AP 开启）
esp_err_t zw_wifi_connect_sta_only(const char *ssid, const char *password);
esp_err_t zw_wifi_disconnect(void);
zw_wifi_state_t zw_wifi_get_state(void);
bool zw_wifi_is_connected(void);

// WiFi 扫描
#define ZW_WIFI_SCAN_MAX_AP  20
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool encrypted;
} zw_wifi_ap_t;

// 开始扫描，返回扫描到的 AP 数量（阻塞式，约 2-3 秒）
int zw_wifi_scan(zw_wifi_ap_t *aps, int max_aps);
