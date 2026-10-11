/*
 * ZoneWar - SoftAP 配网模块
 */
#include "zw_provision.h"
#include "zw_wifi.h"
#include "zw_nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "zw_prov";

static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_server = NULL;
static volatile zw_prov_state_t s_state = ZW_PROV_IDLE;
static char s_ssid[32] = {0};
static char s_password[64] = {0};
static char s_ap_ssid[32] = {0};
static char s_prov_ssid[32] = {0};
static volatile bool s_stop_requested = false;

// 嵌入式配网网页（精简版，单文件）
static const char *PROV_HTML =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<title>ZoneWar 配网</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;min-height:100vh;padding:20px}"
".container{max-width:400px;margin:0 auto}"
"h1{text-align:center;font-size:22px;margin-bottom:8px;background:linear-gradient(90deg,#ff6b6b,#4ecdc4);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}"
".subtitle{text-align:center;color:#8892b0;font-size:13px;margin-bottom:20px}"
".card{background:rgba(255,255,255,.05);border-radius:16px;padding:20px;margin-bottom:16px;border:1px solid rgba(255,255,255,.1)}"
".scan-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}"
".scan-title{color:#8892b0;font-size:13px}"
".scan-btn{padding:6px 12px;font-size:12px;border-radius:8px;background:rgba(102,126,234,.2);color:#a5b4fc;border:1px solid rgba(102,126,234,.3);cursor:pointer}"
".wifi-list{max-height:280px;overflow-y:auto;margin-bottom:12px;border-radius:10px;background:rgba(0,0,0,.2)}"
".wifi-item{display:flex;align-items:center;padding:12px 14px;border-bottom:1px solid rgba(255,255,255,.05);cursor:pointer}"
".wifi-item:last-child{border-bottom:none}"
".wifi-item:active{background:rgba(102,126,234,.2)}"
".wifi-item-info{flex:1;min-width:0}"
".wifi-ssid{font-size:14px;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".wifi-desc{font-size:11px;color:#8892b0;margin-top:2px}"
".wifi-icon{margin-left:8px;font-size:14px;color:#8892b0}"
".status-msg{text-align:center;padding:24px;color:#8892b0;font-size:13px}"
".spinner{display:inline-block;width:18px;height:18px;border:2px solid rgba(255,255,255,.1);border-top-color:#667eea;border-radius:50%;animation:spin .8s linear infinite;margin-right:8px;vertical-align:middle}"
"@keyframes spin{to{transform:rotate(360deg)}}"
"input{width:100%;padding:14px;border:1px solid rgba(255,255,255,.1);border-radius:10px;background:rgba(255,255,255,.05);color:#fff;font-size:15px;margin-bottom:12px}"
"input:focus{outline:none;border-color:#667eea}"
"input::placeholder{color:#5a6a8a}"
".label{display:block;color:#8892b0;font-size:13px;margin-bottom:6px}"
".btn{width:100%;padding:16px;border:none;border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;margin-bottom:12px}"
".btn-success{background:linear-gradient(135deg,#4ecdc4,#44a08d);color:#fff}"
".btn:active{transform:scale(.98)}"
".btn:disabled{opacity:.5;cursor:not-allowed}"
".result{text-align:center;padding:20px}"
".result-icon{font-size:48px;margin-bottom:12px}"
".result-text{font-size:16px;font-weight:600;margin-bottom:8px}"
".result-sub{color:#8892b0;font-size:13px}"
".success .result-text{color:#4ecdc4}"
".error .result-text{color:#ff6b6b}"
"</style></head><body>"
"<div class='container'>"
"<h1>⚔️ ZoneWar</h1>"
"<p class='subtitle'>Wi-Fi 配网</p>"

"<div class='card'>"
"<div class='scan-header'>"
"<span class='scan-title'>附近 Wi-Fi</span>"
"<button class='scan-btn' id='scanBtn' onclick='scanWifi()'>🔍 扫描</button>"
"</div>"
"<div class='wifi-list' id='wifiList'>"
"<div class='status-msg'>点击「扫描」查看附近 Wi-Fi</div>"
"</div>"
"</div>"

"<div class='card'>"
"<label class='label'>Wi-Fi 名称</label>"
"<input type='text' id='ssid' placeholder='请输入或从上方选择'>"
"<label class='label'>Wi-Fi 密码</label>"
"<input type='password' id='password' placeholder='请输入 Wi-Fi 密码'>"
"<button class='btn btn-success' id='submitBtn' onclick='submitConfig()'>📡 连接 Wi-Fi</button>"
"</div>"

"<div id='resultCard' class='card' style='display:none'>"
"<div class='result' id='resultDiv'></div>"
"</div>"
"</div>"

"<script>"
"function escapeHtml(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML}"
"function signalBars(r){var l=r>=-50?4:r>=-60?3:r>=-70?2:r>=-80?1:0;return '█'.repeat(l)+'░'.repeat(4-l)}"

"function renderList(aps){"
"var el=document.getElementById('wifiList');"
"if(!aps||!aps.length){el.innerHTML='<div class=\"status-msg\">未找到 Wi-Fi</div>';return}"
"aps.sort(function(a,b){return b.r-a.r});"
"var h='';"
"for(var i=0;i<aps.length;i++){var a=aps[i];"
"var lock=a.e?'🔒':'🔓';"
"h+='<div class=\"wifi-item\" onclick=\"selectSsid(\\''+a.s.replace(/'/g,\"\\\\'\")+'\\')\">'"
"+'<div class=\"wifi-item-info\">'"
"+'<div class=\"wifi-ssid\">'+escapeHtml(a.s)+'</div>'"
"+'<div class=\"wifi-desc\">信号 '+signalBars(a.r)+' '+a.r+' dBm</div>'"
+"'</div>'"
+'<span class="wifi-icon">'+lock+'</span>'
+"</div>"}"
"el.innerHTML=h}"

