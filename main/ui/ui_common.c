#include "ui_common.h"
#include "battery_service.h"
#include "ui_fonts.h"
#include "ui_pixel_icons.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_wifi_icon;
static lv_obj_t *s_battery_text;

static void base_obj(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}
lv_obj_t *ui_common_label(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align, bool title)
{
    lv_obj_t *label = lv_label_create(parent); lv_obj_set_pos(label, x, y); lv_obj_set_width(label, w);
    const lv_font_t *font = title ? UI_FONT_TITLE : UI_FONT_BODY;
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(KP_TEXT), 0); lv_obj_set_style_text_align(label, align, 0);
    char limited[128];
    ui_text_limit_lines(text, limited, sizeof(limited), UI_TEXT_STANDARD_MAX_CHARS);
    ui_text_font_fallback(font, limited);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); lv_label_set_text(label, limited); return label;
}
lv_obj_t *ui_common_label_small(lv_obj_t *parent, const char *text, int x, int y, int w, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent); lv_obj_set_pos(label, x, y); lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(KP_TEXT), 0); lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    char safe[256]; strlcpy(safe, text ? text : "", sizeof(safe)); ui_text_font_fallback(UI_FONT_SMALL, safe);
    lv_label_set_text(label, safe); return label;
}
void ui_statusbar_update(const app_model_snapshot_t *model)
{
    if (!model || !s_wifi_icon || !s_battery_text) return;
    lv_obj_set_style_image_recolor(s_wifi_icon, lv_color_hex(model->wifi_online ? KP_GREEN : KP_MUTED), 0);
    battery_snapshot_t battery = {0};
    battery_service_snapshot(&battery);
    char text[8];
    snprintf(text, sizeof(text), battery.valid ? "%d%%" : "--%%", battery.percent);
    lv_label_set_text(s_battery_text, text);
    lv_obj_set_style_text_color(s_battery_text, lv_color_hex(battery.valid && battery.percent <= 15 ? KP_RED : KP_MUTED_LIGHT), 0);
}
lv_obj_t *ui_statusbar_create(lv_obj_t *parent, const app_model_snapshot_t *model)
{
    lv_obj_t *bar = lv_obj_create(parent); base_obj(bar); lv_obj_set_pos(bar, 0, 0); lv_obj_set_size(bar, 240, 20);
    lv_obj_set_style_bg_color(bar, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    s_wifi_icon = ui_pixel_icon_create(bar, UI_PIXEL_ICON_WIFI, 160, 4, KP_MUTED, 1);
    ui_pixel_icon_create(bar, UI_PIXEL_ICON_BATTERY, 178, 4, KP_MUTED_LIGHT, 1);
    s_battery_text = ui_common_label_small(bar, "--%", 194, 1, 42, LV_TEXT_ALIGN_RIGHT);
    ui_statusbar_update(model);
    return bar;
}
void ui_statusbar_home_foreground(lv_obj_t *bar)
{
    if (!bar) return;
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_move_foreground(bar);
}
lv_obj_t *ui_common_screen(const char *title, const app_model_snapshot_t *model)
{
    lv_obj_t *screen = lv_obj_create(NULL); base_obj(screen); lv_obj_set_style_bg_color(screen, lv_color_hex(KP_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *bar = ui_statusbar_create(screen, model);
    (void)bar;
    /* Keep sub-page headers compact: title-only content on the left, status on the right. */
    lv_obj_t *title_label = ui_common_label(screen, title, 8, 1, 144, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_height(title_label, 19);
    lv_obj_set_style_text_color(title_label, lv_color_hex(KP_TEXT), 0);
    return screen;
}
void ui_common_card_set_selected(lv_obj_t *card, bool selected)
{
    if (!card) return;
    lv_obj_set_style_bg_color(card, lv_color_hex(selected ? KP_CARD_ALT : KP_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3C55), 0);
    lv_obj_t *bar = lv_obj_get_child(card, 0);
    if (bar) {
        if (selected) lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_common_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected, bool enabled)
{
    lv_obj_t *obj = lv_obj_create(parent); base_obj(obj); lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 10, 0); lv_obj_set_style_pad_all(obj, 4, 0);
    lv_obj_set_style_bg_opa(obj, enabled ? LV_OPA_COVER : LV_OPA_60, 0);
    /* The first child is always the selection marker, allowing animated cards to move it safely. */
    lv_obj_t *bar = lv_obj_create(obj);
    base_obj(bar);
    lv_obj_set_size(bar, 3, (h * 60) / 100);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x4488FF), 0);
    ui_common_card_set_selected(obj, selected);
    return obj;
}
void ui_common_footer(lv_obj_t *screen, const char *text, bool loading)
{
    lv_obj_t *bar = lv_obj_create(screen); base_obj(bar); lv_obj_set_pos(bar, 0, 288); lv_obj_set_size(bar, 240, 32);
    lv_obj_set_style_bg_color(bar, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_t *label = lv_label_create(bar);
    lv_obj_set_width(label, 236); lv_obj_set_height(label, 18);
    lv_obj_set_style_text_font(label, UI_FONT_SMALL, 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(loading ? KP_YELLOW : KP_MUTED), 0);
    lv_obj_set_style_pad_top(label, 0, 0); lv_obj_set_style_pad_bottom(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); lv_label_set_text(label, loading ? "请稍候..." : text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -1);
}
void ui_common_message(lv_obj_t *screen, const char *text, bool error)
{
    lv_obj_t *box = ui_common_card(screen, 12, 238, 216, 42, false, true);
    lv_obj_set_style_bg_color(box, lv_color_hex(error ? 0x592A35 : 0x183D37), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(error ? KP_RED : KP_GREEN), 0);
    lv_obj_t *label = lv_label_create(box);
    lv_obj_set_width(label, 198); lv_obj_set_height(label, 34);
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(KP_TEXT), 0);
    lv_obj_set_style_pad_top(label, 0, 0); lv_obj_set_style_pad_bottom(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -1);
}
void ui_common_find_overlay(lv_obj_t *screen, bool bright)
{
    lv_obj_t *box = ui_common_card(screen, 12, 84, 216, 132, false, true);
    lv_obj_set_style_bg_color(box, lv_color_hex(bright ? 0x713C18 : 0x402511), 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(KP_YELLOW), 0);
    ui_radar_icon_create(box, 84, 10, KP_YELLOW, 3);
    ui_common_label(box, "伙伴正在找你！", 8, 56, 192, LV_TEXT_ALIGN_CENTER, true);
    ui_common_label(box, "按任意键停止\n并回应", 8, 82, 192, LV_TEXT_ALIGN_CENTER, false);
}
