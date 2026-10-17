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

// WiFi 定位用 AP 记录（含 BSSID）
#define ZW_WIFI_GEO_MAX_AP  15
typedef struct {
    uint8_t bssid[6];   // AP MAC 地址
    int8_t rssi;        // 信号强度 dBm
    uint8_t channel;    // 信道
} zw_wifi_geo_ap_t;

// 开始扫描，返回扫描到的 AP 数量（阻塞式，约 2-3 秒）
int zw_wifi_scan(zw_wifi_ap_t *aps, int max_aps);

// 扫描并输出含 BSSID 的 AP 记录（供 WiFi 定位反查用），返回 AP 数量
int zw_wifi_scan_geo(zw_wifi_geo_ap_t *aps, int max_aps);

// 扫描诊断信息
esp_err_t zw_wifi_get_last_scan_err(void);
int zw_wifi_get_last_raw_ap_num(void);

// ---- 启动期预扫描缓存（开机 BLE 启动前调用，独占射频，成功率高）----
// 在 BLE 启动前抢扫一次 WiFi 并缓存结果，供 BLE 特征读取返回
esp_err_t zw_wifi_boot_scan(void);
// 获取缓存的扫描结果（NULL 表示未扫过/失败），返回 AP 数量
int zw_wifi_get_cached_aps(const zw_wifi_ap_t **out_aps);
