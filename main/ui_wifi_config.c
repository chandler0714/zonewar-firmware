/*
 * ZoneWar - 蓝牙配网页（BLE_CONFIG）
 * 显示设备名 + 6位配对码，等待手机浏览器（Web Bluetooth）连接配对
 * 手机端推送 WiFi 凭证由 zw_ui_handle_wifi_cred 全局处理
 * 异常处理：WiFi 连接失败留在本页等待重发；长按OK逻辑见 zw_ui_dispatch_key
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "esp_timer.h"
#include "zw_theme.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_ble_cfg";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_name_label = NULL;
static lv_obj_t *s_code_label = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_timer_t *s_poll_timer = NULL;

// 手机连上 BLE 的起始时刻（us），保持连接 2s 后自动进入 Wi-Fi 列表页
static int64_t s_connected_since_us = 0;
#define BLE_CONNECTED_HOLD_US 2000000

// 轮询 BLE 连接状态与配网状态
static void poll_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_status) return;

    const char *st = zw_ble_get_status();
    if (strcmp(st, "CONN") == 0) {
        lv_label_set_text(s_status, "CONNECTING WI-FI");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
        return;
    }
    if (strcmp(st, "FAIL") == 0) {
        lv_label_set_text(s_status, "WI-FI FAILED / RETRY PHONE");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
        return;
    }

    if (zw_ble_is_connected()) {
        int64_t now = esp_timer_get_time();
        if (s_connected_since_us == 0) {
            // 刚连上：先停留，给手机端完成服务发现/读取配对码的时间
            s_connected_since_us = now;
            lv_label_set_text(s_status, "PHONE CONNECTED");
            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
        } else if (now - s_connected_since_us >= BLE_CONNECTED_HOLD_US) {
            // 蓝牙已稳定连接：自动进入 Wi-Fi 扫描选择页（进入即自动扫描）
            ESP_LOGI(TAG, "BLE linked, auto enter Wi-Fi select");
            zw_ui_switch_page(ZW_PAGE_WIFI_SELECT);
        }
    } else {
        s_connected_since_us = 0;
        lv_label_set_text(s_status, "WAITING FOR PHONE");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
    }
}

void ui_wifi_config_enter(void) {
    ESP_LOGI(TAG, "Enter BLE config");

    // 重置连接计时（含从 Wi-Fi 页长按 OK 返回的场景）
    s_connected_since_us = 0;

    // 生成新的 6 位配对码（手机端通过 f5 读取核对）
    const char *code = zw_ble_generate_pair_code();

    s_scr = zw_theme_screen(ZW_C_CYAN);
    zw_theme_header(s_scr, "ZONEWAR  /  02", "PAIR DEVICE", ZW_C_CYAN);
    zw_theme_footer(s_scr, g_zw.config.wifi_configured
        ? "OK  RETRY SAVED WI-FI" : "CONNECT IN PHONE BROWSER");

    // 设备名（手机端搜索用）
    s_name_label = lv_label_create(s_scr);
    lv_label_set_text(s_name_label, zw_ble_get_device_name());
    lv_obj_set_style_text_font(s_name_label, &lv_font_montserrat_16, 0);
    lv_obj_set_size(s_name_label, 212, 42);
    zw_theme_panel(s_name_label);
    lv_obj_set_style_pad_top(s_name_label, 10, 0);
    lv_obj_set_style_text_align(s_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_hex(ZW_C_CYAN), 0);
    lv_obj_set_pos(s_name_label, 14, 75);

    // 配对码标题
    lv_obj_t *code_title = lv_label_create(s_scr);
    lv_label_set_text(code_title, "VERIFY THIS CODE ON YOUR PHONE");
    lv_obj_set_style_text_font(code_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(code_title, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_pos(code_title, 14, 130);

    // 6 位配对码（大字显示）
    s_code_label = lv_label_create(s_scr);
    lv_label_set_text(s_code_label, code);
    lv_obj_set_style_text_font(s_code_label, &lv_font_montserrat_28, 0);
    lv_obj_set_size(s_code_label, 212, 65);
    zw_theme_panel(s_code_label);
    lv_obj_set_style_pad_top(s_code_label, 14, 0);
    lv_obj_set_style_text_align(s_code_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_code_label, lv_color_hex(ZW_C_AMBER), 0);
    lv_obj_set_pos(s_code_label, 14, 151);

    // 状态行
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "WAITING FOR PHONE");
    lv_obj_set_width(s_status, 216);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
    lv_obj_set_pos(s_status, 12, 233);

    // 提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "NEXT: SELECT A WI-FI NETWORK");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 260);

    lv_screen_load(s_scr);

    // 确保 BLE 广播开启（断开后 gap_event 会自动恢复广播）
    zw_ble_start_advertising();
    zw_ble_set_status("IDLE");

    // 状态轮询定时器
    s_poll_timer = lv_timer_create(poll_timer_cb, 500, NULL);

    ESP_LOGI(TAG, "BLE config ready, pair code: %s", code);
}

void ui_wifi_config_exit(void) {
    if (s_poll_timer) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_name_label = NULL;
        s_code_label = NULL;
        s_status = NULL;
        s_hint = NULL;
    }
}

void ui_wifi_config_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;

    // 已存 WiFi 时按 OK 直接重试连接（不走手机推送）
    if (g_zw.config.wifi_configured && !zw_ui_wifi_busy()) {
        zw_wifi_cred_t cred = {0};
        strncpy(cred.ssid, g_zw.config.wifi_ssid, sizeof(cred.ssid) - 1);
        strncpy(cred.password, g_zw.config.wifi_password, sizeof(cred.password) - 1);
        cred.valid = true;
        zw_ui_handle_wifi_cred(cred);
    }
}
