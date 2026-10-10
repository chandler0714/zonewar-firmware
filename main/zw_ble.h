/*
 * ZoneWar - BLE 模块
 * 提供 GATT 服务：GPS 坐标接收 + Wi-Fi 配网 + 设备信息
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    double lat;
    double lng;
    bool valid;
} zw_coord_t;

typedef struct {
    char ssid[32];
    char password[64];
    bool valid;
} zw_wifi_cred_t;

typedef void (*zw_ble_coord_cb_t)(zw_coord_t coord);
typedef void (*zw_ble_wifi_cb_t)(zw_wifi_cred_t cred);

esp_err_t zw_ble_init(void);
esp_err_t zw_ble_start_advertising(void);
esp_err_t zw_ble_stop_advertising(void);
bool zw_ble_is_connected(void);
void zw_ble_set_coord_callback(zw_ble_coord_cb_t cb);
void zw_ble_set_wifi_callback(zw_ble_wifi_cb_t cb);
// 设置当前设备信息（供手机端读取）
void zw_ble_set_device_info(const char *device_id, int team);
