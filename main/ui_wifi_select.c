/*
 * ZoneWar - Wi-Fi 扫描选择页（WIFI_SELECT）
 * 后台扫描 AP 列表，上下键选择，OK 键通过 BLE f6 通知手机端输入密码
 * 凭证推送后由 zw_ui_handle_wifi_cred 统一连接
 * 异常：BLE 断开提示长按OK回蓝牙配网页；密码错误显示 FAIL 等待手机重发
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "zw_theme.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_wifi_sel";

#define MAX_ROWS 20

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_list = NULL;      // 可滚动列表容器
static lv_obj_t *s_rows[MAX_ROWS];   // 每行 label
static lv_obj_t *s_status = NULL;
static lv_timer_t *s_poll_timer = NULL;

static zw_wifi_ap_t s_aps[MAX_ROWS];
static int s_ap_count = 0;
static volatile int s_sel_idx = 0;
static volatile bool s_page_active = false;
static volatile bool s_scanning = false;
static bool s_waiting_password = false;

static void update_highlight(void) {
    for (int i = 0; i < s_ap_count && i < MAX_ROWS; i++) {
        if (i == s_sel_idx) {
            lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(ZW_C_RAISED), 0);
            lv_obj_set_style_border_color(s_rows[i], lv_color_hex(ZW_C_CYAN), 0);
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(ZW_C_TEXT), 0);
            lv_obj_scroll_to_view(s_rows[i], LV_ANIM_ON);
        } else {
            lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(ZW_C_SURFACE), 0);
            lv_obj_set_style_border_color(s_rows[i], lv_color_hex(ZW_C_LINE), 0);
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(ZW_C_MUTED), 0);
        }
    }
}

// 后台扫描任务
static void scan_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "Scan task start");
    int n = zw_wifi_scan(s_aps, MAX_ROWS);

    if (!s_page_active) {
        s_scanning = false;
        vTaskDelete(NULL);
        return;
    }

    if (n < 0) {
        // 扫描互斥冲突，稍后由用户按键重新触发或定时重扫
        ESP_LOGW(TAG, "Scan busy");
    } else {
        s_ap_count = n;
    }

    if (bsp_lvgl_lock(200)) {
        if (s_page_active && s_list) {
            if (n < 0) {
                lv_label_set_text(s_status, "Scan busy, try again");
            } else if (s_ap_count == 0) {
                // 屏幕直接显示硬件原始结果和错误码，方便定位共存/驱动问题
                esp_err_t serr = zw_wifi_get_last_scan_err();
                char diag[64];
                if (serr != ESP_OK) {
                    snprintf(diag, sizeof(diag), "No Wi-Fi! err:%s",
                             esp_err_to_name(serr));
                } else {
                    snprintf(diag, sizeof(diag), "No Wi-Fi! raw:%d",
                             zw_wifi_get_last_raw_ap_num());
                }
                lv_label_set_text(s_status, diag);
            } else {
                // 填充列表
                for (int i = 0; i < s_ap_count && i < MAX_ROWS; i++) {
                    char buf[48];
                    if (s_aps[i].encrypted) {
                        snprintf(buf, sizeof(buf), "%s (%d)", s_aps[i].ssid, s_aps[i].rssi);
                    } else {
                        snprintf(buf, sizeof(buf), "%s [open]", s_aps[i].ssid);
                    }
                    lv_label_set_text(s_rows[i], buf);
                    lv_obj_set_hidden(s_rows[i], false);
                }
                for (int i = s_ap_count; i < MAX_ROWS; i++)
                    lv_obj_set_hidden(s_rows[i], true);
                s_sel_idx = 0;
                update_highlight();
                char buf[64];
                snprintf(buf, sizeof(buf), "Found %d. OK: select", s_ap_count);
                lv_label_set_text(s_status, buf);
            }
        }
        bsp_lvgl_unlock();
    }

    s_scanning = false;
    vTaskDelete(NULL);
}

// 轮询连接状态（CONN/FAIL 来自 zw_ble_get_status）
static void poll_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_status) return;

    const char *st = zw_ble_get_status();
    if (strcmp(st, "CONN") == 0) {
        lv_label_set_text(s_status, "Connecting Wi-Fi...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
        return;
    }
    if (strcmp(st, "FAIL") == 0) {
        s_waiting_password = false;
        lv_label_set_text(s_status, "Wrong password! Resend");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
        return;
    }
    if (!zw_ble_is_connected() && s_waiting_password) {
        lv_label_set_text(s_status, "BLE lost! LONG OK");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
        return;
    }
    if (s_waiting_password) {
        lv_label_set_text(s_status, "Enter pwd on phone...");
    }
}

void ui_wifi_select_enter(void) {
    ESP_LOGI(TAG, "Enter wifi select");

    s_page_active = true;
    s_ap_count = 0;
    s_sel_idx = 0;
    s_waiting_password = false;

    s_scr = zw_theme_screen(ZW_C_CYAN);
    zw_theme_header(s_scr, "ZONEWAR  /  03", "SELECT WI-FI", ZW_C_CYAN);
    zw_theme_footer(s_scr, "UP/DOWN  MOVE      OK  CHOOSE");

    // 列表容器（可滚动）
    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 216, 188);
    lv_obj_set_pos(s_list, 12, 72);
    zw_theme_panel(s_list);
    lv_obj_set_style_pad_all(s_list, 6, 0);
    lv_obj_set_style_pad_row(s_list, 5, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < MAX_ROWS; i++) {
        s_rows[i] = lv_label_create(s_list);
        lv_label_set_text(s_rows[i], "");
        lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_hor(s_rows[i], 10, 0);
        lv_obj_set_style_pad_ver(s_rows[i], 8, 0);
        lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rows[i], 1, 0);
        lv_obj_set_style_radius(s_rows[i], 8, 0);
        lv_obj_set_width(s_rows[i], 194);
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_hidden(s_rows[i], true);
    }

    // 状态行
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Scanning Wi-Fi...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
    lv_obj_set_width(s_status, 224);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status, 8, 265);

    lv_screen_load(s_scr);

    // 启动后台扫描
    s_scanning = true;
    xTaskCreate(scan_task, "wifi_scan", 8192, NULL, 5, NULL);

    // 状态轮询
    s_poll_timer = lv_timer_create(poll_timer_cb, 500, NULL);
}

void ui_wifi_select_exit(void) {
    s_page_active = false;
    if (s_poll_timer) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_list = NULL;
        s_status = NULL;
        for (int i = 0; i < MAX_ROWS; i++) s_rows[i] = NULL;
    }
}

void ui_wifi_select_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (s_ap_count == 0) return;
        if (btn == BSP_BTN_UP) {
            s_sel_idx = (s_sel_idx - 1 + s_ap_count) % s_ap_count;
        } else {
            s_sel_idx = (s_sel_idx + 1) % s_ap_count;
        }
        update_highlight();
    } else if (btn == BSP_BTN_OK) {
        if (s_ap_count == 0 || s_sel_idx >= s_ap_count) return;

        // 通过 BLE f6 通知手机端：设备已选中该 WiFi，等手机输密码
        zw_ble_set_selected_ssid(s_aps[s_sel_idx].ssid);
        s_waiting_password = true;
        lv_label_set_text(s_status, "Enter pwd on phone...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
        ESP_LOGI(TAG, "Selected SSID: %s", s_aps[s_sel_idx].ssid);
    }
}
