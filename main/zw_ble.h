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

// 配对码（6位数字，进入配网页时生成，手机端通过 f5 读取核对）
const char* zw_ble_generate_pair_code(void);
const char* zw_ble_get_pair_code(void);

// 设备广播名（ZoneWar-XXXX）
const char* zw_ble_get_device_name(void);

// 设备状态通道（f6，手机端轮询读取）：
// IDLE / SEL:<ssid>(设备已选中WiFi等手机输密码) / CONN(连接中) / OK(成功) / FAIL(失败)
void zw_ble_set_status(const char *status);
const char* zw_ble_get_status(void);
void zw_ble_set_selected_ssid(const char *ssid);
