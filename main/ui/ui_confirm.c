#include "ui_confirm.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_pixel_icons.h"
#include "ui_text.h"
#include <stdio.h>
lv_obj_t *ui_confirm_build(const app_model_snapshot_t *model, confirm_kind_t kind, const char *name, int points, int selected)
{
    lv_obj_t *screen = ui_common_screen(kind == CONFIRM_TASK ? "确认打卡" : "确认兑换", model);
    lv_obj_t *modal = ui_common_card(screen, 20, 34, 200, 190, false, true);
    ui_pixel_icon_create(modal, kind == CONFIRM_TASK ? UI_PIXEL_ICON_CHECK : UI_PIXEL_ICON_GIFT, 82, 10, KP_THEME, 3);
    ui_common_label(modal, kind == CONFIRM_TASK ? "确认完成？" : "确认兑换？", 4, 50, 184, LV_TEXT_ALIGN_CENTER, true);
    char short_name[64];
    ui_text_limit_lines(name, short_name, sizeof(short_name), UI_TEXT_STANDARD_MAX_CHARS);
    char line[96]; snprintf(line, sizeof(line), "%s\n%s%ld分", short_name, kind == CONFIRM_TASK ? "+" : "", (long)points);
    ui_common_label(modal, line, 4, 82, 184, LV_TEXT_ALIGN_CENTER, false);
    const char *buttons[] = {"取消", "确认"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *card = ui_common_card(modal, 8 + i * 92, 118, 84, 36, selected == i, true);
        lv_obj_t *label = lv_label_create(card);
        lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(KP_TEXT), 0);
        lv_obj_set_style_pad_top(label, 0, 0); lv_obj_set_style_pad_bottom(label, 0, 0);
        lv_label_set_text(label, buttons[i]);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -1);
    }
    ui_common_footer(screen, "上下选择 中键执行 长按上键主页", false); return screen;
}
