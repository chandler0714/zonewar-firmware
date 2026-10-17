/*
 * ZoneWar - WiFi 定位模块（Mozilla Location Service）
 * 设备扫周围 WiFi AP 的 BSSID+RSSI，POST 给 Mozilla 反查估算坐标
 * 全程设备独立完成，无需手机/GPS
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    double lat;         // 纬度
    double lng;         // 经度
    double accuracy;    // 估算精度（米）
} zw_geo_result_t;

// 扫描周围 WiFi 并调用 Mozilla Location Service 反查坐标
// 返回 ESP_OK 时 out 填充有效坐标；失败返回 ESP_FAIL / ESP_ERR_TIMEOUT / ESP_ERR_INVALID_STATE
// 阻塞式调用，整体耗时约 5-10 秒（扫描 2-3s + HTTPS 请求 3-5s）
esp_err_t zw_geolocate(zw_geo_result_t *out);

// 获取最近一次定位的错误描述（用于屏幕诊断显示）
const char *zw_geolocate_get_last_error(void);
