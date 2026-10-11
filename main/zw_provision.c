/*
 * ZoneWar - SoftAP 配网模块
 * 设备创建 WiFi 热点，手机连接后通过 HTTP 网页配置 WiFi
 */
#include "zw_provision.h"
#include "zw_wifi.h"
#include "zw_nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "zw_prov";

static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_server = NULL;
static volatile zw_prov_state_t s_state = ZW_PROV_IDLE;
static char s_ap_ssid[32] = {0};
static char s_prov_ssid[32] = {0};
static char s_prov_password[64] = {0};

// 配网网页
static const char *PROV_HTML =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ZoneWar 配网</title>"
"<style>body{font-family:sans-serif;background:#1a1a2e;color:#fff;padding:20px}"
".c{max-width:400px;margin:0 auto}h1{text-align:center;color:#667eea}"
".card{background:rgba(255,255,255,.05);border-radius:16px;padding:20px;margin-bottom:16px}"
"input{width:100%;padding:14px;border-radius:10px;border:1px solid #333;background:#222;color:#fff;margin-bottom:12px;font-size:15px}"
".btn{width:100%;padding:16px;border:none;border-radius:12px;background:linear-gradient(135deg,#4ecdc4,#44a08d);color:#fff;font-size:16px;font-weight:600;cursor:pointer}"
".wifilist{max-height:200px;overflow-y:auto;background:rgba(0,0,0,.2);border-radius:10px;margin-bottom:12px}"
".wifiitem{padding:12px;border-bottom:1px solid #333;cursor:pointer}"
".wifiitem:active{background:#333}"
".ssid{font-size:14px}.desc{font-size:11px;color:#888;margin-top:2px}"
".status{text-align:center;padding:20px;color:#888;font-size:13px}"
".scanbtn{padding:6px 12px;font-size:12px;border-radius:8px;background:#667eea33;color:#a5b4fc;border:1px solid #667eea55;cursor:pointer;float:right}"
".hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}"
".hdr span{color:#888;font-size:13px}"
"label{display:block;color:#888;font-size:13px;margin-bottom:6px}"
".result{text-align:center;padding:20px}.ok{color:#4ade80}.err{color:#ff6b6b}"
"</style></head><body><div class='c'>"
"<h1>ZoneWar 配网</h1>"
"<div class='card'><div class='hdr'><span>附近 Wi-Fi</span><button class='scanbtn' onclick='scanWifi()'>扫描</button></div>"
"<div class='wifilist' id='wlist'><div class='status'>点击扫描</div></div></div>"
"<div class='card'><label>Wi-Fi 名称</label><input id='ssid' placeholder='输入或选择'>"
"<label>Wi-Fi 密码</label><input id='pwd' type='password' placeholder='输入密码'>"
"<button class='btn' onclick='connect()'>连接 Wi-Fi</button></div>"
"<div id='res' class='card' style='display:none'></div>"
"</div><script>"
"function esc(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML}"
"function bars(r){var l=r>=-50?4:r>=-60?3:r>=-70?2:1;return '█'.repeat(l)+'░'.repeat(4-l)}"
"function render(aps){"
"var el=document.getElementById('wlist');if(!aps.length){el.innerHTML='<div class=\"status\">未找到 WiFi</div>';return}"
"aps.sort(function(a,b){return b.r-a.r});var h='';"
"for(var i=0;i<aps.length;i++){var a=aps[i];var l=a.e?'🔒':'🔓';"
"h+='<div class=\"wifiitem\" onclick=\"sel(\\''+a.s.replace(/'/g,\"\\\\'\")+'\\')\">'"
"+'<div class=\"ssid\">'+esc(a.s)+'</div><div class=\"desc\">'+l+' 信号 '+bars(a.r)+' '+a.r+'dBm</div></div>'}"
"el.innerHTML=h}"
"function sel(s){document.getElementById('ssid').value=s;document.getElementById('pwd').focus()}"
"async function scanWifi(){"
"var btn=event.target;btn.disabled=true;"
"document.getElementById('wlist').innerHTML='<div class=\"status\">扫描中...（约3秒）</div>';"
"try{"
"var r=await fetch('/scan');if(!r.ok)throw 'HTTP '+r.status;"
"var d=await r.json();"
"if(d.error){document.getElementById('wlist').innerHTML='<div class=\"status\">扫描失败：'+esc(d.error)+'</div>'}"
"else{render(d.aps||[])}"
"}catch(e){"
"console.log('scan error:',e);"
"document.getElementById('wlist').innerHTML='<div class=\"status\">扫描失败，请重试</div>'"
"}"
"btn.disabled=false}"
"async function connect(){"
"var s=document.getElementById('ssid').value.trim();"
"var p=document.getElementById('pwd').value;"
"if(!s){alert('请输入 Wi-Fi 名称');return}"
"var body='ssid='+encodeURIComponent(s)+'&password='+encodeURIComponent(p);"
"try{var r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body});var d=await r.json();"
"var el=document.getElementById('res');el.style.display='block';"
"if(d.success){"
"el.innerHTML='<div class=\"result ok\">⏳ 正在连接...<br><small>请稍候，约需 10 秒</small></div>';"
"pollStatus();"
"}else{"
"el.innerHTML='<div class=\"result err\">❌ 连接失败<br><small>'+esc(d.message||'请检查密码')+'</small></div>'}"
"}"
"catch(e){alert('请求失败')}"
"}"
"async function pollStatus(){"
"for(var i=0;i<25;i++){"
"try{"
"var r=await fetch('/status');var d=await r.json();"
"var el=document.getElementById('res');"
"if(d.state==='success'){"
"el.innerHTML='<div class=\"result ok\">✅ 连接成功！<br><small>可以关闭此页面</small></div>';"
"return;"
"}else if(d.state==='failed'){"
"el.innerHTML='<div class=\"result err\">❌ 连接失败<br><small>密码错误或信号太差，请检查后重试</small></div>';"
"return;"
"}"
"}catch(e){/* AP 可能已断开，说明连接成功 */"
"var el=document.getElementById('res');"
"el.innerHTML='<div class=\"result ok\">✅ 连接成功！<br><small>设备已连网，可以关闭此页面</small></div>';"
"return;"
"}"
"await new Promise(r=>setTimeout(r,1000));"
"}"
"// 超时"
"var el=document.getElementById('res');"
"el.innerHTML='<div class=\"result err\">⏱️ 连接超时<br><small>请检查 WiFi 密码是否正确</small></div>';"
"}"
"</script></body></html>";

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PROV_HTML, strlen(PROV_HTML));
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
    const char *state_str = "idle";
    switch (s_state) {
        case ZW_PROV_IDLE: state_str = "idle"; break;
        case ZW_PROV_STARTING: state_str = "starting"; break;
        case ZW_PROV_WAITING: state_str = "waiting"; break;
        case ZW_PROV_CONNECTING: state_str = "connecting"; break;
        case ZW_PROV_SUCCESS: state_str = "success"; break;
        case ZW_PROV_FAILED: state_str = "failed"; break;
        default: state_str = "unknown"; break;
    }
    
    char buf[256];
    int len = snprintf(buf, sizeof(buf), 
        "{\"state\":\"%s\",\"ssid\":\"%s\"}", 
        state_str, s_prov_ssid);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Scan request received");
    
    // 用堆分配而不是栈分配，避免 HTTP 任务栈溢出
    zw_wifi_ap_t *aps = malloc(ZW_WIFI_SCAN_MAX_AP * sizeof(zw_wifi_ap_t));
    if (!aps) {
        ESP_LOGE(TAG, "Failed to alloc scan buffer");
        const char *resp = "{\"error\":\"内存不足\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
    
    int count = zw_wifi_scan(aps, ZW_WIFI_SCAN_MAX_AP);
    ESP_LOGI(TAG, "Scan result: %d APs", count);
    
    // 计算需要的 buffer 大小并动态分配
    int buf_size = 200 + count * 100;  // 每个 AP 约 100 字节
    char *buf = malloc(buf_size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to alloc response buffer");
        free(aps);
        const char *resp = "{\"error\":\"内存不足\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
    
    int offset = snprintf(buf, buf_size, "{\"aps\":[");
    
    for (int i = 0; i < count && i < ZW_WIFI_SCAN_MAX_AP; i++) {
        if (offset + 128 >= buf_size) break;
        if (i > 0) buf[offset++] = ',';
        
        // 安全转义 SSID
        char safe_ssid[128];
        int si = 0;
        for (int j = 0; aps[i].ssid[j] && si < 120; j++) {
            char c = aps[i].ssid[j];
            if (c == '"' || c == '\\') { safe_ssid[si++] = '\\'; safe_ssid[si++] = c; }
            else if (c >= 0x20 && c < 0x7f) { safe_ssid[si++] = c; }
        }
        safe_ssid[si] = '\0';
        
        offset += snprintf(buf + offset, buf_size - offset,
            "{\"s\":\"%s\",\"r\":%d,\"e\":%d}",
            safe_ssid, aps[i].rssi, aps[i].encrypted ? 1 : 0);
    }
    
    offset += snprintf(buf + offset, buf_size - offset, "]}");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, offset);
    
    free(aps);
    free(buf);
    
    ESP_LOGI(TAG, "Scan response sent, %d bytes", offset);
    return ESP_OK;
}

