/*
 * ZoneWar - 管理界面（Dashboard）
 */
#include "zw_ui.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"

static const char *TAG = "ui_dashboard";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_team_badge = NULL;
static lv_obj_t *s_online = NULL;
static lv_obj_t *s_territories = NULL;
static lv_obj_t *s_area = NULL;
static lv_obj_t *s_win_rate = NULL;
static lv_obj_t *s_wifi_status = NULL;
static lv_obj_t *s_sync_hint = NULL;

void ui_dashboard_refresh(void);

void ui_dashboard_enter(void) {
    ESP_LOGI(TAG, "Enter dashboard");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 顶部标题栏
    lv_obj_t *top_bar = lv_obj_create(s_scr);
    lv_obj_set_size(top_bar, 240, 42);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1a1a3a), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *title = lv_label_create(top_bar);
    lv_label_set_text(title, "ZONEWAR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Wi-Fi 状态（用 ASCII 符号代替 LV_SYMBOL）
    s_wifi_status = lv_label_create(top_bar);
    lv_label_set_text(s_wifi_status, "WiFi");
    lv_obj_set_style_text_font(s_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(0x4ade80), 0);
    lv_obj_align(s_wifi_status, LV_ALIGN_RIGHT_MID, -10, 0);
    
    // 战队徽章（大字号，醒目）
    s_team_badge = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_team_badge, &lv_font_montserrat_20, 0);
    lv_obj_align(s_team_badge, LV_ALIGN_TOP_MID, 0, 52);
    
    // 数据卡片区域 - 缩小高度适配屏幕
    lv_obj_t *card = lv_obj_create(s_scr);
    lv_obj_set_size(card, 220, 150);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141428), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 82);
    
    // 在线人数
    lv_obj_t *online_label = lv_label_create(card);
    lv_label_set_text(online_label, "Online");
    lv_obj_set_style_text_font(online_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(online_label, lv_color_hex(0x888888), 0);
    lv_obj_align(online_label, LV_ALIGN_TOP_LEFT, 12, 8);
    
    s_online = lv_label_create(card);
    lv_label_set_text(s_online, "--/--");
    lv_obj_set_style_text_font(s_online, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_online, lv_color_hex(0x4ecdc4), 0);
    lv_obj_align(s_online, LV_ALIGN_TOP_LEFT, 12, 24);
    
    // 领地数
    lv_obj_t *terr_label = lv_label_create(card);
    lv_label_set_text(terr_label, "Territories");
    lv_obj_set_style_text_font(terr_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(terr_label, lv_color_hex(0x888888), 0);
    lv_obj_align(terr_label, LV_ALIGN_TOP_RIGHT, -12, 8);
    
    s_territories = lv_label_create(card);
    lv_label_set_text(s_territories, "--");
    lv_obj_set_style_text_font(s_territories, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_territories, lv_color_hex(0xffd93d), 0);
    lv_obj_align(s_territories, LV_ALIGN_TOP_RIGHT, -12, 24);
    
    // 分割线
    lv_obj_t *line1 = lv_obj_create(card);
    lv_obj_set_size(line1, 196, 1);
    lv_obj_set_style_bg_color(line1, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_border_width(line1, 0, 0);
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 55);
    
    // 占领面积
    lv_obj_t *area_label = lv_label_create(card);
    lv_label_set_text(area_label, "Area");
    lv_obj_set_style_text_font(area_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(area_label, lv_color_hex(0x888888), 0);
    lv_obj_align(area_label, LV_ALIGN_TOP_LEFT, 12, 65);
    
    s_area = lv_label_create(card);
    lv_label_set_text(s_area, "--");
    lv_obj_set_style_text_font(s_area, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_area, lv_color_hex(0xa78bfa), 0);
    lv_obj_align(s_area, LV_ALIGN_TOP_LEFT, 12, 82);
    
    // 分割线
    lv_obj_t *line2 = lv_obj_create(card);
    lv_obj_set_size(line2, 196, 1);
    lv_obj_set_style_bg_color(line2, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_border_width(line2, 0, 0);
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 115);
    
    // 战队胜率
    lv_obj_t *wr_label = lv_label_create(card);
    lv_label_set_text(wr_label, "Win Rate");
    lv_obj_set_style_text_font(wr_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wr_label, lv_color_hex(0x888888), 0);
    lv_obj_align(wr_label, LV_ALIGN_TOP_LEFT, 12, 125);
    
    s_win_rate = lv_label_create(card);
    lv_label_set_text(s_win_rate, "--%");
    lv_obj_set_style_text_font(s_win_rate, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0x4ade80), 0);
    lv_obj_align(s_win_rate, LV_ALIGN_TOP_LEFT, 12, 142);
    
    // 底部提示
    s_sync_hint = lv_label_create(s_scr);
    lv_label_set_text(s_sync_hint, "OK: Capture  Up/Down: Refresh");
    lv_obj_set_style_text_font(s_sync_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_sync_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_sync_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // 刷新显示
    ui_dashboard_refresh();
    
    lv_screen_load(s_scr);
    
    // 请求一次同步
    if (zw_wifi_is_connected()) {
        zw_ui_request_sync();
    }
}

void ui_dashboard_refresh(void) {
    if (!s_scr) return;
    
    // 设备ID
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "ID: %s", g_zw.config.device_id);
    lv_label_set_text(s_device_id, id_buf);
    
    // 战队徽章
    if (g_zw.config.team == 1) {
        lv_label_set_text(s_team_badge, "RED TEAM");
        lv_obj_set_style_text_color(s_team_badge, lv_color_hex(0xff6b6b), 0);
    } else if (g_zw.config.team == 2) {
        lv_label_set_text(s_team_badge, "BLUE TEAM");
        lv_obj_set_style_text_color(s_team_badge, lv_color_hex(0x4ecdc4), 0);
    }
    
    // Wi-Fi status
    if (zw_wifi_is_connected()) {
        lv_label_set_text(s_wifi_status, "WiFi");
        lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(0x4ade80), 0);
    } else {
        lv_label_set_text(s_wifi_status, "Off");
        lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(0xff6b6b), 0);
    }
    
    // 统计数据
    if (g_zw.stats_valid) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d / %d", g_zw.stats.team_online, g_zw.stats.team_total);
        lv_label_set_text(s_online, buf);
        
        snprintf(buf, sizeof(buf), "%d", g_zw.stats.territory_count);
        lv_label_set_text(s_territories, buf);
        
        zw_format_area(g_zw.stats.total_area, buf, sizeof(buf));
        lv_label_set_text(s_area, buf);
        
        snprintf(buf, sizeof(buf), "%.1f%%", g_zw.stats.win_rate);
        lv_label_set_text(s_win_rate, buf);
        
        // 根据胜率改颜色
        if (g_zw.stats.win_rate >= 50) {
            lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0x4ade80), 0);
        } else {
            lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0xff6b6b), 0);
        }
        
        lv_label_set_text(s_sync_hint, "OK: Capture  Up/Down: Refresh");
    } else {
        lv_label_set_text(s_sync_hint, "Loading data...");
    }
}

void ui_dashboard_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_team_badge = NULL;
        s_online = NULL;
        s_territories = NULL;
        s_area = NULL;
        s_win_rate = NULL;
        s_wifi_status = NULL;
        s_sync_hint = NULL;
    }
}

void ui_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_OK) {
        // 进入占领页面
        zw_ui_switch_page(ZW_PAGE_CAPTURE);
    } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        // 手动刷新
        if (zw_wifi_is_connected() && !g_zw.sync_in_progress) {
            lv_label_set_text(s_sync_hint, "Syncing...");
            zw_ui_request_sync();
        }
    }
}
