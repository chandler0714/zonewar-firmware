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
#include <string.h>

static const char *TAG = "zw_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static volatile zw_wifi_state_t s_state = ZW_WIFI_DISCONNECTED;
static int s_retry_num = 0;
static volatile bool s_scan_busy = false;  // 扫描互斥：防止 BLE 触发扫描与 UI 扫描任务并发冲突
static volatile bool s_scan_mode = false; // 扫描配网模式：STA_START 时不要自动 connect
static esp_err_t s_last_scan_err = ESP_OK;
static int s_last_raw_ap_num = -1;        // 最近一次扫描硬件返回的原始 AP 数（-1=未扫描）
#define MAX_RETRY 5

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // 仅在真正发起连接时才自动 connect；纯扫描模式下 WiFi 已就绪，无需连接
        if (!s_scan_mode) {
            esp_wifi_connect();
            s_state = ZW_WIFI_CONNECTING;
        } else {
            ESP_LOGI(TAG, "STA started in scan mode, no auto connect");
        }
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
    if (s_state == ZW_WIFI_CONNECTED || s_state == ZW_WIFI_CONNECTING) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    s_scan_mode = false;  // 进入真实连接流程，恢复 STA_START 自动 connect
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

    // 清除上次连接的结果标志，避免残留 FAIL_BIT 导致立即假失败
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_LOGI(TAG, "Connecting to %s...", ssid);

    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_STATE || err == ESP_OK) {
        // 已启动状态下 esp_wifi_start 返回 ESP_ERR_WIFI_STATE，需手动触发连接
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start/connect: %s", esp_err_to_name(err));
        s_state = ZW_WIFI_FAILED;
        return err;
    }

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
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
        s_state = ZW_WIFI_FAILED;
        return err;
    }
    ESP_LOGI(TAG, "Current WiFi mode: %d", mode);
    
    // 配置 STA
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(err));
        s_state = ZW_WIFI_FAILED;
        return err;
    }
    
    // 清空事件位
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // 开始连接
    err = esp_wifi_connect();
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

// 诊断信息（供 UI 显示扫描失败原因）
esp_err_t zw_wifi_get_last_scan_err(void) { return s_last_scan_err; }
int zw_wifi_get_last_raw_ap_num(void) { return s_last_raw_ap_num; }

int zw_wifi_scan(zw_wifi_ap_t *aps, int max_aps) {
    if (!aps || max_aps <= 0) return 0;
    if (s_scan_busy) {
        ESP_LOGW(TAG, "Scan busy, skip");
        return -1;
    }
    s_scan_busy = true;

    int count = 0;
    wifi_ap_record_t *records = NULL;
    s_last_scan_err = ESP_OK;
    s_last_raw_ap_num = -1;

    ESP_LOGI(TAG, "Starting WiFi scan...");

    // 检查 WiFi 状态
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        s_last_scan_err = err;
        goto done;
    }

    ESP_LOGI(TAG, "WiFi mode: %d", mode);

    if (mode == WIFI_MODE_NULL) {
        // WiFi 未启动，先以扫描模式启动 STA（STA_START 时不会自动 connect）
        ESP_LOGI(TAG, "WiFi not started, starting STA in scan mode");
        s_scan_mode = true;
        if (s_sta_netif == NULL) {
            s_sta_netif = esp_netif_create_default_wifi_sta();
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        // BLE 连接共存场景下给射频/驱动足够就绪时间
        vTaskDelay(pdMS_TO_TICKS(800));
    }
    // AP 或 APSTA 模式下直接扫描（STA 接口已经在运行）

    // 扫描参数：主动扫描，放宽每信道停留时间以适配 BLE/WiFi 共存
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 120,
        .scan_time.active.max = 300,
    };

    // 最多 3 轮扫描，任一轮扫到 AP 即采用（BLE 共存下首轮可能丢包）
    for (int round = 1; round <= 3 && count == 0; round++) {
        ESP_LOGI(TAG, "Scan round %d, calling esp_wifi_scan_start...", round);
        err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_ERR_WIFI_NOT_STARTED) {
            // 兜底：WiFi 未 start 时先 start 再重试（ESP-IDF v5.x init 后 mode 已设但未 start）
            ESP_LOGW(TAG, "WiFi not started, starting now and retry");
            if (s_sta_netif == NULL) {
                s_sta_netif = esp_netif_create_default_wifi_sta();
            }
            s_scan_mode = true;
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_err_t st = esp_wifi_start();
            if (st == ESP_OK || st == ESP_ERR_WIFI_STATE) {
                vTaskDelay(pdMS_TO_TICKS(500));
                err = esp_wifi_scan_start(&scan_cfg, true);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
            s_last_scan_err = err;
            // 若驱动未就绪，等待后重试
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint16_t ap_num = 0;
        err = esp_wifi_scan_get_ap_num(&ap_num);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get AP num: %s", esp_err_to_name(err));
            s_last_scan_err = err;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        s_last_raw_ap_num = ap_num;
        ESP_LOGI(TAG, "Round %d: found %d APs total", round, ap_num);

        if (ap_num == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));  // 空结果也再试一轮
            continue;
        }

        uint16_t to_get = ap_num > max_aps ? max_aps : ap_num;
        records = malloc(to_get * sizeof(wifi_ap_record_t));
        if (!records) {
            ESP_LOGE(TAG, "Failed to alloc scan records");
            s_last_scan_err = ESP_ERR_NO_MEM;
            goto done;
        }

        uint16_t found = to_get;
        err = esp_wifi_scan_get_ap_records(&found, records);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
            s_last_scan_err = err;
            free(records);
            records = NULL;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        for (int i = 0; i < found && count < max_aps; i++) {
            if (records[i].ssid[0] == '\0') continue;  // 跳过空 SSID
            strncpy(aps[count].ssid, (const char*)records[i].ssid, sizeof(aps[count].ssid) - 1);
            aps[count].ssid[sizeof(aps[count].ssid) - 1] = '\0';
            aps[count].rssi = records[i].rssi;
            aps[count].encrypted = (records[i].authmode != WIFI_AUTH_OPEN);
            count++;
        }
        free(records);
        records = NULL;

        ESP_LOGI(TAG, "Round %d: %d valid APs", round, count);
        if (count == 0) vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (count == 0 && s_last_scan_err == ESP_OK) {
        ESP_LOGW(TAG, "All scan rounds returned 0 AP");
    }