// URL 解码
static int url_decode(const char *src, char *dst, int dst_size) {
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return j;
}

static void connect_task(void *arg) {
    (void)arg;
    
    char ssid[32], password[64];
    strncpy(ssid, s_prov_ssid, sizeof(ssid) - 1);
    strncpy(password, s_prov_password, sizeof(password) - 1);
    
    ESP_LOGI(TAG, "Connect task start: ssid=%s, pwd_len=%d", ssid, (int)strlen(password));
    
    // 尝试连接（不切换 WiFi 模式，保持 AP 开启）
    esp_err_t err = zw_wifi_connect_sta_only(ssid, password);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Connect task: SUCCESS");
        s_state = ZW_PROV_SUCCESS;
    } else {
        ESP_LOGE(TAG, "Connect task: FAILED, err=%s", esp_err_to_name(err));
        s_state = ZW_PROV_FAILED;
    }
    
    ESP_LOGI(TAG, "Connect task done, state=%d", s_state);
    vTaskDelete(NULL);
}

static esp_err_t connect_handler(httpd_req_t *req) {
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int total_len = req->content_len;
    int cur_len = 0;
    
    while (cur_len < total_len) {
        int recv = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (recv <= 0) break;
        cur_len += recv;
    }
    buf[cur_len] = '\0';
    
    ESP_LOGI(TAG, "Connect request body: %s", buf);
    
    // 解析 form data (URL-encoded)
    char ssid[64] = {0};
    char password[128] = {0};
    
    char *p = strstr(buf, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        char tmp[128];
        int len = end - p;
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, p, len);
        tmp[len] = '\0';
        url_decode(tmp, ssid, sizeof(ssid));
    }
    
    p = strstr(buf, "password=");
    if (p) {
        p += 9;
        char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        char tmp[128];
        int len = end - p;
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, p, len);
        tmp[len] = '\0';
        url_decode(tmp, password, sizeof(password));
    }
    
    free(buf);
    
    if (!ssid[0]) {
        const char *resp = "{\"success\":false,\"message\":\"请输入 Wi-Fi 名称\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Connect request: ssid=%s, pwd_len=%d", ssid, (int)strlen(password));
    
    // 保存凭证
    strncpy(s_prov_ssid, ssid, sizeof(s_prov_ssid) - 1);
    strncpy(s_prov_password, password, sizeof(s_prov_password) - 1);
    zw_nvs_save_wifi(ssid, password);
    
    s_state = ZW_PROV_CONNECTING;
    
    // 立即返回响应，后台任务去连接
    const char *resp = "{\"success\":true,\"message\":\"正在连接...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    // 启动后台任务连接 WiFi（8KB 栈）
    xTaskCreate(connect_task, "prov_connect", 8192, NULL, 5, NULL);
    
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

