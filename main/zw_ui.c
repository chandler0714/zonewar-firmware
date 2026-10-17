/*
 * ZoneWar - UI 管理模块
 * 页面切换、按键路由、全局 BLE 凭证处理、后台同步
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "zw_ui";

zw_state_t g_zw = {0};

static zw_page_t s_current_page = ZW_PAGE_BOOT;
static lv_timer_t *s_sync_timer = NULL;
static volatile bool s_wifi_task_running = false;

// 各页面函数声明
void ui_team_select_enter(void);
void ui_team_select_exit(void);
void ui_team_select_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_wifi_config_enter(void);
void ui_wifi_config_exit(void);
void ui_wifi_config_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_wifi_select_enter(void);
void ui_wifi_select_exit(void);
void ui_wifi_select_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_team_mgmt_enter(void);
void ui_team_mgmt_exit(void);
void ui_team_mgmt_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void ui_team_mgmt_refresh(void);

void ui_settings_enter(void);
void ui_settings_exit(void);
void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void ui_exit_confirm_enter(void);
void ui_exit_confirm_exit(void);
void ui_exit_confirm_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 同步定时器回调：联网状态下每 10 分钟自动同步服务器数据
static void sync_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (zw_wifi_is_connected() && !g_zw.sync_in_progress) {
        zw_ui_request_sync();
    }
}

esp_err_t zw_ui_init(void) {
    ESP_LOGI(TAG, "UI init");

    // 创建 10 分钟同步定时器
    s_sync_timer = lv_timer_create(sync_timer_cb, 600000, NULL);

    // 注册全局 BLE Wi-Fi 凭证回调（手机端网页推送密码的唯一入口）
    // 注意：回调运行在 NimBLE host 任务上下文，只做转交不阻塞
    zw_ble_set_wifi_callback(zw_ui_handle_wifi_cred);

    return ESP_OK;
}

void zw_ui_switch_page(zw_page_t page) {
    if (page == s_current_page) return;

    // 退出当前页
    switch (s_current_page) {
    case ZW_PAGE_TEAM_SELECT:  ui_team_select_exit(); break;
    case ZW_PAGE_BLE_CONFIG:   ui_wifi_config_exit(); break;
    case ZW_PAGE_WIFI_SELECT:  ui_wifi_select_exit(); break;
    case ZW_PAGE_TEAM_MGMT:    ui_team_mgmt_exit(); break;
    case ZW_PAGE_SETTINGS:     ui_settings_exit(); break;
    case ZW_PAGE_EXIT_CONFIRM: ui_exit_confirm_exit(); break;
    default: break;
    }

    s_current_page = page;

    // 进入新页
    switch (page) {
    case ZW_PAGE_TEAM_SELECT:  ui_team_select_enter(); break;
    case ZW_PAGE_BLE_CONFIG:   ui_wifi_config_enter(); break;
    case ZW_PAGE_WIFI_SELECT:  ui_wifi_select_enter(); break;
    case ZW_PAGE_TEAM_MGMT:    ui_team_mgmt_enter(); break;
    case ZW_PAGE_SETTINGS:     ui_settings_enter(); break;
    case ZW_PAGE_EXIT_CONFIRM: ui_exit_confirm_enter(); break;
    default: break;
    }

    ESP_LOGI(TAG, "Page switched: %d", page);
}

zw_page_t zw_ui_current_page(void) {
    return s_current_page;
}

void zw_ui_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // ---- 长按通用规则 ----
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP) {
            // 长按上键：进入设置页（阵营选择/设置/确认页除外）
            if (s_current_page != ZW_PAGE_TEAM_SELECT &&
                s_current_page != ZW_PAGE_SETTINGS &&
                s_current_page != ZW_PAGE_EXIT_CONFIRM) {
                zw_ui_switch_page(ZW_PAGE_SETTINGS);
            }
            return;
        }
        if (btn == BSP_BTN_OK) {
            // 配网选网页长按 OK：蓝牙断开/异常时回蓝牙配置页重新配对
            if (s_current_page == ZW_PAGE_WIFI_SELECT) {
                zw_ui_switch_page(ZW_PAGE_BLE_CONFIG);
                return;
            }
            // 长按 OK：返回阵营管理页（仅已选阵营时有效）
            if (g_zw.config.team != ZW_TEAM_NONE &&
                s_current_page != ZW_PAGE_TEAM_MGMT) {
                zw_ui_switch_page(ZW_PAGE_TEAM_MGMT);
            }
            return;
        }
        return;
    }

    // ---- 单击分发 ----
    if (ev != BSP_BTN_CLICK) return;

    switch (s_current_page) {
    case ZW_PAGE_TEAM_SELECT:  ui_team_select_key(btn, ev); break;
    case ZW_PAGE_BLE_CONFIG:   ui_wifi_config_key(btn, ev); break;
    case ZW_PAGE_WIFI_SELECT:  ui_wifi_select_key(btn, ev); break;
    case ZW_PAGE_TEAM_MGMT:    ui_team_mgmt_key(btn, ev); break;
    case ZW_PAGE_SETTINGS:     ui_settings_key(btn, ev); break;
    case ZW_PAGE_EXIT_CONFIRM: ui_exit_confirm_key(btn, ev); break;
    default: break;
    }
}

// ---- Wi-Fi 凭证统一处理：保存 → 连接 → 分流 ----
static void wifi_apply_task(void *arg) {
    zw_wifi_cred_t *cred = (zw_wifi_cred_t *)arg;

    ESP_LOGI(TAG, "WiFi cred received: ssid=%s", cred->ssid);

    // 保存到 NVS 与全局配置
    zw_nvs_save_wifi(cred->ssid, cred->password);
    strncpy(g_zw.config.wifi_ssid, cred->ssid, sizeof(g_zw.config.wifi_ssid) - 1);
    strncpy(g_zw.config.wifi_password, cred->password, sizeof(g_zw.config.wifi_password) - 1);
    g_zw.config.wifi_configured = true;

    // 通知手机端：连接中
    zw_ble_set_status("CONN");

    esp_err_t err = zw_wifi_connect(cred->ssid, cred->password);

    if (err == ESP_OK) {
        zw_ble_set_status("OK");

        // 向服务器注册并查询阵营
        bool has_team = false;
        int team = 0;
        if (zw_api_register(&has_team, &team) == ESP_OK) {
            ESP_LOGI(TAG, "Registered: has_team=%d, team=%d", has_team, team);
            if (has_team && g_zw.config.team == ZW_TEAM_NONE) {
                g_zw.config.team = team;
                zw_nvs_save_team(team);
                zw_ble_set_device_info(g_zw.config.device_id, team);
            }
        }

        // 分流：已选阵营进管理页，否则进阵营选择
        if (bsp_lvgl_lock(200)) {
            zw_ui_switch_page(g_zw.config.team != ZW_TEAM_NONE
                                  ? ZW_PAGE_TEAM_MGMT : ZW_PAGE_TEAM_SELECT);
            bsp_lvgl_unlock();
        }
    } else {
        // 密码错误等失败：留在当前页，手机端重新输入推送
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
        zw_ble_set_status("FAIL");
    }

    free(cred);
    s_wifi_task_running = false;
    vTaskDelete(NULL);
}

void zw_ui_handle_wifi_cred(zw_wifi_cred_t cred) {
    if (!cred.valid) return;
    if (s_wifi_task_running) {
        ESP_LOGW(TAG, "WiFi task busy, drop cred");
        return;
    }

    zw_wifi_cred_t *copy = malloc(sizeof(zw_wifi_cred_t));
    if (!copy) return;
    memcpy(copy, &cred, sizeof(zw_wifi_cred_t));

    s_wifi_task_running = true;
    if (xTaskCreate(wifi_apply_task, "wifi_apply", 8192, copy, 5, NULL) != pdPASS) {
        free(copy);
        s_wifi_task_running = false;
    }
}

bool zw_ui_wifi_busy(void) {
    return s_wifi_task_running;
}

// ---- 后台数据同步（心跳 + 统计 + 坐标列表）----
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
        ESP_LOGI(TAG, "Stats updated: territories=%d, area=%.2f",
                 stats.territory_count, stats.total_area);
    }

    // 获取当前待处理坐标列表（pending 点，凑领地用；形成领地后自动清空）
    int count = 0;
    if (zw_api_get_markers(g_zw.markers, 10, &count) == ESP_OK) {
        g_zw.marker_count = count;
        ESP_LOGI(TAG, "Markers updated: %d", count);
    }

    // 刷新UI
    if (s_current_page == ZW_PAGE_TEAM_MGMT && g_zw.stats_valid) {
        if (bsp_lvgl_lock(100)) {
            ui_team_mgmt_refresh();
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
    // 仅使用 ASCII 字符，避免 Montserrat 字体缺字显示方块
    if (area_m2 < 1000) {
        snprintf(buf, buf_len, "%.0f m2", area_m2);
    } else if (area_m2 < 1000000) {
        snprintf(buf, buf_len, "%.2f km2", area_m2 / 1000000.0);
    } else {
        snprintf(buf, buf_len, "%.1f km2", area_m2 / 1000000.0);
    }
}
