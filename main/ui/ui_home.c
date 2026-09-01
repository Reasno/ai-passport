#include "ui_home.h"
#include "ui_avatar.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include <stdio.h>

static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, h / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void entry_button(lv_obj_t *screen, int x, bool selected, const char *icon, const char *text)
{
    lv_obj_t *button = lv_obj_create(screen);
    lv_obj_set_pos(button, x, 207);
    lv_obj_set_size(button, 70, 72);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, 13, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(selected ? 0xF5C84C : 0x17243A), 0);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(selected ? 0xFFF0A3 : 0x2B405F), 0);

    lv_obj_t *icon_label = ui_common_label(button, icon, 0, 6, 68, LV_TEXT_ALIGN_CENTER, true);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(selected ? 0x172033 : KP_THEME), 0);
    lv_obj_t *text_label = ui_common_label(button, text, 1, 46, 66, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_font(text_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(text_label, lv_color_hex(selected ? 0x172033 : KP_TEXT), 0);
}

lv_obj_t *ui_home_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080F1D), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    /* Native 96x156 official artwork, centered at x=72 with no scaling. */
    ui_avatar_create(screen, 72, 0);

#ifdef CONFIG_KIDS_THEME_SISTER
    const char *name = "谷沁萱";
    const char *role = "妹妹";
#else
    const char *name = "谷沁园";
    const char *role = "哥哥";
#endif
    ui_common_label(screen, name, 8, 7, 66, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_t *role_label = ui_common_label(screen, role, 8, 30, 52, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_font(role_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(role_label, lv_color_hex(KP_MUTED), 0);

    lv_obj_t *online = pill(screen, 178, 8, 54, 25, model->mqtt_online ? 0x183C35 : 0x293241);
    lv_obj_t *online_label = ui_common_label(online, model->mqtt_online ? "在线" : "离线", 0, 3, 54,
                                              LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_font(online_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(online_label, lv_color_hex(model->mqtt_online ? KP_GREEN : 0xA6AFBD), 0);

    char balance[32];
    snprintf(balance, sizeof(balance), model->balance_valid ? "%ld 分" : "— 分", (long)model->balance);
    lv_obj_t *balance_pill = pill(screen, 8, 157, 108, 40, 0x3A3017);
    lv_obj_t *balance_title = ui_common_label(balance_pill, "积分余额", 8, 1, 92, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_font(balance_title, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(balance_title, lv_color_hex(0xD5BC70), 0);
    lv_obj_t *balance_value = ui_common_label(balance_pill, balance, 8, 17, 92, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_color(balance_value, lv_color_hex(KP_YELLOW), 0);

    int done = 0;
    for (size_t i = 0; i < model->task_count; i++) {
        if (model->tasks[i].completed_today) done++;
    }
    char progress[32];
    snprintf(progress, sizeof(progress), "%d / %u", done, (unsigned)model->task_count);
    lv_obj_t *progress_pill = pill(screen, 124, 157, 108, 40, 0x172B35);
    lv_obj_t *progress_title = ui_common_label(progress_pill, "今日进度", 8, 1, 92, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_font(progress_title, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(progress_title, lv_color_hex(0x80C9C2), 0);
    lv_obj_t *progress_value = ui_common_label(progress_pill, progress, 8, 17, 92, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_color(progress_value, lv_color_hex(KP_GREEN), 0);

    entry_button(screen, 8, selected == 0, "任", "今日任务");
    entry_button(screen, 85, selected == 1, "兑", "兑换");
    entry_button(screen, 162, selected == 2, "奖", "抽奖");

    lv_obj_t *hint = ui_common_label(screen,
                                     model->pending_type != APP_PENDING_NONE ? "请稍候..." : "↑↓ 选择   ✓ 确认   长按 ↑ 回主页",
                                     5, 296, 230, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x66758A), 0);
    return screen;
}
