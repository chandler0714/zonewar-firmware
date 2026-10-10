/*
 * ZoneWar - 已标记点列表页面
 */
#include "zw_ui.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "zw_wifi.h"

static const char *TAG = "ui_marker_list";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_items[5] = {0};
static lv_obj_t *s_labels[5] = {0};
static lv_obj_t *s_hint = NULL;
static int s_selected = 0;
static int s_count = 0;
static bool s_deleting = false;

static void refresh_list(void) {
    s_count = g_zw.marker_count;
    if (s_count > 5) s_count = 5;
    
    for (int i = 0; i < 5; i++) {
        if (i < s_count) {
            char buf[64];
            snprintf(buf, sizeof(buf), "#%d  %.3f, %.3f",
                     g_zw.markers[i].id,
                     g_zw.markers[i].lat,
                     g_zw.markers[i].lng);
            lv_label_set_text(s_labels[i], buf);
            lv_obj_clear_flag(s_items[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_items[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    if (s_selected >= s_count && s_count > 0) {
        s_selected = s_count - 1;
    }
    if (s_count == 0) s_selected = 0;
    
    // 更新选中状态
    for (int i = 0; i < 5; i++) {
        if (i == s_selected && i < s_count) {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x2a2a5a), 0);
            lv_obj_set_style_border_width(s_items[i], 2, 0);
            lv_obj_set_style_border_color(s_items[i], lv_color_hex(0xff6b6b), 0);
        } else if (i < s_count) {
            lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x1a1a3a), 0);
            lv_obj_set_style_border_width(s_items[i], 1, 0);
            lv_obj_set_style_border_color(s_items[i], lv_color_hex(0x2a2a4a), 0);
        }
    }
}

static void delete_marker_task(void *arg) {
    int idx = (int)arg;
    int marker_id = g_zw.markers[idx].id;
    
    esp_err_t err = zw_api_delete_marker(marker_id);
    
    if (bsp_lvgl_lock(200)) {
        if (err == ESP_OK) {
            // 从列表中移除
            for (int i = idx; i < g_zw.marker_count - 1; i++) {
                g_zw.markers[i] = g_zw.markers[i + 1];
            }
            g_zw.marker_count--;
            refresh_list();
            lv_label_set_text(s_hint, LV_SYMBOL_OK " Deleted   ↑↓ Select   OK Delete");
        } else {
            lv_label_set_text(s_hint, LV_SYMBOL_CLOSE " Delete failed");
        }
        s_deleting = false;
        bsp_lvgl_unlock();
    }
    
    vTaskDelete(NULL);
}

void ui_marker_list_enter(void) {
    ESP_LOGI(TAG, "Enter marker list");
    
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0a0a1a), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Marker List");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);
    
    // 数量提示
    lv_obj_t *count_label = lv_label_create(s_scr);
    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %d (3 forms territory)", g_zw.marker_count);
    lv_label_set_text(count_label, buf);
    lv_obj_set_style_text_font(count_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(count_label, lv_color_hex(0x888888), 0);
    lv_obj_align(count_label, LV_ALIGN_TOP_MID, 0, 55);
    
    // 列表项
    for (int i = 0; i < 5; i++) {
        s_items[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_items[i], 210, 42);
        lv_obj_set_style_bg_color(s_items[i], lv_color_hex(0x1a1a3a), 0);
        lv_obj_set_style_border_width(s_items[i], 1, 0);
        lv_obj_set_style_border_color(s_items[i], lv_color_hex(0x2a2a4a), 0);
        lv_obj_set_style_radius(s_items[i], 8, 0);
        lv_obj_align(s_items[i], LV_ALIGN_TOP_MID, 0, 78 + i * 48);
        
        s_labels[i] = lv_label_create(s_items[i]);
        lv_obj_set_style_text_font(s_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_labels[i], lv_color_hex(0xffffff), 0);
        lv_obj_align(s_labels[i], LV_ALIGN_LEFT_MID, 10, 0);
    }
    
    // 底部提示
    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "↑↓ Select   OK Delete   Long-press Back");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    s_selected = 0;
    s_deleting = false;
    refresh_list();
    
    lv_screen_load(s_scr);
}

void ui_marker_list_exit(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        for (int i = 0; i < 5; i++) {
            s_items[i] = NULL;
            s_labels[i] = NULL;
        }
        s_hint = NULL;
    }
}

void ui_marker_list_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (s_deleting) return;
    
    if (btn == BSP_BTN_UP) {
        if (s_selected > 0) {
            s_selected--;
            refresh_list();
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (s_selected < s_count - 1) {
            s_selected++;
            refresh_list();
        }
    } else if (btn == BSP_BTN_OK) {
        if (s_count > 0) {
            s_deleting = true;
            lv_label_set_text(s_hint, "Deleting...");
            xTaskCreate(delete_marker_task, "del_marker", 4096, (void*)s_selected, 5, NULL);
        }
    }
}
