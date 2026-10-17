/*
 * ZoneWar - 战队选择页面
 */
#include "zw_ui.h"
#include "zw_api.h"
#include "zw_ble.h"
#include "zw_nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_theme.h"

static const char *TAG = "ui_team";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_red_panel = NULL;
static lv_obj_t *s_blue_panel = NULL;
static lv_obj_t *s_status = NULL;
static int s_selected = 0;  // 0=red, 1=blue
static bool s_confirming = false;

static void update_selection(void) {
    lv_obj_set_style_border_width(s_red_panel, s_selected == 0 ? 2 : 1, 0);
    lv_obj_set_style_border_color(s_red_panel,
        lv_color_hex(s_selected == 0 ? ZW_C_RED : ZW_C_LINE), 0);
    lv_obj_set_style_bg_color(s_red_panel,
        lv_color_hex(s_selected == 0 ? 0x482535 : ZW_C_SURFACE), 0);
    lv_obj_set_style_border_width(s_blue_panel, s_selected == 1 ? 2 : 1, 0);
    lv_obj_set_style_border_color(s_blue_panel,
        lv_color_hex(s_selected == 1 ? ZW_C_BLUE : ZW_C_LINE), 0);
    lv_obj_set_style_bg_color(s_blue_panel,
        lv_color_hex(s_selected == 1 ? 0x213A62 : ZW_C_SURFACE), 0);
}

static void join_team_task(void *arg) {
    int team = (int)arg;
    
    ESP_LOGI(TAG, "Joining team %d", team);
    
    // 调用服务器 API
    esp_err_t err = zw_api_join_team(team + 1);
    
    ESP_LOGI(TAG, "Join result: %s", err == ESP_OK ? "success" : "failed");
    
    if (bsp_lvgl_lock(200)) {
        if (err == ESP_OK) {
            // 保存到 NVS
            zw_nvs_save_team(team + 1);
            g_zw.config.team = team + 1;
            lv_label_set_text(s_status, "Joined!");
            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_CYAN), 0);
            
            // 更新 BLE 设备信息
            zw_ble_set_device_info(g_zw.config.device_id, team + 1);
            
            bsp_lvgl_unlock();
            
            // 延迟后跳转（在任务里延迟，不要在锁里延迟）
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            zw_ui_switch_page(ZW_PAGE_TEAM_MGMT);
        } else {
            lv_label_set_text(s_status, "Join failed, retry");
            lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_RED), 0);
            s_confirming = false;
            bsp_lvgl_unlock();
        }
    }
    
    vTaskDelete(NULL);
}

void ui_team_select_enter(void) {
    ESP_LOGI(TAG, "Enter team select");
    
    s_scr = zw_theme_screen(ZW_C_CYAN);
    zw_theme_header(s_scr, "ZONEWAR  /  01", "CHOOSE SIDE", ZW_C_CYAN);
    zw_theme_footer(s_scr, "UP/DOWN  SELECT      OK  JOIN");
    
    // 副标题
    lv_obj_t *sub = lv_label_create(s_scr);
    lv_label_set_text(sub, "Claim ground for your faction");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_pos(sub, 14, 66);
    
    // 红队面板
    s_red_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_red_panel, 100, 143);
    zw_theme_panel(s_red_panel);
    lv_obj_set_pos(s_red_panel, 14, 96);
    lv_obj_set_scrollable(s_red_panel, false);
    
    lv_obj_t *red_icon = lv_label_create(s_red_panel);
    lv_label_set_text(red_icon, "R");
    lv_obj_set_style_text_font(red_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(red_icon, lv_color_hex(ZW_C_RED), 0);
    lv_obj_align(red_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *red_text = lv_label_create(s_red_panel);
    lv_label_set_text(red_text, "RED");
    lv_obj_set_style_text_font(red_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(red_text, lv_color_hex(ZW_C_TEXT), 0);
    lv_obj_align(red_text, LV_ALIGN_BOTTOM_MID, 0, -19);
    
    // 蓝队面板
    s_blue_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_blue_panel, 100, 143);
    zw_theme_panel(s_blue_panel);
    lv_obj_set_pos(s_blue_panel, 126, 96);
    lv_obj_set_scrollable(s_blue_panel, false);
    
    lv_obj_t *blue_icon = lv_label_create(s_blue_panel);
    lv_label_set_text(blue_icon, "B");
    lv_obj_set_style_text_font(blue_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(blue_icon, lv_color_hex(ZW_C_BLUE), 0);
    lv_obj_align(blue_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *blue_text = lv_label_create(s_blue_panel);
    lv_label_set_text(blue_text, "BLUE");
    lv_obj_set_style_text_font(blue_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(blue_text, lv_color_hex(ZW_C_TEXT), 0);
    lv_obj_align(blue_text, LV_ALIGN_BOTTOM_MID, 0, -19);
    
    // 状态提示
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "SELECT A FACTION");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 252);
    
    s_selected = 0;
    s_confirming = false;
    update_selection();
    
    lv_screen_load(s_scr);
}

void ui_team_select_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_red_panel = NULL;
        s_blue_panel = NULL;
        s_status = NULL;
    }
}

void ui_team_select_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (s_confirming) return;
    
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        s_selected = (s_selected == 0) ? 1 : 0;
        update_selection();
    } else if (btn == BSP_BTN_OK) {
        s_confirming = true;
        lv_label_set_text(s_status, "Joining...");
        
        // 后台执行加入 - 8KB 栈（HTTP 请求需要较多栈空间）
        xTaskCreate(join_team_task, "join_team", 8192, (void*)s_selected, 5, NULL);
    }
}
