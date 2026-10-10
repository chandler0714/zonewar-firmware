/*
 * ZoneWar - UI 管理模块
 * 管理页面切换、全局状态
 */
#pragma once

#include "esp_err.h"
#include "bsp_button.h"
#include "zw_nvs.h"
#include "zw_api.h"

// 页面枚举
typedef enum {
    ZW_PAGE_BOOT = 0,        // 启动页
    ZW_PAGE_TEAM_SELECT,     // 战队选择
    ZW_PAGE_WIFI_CONFIG,     // Wi-Fi 配置
    ZW_PAGE_DASHBOARD,       // 管理界面
    ZW_PAGE_CAPTURE,         // 占领页面
    ZW_PAGE_ADD_MARKER,      // 新增标记点
    ZW_PAGE_MARKER_LIST,     // 已标记点列表
} zw_page_t;

// 全局状态
typedef struct {
    zw_config_t config;
    zw_stats_t stats;
    zw_marker_t markers[10];
    int marker_count;
    bool stats_valid;
    bool wifi_connecting;
    bool sync_in_progress;
} zw_state_t;

extern zw_state_t g_zw;

// UI 初始化
esp_err_t zw_ui_init(void);

// 页面切换
void zw_ui_switch_page(zw_page_t page);

// 当前页面
zw_page_t zw_ui_current_page(void);

// 按键事件分发
void zw_ui_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 请求数据同步（后台进行）
void zw_ui_request_sync(void);

// 格式化面积显示
void zw_format_area(double area_m2, char *buf, int buf_len);