"function selectSsid(s){document.getElementById('ssid').value=s;document.getElementById('password').focus()}"

"async function scanWifi(){"
"var btn=document.getElementById('scanBtn');"
"var list=document.getElementById('wifiList');"
"btn.disabled=true;btn.textContent='扫描中...';"
"list.innerHTML='<div class=\"status-msg\"><span class=\"spinner\"></span>正在扫描...</div>';"
"try{"
"var r=await fetch('/scan');var d=await r.json();"
"renderList(d.aps||[]);"
"btn.textContent='🔍 重新扫描'"
"}catch(e){list.innerHTML='<div class=\"status-msg\">扫描失败</div>';btn.textContent='🔍 扫描'}"
"btn.disabled=false}"

"async function submitConfig(){"
"var ssid=document.getElementById('ssid').value.trim();"
"var pwd=document.getElementById('password').value;"
"if(!ssid){alert('请输入 Wi-Fi 名称');return}"
"var btn=document.getElementById('submitBtn');"
"btn.disabled=true;btn.textContent='连接中...';"
"try{"
"var fd=new FormData();fd.append('ssid',ssid);fd.append('password',pwd);"
"var r=await fetch('/connect',{method:'POST',body:fd});"
"var d=await r.json();"
"showResult(d.success,d.message||'')"
"}catch(e){showResult(false,'请求失败')}"
"btn.disabled=false;btn.textContent='📡 连接 Wi-Fi'"
"}"

"function showResult(ok,msg){"
"var card=document.getElementById('resultCard');"
"var div=document.getElementById('resultDiv');"
"card.style.display='block';"
"div.className='result '+(ok?'success':'error');"
"div.innerHTML=ok"
"?'<div class=\"result-icon\">✅</div><div class=\"result-text\">连接成功！</div><div class=\"result-sub\">设备已连上 Wi-Fi，你可以关闭此页面</div>'"
":('<div class=\"result-icon\">❌</div><div class=\"result-text\">连接失败</div><div class=\"result-sub\">'+escapeHtml(msg||'请检查密码后重试')+'</div>')"
"}"
"</script></body></html>";

