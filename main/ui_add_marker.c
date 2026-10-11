/*
 * ZoneWar - 新增标记点页面
 * 通过 BLE 接收手机发来的 GPS 坐标
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_ble.h"
#include "zw_api.h"
#include "zw_wifi.h"

static const char *TAG = "ui_add_marker";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_coord = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_ble_status = NULL;

static bool s_coord_received = false;
static zw_coord_t s_pending_coord = {0};

static void coord_received_cb(zw_coord_t coord) {
    if (!s_scr) return;
    
    s_pending_coord = coord;
    s_coord_received = true;
    
    if (bsp_lvgl_lock(100)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.4f, %.4f", coord.lat, coord.lng);
        lv_label_set_text(s_coord, buf);
        lv_label_set_text(s_status, "[OK] Coords received");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
        lv_label_set_text(s_hint, "OK: Upload   Up/Down: Back");
        bsp_lvgl_unlock();
    }
}

static void upload_marker_task(void *arg) {
    (void)arg;
    
    if (!zw_wifi_is_connected()) {
        if (bsp_lvgl_lock(100)) {
            lv_label_set_text(s_status, "[X] Wi-Fi not connected");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xff6b6b), 0);
            bsp_lvgl_unlock();
        }
        vTaskDelete(NULL);
        return;
    }
    
    bool territory_created = false;
    double new_area = 0;
    int pending_count = 0;
    
    esp_err_t err = zw_api_add_marker(s_pending_coord.lat, s_pending_coord.lng,
                                      &territory_created, &new_area, &pending_count);
    
    if (bsp_lvgl_lock(200)) {
        if (err == ESP_OK) {
            if (territory_created) {
                char buf[128];
                char area_buf[32];
                zw_format_area(new_area, area_buf, sizeof(area_buf));
                snprintf(buf, sizeof(buf), "[OK] New Territory!\nArea: %s", area_buf);
                lv_label_set_text(s_status, buf);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "[OK] Marker added (%d/3)", pending_count);
                lv_label_set_text(s_status, buf);
            }
            lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
            lv_label_set_text(s_hint, "OK: Back to Capture");
            g_zw.marker_count = pending_count;
            
            // 同步最新数据
            zw_ui_request_sync();
        } else {
            lv_label_set_text(s_status, "[X] Upload failed");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xff6b6b), 0);
        }
        bsp_lvgl_unlock();
    }
    
    vTaskDelete(NULL);
}

void ui_add_marker_enter(void) {
    ESP_LOGI(TAG, "Enter add marker");
    
    s_coord_received = false;
    s_pending_coord.valid = false;
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Add Marker");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // BLE 状态
    s_ble_status = lv_label_create(s_scr);
    lv_label_set_text(s_ble_status, "BLE: Ready - send GPS from phone");
    lv_obj_set_style_text_font(s_ble_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ble_status, lv_color_hex(0x4ecdc4), 0);
    lv_obj_align(s_ble_status, LV_ALIGN_TOP_MID, 0, 65);
    
    // 坐标显示框
    lv_obj_t *coord_box = lv_obj_create(s_scr);
    lv_obj_set_size(coord_box, 210, 80);
    lv_obj_set_style_bg_color(coord_box, lv_color_hex(0x141428), 0);
    lv_obj_set_style_border_width(coord_box, 1, 0);
    lv_obj_set_style_border_color(coord_box, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_radius(coord_box, 10, 0);
    lv_obj_align(coord_box, LV_ALIGN_TOP_MID, 0, 100);
    
    s_coord = lv_label_create(coord_box);
    lv_label_set_text(s_coord, "Waiting for coords...");
    lv_obj_set_style_text_font(s_coord, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_coord, lv_color_hex(0x888888), 0);
    lv_obj_center(s_coord);
    
    // 状态
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Send location from ZoneWar app");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 200);
    
    // 底部提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Up/Down: Back");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    lv_screen_load(s_scr);
    
    // 注册坐标回调（BLE 已在启动时开启）
    zw_ble_set_coord_callback(coord_received_cb);
}

void ui_add_marker_exit(void) {
    // 注销坐标回调（BLE 广播保持开启）
    zw_ble_set_coord_callback(NULL);
    
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = NULL;
        s_coord = NULL;
        s_hint = NULL;
        s_ble_status = NULL;
    }
}

void ui_add_marker_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        // 返回占领页面
        zw_ui_switch_page(ZW_PAGE_CAPTURE);
    } else if (btn == BSP_BTN_OK) {
        if (s_coord_received) {
            // 已经收到坐标，上传
            s_coord_received = false;
            lv_label_set_text(s_status, "Uploading...");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
            xTaskCreate(upload_marker_task, "upload_marker", 4096, NULL, 5, NULL);
        }
    }
}
