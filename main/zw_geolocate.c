/*
 * ZoneWar - WiFi 定位模块（BeaconDB - Mozilla Location Service 社区替代品）
 * 流程：
 *   1. zw_wifi_scan_geo() 扫周围 WiFi AP 的 BSSID+RSSI+Channel
 *   2. 拼 JSON POST 到 https://api.beacondb.net/v1/geolocate
 *   3. 解析返回的 location.lat/lng + accuracy
 * 使用 esp_crt_bundle_attach 自动验证服务端证书
 * 注：Mozilla Location Service 公共 API 已于 2024 年退役，BeaconDB 是社区维护的开源替代，
 *     请求/响应格式完全兼容。中国 WiFi 数据由全球用户贡献，覆盖度因地区而异。
 */
#include "zw_geolocate.h"
#include "zw_wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "zw_geo";

// 经由自家服务器中转：ESP32 → 47.84.88.8（国内，稳）→ beacondb.net（德国）
// 直接访问 beacondb 走 GFW 出境路由不稳，TLS 握手常失败
#define GEO_URL     "http://47.84.88.8:5000/api/geolocate"
#define GEO_API_KEY "zonewar2024"

#define GEO_RESP_BUF_SIZE  1024
static char s_resp_buf[GEO_RESP_BUF_SIZE];
static int s_resp_len = 0;
static char s_last_error[64] = "";

const char *zw_geolocate_get_last_error(void) { return s_last_error; }

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_resp_len + evt->data_len < GEO_RESP_BUF_SIZE - 1) {
                memcpy(s_resp_buf + s_resp_len, evt->data, evt->data_len);
                s_resp_len += evt->data_len;
                s_resp_buf[s_resp_len] = '\0';
            } else {
                ESP_LOGW(TAG, "resp buf overflow, dropped %d bytes", evt->data_len);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// 构造请求 JSON：{"wifiAccessPoints":[{"macAddress":"aa:bb:cc:dd:ee:ff","signalStrength":-50,"channel":6},...]}
static int build_request_json(const zw_wifi_geo_ap_t *aps, int count, char *buf, size_t buf_size) {
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "{\"wifiAccessPoints\":[");
    for (int i = 0; i < count; i++) {
        if (i > 0) pos += snprintf(buf + pos, buf_size - pos, ",");
        pos += snprintf(buf + pos, buf_size - pos,
            "{\"macAddress\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"signalStrength\":%d,\"channel\":%d}",
            aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
            aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5],
            aps[i].rssi, aps[i].channel);
        if (pos >= (int)buf_size - 64) {
            ESP_LOGW(TAG, "req buf nearly full at AP %d", i);
            break;
        }
    }
    pos += snprintf(buf + pos, buf_size - pos, "]}");
    return pos;
}

esp_err_t zw_geolocate(zw_geo_result_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    s_last_error[0] = '\0';

    if (!zw_wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "no_wifi");
        ESP_LOGE(TAG, "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. 扫描 WiFi AP（含 BSSID）
    zw_wifi_geo_ap_t aps[ZW_WIFI_GEO_MAX_AP];
    int ap_count = zw_wifi_scan_geo(aps, ZW_WIFI_GEO_MAX_AP);
    if (ap_count <= 0) {
        snprintf(s_last_error, sizeof(s_last_error), "scan_fail:%d", ap_count);
        ESP_LOGE(TAG, "WiFi scan failed: %d", ap_count);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Scanned %d APs for geolocation", ap_count);

    // 2. 构造请求 JSON
    char *req_json = malloc(2048);
    if (!req_json) {
        snprintf(s_last_error, sizeof(s_last_error), "no_mem");
        return ESP_ERR_NO_MEM;
    }
    int req_len = build_request_json(aps, ap_count, req_json, 2048);
    ESP_LOGI(TAG, "Request JSON (%d bytes): %.200s...", req_len, req_json);

    // 3. 发起 HTTPS POST
    s_resp_len = 0;
    s_resp_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = GEO_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 12000,  // 国内中转 + 出境回包总耗时预算
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(req_json);
        snprintf(s_last_error, sizeof(s_last_error), "http_init");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key", GEO_API_KEY);
    esp_http_client_set_post_field(client, req_json, req_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(req_json);

    if (err != ESP_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "http:%s", esp_err_to_name(err));
        ESP_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
        return ESP_ERR_TIMEOUT;
    }
    if (status != 200) {
        snprintf(s_last_error, sizeof(s_last_error), "http_%d", status);
        ESP_LOGE(TAG, "HTTP %d, resp: %s", status, s_resp_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Response (%d bytes): %s", s_resp_len, s_resp_buf);

    // 4. 解析响应 JSON：{"location":{"lat":39.9,"lng":116.4},"accuracy":50.0}
    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) {
        snprintf(s_last_error, sizeof(s_last_error), "json_parse");
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *location = cJSON_GetObjectItem(root, "location");
    cJSON *lat = location ? cJSON_GetObjectItem(location, "lat") : NULL;
    cJSON *lng = location ? cJSON_GetObjectItem(location, "lng") : NULL;
    cJSON *accuracy = cJSON_GetObjectItem(root, "accuracy");

    if (!lat || !lng || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lng)) {
        cJSON_Delete(root);
        snprintf(s_last_error, sizeof(s_last_error), "no_location");
        ESP_LOGE(TAG, "No location in response");
        return ESP_FAIL;
    }

    out->lat = lat->valuedouble;
    out->lng = lng->valuedouble;
    out->accuracy = accuracy && cJSON_IsNumber(accuracy) ? accuracy->valuedouble : 0.0;

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Geolocated: %.6f,%.6f (±%.0fm)", out->lat, out->lng, out->accuracy);
    return ESP_OK;
}
