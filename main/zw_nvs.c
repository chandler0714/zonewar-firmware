/*
 * ZoneWar - NVS 存储模块
 */
#include "zw_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "zw_nvs";
static char s_device_id[ZW_DEVICE_ID_LEN] = {0};

#define NVS_NAMESPACE "zonewar"

esp_err_t zw_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    // 生成设备ID（基于MAC地址）
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Device ID: %s", s_device_id);
    return err;
}

esp_err_t zw_nvs_load(zw_config_t *config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // 第一次运行，返回默认值
        strncpy(config->device_id, s_device_id, ZW_DEVICE_ID_LEN - 1);
        config->team = ZW_TEAM_NONE;
        strncpy(config->wifi_ssid, "YourWiFiSSID", sizeof(config->wifi_ssid) - 1);
        strncpy(config->wifi_password, "YourWiFiPassword", sizeof(config->wifi_password) - 1);
        config->wifi_configured = false;
        strncpy(config->server_url, "http://47.84.88.8:5000", sizeof(config->server_url) - 1);
        return ESP_OK;
    }
    
    // 先设默认值，NVS 中有值的会覆盖
    strncpy(config->device_id, s_device_id, ZW_DEVICE_ID_LEN - 1);
    config->team = ZW_TEAM_NONE;
    config->wifi_ssid[0] = '\0';
    config->wifi_password[0] = '\0';
    config->wifi_configured = false;
    strncpy(config->server_url, "http://47.84.88.8:5000", sizeof(config->server_url) - 1);
    
    int32_t team = ZW_TEAM_NONE;
    nvs_get_i32(handle, "team", &team);
    config->team = (int)team;
    
    size_t len = sizeof(config->wifi_ssid);
    nvs_get_str(handle, "wifi_ssid", config->wifi_ssid, &len);
    
    len = sizeof(config->wifi_password);
    nvs_get_str(handle, "wifi_password", config->wifi_password, &len);
    
    uint8_t wifi_cfg = 0;
    nvs_get_u8(handle, "wifi_cfg", &wifi_cfg);
    config->wifi_configured = (wifi_cfg == 1);
    
    len = sizeof(config->server_url);
    nvs_get_str(handle, "server_url", config->server_url, &len);
    
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t zw_nvs_save_team(int team) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_i32(handle, "team", team);
    if (err == ESP_OK) err = nvs_commit(handle);
    
    nvs_close(handle);
    ESP_LOGI(TAG, "Saved team: %d", team);
    return err;
}

esp_err_t zw_nvs_save_wifi(const char *ssid, const char *password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_password", password);
    if (err == ESP_OK) err = nvs_set_u8(handle, "wifi_cfg", 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    
    nvs_close(handle);
    ESP_LOGI(TAG, "Saved Wi-Fi: %s", ssid);
    return err;
}

esp_err_t zw_nvs_save_server_url(const char *url) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(handle, "server_url", url);
    if (err == ESP_OK) err = nvs_commit(handle);
    
    nvs_close(handle);
    return err;
}

const char* zw_nvs_get_device_id(void) {
    return s_device_id;
}
