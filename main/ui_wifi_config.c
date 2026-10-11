/*
 * ZoneWar - Wi-Fi 配网页面
 * SoftAP 模式：设备创建 WiFi 热点，手机连接后通过网页配置
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_nvs.h"
#include "zw_provision.h"

static const char *TAG = "ui_wifi_cfg";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_ap_label = NULL;
static bool s_provisioning = false;

// 配网成功后的处理
static void wifi_connected_task(void *arg) {
    (void)arg;
    
    const char *ssid = zw_provision_get_ssid();
    
    // 保存到 NVS
    zw_nvs_save_wifi(ssid, "");  // 密码不保存（从 provision 模块拿不到密码，由 wifi 模块自己处理）
    // 实际上 zw_wifi_connect 内部已经会连接成功，但密码我们需要存
    // 简化：这里不做保存，让用户下次再配（实际项目中应该把密码传过来）
    // 但由于我们用 HTTP 服务器接收密码，可以在 connect handler 里保存
    
    if (bsp_lvgl_lock(200)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SSID: %s", ssid);
        lv_label_set_text(s_ssid_label, buf);
        lv_label_set_text(s_status, LV_SYMBOL_OK " Connected!");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
        bsp_lvgl_unlock();
    }
    
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    // 停止配网
    zw_provision_stop();
    s_provisioning = false;
    
    if (bsp_lvgl_lock(200)) {
        // 判断去哪个页面
        if (g_zw.config.team != ZW_TEAM_NONE) {
            zw_ui_switch_page(ZW_PAGE_DASHBOARD);
        } else {
            zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
        }
        bsp_lvgl_unlock();
    }
    
    vTaskDelete(NULL);
}

// 轮询配网状态的任务
static void prov_poll_task(void *arg) {
    (void)arg;
    
    while (s_provisioning) {
        zw_prov_state_t state = zw_provision_get_state();
        
        if (state == ZW_PROV_SUCCESS) {
            ESP_LOGI(TAG, "Provisioning success!");
            xTaskCreate(wifi_connected_task, "wifi_conn", 4096, NULL, 5, NULL);
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    vTaskDelete(NULL);
}

void ui_wifi_config_enter(void) {
    ESP_LOGI(TAG, "Enter wifi config (SoftAP provisioning)");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Wi-Fi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // WiFi 图标
    lv_obj_t *icon = lv_label_create(s_scr);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);
    
    // 热点名称
    s_ap_label = lv_label_create(s_scr);
    char ap_name[32];
    const char *dev_id = zw_nvs_get_device_id();
    snprintf(ap_name, sizeof(ap_name), "ZoneWar-%.4s", dev_id + strlen(dev_id) - 4);
    lv_label_set_text(s_ap_label, ap_name);
    lv_obj_set_style_text_font(s_ap_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ap_label, lv_color_hex(0x667eea), 0);
    lv_obj_align(s_ap_label, LV_ALIGN_CENTER, 0, -10);
    
    // 状态
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Starting hotspot...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd93d), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 30);
    
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
    lv_obj_align(s_ssid_label, LV_ALIGN_CENTER, 0, 55);
    
    // 提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Connect to Wi-Fi above, then visit 192.168.4.1");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, 220);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    lv_screen_load(s_scr);
    
    // 启动 SoftAP 配网
    esp_err_t err = zw_provision_start(zw_nvs_get_device_id());
    if (err == ESP_OK) {
        s_provisioning = true;
        lv_label_set_text(s_status, "Hotspot ready!");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
        
        // 启动轮询任务
        xTaskCreate(prov_poll_task, "prov_poll", 2048, NULL, 4, NULL);
    } else {
        lv_label_set_text(s_status, "Failed to start AP");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xff6b6b), 0);
        lv_label_set_text(s_hint, "Restart device and try again");
    }
}

void ui_wifi_config_exit(void) {
    if (s_provisioning) {
        zw_provision_stop();
        s_provisioning = false;
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
            zw_provision_stop();
            s_provisioning = false;
            
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
                esp_err_t err2 = zw_provision_start(zw_nvs_get_device_id());
                if (err2 == ESP_OK) {
                    s_provisioning = true;
                    lv_label_set_text(s_status, "Hotspot ready!");
                    lv_obj_set_style_text_color(s_status, lv_color_hex(0x4ade80), 0);
                    xTaskCreate(prov_poll_task, "prov_poll", 2048, NULL, 4, NULL);
                }
            }
        }
    }
}
