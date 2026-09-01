#include "ui_confirm.h"
#include "ui_common.h"
#include <stdio.h>
lv_obj_t *ui_confirm_build(const app_model_snapshot_t *model, confirm_kind_t kind, const char *name, int points, int selected)
{
    lv_obj_t *screen = ui_common_screen(kind == CONFIRM_TASK ? "确认打卡" : "确认兑换", model);
    lv_obj_t *modal = ui_common_card(screen, 24, 72, 192, 154, false, true);
    ui_common_label(modal, kind == CONFIRM_TASK ? "确认完成？" : "确认兑换？", 4, 8, 176, LV_TEXT_ALIGN_CENTER, true);
    char line[96]; snprintf(line, sizeof(line), "%s  %s%ld分", name, kind == CONFIRM_TASK ? "+" : "", (long)points);
    ui_common_label(modal, line, 4, 55, 176, LV_TEXT_ALIGN_CENTER, false);
    const char *buttons[] = {"取消", "确认"};
    for (int i = 0; i < 2; i++) { lv_obj_t *card = ui_common_card(modal, 8 + i * 88, 104, 80, 34, selected == i, true); ui_common_label(card, buttons[i], 2, 2, 68, LV_TEXT_ALIGN_CENTER, false); }
    ui_common_footer(screen, "↑↓选择  ✓确认  ↑长按主页", false);
    return screen;
}
