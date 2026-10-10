/*
 * ZoneWar - 领地战争
 * AI Passport 固件主程序
 */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "lvgl.h"

#include "zw_nvs.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "zw_api.h"
#include "zw_ui.h"

static const char *TAG = "zonewar_main";

// 按键回调
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(50)) return;
    zw_ui_dispatch_key(btn, ev);
    bsp_lvgl_unlock();
}

// 启动任务
static void startup_task(void *arg) {
    (void)arg;
    
    ESP_LOGI(TAG, "ZoneWar starting up...");
    
    // 等待 LVGL 就绪
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 加载配置
    zw_nvs_load(&g_zw.config);
    ESP_LOGI(TAG, "Config loaded: team=%d, wifi_cfg=%d",
             g_zw.config.team, g_zw.config.wifi_configured);
    
    // 初始化 Wi-Fi
    zw_wifi_init();
    
    // 初始化 BLE（不启动广播）
    zw_ble_init();
    zw_ble_set_device_info(g_zw.config.device_id, g_zw.config.team);
    
    // 初始化 API
    zw_api_init(g_zw.config.server_url);
    
    // 初始化 UI
    zw_ui_init();
    
    // 如果已配置 Wi-Fi，尝试连接
    if (g_zw.config.wifi_configured) {
        ESP_LOGI(TAG, "Auto-connecting Wi-Fi...");
        zw_wifi_connect(g_zw.config.wifi_ssid, g_zw.config.wifi_password);
        
        // 注册设备（后台）
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
    }
    
    // 决定进入哪个页面
    if (!g_zw.config.wifi_configured) {
        zw_ui_switch_page(ZW_PAGE_WIFI_CONFIG);
    } else if (g_zw.config.team == ZW_TEAM_NONE) {
        zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
    } else {
        zw_ui_switch_page(ZW_PAGE_DASHBOARD);
    }
    
    ESP_LOGI(TAG, "Startup complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ZoneWar v1.0 ===");
    
    // 初始化 NVS
    zw_nvs_init();
    
    // 初始化 I2C（必须在 display 之前）
    bsp_i2c_init();
    
    // 初始化显示
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }
    
    // 初始化 LVGL
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL init failed!");
        return;
    }
    bsp_display_backlight(100);
    
    // 初始化按键
    esp_err_t btn_err = bsp_button_init(on_key, NULL);
    if (btn_err != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(btn_err));
    }
    
    // 创建启动任务
    xTaskCreate(startup_task, "zw_startup", 8192, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "app_main done, LVGL running...");
}
