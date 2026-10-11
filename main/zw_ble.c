/*
 * ZoneWar - BLE 模块
 * GATT 服务接收手机 GPS 坐标
 */
#include "zw_ble.h"
#include "zw_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "zw_ble";

// GATT UUID
#define ZW_BLE_SERVICE_UUID        0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0
#define ZW_BLE_CHAR_COORD_UUID     0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1
#define ZW_BLE_CHAR_DEVICE_UUID    0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2
#define ZW_BLE_CHAR_WIFI_UUID      0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3
#define ZW_BLE_CHAR_WIFI_SCAN_UUID 0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4

#define DEVICE_NAME "ZoneWar"

static zw_ble_coord_cb_t s_coord_cb = NULL;
static zw_ble_wifi_cb_t s_wifi_cb = NULL;
static char s_device_info[64] = "--------|none";
static volatile bool s_initialized = false;
static volatile bool s_connected = false;
static volatile bool s_advertising = false;
static uint8_t s_addr_type;

static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static bool s_host_done;

// GATT 特征值回调
static int coord_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    
    char buf[64] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    
    int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
    
    ESP_LOGI(TAG, "Received coord: %s", buf);
    
    // 解析坐标 "lat,lng"
    zw_coord_t coord = {0};
    char *comma = strchr(buf, ',');
    if (comma) {
        *comma = '\0';
        coord.lat = strtod(buf, NULL);
        coord.lng = strtod(comma + 1, NULL);
        coord.valid = true;
        ESP_LOGI(TAG, "Parsed: lat=%f, lng=%f", coord.lat, coord.lng);
        
        if (s_coord_cb) {
            s_coord_cb(coord);
        }
    }
    
    return 0;
}

static int device_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    
    int rc = os_mbuf_append(ctxt->om, s_device_info, strlen(s_device_info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// GATT 服务定义
// Wi-Fi 配网写入回调
static int wifi_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    
    char buf[128] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    
    int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
    
    ESP_LOGI(TAG, "Received WiFi config (len=%d)", len);
    
    // 解析 "ssid;password" 格式
    zw_wifi_cred_t cred = {0};
    char *sep = strchr(buf, ';');
    if (sep) {
        *sep = '\0';
        strncpy(cred.ssid, buf, sizeof(cred.ssid) - 1);
        strncpy(cred.password, sep + 1, sizeof(cred.password) - 1);
        cred.valid = true;
        ESP_LOGI(TAG, "WiFi: ssid=%s, password_len=%d", cred.ssid, (int)strlen(cred.password));
        
        if (s_wifi_cb) {
            s_wifi_cb(cred);
        }
    }
    
    return 0;
}

// WiFi 扫描特征值：写入 "SCAN" 触发扫描，读取返回 JSON 结果
static char s_scan_result[1024] = "";
static volatile bool s_scan_in_progress = false;

static int wifi_scan_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    
    char buf[16] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    
    int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
    
    ESP_LOGI(TAG, "WiFi scan command: %s", buf);
    
    if (strncmp(buf, "SCAN", 4) == 0) {
        if (s_scan_in_progress) {
            strncpy(s_scan_result, "{\"error\":\"busy\"}", sizeof(s_scan_result) - 1);
            return 0;
        }
        
        s_scan_in_progress = true;
        
        // 在后台任务中执行扫描（避免阻塞 BLE 栈）
        xTaskCreatePinnedToCore([](void *pvParameters) {
            zw_wifi_ap_t aps[ZW_WIFI_SCAN_MAX_AP];
            int count = zw_wifi_scan(aps, ZW_WIFI_SCAN_MAX_AP);
            
            // 格式化为紧凑 JSON
            int offset = 0;
            offset += snprintf(s_scan_result + offset, sizeof(s_scan_result) - offset, "[");
            
            for (int i = 0; i < count && i < 15; i++) {
                if (i > 0) {
                    offset += snprintf(s_scan_result + offset, sizeof(s_scan_result) - offset, ",");
                }
                // 转义特殊字符
                char safe_ssid[64];
                int si = 0;
                for (int j = 0; aps[i].ssid[j] && si < 60; j++) {
                    char c = aps[i].ssid[j];
                    if (c == '"' || c == '\\') {
                        safe_ssid[si++] = '\\';
                        safe_ssid[si++] = c;
                    } else if (c >= 0x20 && c < 0x7f) {
                        safe_ssid[si++] = c;
                    }
                }
                safe_ssid[si] = '\0';
                
                offset += snprintf(s_scan_result + offset, sizeof(s_scan_result) - offset,
                    "{\"s\":\"%s\",\"r\":%d,\"e\":%d}",
                    safe_ssid, aps[i].rssi, aps[i].encrypted ? 1 : 0);
            }
            
            offset += snprintf(s_scan_result + offset, sizeof(s_scan_result) - offset, "]");
            s_scan_result[sizeof(s_scan_result) - 1] = '\0';
            
            ESP_LOGI(TAG, "Scan result: %d APs, json len=%d", count, (int)strlen(s_scan_result));
            s_scan_in_progress = false;
            vTaskDelete(NULL);
        }, "wifi_scan", 4096, NULL, 5, NULL, 0);
    }
    
    return 0;
}

