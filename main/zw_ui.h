/*
 * ZoneWar - UI 管理模块
 * 管理页面切换、全局状态、按键路由
 *
 * 页面流转：
 *   开机 → (无WiFi) BLE_CONFIG → WIFI_SELECT → WiFi连接 → TEAM_SELECT/TEAM_MGMT
 *        → (有WiFi) 直连 → register → TEAM_SELECT / TEAM_MGMT
 *   通用按键：长按上键 = 设置页；长按OK = 返回阵营管理页（WIFI_SELECT 时回蓝牙配网页）
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "bsp_button.h"
#include "zw_nvs.h"
#include "zw_api.h"
#include "zw_ble.h"

// 页面枚举
typedef enum {
    ZW_PAGE_BOOT = 0,        // 启动过渡页（无实现，直接分流）
    ZW_PAGE_BLE_CONFIG,      // 蓝牙配网页：显示设备名+配对码，等待手机连接
    ZW_PAGE_WIFI_SELECT,     // Wi-Fi 扫描选择页：上下选网络，OK 通知手机输密码
    ZW_PAGE_TEAM_SELECT,     // 阵营选择
    ZW_PAGE_TEAM_MGMT,       // 阵营管理页（游戏主页）
    ZW_PAGE_SETTINGS,        // 设置页（长按上键进入）
    ZW_PAGE_EXIT_CONFIRM,    // 退出阵营二次确认
} zw_page_t;

// 全局状态
typedef struct {
    zw_config_t config;
    zw_stats_t stats;
    zw_marker_t markers[10];   // 最近坐标列表（管理页底部显示）
    int marker_count;
    bool stats_valid;
    bool wifi_connecting;
    bool sync_in_progress;
} zw_state_t;

extern zw_state_t g_zw;

// UI 初始化（内部注册 BLE 凭证回调）
esp_err_t zw_ui_init(void);

// 页面切换
void zw_ui_switch_page(zw_page_t page);

// 当前页面
zw_page_t zw_ui_current_page(void);

// 按键事件分发
void zw_ui_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 手机端通过 BLE 推送 Wi-Fi 凭证的全局入口（连接+保存+分流由后台任务统一处理）
void zw_ui_handle_wifi_cred(zw_wifi_cred_t cred);

// 是否有 Wi-Fi 凭证正在处理/连接中
bool zw_ui_wifi_busy(void);

// 请求数据同步（后台进行）
void zw_ui_request_sync(void);

// 格式化面积显示
void zw_format_area(double area_m2, char *buf, int buf_len);