static const httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
};

esp_err_t zw_provision_start(const char *device_id) {
    if (s_state != ZW_PROV_IDLE && s_state != ZW_PROV_FAILED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_state = ZW_PROV_STARTING;
    
    // 生成 AP SSID: ZoneWar-XXXX
    int id_len = strlen(device_id);
    const char *suffix = (id_len >= 4) ? (device_id + id_len - 4) : device_id;
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ZoneWar-%.4s", suffix);
    
    ESP_LOGI(TAG, "Starting SoftAP: %s", s_ap_ssid);
    
    // 创建 AP netif
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        s_state = ZW_PROV_IDLE;
        return ESP_FAIL;
    }
    
    // 确保 STA netif 存在（用于扫描和后续连接）
    esp_netif_create_default_wifi_sta();
    
    // 配置 AP
    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    strncpy((char*)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "AP started (APSTA mode)");
    
    // 启动 HTTP 服务器 - 增大栈空间（扫描需要更多栈）
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;  // 默认 4096 不够，扫描函数需要更多
    
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        esp_wifi_stop();
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        s_state = ZW_PROV_IDLE;
        return ESP_FAIL;
    }
    
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &connect_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    
    ESP_LOGI(TAG, "HTTP server ready (stack=8192)");
    
    s_state = ZW_PROV_WAITING;
    return ESP_OK;
}

esp_err_t zw_provision_stop(void) {
    ESP_LOGI(TAG, "Stopping provisioning, state=%d", s_state);
    
    // 停 HTTP server
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    
    if (s_state == ZW_PROV_SUCCESS) {
        // 连接成功：只关掉 AP，保持 STA 连接
        ESP_LOGI(TAG, "Success mode: stopping AP only, keeping STA connected");
        
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_APSTA) {
            // 从 APSTA 切到 STA（只关 AP，STA 保持连接）
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
        
        // 销毁 AP netif
        if (s_ap_netif) {
            esp_netif_destroy(s_ap_netif);
            s_ap_netif = NULL;
        }
    } else {
        // 未连接成功：完全停止 WiFi
        ESP_LOGI(TAG, "Non-success: stopping full WiFi");
        
        esp_wifi_stop();
        
        if (s_ap_netif) {
            esp_netif_destroy(s_ap_netif);
            s_ap_netif = NULL;
        }
    }
    
    s_state = ZW_PROV_IDLE;
    return ESP_OK;
}

zw_prov_state_t zw_provision_get_state(void) {
    return s_state;
}

const char *zw_provision_get_ssid(void) {
    return s_prov_ssid;
}