static int wifi_scan_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    
    const char *result = s_scan_result[0] ? s_scan_result : "[]";
    int rc = os_mbuf_append(ctxt->om, result, strlen(result));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(ZW_BLE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID128_DECLARE(ZW_BLE_CHAR_COORD_UUID),
                .access_cb = coord_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID128_DECLARE(ZW_BLE_CHAR_DEVICE_UUID),
                .access_cb = device_read_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID128_DECLARE(ZW_BLE_CHAR_WIFI_UUID),
                .access_cb = wifi_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID128_DECLARE(ZW_BLE_CHAR_WIFI_SCAN_UUID),
                .access_cb = wifi_scan_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },
    { 0 }
};

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data: %d", rc);
        return rc;
    }
    
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
    if (rc == 0) {
        s_advertising = true;
        ESP_LOGI(TAG, "Advertising started");
    }
    return rc;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connected, status=%d", event->connect.status);
        if (event->connect.status == 0) {
            s_connected = true;
            s_advertising = false;
        } else {
            advertise();
        }
        break;
        
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
        s_connected = false;
        // 重新广播
        advertise();
        break;
        
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        s_advertising = false;
        break;
        
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "BLE reset: %d", reason);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot ensure address: %d", rc);
        return;
    }
    
    rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot infer address type: %d", rc);
        return;
    }
    
    ESP_LOGI(TAG, "BLE sync complete");
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t zw_ble_init(void) {
    if (s_initialized) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing BLE...");
    
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;
    
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    int rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Set device name failed: %d", rc);
        return ESP_FAIL;
    }
    
    // 注册 GATT 服务
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }
    
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    
    if (xTaskCreatePinnedToCore(host_task, "nimble_host", 4096,
                               NULL, configMAX_PRIORITIES - 4, &s_host_task,
                               NIMBLE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create host task");
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    s_host_done = false;
    ESP_LOGI(TAG, "BLE initialized");
    return ESP_OK;
}

esp_err_t zw_ble_start_advertising(void) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "BLE not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_advertising) return ESP_OK;
    
    int rc = advertise();
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t zw_ble_stop_advertising(void) {
    if (!s_advertising) return ESP_OK;
    
    int rc = ble_gap_adv_stop();
    s_advertising = false;
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

bool zw_ble_is_connected(void) {
    return s_connected;
}

void zw_ble_set_coord_callback(zw_ble_coord_cb_t cb) {
    s_coord_cb = cb;
}

void zw_ble_set_wifi_callback(zw_ble_wifi_cb_t cb) {
    s_wifi_cb = cb;
}

void zw_ble_set_device_info(const char *device_id, int team) {
    const char *team_str = "none";
    if (team == 1) team_str = "red";
    else if (team == 2) team_str = "blue";
    
    snprintf(s_device_info, sizeof(s_device_info), "%s|%s", device_id, team_str);
    ESP_LOGI(TAG, "Device info set: %s", s_device_info);
}
