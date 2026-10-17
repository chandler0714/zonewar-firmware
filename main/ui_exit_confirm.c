/*
 * ZoneWar - 退出阵营二次确认页（EXIT_CONFIRM）
 * 确认后：服务器清零该用户全部领地与待定标记点，本地清空阵营，重新计数
 * 上下键选择 Confirm/Cancel，OK 执行；长按OK等效取消（通用规则）
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_nvs.h"
#include "zw_ble.h"
#include "zw_theme.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_exit_confirm";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_cancel = NULL;
static lv_obj_t *s_confirm = NULL;
static lv_obj_t *s_error = NULL;
static int s_sel = 0;  // 0=Cancel, 1=Confirm
static bool s_working = false;

static void update_highlight(void) {
    if (s_sel == 0) {
        lv_obj_set_style_bg_color(s_cancel, lv_color_hex(ZW_C_RAISED), 0);
        lv_obj_set_style_border_color(s_cancel, lv_color_hex(ZW_C_CYAN), 0);
        lv_obj_set_style_text_color(s_cancel, lv_color_hex(ZW_C_TEXT), 0);
        lv_obj_set_style_bg_color(s_confirm, lv_color_hex(ZW_C_SURFACE), 0);
        lv_obj_set_style_border_color(s_confirm, lv_color_hex(ZW_C_LINE), 0);
        lv_obj_set_style_text_color(s_confirm, lv_color_hex(ZW_C_RED), 0);
    } else {
        lv_obj_set_style_bg_color(s_confirm, lv_color_hex(0x482535), 0);
        lv_obj_set_style_border_color(s_confirm, lv_color_hex(ZW_C_RED), 0);
        lv_obj_set_style_text_color(s_confirm, lv_color_hex(ZW_C_TEXT), 0);
        lv_obj_set_style_bg_color(s_cancel, lv_color_hex(ZW_C_SURFACE), 0);
        lv_obj_set_style_border_color(s_cancel, lv_color_hex(ZW_C_LINE), 0);
        lv_obj_set_style_text_color(s_cancel, lv_color_hex(ZW_C_MUTED), 0);
    }
}

static void exit_team_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "Exiting team...");

    // 1. 服务器端清零该用户领地与待定标记点（触发全量重算）
    esp_err_t err = zw_api_exit_team();

    // 2. 本地清空阵营（无论服务器结果，避免卡死在确认页）
    zw_nvs_clear_team();
    g_zw.config.team = ZW_TEAM_NONE;
    zw_ble_set_device_info(g_zw.config.device_id, ZW_TEAM_NONE);
    g_zw.stats_valid = false;
    g_zw.marker_count = 0;

    if (bsp_lvgl_lock(200)) {
        if (err == ESP_OK) {
            zw_ui_switch_page(ZW_PAGE_TEAM_SELECT);
        } else {
            // 失败留在本页提示重试
            s_working = false;
            if (s_error) lv_label_set_text(s_error, "SERVER ERROR / RETRY");
        }
        bsp_lvgl_unlock();
    }

    vTaskDelete(NULL);
}

void ui_exit_confirm_enter(void) {
    ESP_LOGI(TAG, "Enter exit confirm");

    s_sel = 0;
    s_working = false;

    s_scr = zw_theme_screen(ZW_C_RED);
    zw_theme_header(s_scr, "ZONEWAR  /  DANGER", "EXIT FACTION?", ZW_C_RED);
    zw_theme_footer(s_scr, "UP/DOWN  MOVE      OK  CHOOSE");

    // 警告
    lv_obj_t *warn = lv_label_create(s_scr);
    lv_label_set_text(warn, "YOUR AREAS AND PENDING\nMARKERS WILL BE REMOVED");
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_14, 0);
    lv_obj_set_size(warn, 212, 73);
    zw_theme_panel(warn);
    lv_obj_set_style_pad_top(warn, 16, 0);
    lv_obj_set_style_text_color(warn, lv_color_hex(ZW_C_AMBER), 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(warn, 14, 83);

    // Cancel
    s_cancel = lv_label_create(s_scr);
    lv_label_set_text(s_cancel, "CANCEL");
    lv_obj_set_size(s_cancel, 212, 43);
    lv_obj_set_style_text_font(s_cancel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_cancel, LV_TEXT_ALIGN_CENTER, 0);
    zw_theme_panel(s_cancel);
    lv_obj_set_style_pad_top(s_cancel, 12, 0);
    lv_obj_set_pos(s_cancel, 14, 177);

    // Confirm
    s_confirm = lv_label_create(s_scr);
    lv_label_set_text(s_confirm, "CONFIRM EXIT");
    lv_obj_set_size(s_confirm, 212, 43);
    lv_obj_set_style_text_font(s_confirm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_confirm, LV_TEXT_ALIGN_CENTER, 0);
    zw_theme_panel(s_confirm);
    lv_obj_set_style_pad_top(s_confirm, 12, 0);
    lv_obj_set_pos(s_confirm, 14, 229);

    s_error = lv_label_create(s_scr);
    lv_label_set_text(s_error, "");
    lv_obj_set_style_text_font(s_error, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_error, lv_color_hex(ZW_C_RED), 0);
    lv_obj_align(s_error, LV_ALIGN_TOP_MID, 0, 272);

    update_highlight();
    lv_screen_load(s_scr);
}

void ui_exit_confirm_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_cancel = NULL;
        s_confirm = NULL;
        s_error = NULL;
    }
}

void ui_exit_confirm_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK || s_working) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        s_sel = (s_sel == 0) ? 1 : 0;
        update_highlight();
    } else if (btn == BSP_BTN_OK) {
        if (s_sel == 0) {
            // 取消 → 回阵营管理页
            zw_ui_switch_page(ZW_PAGE_TEAM_MGMT);
        } else {
            // 确认退出 → 后台执行
            s_working = true;
            xTaskCreate(exit_team_task, "exit_team", 8192, NULL, 5, NULL);
        }
    }
}
