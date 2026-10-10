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
esp_err_t zw_wifi_disconnect(void);
zw_wifi_state_t zw_wifi_get_state(void);
bool zw_wifi_is_connected(void);
