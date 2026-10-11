#include "ui_notification.h"

#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_text.h"
#include <stdio.h>

#define NOTIFICATION_TEXT_CHILD 1
#define NOTIFICATION_COUNTER_CHILD 2

static void reset_object(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width,
                            int height, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, &lv_font_noto_sc_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF3F6FA), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t *ui_notification_create(lv_obj_t *parent)
{
    lv_obj_t *overlay = lv_obj_create(parent);
    reset_object(overlay);
    lv_obj_set_size(overlay, 240, 320);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x08101D), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(overlay);
    reset_object(card);
    lv_obj_set_pos(card, 10, 54);
    lv_obj_set_size(card, 220, 212);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x172A43), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x52D3B0), 0);

    lv_obj_t *title = make_label(card, 12, 10, 196, 24, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(title, "新通知");
    lv_obj_t *text = make_label(card, 14, 44, 192, 104, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_line_space(text, 3, 0);
    lv_obj_t *counter = make_label(card, 14, 151, 192, 22, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(counter, lv_color_hex(0xA9B9CA), 0);
    lv_obj_t *hint = make_label(card, 8, 179, 204, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(KP_MUTED), 0);
    lv_label_set_text(hint, "B1已读  B2下一条  B3关闭");

    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    return overlay;
}

void ui_notification_show(lv_obj_t *overlay, const notification_item_t *item,
                          size_t position, size_t count)
{
    if (!overlay || !item || count == 0) return;
    lv_obj_t *card = lv_obj_get_child(overlay, 0);
    lv_obj_t *text = card ? lv_obj_get_child(card, NOTIFICATION_TEXT_CHILD) : NULL;
    lv_obj_t *counter = card ? lv_obj_get_child(card, NOTIFICATION_COUNTER_CHILD) : NULL;
    if (!text || !counter) return;

    char safe[NOTIFICATION_TEXT_LEN];
    notification_utf8_copy(safe, sizeof(safe), item->text);
    ui_text_font_fallback(&lv_font_noto_sc_18, safe);
    lv_label_set_text(text, safe);
    char status[24];
    snprintf(status, sizeof(status), "%u / %u", (unsigned)(position + 1), (unsigned)count);
    lv_label_set_text(counter, status);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
}

void ui_notification_hide(lv_obj_t *overlay)
{
    if (overlay) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}
