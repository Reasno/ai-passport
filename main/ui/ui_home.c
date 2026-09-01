#include "ui_home.h"
#include "ui_avatar.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_pixel_icons.h"
#include <stdio.h>

static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0); lv_obj_set_style_radius(obj, h / 2, 0); lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0); return obj;
}
static void entry(lv_obj_t *screen, int x, bool selected, ui_pixel_icon_t icon, const char *text)
{
    lv_obj_t *button = ui_common_card(screen, x, 208, 70, 72, selected, true);
    lv_obj_t *image = ui_home_menu_icon_create(button, icon, 15, 5);
    if (image && !selected) lv_obj_set_style_opa(image, LV_OPA_70, 0);
    ui_common_label_small(button, text, 14, 48, 48, LV_TEXT_ALIGN_CENTER);
}
lv_obj_t *ui_home_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = lv_obj_create(NULL); lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0); lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_t *band = lv_obj_create(screen); lv_obj_set_pos(band, 0, 118); lv_obj_set_size(band, 240, 90);
    lv_obj_set_style_bg_color(band, lv_color_hex(KP_BG), 0); lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0); lv_obj_set_style_border_width(band, 0, 0); lv_obj_set_style_radius(band, 0, 0);
    lv_obj_t *avatar = ui_avatar_create(screen, 72, 0);
    const char *name = CONFIG_KP_CHILD_NAME;
    const char *role = CONFIG_KP_CHILD_ROLE;
    ui_common_label(screen, name, 6, 32, 66, LV_TEXT_ALIGN_LEFT, true);
    lv_obj_t *role_label = ui_common_label_small(screen, role, 8, 62, 52, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(role_label, lv_color_hex(KP_MUTED), 0);
    lv_obj_t *balance = pill(screen, 8, 154, 108, 44, 0x3A3017);
    lv_obj_t *bt = ui_common_label_small(balance, "积分余额", 0, 4, 108, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(bt, 14);
    lv_obj_set_style_text_color(bt, lv_color_hex(0xD5BC70), 0);
    char value[24]; snprintf(value, sizeof(value), model->balance_valid ? "%ld 分" : "-- 分", (long)model->balance);
    lv_obj_t *bv = ui_common_label(balance, value, 0, 19, 108, LV_TEXT_ALIGN_CENTER, true);
    lv_obj_set_height(bv, 22);
    lv_obj_set_style_text_color(bv, lv_color_hex(KP_YELLOW), 0);
    int done = 0; for (size_t i = 0; i < model->task_count; i++) if (model->tasks[i].completed_today) done++;
    lv_obj_t *progress = pill(screen, 124, 154, 108, 44, 0x172B35);
    lv_obj_t *pt = ui_common_label_small(progress, "今日进度", 0, 4, 108, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(pt, 14);
    char pv[16]; snprintf(pv, sizeof(pv), "%d/%u", done, (unsigned)model->task_count);
    lv_obj_t *progress_value = ui_common_label(progress, pv, 0, 19, 108, LV_TEXT_ALIGN_CENTER, true);
    lv_obj_set_height(progress_value, 22);
    lv_obj_set_style_text_color(progress_value, lv_color_hex(KP_GREEN), 0);
    entry(screen, 8, selected == 0, UI_PIXEL_ICON_TASK, "今日任务");
    entry(screen, 85, selected == 1, UI_PIXEL_ICON_GIFT, "兑换奖品");
    entry(screen, 162, selected == 2, UI_PIXEL_ICON_GAME, "互动游戏");
    lv_obj_t *hint = ui_common_label_small(screen, model->pending_type != APP_PENDING_NONE ? "请稍候..." : "上下选择 中键确认 长按上键主页", 5, 296, 230, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(hint, lv_color_hex(KP_MUTED), 0);
    lv_obj_t *statusbar = ui_statusbar_create(screen, model);
    char device_text[32];
    snprintf(device_text, sizeof(device_text), "%s设备", CONFIG_KP_CHILD_ROLE);
    lv_obj_t *device = ui_common_label_small(statusbar, device_text, 4, 1, 55, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(device, lv_color_hex(KP_MUTED_LIGHT), 0);
    lv_obj_move_foreground(avatar);
    ui_statusbar_home_foreground(statusbar);
    return screen;
}
