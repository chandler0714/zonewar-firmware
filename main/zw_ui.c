/*
 * ZoneWar - UI 管理模块
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "zw_ui";

zw_state_t g_zw = {0};

static zw_page_t s_current_page = ZW_PAGE_BOOT;
static lv_timer_t *s_sync_timer = NULL;

// 各页面函数声明
void ui_team_select_enter(void);
void ui_team_select_exit(void);
void ui_team_select_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_dashboard_enter(void);
void ui_dashboard_exit(void);
void ui_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void ui_dashboard_refresh(void);

void ui_capture_enter(void);
void ui_capture_exit(void);
void ui_capture_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_add_marker_enter(void);
void ui_add_marker_exit(void);
void ui_add_marker_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_marker_list_enter(void);
void ui_marker_list_exit(void);
void ui_marker_list_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_wifi_config_enter(void);
void ui_wifi_config_exit(void);
void ui_wifi_config_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 同步定时器回调
static void sync_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (zw_wifi_is_connected() && !g_zw.sync_in_progress) {
        zw_ui_request_sync();
    }
}

esp_err_t zw_ui_init(void) {
    ESP_LOGI(TAG, "UI init");
    
    // 创建 10 分钟同步定时器
    s_sync_timer = lv_timer_create(sync_timer_cb, 600000, NULL); // 10分钟
    
    return ESP_OK;
}

void zw_ui_switch_page(zw_page_t page) {
    if (page == s_current_page) return;
    
    // 退出当前页
    switch (s_current_page) {
    case ZW_PAGE_TEAM_SELECT: ui_team_select_exit(); break;
    case ZW_PAGE_DASHBOARD:   ui_dashboard_exit(); break;
    case ZW_PAGE_CAPTURE:     ui_capture_exit(); break;
    case ZW_PAGE_ADD_MARKER:  ui_add_marker_exit(); break;
    case ZW_PAGE_MARKER_LIST: ui_marker_list_exit(); break;
    case ZW_PAGE_WIFI_CONFIG: ui_wifi_config_exit(); break;
    default: break;
    }
    
    s_current_page = page;
    
    // 进入新页
    switch (page) {
    case ZW_PAGE_TEAM_SELECT: ui_team_select_enter(); break;
    case ZW_PAGE_DASHBOARD:   ui_dashboard_enter(); break;
    case ZW_PAGE_CAPTURE:     ui_capture_enter(); break;
    case ZW_PAGE_ADD_MARKER:  ui_add_marker_enter(); break;
    case ZW_PAGE_MARKER_LIST: ui_marker_list_enter(); break;
    case ZW_PAGE_WIFI_CONFIG: ui_wifi_config_enter(); break;
    default: break;
    }
    
    ESP_LOGI(TAG, "Page switched: %d", page);
}

zw_page_t zw_ui_current_page(void) {
    return s_current_page;
}

void zw_ui_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return; // 只处理单击
    
    switch (s_current_page) {
    case ZW_PAGE_TEAM_SELECT: ui_team_select_key(btn, ev); break;
    case ZW_PAGE_DASHBOARD:   ui_dashboard_key(btn, ev); break;
    case ZW_PAGE_CAPTURE:     ui_capture_key(btn, ev); break;
    case ZW_PAGE_ADD_MARKER:  ui_add_marker_key(btn, ev); break;
    case ZW_PAGE_MARKER_LIST: ui_marker_list_key(btn, ev); break;
    case ZW_PAGE_WIFI_CONFIG: ui_wifi_config_key(btn, ev); break;
    default: break;
    }
}

// 后台同步任务
static void sync_task(void *arg) {
    (void)arg;
    
    if (!zw_wifi_is_connected()) {
        g_zw.sync_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    // 发送心跳
    zw_api_heartbeat();
    
    // 获取统计数据
    zw_stats_t stats = {0};
    if (zw_api_get_stats(&stats) == ESP_OK) {
        g_zw.stats = stats;
        g_zw.stats_valid = true;
        ESP_LOGI(TAG, "Stats updated: territories=%d, area=%.2f, win_rate=%.1f",
                 stats.territory_count, stats.total_area, stats.win_rate);
    }
    
    // 获取标记点
    int count = 0;
    if (zw_api_get_markers(g_zw.markers, 10, &count) == ESP_OK) {
        g_zw.marker_count = count;
        ESP_LOGI(TAG, "Markers updated: %d", count);
    }
    
    // 刷新UI
    if (s_current_page == ZW_PAGE_DASHBOARD && g_zw.stats_valid) {
        if (bsp_lvgl_lock(100)) {
            ui_dashboard_refresh();
            bsp_lvgl_unlock();
        }
    }
    
    g_zw.sync_in_progress = false;
    vTaskDelete(NULL);
}

void zw_ui_request_sync(void) {
    if (g_zw.sync_in_progress) return;
    g_zw.sync_in_progress = true;
    
    xTaskCreate(sync_task, "zw_sync", 8192, NULL, 5, NULL);
}

void zw_format_area(double area_m2, char *buf, int buf_len) {
    if (area_m2 < 1000) {
        snprintf(buf, buf_len, "%.0f m²", area_m2);
    } else if (area_m2 < 1000000) {
        snprintf(buf, buf_len, "%.2f km²", area_m2 / 1000000.0);
    } else {
        snprintf(buf, buf_len, "%.1f km²", area_m2 / 1000000.0);
    }
}