// HTTP 处理：根路径返回配网网页
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PROV_HTML, strlen(PROV_HTML));
    return ESP_OK;
}

// HTTP 处理：扫描 WiFi
static esp_err_t scan_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi scan requested");
    
    zw_wifi_ap_t aps[ZW_WIFI_SCAN_MAX_AP];
    int count = zw_wifi_scan(aps, ZW_WIFI_SCAN_MAX_AP);
    
    // 构建 JSON
    char buf[1500];
    int offset = 0;
    offset += snprintf(buf + offset, sizeof(buf) - offset, "{\"aps\":[");
    
    for (int i = 0; i < count && i < 15; i++) {
        if (i > 0) buf[offset++] = ',';
        // 简化：直接用 snprintf
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
        offset += snprintf(buf + offset, sizeof(buf) - offset,
            "{\"s\":\"%s\",\"r\":%d,\"e\":%d}",
            safe_ssid, aps[i].rssi, aps[i].encrypted ? 1 : 0);
    }
    
    offset += snprintf(buf + offset, sizeof(buf) - offset, "]}");
    buf[sizeof(buf) - 1] = '\0';
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

// HTTP 处理：连接 WiFi
static esp_err_t connect_handler(httpd_req_t *req) {
    char buf[256];
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    
    if (total_len >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    while (cur_len < total_len) {
        int recv = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (recv <= 0) break;
        cur_len += recv;
    }
    buf[cur_len] = '\0';
    
    ESP_LOGI(TAG, "Connect request: %.*s", cur_len, buf);
    
    // 解析 form data (简单解析 ssid=xxx&password=xxx)
    char ssid[32] = {0};
    char password[64] = {0};
    
    char *ssid_start = strstr(buf, "ssid=");
    char *pwd_start = strstr(buf, "password=");
    
    if (ssid_start) {
        ssid_start += 5;
        char *end = strstr(ssid_start, "&");
        if (!end) end = ssid_start + strlen(ssid_start);
        int len = end - ssid_start;
        if (len > sizeof(ssid) - 1) len = sizeof(ssid) - 1;
        strncpy(ssid, ssid_start, len);
        ssid[len] = '\0';
        // URL 解码（简单处理）
        // 这里不做完整 URL 解码，大多数 SSID 没有特殊字符
    }
    
    if (pwd_start) {
        pwd_start += 9;
        char *end = strstr(pwd_start, "&");
        if (!end) end = pwd_start + strlen(pwd_start);
        int len = end - pwd_start;
        if (len > sizeof(password) - 1) len = sizeof(password) - 1;
        strncpy(password, pwd_start, len);
        password[len] = '\0';
    }
    
    ESP_LOGI(TAG, "SSID: %s, pwd_len: %d", ssid, (int)strlen(password));
    
    if (!ssid[0]) {
        const char *resp = "{\"success\":false,\"message\":\"SSID is required\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
    
    s_state = ZW_PROV_CONNECTING;
    strncpy(s_prov_ssid, ssid, sizeof(s_prov_ssid) - 1);
    strncpy(s_password, password, sizeof(s_password) - 1);
    
    // 保存到 NVS
    zw_nvs_save_wifi(ssid, password);
    
    // 尝试连接
    esp_err_t err = zw_wifi_connect(ssid, password);
    
    if (err == ESP_OK) {
        s_state = ZW_PROV_SUCCESS;
        const char *resp = "{\"success\":true,\"message\":\"Connected\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        
        // 5 秒后请求停止 AP
        s_stop_requested = true;
    } else {
        s_state = ZW_PROV_WAITING;
        const char *resp = "{\"success\":false,\"message\":\"Failed to connect. Check password.\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
    }
    
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
};

static const httpd_uri_t scan_uri = {
    .uri = "/scan",
    .method = HTTP_GET,
    .handler = scan_handler,
};

static const httpd_uri_t connect_uri = {
    .uri = "/connect",
    .method = HTTP_POST,
    .handler = connect_handler,
};

// 注册 captive portal 路由（iOS/Android 自动检测）
static void register_captive_portal(httpd_handle_t server) {
    httpd_uri_t uri;
    memset(&uri, 0, sizeof(uri));
    uri.method = HTTP_GET;
    uri.handler = root_handler;
    
    // iOS / macOS
    uri.uri = "/hotspot-detect.html";
    httpd_register_uri_handler(server, &uri);
    
    // Android
    uri.uri = "/generate_204";
    httpd_register_uri_handler(server, &uri);
    
    uri.uri = "/gen_204";
    httpd_register_uri_handler(server, &uri);
    
    // Windows
    uri.uri = "/ncsi.txt";
    httpd_register_uri_handler(server, &uri);
    
    uri.uri = "/connecttest.txt";
    httpd_register_uri_handler(server, &uri);
}

esp_err_t zw_provision_start(const char *device_id) {
    if (s_state != ZW_PROV_IDLE && s_state != ZW_PROV_FAILED) {
        ESP_LOGW(TAG, "Already provisioning, state=%d", s_state);
        return ESP_ERR_INVALID_STATE;
    }
    
    s_state = ZW_PROV_STARTING;
    s_stop_requested = false;
    
    // 生成 AP SSID: ZoneWar-XXXX (后4位设备ID)
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ZoneWar-%s", 
             device_id + strlen(device_id) - 4 < device_id ? device_id : device_id + strlen(device_id) - 4);
    // 保险处理
    if (strlen(device_id) >= 4) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ZoneWar-%.4s", device_id + strlen(device_id) - 4);
    } else {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ZoneWar-%s", device_id);
    }
    
    ESP_LOGI(TAG, "Starting SoftAP provisioning: %s", s_ap_ssid);
    
    // 确保 WiFi 已初始化
    // 注意：zw_wifi_init 已经在主程序调用了
    
    // 创建 AP 和 STA 接口
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        s_state = ZW_PROV_IDLE;
        return ESP_FAIL;
    }
    
    // 确保 STA netif 也存在（用于 WiFi 扫描和后续连接）
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGW(TAG, "STA netif may already exist (ok)");
    }
    
    // 配置 AP 模式
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .password = "",
            .channel = 6,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char*)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    
    // 设置为 AP+STA 模式（STA 用于扫描目标 WiFi）
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "AP started: %s", s_ap_ssid);
    
    // 启动 HTTP 服务器
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    
    if (httpd_start(&s_server, &http_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        esp_wifi_stop();
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        s_state = ZW_PROV_IDLE;
        return ESP_FAIL;
    }
    
    // 注册路由
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &connect_uri);
    
    ESP_LOGI(TAG, "HTTP server started on 192.168.4.1");
    
    s_state = ZW_PROV_WAITING;
    return ESP_OK;
}

esp_err_t zw_provision_stop(void) {
    ESP_LOGI(TAG, "Stopping provisioning...");
    
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    
    // 停止 AP
    esp_wifi_stop();
    
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    
    // 切换回 STA 模式
    if (s_state == ZW_PROV_SUCCESS) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }
    
    s_state = ZW_PROV_IDLE;
    return ESP_OK;
}

zw_prov_state_t zw_provision_get_state(void) {
    // 检查是否需要停止
    if (s_stop_requested && s_state == ZW_PROV_SUCCESS) {
        // 延迟停止，让网页先收到响应
        vTaskDelay(pdMS_TO_TICKS(2000));
        s_stop_requested = false;
    }
    return s_state;
}

const char *zw_provision_get_ssid(void) {
    return s_prov_ssid;
}
