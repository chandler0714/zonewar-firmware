/*
 * ZoneWar - 服务器 API 模块
 */
#include "zw_api.h"
#include "zw_nvs.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "zw_api";

#define ZW_API_KEY "zonewar2024"

static char s_server_url[128] = {0};
static char s_device_id[32] = {0};

// 保护 s_resp_buf 的互斥锁：防止并发 HTTP 请求导致响应缓冲区串包
static SemaphoreHandle_t s_api_mutex = NULL;

// HTTP 响应缓冲区
#define RESP_BUF_SIZE 2048
static char s_resp_buf[RESP_BUF_SIZE];
static int s_resp_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 && s_resp_len < RESP_BUF_SIZE - 1) {
            int copy_len = evt->data_len;
            if (s_resp_len + copy_len >= RESP_BUF_SIZE) {
                copy_len = RESP_BUF_SIZE - 1 - s_resp_len;
            }
            memcpy(s_resp_buf + s_resp_len, evt->data, copy_len);
            s_resp_len += copy_len;
            s_resp_buf[s_resp_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t zw_api_init(const char *server_url) {
    strncpy(s_server_url, server_url, sizeof(s_server_url) - 1);
    strncpy(s_device_id, zw_nvs_get_device_id(), sizeof(s_device_id) - 1);
    if (s_api_mutex == NULL) {
        s_api_mutex = xSemaphoreCreateMutex();
    }
    ESP_LOGI(TAG, "API init, server: %s, device: %s", s_server_url, s_device_id);
    return ESP_OK;
}

// 加锁辅助：do_post/do_get + 解析期间必须持锁，避免 s_resp_buf 串包
static inline bool api_lock(void) {
    return s_api_mutex && xSemaphoreTake(s_api_mutex, pdMS_TO_TICKS(10000)) == pdTRUE;
}

static inline void api_unlock(void) {
    if (s_api_mutex) xSemaphoreGive(s_api_mutex);
}

static esp_err_t do_post(const char *path, const char *body) {
    if (!s_server_url[0]) {
        ESP_LOGE(TAG, "POST %s failed: server URL not configured", path);
        return ESP_ERR_INVALID_STATE;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_server_url, path);
    
    s_resp_len = 0;
    s_resp_buf[0] = '\0';
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key", ZW_API_KEY);
    esp_http_client_set_post_field(client, body, strlen(body));
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "POST %s -> %d", path, status);
        if (status >= 200 && status < 300) {
            err = ESP_OK;
        } else {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s", path, esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t do_get(const char *path) {
    if (!s_server_url[0]) {
        ESP_LOGE(TAG, "GET %s failed: server URL not configured", path);
        return ESP_ERR_INVALID_STATE;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_server_url, path);
    
    s_resp_len = 0;
    s_resp_buf[0] = '\0';
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "X-API-Key", ZW_API_KEY);
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "GET %s -> %d", path, status);
        if (status >= 200 && status < 300) {
            err = ESP_OK;
        } else {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s", path, esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t zw_api_register(bool *has_team, int *team) {
    char body[128];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", s_device_id);

    esp_err_t err = ESP_FAIL;
    cJSON *root = NULL;

    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post("/api/register", body);
    if (err == ESP_OK) {
        root = cJSON_Parse(s_resp_buf);
        if (!root) {
            err = ESP_FAIL;
        } else {
            cJSON *has_team_json = cJSON_GetObjectItem(root, "has_team");
            cJSON *team_json = cJSON_GetObjectItem(root, "team");

            if (has_team) *has_team = cJSON_IsTrue(has_team_json);
            if (team) {
                if (team_json && team_json->valuestring) {
                    if (strcmp(team_json->valuestring, "red") == 0) *team = 1;
                    else if (strcmp(team_json->valuestring, "blue") == 0) *team = 2;
                    else *team = 0;
                } else {
                    *team = 0;
                }
            }
        }
    }
    if (root) cJSON_Delete(root);
    api_unlock();
    return err;
}

esp_err_t zw_api_join_team(int team) {
    const char *team_str = (team == 1) ? "red" : "blue";
    char body[128];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"team\":\"%s\"}", s_device_id, team_str);

    esp_err_t err = ESP_FAIL;
    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post("/api/join_team", body);
    api_unlock();
    return err;
}

esp_err_t zw_api_heartbeat(void) {
    char body[128];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", s_device_id);

    // 心跳失败不影响主流程，静默处理
    esp_err_t err = ESP_FAIL;
    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post("/api/heartbeat", body);
    api_unlock();
    return err;
}

