#include "ui_common.h"
#include "ui_fonts.h"
#include <stdio.h>

lv_obj_t *ui_common_label(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align, bool title)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y); lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, title ? UI_FONT_TITLE : UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(KP_TEXT), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text ? text : "");
    return label;
}
lv_obj_t *ui_common_screen(const char *title, const app_model_snapshot_t *model)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(KP_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    ui_common_label(screen, title, 8, 3, 150, LV_TEXT_ALIGN_LEFT, true);
    lv_obj_t *status = ui_common_label(screen, model->mqtt_online ? (app_model_is_synced() ? "在线" : "同步中") : "离线", 164, 7, 68, LV_TEXT_ALIGN_RIGHT, false);
    lv_obj_set_style_text_color(status, lv_color_hex(model->mqtt_online ? KP_GREEN : KP_MUTED), 0);
    lv_obj_t *line = lv_obj_create(screen); lv_obj_set_pos(line, 0, 31); lv_obj_set_size(line, 240, 2); lv_obj_set_style_bg_color(line, lv_color_hex(KP_THEME), 0); lv_obj_set_style_border_width(line, 0, 0); lv_obj_set_style_radius(line, 0, 0);
    return screen;
}
lv_obj_t *ui_common_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected, bool enabled)
{
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h); lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 8, 0); lv_obj_set_style_pad_all(obj, 4, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(selected ? KP_YELLOW : KP_CARD), 0);
    lv_obj_set_style_bg_opa(obj, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_border_width(obj, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(selected ? 0xFFFFFF : KP_THEME), 0);
    return obj;
}
void ui_common_footer(lv_obj_t *screen, const char *text, bool loading)
{
    lv_obj_t *bar = lv_obj_create(screen); lv_obj_set_pos(bar, 0, 288); lv_obj_set_size(bar, 240, 32); lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_radius(bar, 0, 0); lv_obj_set_style_border_width(bar, 0, 0); lv_obj_set_style_pad_all(bar, 4, 0); lv_obj_set_style_bg_color(bar, lv_color_hex(0x182235), 0);
    lv_obj_t *label = ui_common_label(bar, loading ? "请稍候..." : text, 0, 1, 232, LV_TEXT_ALIGN_CENTER, false); (void)label;
}
void ui_common_message(lv_obj_t *screen, const char *text, bool error)
{
    lv_obj_t *box = ui_common_card(screen, 16, 238, 208, 42, false, true);
    lv_obj_set_style_bg_color(box, lv_color_hex(error ? KP_RED : KP_GREEN), 0);
    ui_common_label(box, text, 3, 5, 194, LV_TEXT_ALIGN_CENTER, false);
}
