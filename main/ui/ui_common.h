#pragma once
#include "app_model.h"
#include "lvgl.h"
#define KP_BG 0x0D1B2A
#define KP_BG_MID 0x0D1B2A
#define KP_CARD 0x17263B
#define KP_CARD_ALT 0x1A3A5C
#define KP_TEXT 0xF4F8FF
#define KP_MUTED 0x728198
#define KP_MUTED_LIGHT 0xA8B5C7
#define KP_YELLOW 0xFFD65A
#define KP_RED 0xF06B78
#define KP_GREEN 0x4ED3A0
#ifdef CONFIG_KIDS_THEME_SISTER
#define KP_THEME 0xF06AA8
#else
#define KP_THEME 0x4B9EFF
#endif
lv_obj_t *ui_common_screen(const char *title, const app_model_snapshot_t *model);
lv_obj_t *ui_statusbar_create(lv_obj_t *parent, const app_model_snapshot_t *model);
/** Keep status text/icons above an overlapping Home avatar without painting an opaque band. */
void ui_statusbar_home_foreground(lv_obj_t *bar);
void ui_statusbar_update(const app_model_snapshot_t *model);
lv_obj_t *ui_common_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected, bool enabled);
void ui_common_card_set_selected(lv_obj_t *card, bool selected);
lv_obj_t *ui_common_label(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align, bool title);
lv_obj_t *ui_common_label_small(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align);
void ui_common_footer(lv_obj_t *screen, const char *text, bool loading);
void ui_common_message(lv_obj_t *screen, const char *text, bool error);
/* Returns the overlay card so the caller can re-tint it for the ring flash
 * without rebuilding (and re-rasterising the CJK text of) the whole screen. */
lv_obj_t *ui_common_find_overlay(lv_obj_t *screen, bool bright);
void ui_common_find_overlay_tint(lv_obj_t *box, bool bright);
