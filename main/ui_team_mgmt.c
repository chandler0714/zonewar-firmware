/*
 * ZoneWar - 阵营管理页（TEAM_MGMT，游戏主页）
 * 布局（240x320，内容可上下滚动）：
 *   固定顶栏：设备编码 + 阵营
 *   滚动区：我的领地 / 阵营统计 / 红蓝对比 / 坐标列表
 *   固定底栏：按键提示
 * 按键：UP/DOWN 滚动，OK 设备自主定位打点（与列表重复的位置不记录）
 * 通用：长按上键进设置页
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"
#include "zw_ble.h"
#include "zw_api.h"
#include "zw_geolocate.h"
#include "zw_theme.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "ui_team_mgmt";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_content = NULL;    // 滚动容器
static lv_obj_t *s_title_team = NULL; // 顶栏阵营
static lv_obj_t *s_dev_label = NULL;
static lv_obj_t *s_my_label = NULL;
static lv_obj_t *s_team_label = NULL;
static lv_obj_t *s_bar_red = NULL;    // 对比条红色部分
static lv_obj_t *s_bar_blue = NULL;
static lv_obj_t *s_vs_label = NULL;
static lv_obj_t *s_coord_label = NULL;
static lv_obj_t *s_status = NULL;     // 底栏状态（同步中/错误）
static int32_t s_scroll_y = 0;
static volatile bool s_sync_running = false;  // 防止 OK 连按导致同步/打点任务重入

static const int32_t SCROLL_STEP = 60;

// 重复点判定阈值（米）：与列表中任一点距离小于该值视为同一位置，不记录。
// WiFi 定位精度约 30m，取 100m 避免同一定位抖动被当成不同点
#define ZW_DUP_DIST_M 100.0

// 两点间近似距离（米）：等距圆柱投影，适用于数公里内的小范围判定
static double marker_dist_m(double lat1, double lng1, double lat2, double lng2) {
    const double R = 6371000.0;
    const double rad = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * rad;
    double dlng = (lng2 - lng1) * rad;
    double cos_lat = cos(((lat1 + lat2) / 2.0) * rad);
    double x = dlng * cos_lat;
    return R * sqrt(x * x + dlat * dlat);
}

static void refresh_bar(void) {
    double red = g_zw.stats.red_area;
    double blue = g_zw.stats.blue_area;
    double total = red + blue;
    if (total <= 0) total = 1;

    int bar_w = 206;
    int red_w = (int)(bar_w * red / total);
    if (red_w < 2 && red > 0) red_w = 2;
    if (red_w > bar_w - 2 && blue > 0) red_w = bar_w - 2;
    lv_obj_set_width(s_bar_red, red_w);
    lv_obj_set_width(s_bar_blue, bar_w - red_w);

    char buf[128];
    char a1[16], a2[16];
    zw_format_area(red, a1, sizeof(a1));
    zw_format_area(blue, a2, sizeof(a2));
    snprintf(buf, sizeof(buf), "R %s | %s B", a1, a2);
    lv_label_set_text(s_vs_label, buf);
}

static void refresh_labels(void) {
    if (!s_scr) return;

    char buf[192];
    char area_buf[16];

    // 顶栏阵营
    const char *team_str = g_zw.config.team == ZW_TEAM_RED ? "RED" :
                           g_zw.config.team == ZW_TEAM_BLUE ? "BLUE" : "NONE";
    lv_label_set_text(s_title_team, team_str);
    lv_obj_set_style_text_color(s_title_team,
        lv_color_hex(g_zw.config.team == ZW_TEAM_RED ? ZW_C_RED : ZW_C_BLUE), 0);

    // 设备编码
    snprintf(buf, sizeof(buf), "UNIT  %s", g_zw.config.device_id);
    lv_label_set_text(s_dev_label, buf);

    // 我的领地
    zw_format_area(g_zw.stats.total_area, area_buf, sizeof(area_buf));
    snprintf(buf, sizeof(buf), "YOUR TERRITORY\n%d AREAS   /   %s",
             g_zw.stats.territory_count, area_buf);
    lv_label_set_text(s_my_label, buf);

    // 阵营统计
    int my_team_areas = (g_zw.config.team == ZW_TEAM_RED) ? g_zw.stats.red_territories
                        : (g_zw.config.team == ZW_TEAM_BLUE) ? g_zw.stats.blue_territories : 0;
    snprintf(buf, sizeof(buf),
             "FACTION INTEL\n%d/%d ONLINE   /   %d AREAS",
             g_zw.stats.team_online, g_zw.stats.team_total,
             my_team_areas);
    lv_label_set_text(s_team_label, buf);

    // 对比条
    refresh_bar();

    // 坐标列表
    if (g_zw.marker_count == 0) {
        lv_label_set_text(s_coord_label, "PENDING MARKERS\nNO MARKERS YET");
    } else {
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "PENDING MARKERS");
        for (int i = 0; i < g_zw.marker_count && pos < (int)sizeof(buf) - 48; i++) {
            const char *mark = "";
            if (g_zw.markers[i].status[0]) {
                if (strcmp(g_zw.markers[i].status, "accepted") == 0) mark = " +";
                else if (strcmp(g_zw.markers[i].status, "rejected") == 0) mark = " x";
                else mark = " .";
            }
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "\n%.4f,%.4f%s",
                            g_zw.markers[i].lat, g_zw.markers[i].lng, mark);
        }
        lv_label_set_text(s_coord_label, buf);
    }
}

// 同步任务：
//   request_mark=true（OK 键）：未联网先尝试用已存配置自动重连 Wi-Fi，
//     成功后设备自主定位（扫 WiFi → 定位服务反查 → 与列表去重后上传）→ 拉取最新数据
//   request_mark=false（进页面/定时）：直接拉取，离线时仅提示不重连
static void sync_now_task(void *arg) {
    bool request_mark = (bool)(uintptr_t)arg;

    if (!zw_wifi_is_connected()) {
        // OK 键且本地有已存 Wi-Fi：自动重连，用户回到原网络环境时无需重走蓝牙配网
        if (request_mark && g_zw.config.wifi_configured && !zw_ui_wifi_busy()) {
            if (bsp_lvgl_lock(100)) {
                if (s_status) {
                    lv_label_set_text(s_status, "Reconnecting Wi-Fi...");
                    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
                }
                bsp_lvgl_unlock();
            }

            esp_err_t cerr = zw_wifi_connect(g_zw.config.wifi_ssid,
                                             g_zw.config.wifi_password);
            if (cerr != ESP_OK) {
                if (bsp_lvgl_lock(100)) {
                    if (s_status) {
                        lv_label_set_text(s_status, "No Wi-Fi! Long UP setup");
                        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
                    }
                    bsp_lvgl_unlock();
                }
                s_sync_running = false;
                vTaskDelete(NULL);
                return;
            }
            // 重连成功，短暂提示后继续定位流程
            if (bsp_lvgl_lock(100)) {
                if (s_status) {
                    lv_label_set_text(s_status, "Wi-Fi connected");
                    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
                }
                bsp_lvgl_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            // 无已存配置（或进页面自动同步）：无法自动重连
            if (bsp_lvgl_lock(100)) {
                if (s_status) {
                    lv_label_set_text(s_status,
                        request_mark ? "No Wi-Fi! Long UP setup" : "Offline - press OK retry");
                    lv_obj_set_style_text_color(s_status,
                        request_mark ? lv_color_hex(ZW_C_RED) : lv_color_hex(ZW_C_MUTED), 0);
                }
                bsp_lvgl_unlock();
            }
            s_sync_running = false;
            vTaskDelete(NULL);
            return;
        }
    }

    if (request_mark) {
        // ---- 设备自主定位流程（不依赖手机/BLE）----
        // 步骤 1：扫描 WiFi
        if (bsp_lvgl_lock(100)) {
            if (s_status) {
                lv_label_set_text(s_status, "Scanning WiFi...");
                lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
            }
            bsp_lvgl_unlock();
        }

        zw_geo_result_t geo = {0};
        esp_err_t gerr = zw_geolocate(&geo);  // 内部：扫描 + HTTPS + 解析，约 5-10s

        if (gerr != ESP_OK) {
            // 定位失败：显示错误码便于诊断
            const char *err_str = zw_geolocate_get_last_error();
            if (bsp_lvgl_lock(100)) {
                if (s_status) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Loc fail: %s", err_str);
                    lv_label_set_text(s_status, buf);
                    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
                }
                bsp_lvgl_unlock();
            }
            ESP_LOGE(TAG, "Geolocate failed: %s", err_str);
            vTaskDelay(pdMS_TO_TICKS(2000));  // 错误信息停留 2 秒
        } else {
            // 步骤 2：定位成功，显示坐标
            if (bsp_lvgl_lock(100)) {
                if (s_status) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Got: %.4f,%.4f", geo.lat, geo.lng);
                    lv_label_set_text(s_status, buf);
                    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
                }
                bsp_lvgl_unlock();
            }

            // 与设备上当前坐标列表（待处理点）比对：与任一已有点距离过近视为
            // 同一位置，不上传、不记录、不计面积；只有与列表所有点都不同才记录
            bool duplicate = false;
            for (int i = 0; i < g_zw.marker_count; i++) {
                if (marker_dist_m(geo.lat, geo.lng,
                                  g_zw.markers[i].lat, g_zw.markers[i].lng) < ZW_DUP_DIST_M) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                ESP_LOGW(TAG, "Duplicate location, skip upload");
                if (bsp_lvgl_lock(100)) {
                    if (s_status) {
                        lv_label_set_text(s_status, "Duplicate point!");
                        lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_AMBER), 0);
                    }
                    bsp_lvgl_unlock();
                }
                vTaskDelay(pdMS_TO_TICKS(2000));  // 提示停留 2 秒
            } else {
                // 与已有点都不同：上传记录，凑齐 3 个不同点形成领地
                bool territory_created = false;
                double new_area = 0;
                int pending_count = 0;
                esp_err_t aerr = zw_api_add_marker(geo.lat, geo.lng,
                                                   &territory_created, &new_area, &pending_count);

                if (bsp_lvgl_lock(100)) {
                    if (s_status) {
                        char buf[80];
                        if (aerr != ESP_OK) {
                            snprintf(buf, sizeof(buf), "Upload fail!");
                            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
                        } else if (territory_created) {
                            char abuf[16];
                            zw_format_area(new_area, abuf, sizeof(abuf));
                            snprintf(buf, sizeof(buf), "Territory! %s", abuf);
                            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
                        } else {
                            snprintf(buf, sizeof(buf), "Marker +1 (%d/3)", pending_count);
                            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
                        }
                        lv_label_set_text(s_status, buf);
                    }
                    bsp_lvgl_unlock();
                }

                // 三个不同点已形成领地：清空设备坐标列表，开始收集下一组
                if (aerr == ESP_OK && territory_created) {
                    g_zw.marker_count = 0;
                }

                vTaskDelay(pdMS_TO_TICKS(1500));  // 结果显示 1.5 秒
            }
        }
    } else if (bsp_lvgl_lock(100)) {
        if (s_status) lv_label_set_text(s_status, "Syncing...");
        bsp_lvgl_unlock();
    }

    zw_api_heartbeat();

    zw_stats_t stats = {0};
    if (zw_api_get_stats(&stats) == ESP_OK) {
        g_zw.stats = stats;
        g_zw.stats_valid = true;
    }

    int count = 0;
    // 只拉取待处理坐标列表（pending 点，凑领地用）：每 3 个不同点成领地后
    // 服务端将这三点置为 used，不再出现在列表中，实现"3 点一轮"自动清空
    if (zw_api_get_markers(g_zw.markers, 10, &count) == ESP_OK) {
        g_zw.marker_count = count;
    }

    if (bsp_lvgl_lock(100)) {
        refresh_labels();
        if (s_status) {
            lv_label_set_text(s_status, "FIELD READY");
            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_MUTED), 0);
        }
        bsp_lvgl_unlock();
    }

    s_sync_running = false;
    vTaskDelete(NULL);
}

void ui_team_mgmt_enter(void) {
    ESP_LOGI(TAG, "Enter team mgmt");

    s_scroll_y = 0;

    uint32_t accent = g_zw.config.team == ZW_TEAM_RED ? ZW_C_RED : ZW_C_BLUE;
    s_scr = zw_theme_screen(accent);
    zw_theme_header(s_scr, "ZONEWAR  /  FIELD", "COMMAND", accent);
    zw_theme_footer(s_scr, "OK  MARK LOCATION    LONG UP  MENU");

    // 固定顶栏：设备编码 + 阵营
    s_dev_label = lv_label_create(s_scr);
    char buf[48];
    snprintf(buf, sizeof(buf), "DEV: %s", g_zw.config.device_id);
    lv_label_set_text(s_dev_label, buf);
    lv_obj_set_style_text_font(s_dev_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_dev_label, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_pos(s_dev_label, 14, 61);

    s_title_team = lv_label_create(s_scr);
    lv_label_set_text(s_title_team, "NONE");
    lv_obj_set_style_text_font(s_title_team, &lv_font_montserrat_14, 0);
    lv_obj_align(s_title_team, LV_ALIGN_TOP_RIGHT, -14, 35);

    // 滚动内容区（顶栏下方、底栏上方）
    s_content = lv_obj_create(s_scr);
    lv_obj_set_size(s_content, 216, 177);
    lv_obj_set_pos(s_content, 12, 82);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(ZW_C_BG), 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_content, 2, 0);
    lv_obj_set_style_pad_row(s_content, 8, 0);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_elastic(s_content, false);

    // 我的领地
    s_my_label = lv_label_create(s_content);
    lv_obj_set_width(s_my_label, 208);
    zw_theme_panel(s_my_label);
    lv_obj_set_style_pad_all(s_my_label, 12, 0);
    lv_obj_set_style_text_font(s_my_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_my_label, lv_color_hex(ZW_C_TEXT), 0);

    // 阵营统计
    s_team_label = lv_label_create(s_content);
    lv_obj_set_width(s_team_label, 208);
    zw_theme_panel(s_team_label);
    lv_obj_set_style_pad_all(s_team_label, 12, 0);
    lv_obj_set_style_text_font(s_team_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_team_label, lv_color_hex(ZW_C_TEXT), 0);

    // 红蓝对比
    lv_obj_t *vs_title = lv_label_create(s_content);
    lv_label_set_text(vs_title, "BATTLEFIELD CONTROL");
    lv_obj_set_style_text_font(vs_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(vs_title, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_style_pad_top(vs_title, 3, 0);

    // 对比条（背景蓝，前景红）
    lv_obj_t *bar_bg = lv_obj_create(s_content);
    lv_obj_set_size(bar_bg, 208, 16);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(ZW_C_SURFACE), 0);
    lv_obj_set_style_border_width(bar_bg, 1, 0);
    lv_obj_set_style_border_color(bar_bg, lv_color_hex(ZW_C_LINE), 0);
    lv_obj_set_style_radius(bar_bg, 8, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);

    s_bar_blue = lv_obj_create(bar_bg);
    lv_obj_set_size(s_bar_blue, 206, 14);
    lv_obj_align(s_bar_blue, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar_blue, lv_color_hex(ZW_C_BLUE), 0);
    lv_obj_set_style_border_width(s_bar_blue, 0, 0);
    lv_obj_set_style_radius(s_bar_blue, 0, 0);

    s_bar_red = lv_obj_create(bar_bg);
    lv_obj_set_size(s_bar_red, 103, 14);
    lv_obj_align(s_bar_red, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar_red, lv_color_hex(ZW_C_RED), 0);
    lv_obj_set_style_border_width(s_bar_red, 0, 0);
    lv_obj_set_style_radius(s_bar_red, 0, 0);

    s_vs_label = lv_label_create(s_content);
    lv_obj_set_style_text_font(s_vs_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_vs_label, lv_color_hex(ZW_C_MUTED), 0);

    // 坐标列表
    s_coord_label = lv_label_create(s_content);
    lv_label_set_text(s_coord_label, "PENDING MARKERS");
    lv_obj_set_width(s_coord_label, 208);
    zw_theme_panel(s_coord_label);
    lv_obj_set_style_pad_all(s_coord_label, 12, 0);
    lv_obj_set_style_text_font(s_coord_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_coord_label, lv_color_hex(ZW_C_CYAN), 0);

    // 固定底栏：状态/按键提示
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "FIELD READY");
    lv_obj_set_width(s_status, 216);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_pos(s_status, 12, 266);

    // 初始填充已有数据
    refresh_labels();

    lv_screen_load(s_scr);

    // 进入页面立即触发一次同步（不发 MARK，仅拉取）
    s_sync_running = true;
    xTaskCreate(sync_now_task, "mgmt_sync", 8192, (void*)(uintptr_t)false, 5, NULL);
}

void ui_team_mgmt_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_content = NULL;
        s_title_team = NULL;
        s_dev_label = NULL;
        s_my_label = NULL;
        s_team_label = NULL;
        s_bar_red = NULL;
        s_bar_blue = NULL;
        s_vs_label = NULL;
        s_coord_label = NULL;
        s_status = NULL;
    }
}

// 后台同步完成后的 UI 刷新入口（供 zw_ui sync_task 调用）
void ui_team_mgmt_refresh(void) {
    refresh_labels();
}

void ui_team_mgmt_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        // 上下滚动查看长内容
        if (!s_content) return;
        if (btn == BSP_BTN_UP) s_scroll_y -= SCROLL_STEP;
        else s_scroll_y += SCROLL_STEP;
        if (s_scroll_y < 0) s_scroll_y = 0;
        lv_obj_scroll_to_y(s_content, -s_scroll_y, LV_ANIM_ON);
    } else if (btn == BSP_BTN_OK) {
        // OK：通过 BLE 通知手机立即打点上报，等待后拉取刷新坐标列表
        if (s_sync_running) return;  // 上一轮打点/同步未完成，忽略连按
        s_sync_running = true;
        xTaskCreate(sync_now_task, "mgmt_sync", 8192, (void*)(uintptr_t)true, 5, NULL);
    }
}
