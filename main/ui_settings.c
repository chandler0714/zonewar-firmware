/*
 * ZoneWar - 设置页（SETTINGS）
 * 长按上键进入。支持：蓝牙重新配对 / Wi-Fi 重新选择 / 退出阵营
 * 上下键选择，OK 执行；长按OK返回阵营管理页（通用规则在 zw_ui_dispatch_key）
 */
#include "zw_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_theme.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_settings";

#define ITEM_COUNT 3

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_items[ITEM_COUNT];
static const char *s_titles[ITEM_COUNT] = {
    "01   BLE CONNECTION", "02   WI-FI NETWORK", "03   EXIT FACTION"
};
static int s_sel = 0;

static void update_highlight(void) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (i == s_sel) {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(ZW_C_RAISED), 0);
            lv_obj_set_style_border_color(s_items[i],
                lv_color_hex(i == 2 ? ZW_C_RED : ZW_C_CYAN), 0);
            lv_obj_set_style_text_color(s_items[i], lv_color_hex(ZW_C_TEXT), 0);
        } else {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(ZW_C_SURFACE), 0);
            lv_obj_set_style_border_color(s_items[i], lv_color_hex(ZW_C_LINE), 0);
            lv_obj_set_style_text_color(s_items[i], lv_color_hex(ZW_C_MUTED), 0);
        }
    }
}

void ui_settings_enter(void) {
    ESP_LOGI(TAG, "Enter settings");

    s_sel = 0;

    s_scr = zw_theme_screen(ZW_C_CYAN);
    zw_theme_header(s_scr, "ZONEWAR  /  SYSTEM", "SETTINGS", ZW_C_CYAN);
    zw_theme_footer(s_scr, "UP/DOWN  MOVE      OK  OPEN");

    lv_obj_t *sub = lv_label_create(s_scr);
    lv_label_set_text(sub, "CONNECTION & FACTION");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_pos(sub, 14, 68);

    // 选项列表
    for (int i = 0; i < ITEM_COUNT; i++) {
        s_items[i] = lv_label_create(s_scr);
        lv_label_set_text(s_items[i], s_titles[i]);
        lv_obj_set_size(s_items[i], 212, 48);
        lv_obj_set_style_text_font(s_items[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_left(s_items[i], 12, 0);
        lv_obj_set_style_pad_top(s_items[i], 15, 0);
        zw_theme_panel(s_items[i]);
        lv_obj_set_pos(s_items[i], 14, 95 + i * 57);
    }

    // 提示
    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "LONG OK  RETURN TO COMMAND");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 269);

    update_highlight();
    lv_screen_load(s_scr);
}

void ui_settings_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        for (int i = 0; i < ITEM_COUNT; i++) s_items[i] = NULL;
    }
}

void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (btn == BSP_BTN_UP) {
            s_sel = (s_sel - 1 + ITEM_COUNT) % ITEM_COUNT;
        } else {
            s_sel = (s_sel + 1) % ITEM_COUNT;
        }
        update_highlight();
    } else if (btn == BSP_BTN_OK) {
        switch (s_sel) {
        case 0:  // 蓝牙重新配对
            zw_ui_switch_page(ZW_PAGE_BLE_CONFIG);
            break;
        case 1:  // Wi-Fi 重新选择（必须手机蓝牙在线才能输入新密码）
            // BLE 未连接时无法配置密码：先进入蓝牙配网页等手机配对，
            // 配对成功后该页会自动跳转到 Wi-Fi 列表，不强制离线设备做无效操作
            if (zw_ble_is_connected()) {
                zw_ui_switch_page(ZW_PAGE_WIFI_SELECT);
            } else {
                zw_ui_switch_page(ZW_PAGE_BLE_CONFIG);
            }
            break;
        case 2:  // 退出阵营（二次确认）
            zw_ui_switch_page(ZW_PAGE_EXIT_CONFIRM);
            break;
        }
    }
}
