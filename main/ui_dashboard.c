/*
 * ZoneWar - 管理界面（Dashboard）
 * 一屏完整显示，无需滚动
 */
#include "zw_ui.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"

static const char *TAG = "ui_dashboard";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_team_label = NULL;
static lv_obj_t *s_online = NULL;
static lv_obj_t *s_territories = NULL;
static lv_obj_t *s_area = NULL;
static lv_obj_t *s_win_rate = NULL;
static lv_obj_t *s_win_rate_bar = NULL;
static lv_obj_t *s_wifi_status = NULL;
static lv_obj_t *s_hint = NULL;

void ui_dashboard_refresh(void);

void ui_dashboard_enter(void) {
    ESP_LOGI(TAG, "Enter dashboard");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_scrollbar_mode(s_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    
    // ===== 顶部栏 =====
    lv_obj_t *top_bar = lv_obj_create(s_scr);
    lv_obj_set_size(top_bar, 240, 38);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1a1a3a), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    s_team_label = lv_label_create(top_bar);
    lv_label_set_text(s_team_label, "ZONEWAR");
    lv_obj_set_style_text_font(s_team_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_team_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_team_label, LV_ALIGN_LEFT_MID, 12, 0);
    
    s_wifi_status = lv_label_create(top_bar);
    lv_label_set_text(s_wifi_status, "WiFi");
    lv_obj_set_style_text_font(s_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(0x4ade80), 0);
    lv_obj_align(s_wifi_status, LV_ALIGN_RIGHT_MID, -12, 0);
    
    // ===== 在线人数 =====
    s_online = lv_label_create(s_scr);
    lv_label_set_text(s_online, "Online: -- / --");
    lv_obj_set_style_text_font(s_online, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_online, lv_color_hex(0x888888), 0);
    lv_obj_align(s_online, LV_ALIGN_TOP_MID, 0, 52);
    
    // ===== 数据卡片行：领地 + 面积 =====
    // 领地卡片
    lv_obj_t *terr_card = lv_obj_create(s_scr);
    lv_obj_set_size(terr_card, 110, 70);
    lv_obj_set_style_bg_color(terr_card, lv_color_hex(0x141428), 0);
    lv_obj_set_style_border_width(terr_card, 1, 0);
    lv_obj_set_style_border_color(terr_card, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_radius(terr_card, 10, 0);
    lv_obj_align(terr_card, LV_ALIGN_TOP_LEFT, 10, 78);
    lv_obj_clear_flag(terr_card, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *terr_label = lv_label_create(terr_card);
    lv_label_set_text(terr_label, "Territories");
    lv_obj_set_style_text_font(terr_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(terr_label, lv_color_hex(0x888888), 0);
    lv_obj_align(terr_label, LV_ALIGN_TOP_MID, 0, 6);
    
    s_territories = lv_label_create(terr_card);
    lv_label_set_text(s_territories, "--");
    lv_obj_set_style_text_font(s_territories, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_territories, lv_color_hex(0xffd93d), 0);
    lv_obj_align(s_territories, LV_ALIGN_TOP_MID, 0, 26);
    
    // 面积卡片
    lv_obj_t *area_card = lv_obj_create(s_scr);
    lv_obj_set_size(area_card, 110, 70);
    lv_obj_set_style_bg_color(area_card, lv_color_hex(0x141428), 0);
    lv_obj_set_style_border_width(area_card, 1, 0);
    lv_obj_set_style_border_color(area_card, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_radius(area_card, 10, 0);
    lv_obj_align(area_card, LV_ALIGN_TOP_RIGHT, -10, 78);
    lv_obj_clear_flag(area_card, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *area_label = lv_label_create(area_card);
    lv_label_set_text(area_label, "Area");
    lv_obj_set_style_text_font(area_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(area_label, lv_color_hex(0x888888), 0);
    lv_obj_align(area_label, LV_ALIGN_TOP_MID, 0, 6);
    
    s_area = lv_label_create(area_card);
    lv_label_set_text(s_area, "--");
    lv_obj_set_style_text_font(s_area, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_area, lv_color_hex(0xa78bfa), 0);
    lv_obj_align(s_area, LV_ALIGN_TOP_MID, 0, 28);
    
    // ===== 红蓝对比条 =====
    lv_obj_t *vs_container = lv_obj_create(s_scr);
    lv_obj_set_size(vs_container, 220, 50);
    lv_obj_set_style_bg_color(vs_container, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_border_width(vs_container, 0, 0);
    lv_obj_align(vs_container, LV_ALIGN_TOP_MID, 0, 158);
    lv_obj_clear_flag(vs_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // 红方数值
    lv_obj_t *red_val = lv_label_create(vs_container);
    lv_label_set_text(red_val, "RED --");
    lv_obj_set_style_text_font(red_val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(red_val, lv_color_hex(0xff6b6b), 0);
    lv_obj_align(red_val, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // 蓝方数值
    lv_obj_t *blue_val = lv_label_create(vs_container);
    lv_label_set_text(blue_val, "BLUE --");
    lv_obj_set_style_text_font(blue_val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(blue_val, lv_color_hex(0x4ecdc4), 0);
    lv_obj_align(blue_val, LV_ALIGN_TOP_RIGHT, 0, 0);
    
    // 进度条背景
    lv_obj_t *bar_bg = lv_obj_create(vs_container);
    lv_obj_set_size(bar_bg, 220, 14);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x1a1a3a), 0);
    lv_obj_set_style_radius(bar_bg, 7, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_align(bar_bg, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);
    
    // 红方进度（左半部分）
    s_win_rate_bar = lv_obj_create(bar_bg);
    lv_obj_set_size(s_win_rate_bar, 110, 14);
    lv_obj_set_style_bg_color(s_win_rate_bar, lv_color_hex(0xff6b6b), 0);
    lv_obj_set_style_radius(s_win_rate_bar, 7, 0);
    lv_obj_align(s_win_rate_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(s_win_rate_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // ===== 胜率 =====
    lv_obj_t *wr_label = lv_label_create(s_scr);
    lv_label_set_text(wr_label, "Team Win Rate");
    lv_obj_set_style_text_font(wr_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wr_label, lv_color_hex(0x888888), 0);
    lv_obj_align(wr_label, LV_ALIGN_TOP_MID, 0, 220);
    
    s_win_rate = lv_label_create(s_scr);
    lv_label_set_text(s_win_rate, "--%");
    lv_obj_set_style_text_font(s_win_rate, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0x4ade80), 0);
    lv_obj_align(s_win_rate, LV_ALIGN_TOP_MID, 0, 238);
    
    // ===== 底部提示 =====
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "OK: Capture   Up: Next");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x555555), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    
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
    
    // 战队名称
    if (g_zw.config.team == 1) {
        lv_label_set_text(s_team_label, "RED TEAM");
        lv_obj_set_style_text_color(s_team_label, lv_color_hex(0xff6b6b), 0);
    } else if (g_zw.config.team == 2) {
        lv_label_set_text(s_team_label, "BLUE TEAM");
        lv_obj_set_style_text_color(s_team_label, lv_color_hex(0x4ecdc4), 0);
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
        
        // 在线人数
        snprintf(buf, sizeof(buf), "Online: %d / %d", 
                g_zw.stats.team_online, g_zw.stats.team_total);
        lv_label_set_text(s_online, buf);
        lv_obj_set_style_text_color(s_online, lv_color_hex(0x4ecdc4), 0);
        
        // 领地数
        snprintf(buf, sizeof(buf), "%d", g_zw.stats.territory_count);
        lv_label_set_text(s_territories, buf);
        
        // 面积
        zw_format_area(g_zw.stats.total_area, buf, sizeof(buf));
        lv_label_set_text(s_area, buf);
        
        // 胜率
        snprintf(buf, sizeof(buf), "%.0f%%", g_zw.stats.win_rate);
        lv_label_set_text(s_win_rate, buf);
        
        if (g_zw.stats.win_rate >= 50) {
            lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0x4ade80), 0);
        } else {
            lv_obj_set_style_text_color(s_win_rate, lv_color_hex(0xff6b6b), 0);
        }
        
        // 红蓝对比条 - 红方占比
        float total = g_zw.stats.red_area + g_zw.stats.blue_area;
        if (total > 0) {
            int red_pct = (int)(g_zw.stats.red_area / total * 220);
            if (red_pct < 4) red_pct = 4; // 最小显示一点
            if (red_pct > 216) red_pct = 216;
            lv_obj_set_width(s_win_rate_bar, red_pct);
        }
        
        lv_label_set_text(s_hint, "OK: Capture   Up: Next");
    } else {
        lv_label_set_text(s_hint, "Loading...");
    }
}

void ui_dashboard_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_team_label = NULL;
        s_online = NULL;
        s_territories = NULL;
        s_area = NULL;
        s_win_rate = NULL;
        s_win_rate_bar = NULL;
        s_wifi_status = NULL;
        s_hint = NULL;
    }
}

void ui_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_OK || btn == BSP_BTN_UP) {
        // OK 和 上键 都进入下一页
        zw_ui_switch_page(ZW_PAGE_CAPTURE);
    } else if (btn == BSP_BTN_DOWN) {
        // 下键 - 手动刷新数据
        if (zw_wifi_is_connected() && !g_zw.sync_in_progress) {
            lv_label_set_text(s_hint, "Syncing...");
            zw_ui_request_sync();
        }
    }
}