done:
    if (records) free(records);
    s_scan_busy = false;
    return count;
}

// ---- 启动期预扫描缓存 ----
// 开机时（BLE 启动前）调用：WiFi 射频独占，能可靠扫到 AP。
// 结果缓存到 s_cached_aps，BLE 特征读取时优先返回，绕开 BLE/WiFi 共存导致的扫描失败。
static zw_wifi_ap_t s_cached_aps[ZW_WIFI_SCAN_MAX_AP];
static int s_cached_count = 0;
static bool s_boot_scanned = false;

esp_err_t zw_wifi_boot_scan(void) {
    if (s_boot_scanned) return ESP_OK;  // 只扫一次
    s_boot_scanned = true;

    ESP_LOGI(TAG, "Boot scan: pre-BLE WiFi scan start");

    // 无条件确保 STA 已 start（ESP-IDF v5.x esp_wifi_init 默认会 set_mode(STA) 但不 start，
    // 因此 get_mode 返回非 NULL 也不能跳过 start）
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    s_scan_mode = true;  // STA_START 时不要自动 connect
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t start_err = esp_wifi_start();
    if (start_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi started for boot scan");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else if (start_err == ESP_ERR_WIFI_STATE) {
        ESP_LOGI(TAG, "WiFi already started");
    } else {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));
        return start_err;
    }

    int n = zw_wifi_scan(s_cached_aps, ZW_WIFI_SCAN_MAX_AP);
    if (n < 0) {
        ESP_LOGW(TAG, "Boot scan busy");
        return ESP_ERR_INVALID_STATE;
    }
    s_cached_count = n;
    ESP_LOGI(TAG, "Boot scan done: %d APs cached", s_cached_count);
    return (n > 0) ? ESP_OK : ESP_FAIL;
}

int zw_wifi_get_cached_aps(const zw_wifi_ap_t **out_aps) {
    if (out_aps) *out_aps = s_cached_aps;
    return s_cached_count;
}

// ---- WiFi 定位用扫描（含 BSSID）----
// 在 STA 已连接状态下进行扫描（coex 由驱动处理），输出 BSSID+RSSI+Channel 供 Mozilla Location Service 反查
int zw_wifi_scan_geo(zw_wifi_geo_ap_t *aps, int max_aps) {
    if (!aps || max_aps <= 0) return 0;
    if (s_scan_busy) {
        ESP_LOGW(TAG, "Geo scan busy");
        return -1;
    }
    s_scan_busy = true;

    int count = 0;
    wifi_ap_record_t *records = NULL;

    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGE(TAG, "Geo scan: WiFi not started");
        s_scan_busy = false;
        return -1;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 120,
        .scan_time.active.max = 300,
    };

    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Geo scan start failed: %s", esp_err_to_name(err));
        s_scan_busy = false;
        return -1;
    }

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK || ap_num == 0) {
        ESP_LOGW(TAG, "Geo scan: 0 AP found");
        s_scan_busy = false;
        return 0;
    }

    uint16_t to_get = ap_num > max_aps ? max_aps : ap_num;
    records = malloc(to_get * sizeof(wifi_ap_record_t));
    if (!records) {
        ESP_LOGE(TAG, "Geo scan: alloc failed");
        s_scan_busy = false;
        return -1;
    }

    uint16_t found = to_get;
    err = esp_wifi_scan_get_ap_records(&found, records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Geo scan get records failed: %s", esp_err_to_name(err));
        free(records);
        s_scan_busy = false;
        return -1;
    }

    for (int i = 0; i < found && count < max_aps; i++) {
        memcpy(aps[count].bssid, records[i].bssid, 6);
        aps[count].rssi = records[i].rssi;
        aps[count].channel = records[i].primary;
        count++;
    }
    free(records);

    ESP_LOGI(TAG, "Geo scan: %d APs with BSSID", count);
    s_scan_busy = false;
    return count;
}
