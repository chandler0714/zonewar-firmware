/*
 * ZoneWar - Wi-Fi 配网页面
 * 支持两种方式：SoftAP（推荐，兼容所有手机）+ BLE（安卓 Chrome 可用）
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
#include "zw_provision.h"

static const char *TAG = "ui_wifi_cfg";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_ap_label = NULL;
static bool s_advertising = false;
static bool s_softap_started = false;

// 配网成功后的处理任务
static void wifi_connect_task(void *arg) {
    zw_wifi_cred_t *cred = (zw_wifi_cred_t *)arg;
    
    ESP_LOGI(TAG, "WiFi connect: ssid=%s", cred->ssid);
    
    // 保存到 NVS
    zw_nvs_save_wifi(cred->ssid, cred->password);
    strncpy(g_zw.config.wifi_ssid, cred->ssid, sizeof(g_zw.config.wifi_ssid) - 1);
    strncpy(g_zw.config.wifi_password, cred->password, sizeof(g_zw.config.wifi_password) - 1);
    g_zw.config.wifi_configured = true;
    
    if (bsp_lvgl_lock(200)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SSID: %s", cred->ssid);
        lv_label_set_text(s_ssid_label, buf);
        lv_label_set_text(s_status, LV_SYMBOL_WIFI " Connecting...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
        bsp_lvgl_unlock();
    }
    
    // 停止配网模式
    if (s_softap_started) {
        zw_provision_stop();
        s_softap_started = false;
    }
    if (s_advertising) {
        zw_ble_stop_advertising();
        s_advertising = false;
    }
    
    // 尝试连接
    esp_err_t err = zw_wifi_connect(cred->ssid, cred->password);
    
    if (bsp_lvgl_lock(200)) {
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
            lv_label_set_text(s_hint, "Check password and retry");
            
            // 重新开启配网
            zw_ble_start_advertising();
            s_advertising = true;
            zw_provision_start(zw_nvs_get_device_id());
            s_softap_started = true;
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
    
    xTaskCreate(wifi_connect_task, "wifi_conn", 6144, cred_copy, 5, NULL);
}

// 轮询 SoftAP 配网状态
static void register_after_prov_task(void *arg) {
    (void)arg;
    
    ESP_LOGI(TAG, "Registering device after provisioning...");
    
    // 等待 WiFi 连接稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (zw_wifi_is_connected()) {
        bool has_team = false;
        int team = 0;
        if (zw_api_register(&has_team, &team) == ESP_OK) {
            ESP_LOGI(TAG, "Registered: has_team=%d, team=%d", has_team, team);
            if (has_team && g_zw.config.team == 0) {
                g_zw.config.team = team;
                zw_nvs_save_team(team);
            }
        }
    }
    
    vTaskDelete(NULL);
}

static void prov_poll_task(void *arg) {
    (void)arg;
    
    while (s_softap_started) {
        zw_prov_state_t state = zw_provision_get_state();
        
        if (state == ZW_PROV_SUCCESS) {
            ESP_LOGI(TAG, "SoftAP provisioning success, stopping AP...");
            
            // 停止 SoftAP（provision 模块已连接 WiFi，只需关掉 AP）
            zw_provision_stop();
            s_softap_started = false;
            
            // 从 NVS 加载配置（provision 模块已保存）
            zw_nvs_load(&g_zw.config);
            
            // 后台注册设备（8KB 栈，HTTP 需要较大栈）
            xTaskCreate(register_after_prov_task, "prov_reg", 8192, NULL, 5, NULL);
            
            // 更新 UI 并跳转
            if (bsp_lvgl_lock(100)) {
                if (g_zw.config.team == ZW_TEAM_NONE) {
                    zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
                } else {
                    zw_ui_switch_page(ZW_PAGE_DASHBOARD);
                }
                bsp_lvgl_unlock();
            }
            
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    vTaskDelete(NULL);
}

void ui_wifi_config_enter(void) {
    ESP_LOGI(TAG, "Enter wifi config (SoftAP + BLE)");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Wi-Fi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // WiFi 图标
    lv_obj_t *icon = lv_label_create(s_scr);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);
    
    // 热点名称
    s_ap_label = lv_label_create(s_scr);
    char ap_name[32];
    const char *dev_id = zw_nvs_get_device_id();
    int id_len = strlen(dev_id);
    const char *suffix = (id_len >= 4) ? (dev_id + id_len - 4) : dev_id;
    snprintf(ap_name, sizeof(ap_name), "ZoneWar-%.4s", suffix);
    lv_label_set_text(s_ap_label, ap_name);
    lv_obj_set_style_text_font(s_ap_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ap_label, lv_color_hex(0x667eea), 0);
    lv_obj_align(s_ap_label, LV_ALIGN_CENTER, 0, -10);
    
    // 状态
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Starting hotspot...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 25);
    
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
    lv_label_set_text(s_hint, "Connect to Wi-Fi above, visit 192.168.4.1");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, 220);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -25);
    
    lv_screen_load(s_scr);
    
    // 设置 BLE Wi-Fi 配网回调
    zw_ble_set_wifi_callback(on_wifi_received);
    
    // 启动 BLE 广播（安卓用户可用）
    zw_ble_start_advertising();
    s_advertising = true;
    
    // 启动 SoftAP 配网（推荐，所有手机兼容）
    esp_err_t err = zw_provision_start(dev_id);
    if (err == ESP_OK) {
        s_softap_started = true;
        lv_label_set_text(s_status, "Hotspot ready!");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
        
        // 启动轮询任务（4KB 栈）
        xTaskCreate(prov_poll_task, "prov_poll", 4096, NULL, 4, NULL);
    } else {
        // SoftAP 失败也没关系，还有 BLE
        lv_label_set_text(s_status, "BLE mode only");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
        lv_label_set_text(s_hint, "Use Android Chrome to connect");
    }
    
    ESP_LOGI(TAG, "WiFi config page ready");
}

void ui_wifi_config_exit(void) {
    if (s_advertising) {
        zw_ble_stop_advertising();
        s_advertising = false;
    }
    zw_ble_set_wifi_callback(NULL);
    
    if (s_softap_started) {
        zw_provision_stop();
        s_softap_started = false;
    }
    
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = NULL;
        s_ssid_label = NULL;
        s_hint = NULL;
        s_ap_label = NULL;
    }
}

void ui_wifi_config_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_OK) {
        // 如果已经有保存的 Wi-Fi，按 OK 可以直接尝试连接
        if (g_zw.config.wifi_configured) {
            // 先停止配网
            if (s_softap_started) {
                zw_provision_stop();
                s_softap_started = false;
            }
            if (s_advertising) {
                zw_ble_stop_advertising();
                s_advertising = false;
            }
            
            lv_label_set_text(s_status, "Connecting saved Wi-Fi...");
            lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
            
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
                // 重新开启配网
                zw_ble_start_advertising();
                s_advertising = true;
                esp_err_t err2 = zw_provision_start(zw_nvs_get_device_id());
                if (err2 == ESP_OK) {
                    s_softap_started = true;
                    lv_label_set_text(s_status, "Hotspot ready!");
                    lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
                    xTaskCreate(prov_poll_task, "prov_poll", 4096, NULL, 4, NULL);
                }
            }
        }
    }
}
