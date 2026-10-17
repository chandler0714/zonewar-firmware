/*
 * ZoneWar - 新增标记点页面
 * 支持两种方式获取坐标：
 *   1. BLE 接收手机发来的 GPS 坐标
 *   2. 从服务器拉取最新标记点（WiFi 模式）
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
#include "zw_nvs.h"

static const char *TAG = "ui_add_marker";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_coord = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_device_id = NULL;
static lv_obj_t *s_marker_count = NULL;

static bool s_coord_received = false;
static zw_coord_t s_pending_coord = {0};
static int s_last_marker_count = 0;
static TaskHandle_t s_poll_task = NULL;
static volatile bool s_poll_running = false;

// BLE 坐标回调
static void coord_received_cb(zw_coord_t coord) {
    if (!s_scr) return;
    
    s_pending_coord = coord;
    s_coord_received = true;
    
    if (bsp_lvgl_lock(100)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.4f, %.4f", coord.lat, coord.lng);
        lv_label_set_text(s_coord, buf);
        lv_obj_set_style_text_color(s_coord, lv_color_hex(0xffffff), 0);
        lv_label_set_text(s_status, "[OK] BLE coord received");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
        lv_label_set_text(s_hint, "OK: Upload   Up/Dn: Back");
        bsp_lvgl_unlock();
    }
}

// 上传标记点任务（BLE 模式用）
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

// 轮询服务器获取最新标记点
static void poll_markers_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Poll task started");
    
    zw_marker_t markers[10];
    int count = 0;
    
    while (s_poll_running) {
        // 每 5 秒拉一次
        for (int i = 0; i < 50 && s_poll_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!s_poll_running) break;
        
        if (!zw_wifi_is_connected()) continue;
        
        esp_err_t err = zw_api_get_markers(markers, 10, &count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Get markers failed: %d", err);
            continue;
        }
        
        ESP_LOGI(TAG, "Got %d markers (was %d)", count, s_last_marker_count);
        
        // 更新全局标记点数据（供列表页使用）
        for (int i = 0; i < count; i++) {
            g_zw.markers[i] = markers[i];
        }
        g_zw.marker_count = count;
        
        if (bsp_lvgl_lock(100)) {
            // 更新标记点数量
            char cnt_buf[32];
            snprintf(cnt_buf, sizeof(cnt_buf), "Markers: %d/3", count);
            lv_label_set_text(s_marker_count, cnt_buf);
            
            // 如果有新标记点，显示最新的
            if (count > 0 && count > s_last_marker_count) {
                // 最新的标记点（数组最后一个是最新的）
                zw_coord_t latest;
                latest.lat = markers[count - 1].lat;
                latest.lng = markers[count - 1].lng;
                latest.valid = true;
                
                char buf[64];
                snprintf(buf, sizeof(buf), "%.4f, %.4f", latest.lat, latest.lng);
                lv_label_set_text(s_coord, buf);
                lv_obj_set_style_text_color(s_coord, lv_color_hex(0xffffff), 0);
                lv_label_set_text(s_status, "[OK] New marker received!");
                lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
                lv_label_set_text(s_hint, "OK: Refresh   Up/Dn: Back");
                
                s_coord_received = true;
                s_pending_coord = latest;
            }
            
            bsp_lvgl_unlock();
        }
        
        s_last_marker_count = count;
    }
    
    ESP_LOGI(TAG, "Poll task stopped");
    vTaskDelete(NULL);
}

void ui_add_marker_enter(void) {
    ESP_LOGI(TAG, "Enter add marker");
    
    s_coord_received = false;
    s_pending_coord.valid = false;
    s_last_marker_count = g_zw.marker_count;
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Add Marker");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 设备 ID
    s_device_id = lv_label_create(s_scr);
    char dev_id[32];
    snprintf(dev_id, sizeof(dev_id), "ID:%s", g_zw.config.device_id);
    lv_label_set_text(s_device_id, dev_id);
    lv_obj_set_style_text_font(s_device_id, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_device_id, lv_color_hex(0x8892b0), 0);
    lv_obj_align(s_device_id, LV_ALIGN_TOP_MID, 0, 46);
    
    // 标记点数量
    s_marker_count = lv_label_create(s_scr);
    char cnt_buf[32];
    snprintf(cnt_buf, sizeof(cnt_buf), "Markers: %d/3", g_zw.marker_count);
    lv_label_set_text(s_marker_count, cnt_buf);
    lv_obj_set_style_text_font(s_marker_count, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_marker_count, lv_color_hex(0x4ecdc4), 0);
    lv_obj_align(s_marker_count, LV_ALIGN_TOP_MID, 0, 64);
    
    // 坐标显示框
    lv_obj_t *coord_box = lv_obj_create(s_scr);
    lv_obj_set_size(coord_box, 210, 70);
    lv_obj_set_style_bg_color(coord_box, lv_color_hex(0x141428), 0);
    lv_obj_set_style_border_width(coord_box, 1, 0);
    lv_obj_set_style_border_color(coord_box, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_radius(coord_box, 10, 0);
    lv_obj_align(coord_box, LV_ALIGN_TOP_MID, 0, 95);
    
    s_coord = lv_label_create(coord_box);
    lv_label_set_text(s_coord, "Waiting for coords...");
    lv_obj_set_style_text_font(s_coord, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_coord, lv_color_hex(0x666666), 0);
    lv_obj_center(s_coord);
    
    // 状态
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Upload from phone webpage");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 185);
    
    // 底部提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "OK: Refresh   Up/Dn: Back");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    
    lv_screen_load(s_scr);
    
    // 注册 BLE 坐标回调（BLE 广播已在启动时开启）
    zw_ble_set_coord_callback(coord_received_cb);
    
    // 启动服务器轮询任务
    s_poll_running = true;
    xTaskCreate(poll_markers_task, "poll_markers", 4096, NULL, 3, &s_poll_task);
}

void ui_add_marker_exit(void) {
    // 停止轮询任务
    s_poll_running = false;
    
    // 注销 BLE 坐标回调（BLE 广播保持开启）
    zw_ble_set_coord_callback(NULL);
    
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = NULL;
        s_coord = NULL;
        s_hint = NULL;
        s_device_id = NULL;
        s_marker_count = NULL;
    }
}

void ui_add_marker_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        // 返回占领页面
        zw_ui_switch_page(ZW_PAGE_CAPTURE);
    } else if (btn == BSP_BTN_OK) {
        if (s_coord_received) {
            // 如果是 BLE 收到的坐标，上传
            // 如果是从服务器拉的，刷新一下列表
            if (s_pending_coord.valid && !s_last_marker_count) {
                // BLE 模式：有坐标但还没上传
                s_coord_received = false;
                lv_label_set_text(s_status, "Uploading...");
                lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
                xTaskCreate(upload_marker_task, "upload_marker", 4096, NULL, 5, NULL);
            } else {
                // WiFi 模式：手动刷新
                lv_label_set_text(s_status, "Refreshing...");
                lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
            }
        } else {
            // 没有坐标时按 OK 也刷新
            lv_label_set_text(s_status, "Refreshing...");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
        }
    }
}
