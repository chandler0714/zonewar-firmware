/*
 * ZoneWar - Wi-Fi 配网页面
 * 通过 BLE 接收手机发来的 Wi-Fi 名称和密码
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "zw_nvs.h"

static const char *TAG = "ui_wifi_cfg";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_hint = NULL;
static bool s_advertising = false;

// 收到 Wi-Fi 配置后的处理任务
static void wifi_received_task(void *arg) {
    zw_wifi_cred_t *cred = (zw_wifi_cred_t *)arg;
    
    ESP_LOGI(TAG, "WiFi received: ssid=%s", cred->ssid);
    
    // 保存到 NVS
    zw_nvs_save_wifi(cred->ssid, cred->password);
    strncpy(g_zw.config.wifi_ssid, cred->ssid, sizeof(g_zw.config.wifi_ssid) - 1);
    strncpy(g_zw.config.wifi_password, cred->password, sizeof(g_zw.config.wifi_password) - 1);
    g_zw.config.wifi_configured = true;
    
    if (bsp_lvgl_lock(200)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SSID: %s", cred->ssid);
        lv_label_set_text(s_ssid_label, buf);
        lv_label_set_text(s_status, LV_SYMBOL_WIFI " Connecting Wi-Fi...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
        bsp_lvgl_unlock();
    }
    
    // 尝试连接
    esp_err_t err = zw_wifi_connect(cred->ssid, cred->password);
    
    if (bsp_lvgl_lock(200)) {
        if (err == ESP_OK) {
            lv_label_set_text(s_status, LV_SYMBOL_OK " Connected!");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 停止 BLE 广播
            zw_ble_stop_advertising();
            s_advertising = false;
            
            // 判断去哪个页面
            if (g_zw.config.team != ZW_TEAM_NONE) {
                zw_ui_switch_page(ZW_PAGE_DASHBOARD);
            } else {
                zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
            }
        } else {
            lv_label_set_text(s_status, LV_SYMBOL_CLOSE " Wi-Fi connect failed");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xff6b6b), 0);
            lv_label_set_text(s_hint, "Check password and retry");
        }
        bsp_lvgl_unlock();
    }
    
    free(cred);
    vTaskDelete(NULL);
}

// BLE 配网回调
static void on_wifi_received(zw_wifi_cred_t cred) {
    if (!cred.valid) return;
    
    zw_wifi_cred_t *cred_copy = malloc(sizeof(zw_wifi_cred_t));
    if (!cred_copy) return;
    memcpy(cred_copy, &cred, sizeof(zw_wifi_cred_t));
    
    xTaskCreate(wifi_received_task, "wifi_recv", 6144, cred_copy, 5, NULL);
}

void ui_wifi_config_enter(void) {
    ESP_LOGI(TAG, "Enter wifi config (BLE provisioning)");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Wi-Fi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // Bluetooth 图标
    lv_obj_t *icon = lv_label_create(s_scr);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);
    
    // 状态
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Waiting for phone...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 20);
    
    // SSID 显示
    s_ssid_label = lv_label_create(s_scr);
    if (g_zw.config.wifi_configured) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Saved: %s", g_zw.config.wifi_ssid);
        lv_label_set_text(s_ssid_label, buf);
    } else {
        lv_label_set_text(s_ssid_label, "");
    }
    lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ssid_label, lv_color_hex(0x666666), 0);
    lv_obj_align(s_ssid_label, LV_ALIGN_CENTER, 0, 50);
    
    // 提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Open web page -> Connect -> Send Wi-Fi");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    lv_screen_load(s_scr);
    
    // 设置 Wi-Fi 配网回调
    zw_ble_set_wifi_callback(on_wifi_received);
    
    // 启动 BLE 广播
    zw_ble_start_advertising();
    s_advertising = true;
    ESP_LOGI(TAG, "BLE advertising started for provisioning");
}

void ui_wifi_config_exit(void) {
    if (s_advertising) {
        zw_ble_stop_advertising();
        s_advertising = false;
    }
    zw_ble_set_wifi_callback(NULL);
    
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = NULL;
        s_ssid_label = NULL;
        s_hint = NULL;
    }
}

void ui_wifi_config_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_OK) {
        // 如果已经有保存的 Wi-Fi，按 OK 可以直接尝试连接（跳过配网）
        if (g_zw.config.wifi_configured) {
            if (s_advertising) {
                zw_ble_stop_advertising();
                s_advertising = false;
            }
            
            lv_label_set_text(s_status, "Connecting saved Wi-Fi...");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
            
            // 直接尝试连接
            esp_err_t err = zw_wifi_connect(g_zw.config.wifi_ssid, g_zw.config.wifi_password);
            if (err == ESP_OK) {
                lv_label_set_text(s_status, LV_SYMBOL_OK " Connected!");
                lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                if (g_zw.config.team != ZW_TEAM_NONE) {
                    zw_ui_switch_page(ZW_PAGE_DASHBOARD);
                } else {
                    zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
                }
            } else {
                lv_label_set_text(s_status, LV_SYMBOL_CLOSE " Connect failed");
                lv_obj_set_style_text_color(s_status, lv_color_hex(0xff6b6b), 0);
                // 重新开启 BLE 配网
                zw_ble_start_advertising();
                s_advertising = true;
            }
        } else {
            // 没配置过，刷新一下状态提示
            lv_label_set_text(s_status, "Send Wi-Fi config from phone");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
        }
    }
}
