/*
 * ZoneWar - SoftAP 配网模块
 * 设备创建 WiFi 热点，手机连接后通过 HTTP 网页配置 WiFi
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    ZW_PROV_IDLE = 0,
    ZW_PROV_STARTING,      // 正在启动 AP
    ZW_PROV_WAITING,       // 等待用户连接配置
    ZW_PROV_CONNECTING,    // 正在连接目标 WiFi
    ZW_PROV_SUCCESS,       // 配置成功
    ZW_PROV_FAILED,        // 配置失败
} zw_prov_state_t;

// 启动 SoftAP 配网
// ssid_hint: 设备 ID 片段，用于生成热点名
esp_err_t zw_provision_start(const char *device_id);

// 停止配网（关闭 AP 和 HTTP 服务）
esp_err_t zw_provision_stop(void);

// 获取当前配网状态
zw_prov_state_t zw_provision_get_state(void);

// 获取成功后的 SSID（成功后有效）
const char *zw_provision_get_ssid(void);
