#pragma once
#include "app_model.h"
#include "lvgl.h"
#define KP_BG 0x101827
#define KP_CARD 0x243247
#define KP_TEXT 0xF7FAFC
#define KP_MUTED 0x8A94A6
#define KP_YELLOW 0xFFD43B
#define KP_RED 0xE35D6A
#define KP_GREEN 0x45C486
#ifdef CONFIG_KIDS_THEME_SISTER
#define KP_THEME 0xF062A6
#else
#define KP_THEME 0x318CE7
#endif
lv_obj_t *ui_common_screen(const char *title, const app_model_snapshot_t *model);
lv_obj_t *ui_common_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected, bool enabled);
lv_obj_t *ui_common_label(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align, bool title);
void ui_common_footer(lv_obj_t *screen, const char *text, bool loading);
void ui_common_message(lv_obj_t *screen, const char *text, bool error);
