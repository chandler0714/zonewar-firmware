/*
 * ZoneWar - NVS 存储模块
 * 保存设备ID、战队、Wi-Fi配置等持久化数据
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define ZW_TEAM_NONE  0
#define ZW_TEAM_RED   1
#define ZW_TEAM_BLUE  2

#define ZW_DEVICE_ID_LEN 17  // MAC 地址格式 xx:xx:xx:xx:xx:xx

typedef struct {
    char device_id[ZW_DEVICE_ID_LEN];
    int team;  // ZW_TEAM_NONE / RED / BLUE
    char wifi_ssid[32];
    char wifi_password[64];
    bool wifi_configured;
    char server_url[128];
} zw_config_t;

esp_err_t zw_nvs_init(void);
esp_err_t zw_nvs_load(zw_config_t *config);
esp_err_t zw_nvs_save_team(int team);
esp_err_t zw_nvs_save_wifi(const char *ssid, const char *password);
esp_err_t zw_nvs_save_server_url(const char *url);
const char* zw_nvs_get_device_id(void);
