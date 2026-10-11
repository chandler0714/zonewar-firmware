/*
 * ZoneWar - Wi-Fi 模块
 */
#include "zw_wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "zw_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static volatile zw_wifi_state_t s_state = ZW_WIFI_DISCONNECTED;
static int s_retry_num = 0;
#define MAX_RETRY 5

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        s_state = ZW_WIFI_CONNECTING;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", s_retry_num, MAX_RETRY);
            s_state = ZW_WIFI_CONNECTING;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            s_state = ZW_WIFI_FAILED;
            ESP_LOGI(TAG, "connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_state = ZW_WIFI_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t zw_wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    return ESP_OK;
}

esp_err_t zw_wifi_connect(const char *ssid, const char *password) {
    if (s_state == ZW_WIFI_CONNECTED) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    s_retry_num = 0;
    s_state = ZW_WIFI_CONNECTING;
    
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    
    // 等待连接结果
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t zw_wifi_connect_sta_only(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "STA connect: %s (mode preserved)", ssid);
    
    if (s_state == ZW_WIFI_CONNECTED) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    s_retry_num = 0;
    s_state = ZW_WIFI_CONNECTING;
    
    // 确保 STA netif 存在
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    // 检查当前模式
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    ESP_LOGI(TAG, "Current WiFi mode: %d", mode);
    
    // 配置 STA
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 清空事件位
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // 开始连接
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        s_state = ZW_WIFI_FAILED;
        return err;
    }
    
    ESP_LOGI(TAG, "Waiting for connection...");
    
    // 等待连接结果
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected to AP");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "STA connect failed");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "STA connect timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t zw_wifi_disconnect(void) {
    esp_err_t err = esp_wifi_stop();
    s_state = ZW_WIFI_DISCONNECTED;
    return err;
}

zw_wifi_state_t zw_wifi_get_state(void) {
    return s_state;
}

bool zw_wifi_is_connected(void) {
    return s_state == ZW_WIFI_CONNECTED;
}

int zw_wifi_scan(zw_wifi_ap_t *aps, int max_aps) {
    if (!aps || max_aps <= 0) return 0;
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // 检查 WiFi 状态
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return 0;
    }
    
    ESP_LOGI(TAG, "WiFi mode: %d", mode);
    
    if (mode == WIFI_MODE_NULL) {
        // WiFi 未启动，先启动 STA 模式
        ESP_LOGI(TAG, "WiFi not started, starting STA mode");
        if (s_sta_netif == NULL) {
            s_sta_netif = esp_netif_create_default_wifi_sta();
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    // AP 或 APSTA 模式下直接扫描（STA 接口已经在运行）
    
    // 配置扫描参数
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 120,  // 每个信道 120ms
    };
    
    // 开始扫描（阻塞）
    ESP_LOGI(TAG, "Calling esp_wifi_scan_start...");
    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return 0;
    }
    
    ESP_LOGI(TAG, "Scan complete, getting results...");
    
    // 获取扫描结果数量
    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP num: %s", esp_err_to_name(err));
        return 0;
    }
    
    ESP_LOGI(TAG, "Found %d APs total", ap_num);
    
    if (ap_num == 0) {
        ESP_LOGI(TAG, "No AP found");
        return 0;
    }
    
    if (ap_num > max_aps) ap_num = max_aps;
    
    // 分配临时 buffer
    wifi_ap_record_t *records = malloc(ap_num * sizeof(wifi_ap_record_t));
    if (!records) {
        ESP_LOGE(TAG, "Failed to alloc scan records");
        return 0;
    }
    
    uint16_t found = ap_num;
    err = esp_wifi_scan_get_ap_records(&found, records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
        free(records);
        return 0;
    }
    
    // 转换为我们的格式
    int count = 0;
    for (int i = 0; i < found && count < max_aps; i++) {
        // 跳过空 SSID
        if (records[i].ssid[0] == '\0') continue;
        
        strncpy(aps[count].ssid, (const char*)records[i].ssid, sizeof(aps[count].ssid) - 1);
        aps[count].ssid[sizeof(aps[count].ssid) - 1] = '\0';
        aps[count].rssi = records[i].rssi;
        aps[count].encrypted = (records[i].authmode != WIFI_AUTH_OPEN);
        count++;
    }
    
    free(records);
    ESP_LOGI(TAG, "Scan done: %d APs (valid)", count);
    return count;
}
