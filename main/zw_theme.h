/* ZoneWar's compact 240x320 tactical UI primitives. */
#pragma once

#include "lvgl.h"

#define ZW_C_BG       0x091321
#define ZW_C_SURFACE  0x142338
#define ZW_C_RAISED   0x1B3049
#define ZW_C_LINE     0x2A4561
#define ZW_C_TEXT     0xF3F7FF
#define ZW_C_MUTED    0x91A8C0
#define ZW_C_CYAN     0x54D9C5
#define ZW_C_RED      0xFF6475
#define ZW_C_BLUE     0x6A9CFF
#define ZW_C_AMBER    0xF4C96B

static inline lv_obj_t *zw_theme_screen(uint32_t accent)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(ZW_C_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);

    lv_obj_t *rail = lv_obj_create(screen);
    lv_obj_set_size(rail, 240, 3);
    lv_obj_set_pos(rail, 0, 0);
    lv_obj_set_style_bg_color(rail, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_radius(rail, 0, 0);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_set_scrollable(rail, false);
    return screen;
}

static inline void zw_theme_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(ZW_C_SURFACE), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ZW_C_LINE), 0);
    lv_obj_set_style_radius(obj, 12, 0);
}

static inline void zw_theme_header(lv_obj_t *screen, const char *section,
                                   const char *title, uint32_t accent)
{
    lv_obj_t *eyebrow = lv_label_create(screen);
    lv_label_set_text(eyebrow, section);
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(eyebrow, lv_color_hex(accent), 0);
    lv_obj_set_pos(eyebrow, 14, 12);

    lv_obj_t *heading = lv_label_create(screen);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(ZW_C_TEXT), 0);
    lv_obj_set_pos(heading, 14, 32);
}

static inline void zw_theme_footer(lv_obj_t *screen, const char *hint)
{
    lv_obj_t *rule = lv_obj_create(screen);
    lv_obj_set_size(rule, 216, 1);
    lv_obj_set_pos(rule, 12, 284);
    lv_obj_set_style_bg_color(rule, lv_color_hex(ZW_C_LINE), 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_set_scrollable(rule, false);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, hint);
    lv_obj_set_width(label, 216);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ZW_C_MUTED), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 12, 294);
}
