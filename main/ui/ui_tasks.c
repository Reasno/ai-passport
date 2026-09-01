#include "ui_tasks.h"
#include "ui_common.h"
#include "ui_pixel_icons.h"
#include <stdio.h>

lv_obj_t *ui_tasks_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = ui_common_screen("今日任务", model);
    if (model->task_count == 0) ui_common_label(screen, "今天还没有任务", 20, 118, 200, LV_TEXT_ALIGN_CENTER, false);
    int start = selected > 4 ? selected - 4 : 0;
    for (int i = start, shown = 0; i < (int)model->task_count && shown < 5; i++, shown++) {
        const app_task_t *task = &model->tasks[i];
        lv_obj_t *card = ui_common_card(screen, 8, 28 + shown * 50, 224, 46, i == selected, true);
        ui_pixel_icon_create(card, task->completed_today ? UI_PIXEL_ICON_CHECK : UI_PIXEL_ICON_TASK, 14, 9,
                             task->completed_today ? KP_GREEN : (task->self_complete ? KP_THEME : KP_MUTED), 2);
        lv_obj_t *name = ui_common_label(card, task->name, 46, 6, 122, LV_TEXT_ALIGN_LEFT, false);
        if (task->completed_today) lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        if (task->completed_today || !task->self_complete) lv_obj_set_style_text_color(name, lv_color_hex(KP_MUTED), 0);
        char points[24]; snprintf(points, sizeof(points), task->completed_today ? "完成" : "+%ld", (long)task->points);
        lv_obj_t *p = ui_common_label(card, points, 166, 6, 46, LV_TEXT_ALIGN_RIGHT, false);
        lv_obj_set_style_text_color(p, lv_color_hex(task->completed_today ? KP_GREEN : KP_YELLOW), 0);
    }
    ui_common_footer(screen, "上下滚动 中键打卡 长按上键主页", model->pending_type != APP_PENDING_NONE); return screen;
}