esp_err_t zw_api_get_stats(zw_stats_t *stats) {
    char path[256];
    snprintf(path, sizeof(path), "/api/stats?device_id=%s", s_device_id);

    esp_err_t err = ESP_FAIL;
    cJSON *root = NULL;

    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_get(path);
    if (err == ESP_OK) {
        root = cJSON_Parse(s_resp_buf);
        if (!root) {
            err = ESP_FAIL;
        } else {
            cJSON *item;
            if ((item = cJSON_GetObjectItem(root, "territory_count"))) stats->territory_count = item->valueint;
            if ((item = cJSON_GetObjectItem(root, "total_area"))) stats->total_area = item->valuedouble;
            if ((item = cJSON_GetObjectItem(root, "team_online"))) stats->team_online = item->valueint;
            if ((item = cJSON_GetObjectItem(root, "team_total"))) stats->team_total = item->valueint;
            if ((item = cJSON_GetObjectItem(root, "win_rate"))) stats->win_rate = item->valuedouble;
            if ((item = cJSON_GetObjectItem(root, "red_area"))) stats->red_area = item->valuedouble;
            if ((item = cJSON_GetObjectItem(root, "blue_area"))) stats->blue_area = item->valuedouble;
            if ((item = cJSON_GetObjectItem(root, "red_territories"))) stats->red_territories = item->valueint;
            if ((item = cJSON_GetObjectItem(root, "blue_territories"))) stats->blue_territories = item->valueint;
        }
    }
    if (root) cJSON_Delete(root);
    api_unlock();
    return err;
}

static esp_err_t get_markers_internal(const char *status, zw_marker_t *markers, int max_count, int *out_count) {
    char path[320];
    if (status && status[0]) {
        snprintf(path, sizeof(path), "/api/markers?device_id=%s&status=%s", s_device_id, status);
    } else {
        snprintf(path, sizeof(path), "/api/markers?device_id=%s", s_device_id);
    }

    esp_err_t err = ESP_FAIL;
    cJSON *root = NULL;

    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_get(path);
    if (err == ESP_OK) {
        root = cJSON_Parse(s_resp_buf);
        if (!root) {
            err = ESP_FAIL;
        } else {
            cJSON *markers_json = cJSON_GetObjectItem(root, "markers");
            int count = markers_json ? cJSON_GetArraySize(markers_json) : 0;
            if (count > max_count) count = max_count;

            for (int i = 0; i < count; i++) {
                cJSON *m = cJSON_GetArrayItem(markers_json, i);
                cJSON *item;
                markers[i].id = cJSON_GetObjectItem(m, "id")->valueint;
                markers[i].lat = cJSON_GetObjectItem(m, "lat")->valuedouble;
                markers[i].lng = cJSON_GetObjectItem(m, "lng")->valuedouble;
                markers[i].status[0] = '\0';
                markers[i].created_at = 0;
                if ((item = cJSON_GetObjectItem(m, "status")) && item->valuestring) {
                    strncpy(markers[i].status, item->valuestring, sizeof(markers[i].status) - 1);
                    markers[i].status[sizeof(markers[i].status) - 1] = '\0';
                }
                if ((item = cJSON_GetObjectItem(m, "created_at"))) {
                    markers[i].created_at = item->valuedouble;
                }
            }

            *out_count = count;
        }
    }
    if (root) cJSON_Delete(root);
    api_unlock();
    return err;
}

esp_err_t zw_api_get_markers(zw_marker_t *markers, int max_count, int *out_count) {
    return get_markers_internal(NULL, markers, max_count, out_count);
}

esp_err_t zw_api_get_markers_ex(const char *status, zw_marker_t *markers, int max_count, int *out_count) {
    return get_markers_internal(status, markers, max_count, out_count);
}

esp_err_t zw_api_add_marker(double lat, double lng, bool *territory_created, double *new_area, int *pending_count) {
    char body[256];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"lat\":%.6f,\"lng\":%.6f}",
             s_device_id, lat, lng);

    esp_err_t err = ESP_FAIL;
    cJSON *root = NULL;

    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post("/api/markers", body);
    if (err == ESP_OK) {
        root = cJSON_Parse(s_resp_buf);
        if (!root) {
            err = ESP_FAIL;
        } else {
            if (territory_created) {
                *territory_created = cJSON_IsTrue(cJSON_GetObjectItem(root, "territory_created"));
            }
            if (new_area && cJSON_GetObjectItem(root, "new_area")) {
                *new_area = cJSON_GetObjectItem(root, "new_area")->valuedouble;
            }
            if (pending_count) {
                cJSON *item = cJSON_GetObjectItem(root, "pending_count");
                *pending_count = item ? item->valueint : 0;
            }
        }
    }
    if (root) cJSON_Delete(root);
    api_unlock();
    return err;
}

esp_err_t zw_api_delete_marker(int marker_id) {
    char path[256];
    char body[128];
    snprintf(path, sizeof(path), "/api/markers/%d/delete", marker_id);
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", s_device_id);

    esp_err_t err = ESP_FAIL;
    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post(path, body);
    api_unlock();
    return err;
}

esp_err_t zw_api_exit_team(void) {
    char body[128];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", s_device_id);

    esp_err_t err = ESP_FAIL;
    if (!api_lock()) return ESP_ERR_TIMEOUT;
    err = do_post("/api/exit_team", body);
    api_unlock();
    return err;
}
