/*
 * ZoneWar - 占领页面
 */
#include "zw_ui.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui_capture";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_items[3] = {0};
static int s_selected = 0;

static const char *s_options[] = {
    "+ Add Marker",
    "<- Back to Dashboard",
    "= Marker List"
};

static void update_selection(void) {
    for (int i = 0; i < 3; i++) {
        if (i == s_selected) {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x2a2a5a), 0);
            lv_obj_set_style_border_width(s_items[i], 2, 0);
            lv_obj_set_style_border_color(s_items[i], lv_color_hex(0x667eea), 0);
        } else {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x1a1a3a), 0);
            lv_obj_set_style_border_width(s_items[i], 1, 0);
            lv_obj_set_style_border_color(s_items[i], lv_color_hex(0x2a2a4a), 0);
        }
    }
}

void ui_capture_enter(void) {
    ESP_LOGI(TAG, "Enter capture");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Capture Territory");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // 待标记点数提示
    char hint_buf[48];
    snprintf(hint_buf, sizeof(hint_buf), "Markers: %d / 3", g_zw.marker_count);
    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, hint_buf);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xffd93d), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 60);
    
    // 三个选项
    for (int i = 0; i < 3; i++) {
        s_items[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_items[i], 200, 55);
        lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x1a1a3a), 0);
        lv_obj_set_style_border_width(s_items[i], 1, 0);
        lv_obj_set_style_border_color(s_items[i], lv_color_hex(0x2a2a4a), 0);
        lv_obj_set_style_radius(s_items[i], 10, 0);
        lv_obj_align(s_items[i], LV_ALIGN_TOP_MID, 0, 90 + i * 65);
        
        lv_obj_t *label = lv_label_create(s_items[i]);
        lv_label_set_text(label, s_options[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_center(label);
    }
    
    // 底部提示
    lv_obj_t *bottom = lv_label_create(s_scr);
    lv_label_set_text(bottom, "Up/Down: Select   OK: Confirm");
    lv_obj_set_style_text_font(bottom, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bottom, lv_color_hex(0x666666), 0);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    s_selected = 0;
    update_selection();
    
    lv_screen_load(s_scr);
}

void ui_capture_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        for (int i = 0; i < 3; i++) s_items[i] = NULL;
    }
}

void ui_capture_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_UP) {
        if (s_selected > 0) {
            s_selected--;
            update_selection();
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (s_selected < 2) {
            s_selected++;
            update_selection();
        }
    } else if (btn == BSP_BTN_OK) {
        switch (s_selected) {
        case 0: // 新增标记点
            zw_ui_switch_page(ZW_PAGE_ADD_MARKER);
            break;
        case 1: // 返回管理页面
            zw_ui_switch_page(ZW_PAGE_DASHBOARD);
            break;
        case 2: // 已标记点坐标
            zw_ui_switch_page(ZW_PAGE_MARKER_LIST);
            break;
        }
    }
}
