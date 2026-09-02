#include "ui_home.h"
#include "ui_avatar.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "logo_assets.h"
#include "ui_pixel_icons.h"
#include <stdio.h>

static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0); lv_obj_set_style_radius(obj, h / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *pill_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(label, 0, 0);
    lv_obj_set_style_pad_bottom(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
    return label;
}
static void entry(lv_obj_t *screen, int x, bool selected, ui_pixel_icon_t icon, const char *text)
{
    lv_obj_t *button = ui_common_card(screen, x, 208, 70, 72, false, true);
    lv_obj_t *marker = lv_obj_get_child(button, 0);
    if (marker) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(button, lv_color_hex(selected ? KP_CARD_ALT : KP_CARD), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(selected ? KP_THEME : 0x263951), 0);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
    lv_obj_t *image = ui_home_menu_icon_create(button, icon, 0, 0);
    if (image) {
        lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 9);
        if (!selected) lv_obj_set_style_opa(image, LV_OPA_60, 0);
    }
    lv_obj_t *label = ui_common_label_small(button, text, 0, 0, 62, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(label, 18);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, icon == UI_PIXEL_ICON_GIFT ? -1 : 0, -3);
    lv_obj_set_style_text_color(label, lv_color_hex(selected ? KP_TEXT : KP_MUTED_LIGHT), 0);
}
lv_obj_t *ui_home_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = lv_obj_create(NULL); lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0); lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_t *band = lv_obj_create(screen); lv_obj_set_pos(band, 0, 118); lv_obj_set_size(band, 240, 90);
    lv_obj_set_style_bg_color(band, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0); lv_obj_set_style_border_width(band, 0, 0); lv_obj_set_style_radius(band, 0, 0);
    lv_obj_t *avatar = ui_avatar_create(screen, 106, 1);
    lv_obj_t *app_logo = lv_image_create(screen);
    lv_image_set_src(app_logo, &logo_01_app_logo);
    lv_image_set_antialias(app_logo, false);
    lv_obj_set_pos(app_logo, 6, 24);
    lv_obj_t *role_badge = pill(screen, 21, 120, 62, 24, 0x172B35);
    pill_label(role_badge, CONFIG_KP_CHILD_ROLE, UI_FONT_SMALL, KP_MUTED_LIGHT, 1);
    lv_obj_t *balance = pill(screen, 8, 154, 108, 44, 0x3A3017);
    pill_label(balance, "积分余额", UI_FONT_SMALL, 0xD5BC70, -11);
    char value[24]; snprintf(value, sizeof(value), model->balance_valid ? "%ld 分" : "-- 分", (long)model->balance);
    pill_label(balance, value, UI_FONT_SMALL, KP_YELLOW, 11);
    int done = 0; for (size_t i = 0; i < model->task_count; i++) if (model->tasks[i].completed_today) done++;
    lv_obj_t *progress = pill(screen, 124, 154, 108, 44, 0x172B35);
    pill_label(progress, "今日进度", UI_FONT_SMALL, KP_TEXT, -11);
    char pv[16]; snprintf(pv, sizeof(pv), "%d/%u", done, (unsigned)model->task_count);
    pill_label(progress, pv, UI_FONT_SMALL, KP_GREEN, 11);
    entry(screen, 8, selected == 0, UI_PIXEL_ICON_TASK, "今日任务");
    entry(screen, 85, selected == 1, UI_PIXEL_ICON_GIFT, "兑换奖品");
    entry(screen, 162, selected == 2, UI_PIXEL_ICON_GAME, "互动游戏");
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B1主页", model->pending_type != APP_PENDING_NONE);
    lv_obj_t *statusbar = ui_statusbar_create(screen, model);
    lv_obj_move_foreground(avatar);
    ui_statusbar_home_foreground(statusbar);
    return screen;
}
