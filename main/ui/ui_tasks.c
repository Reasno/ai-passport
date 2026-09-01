#include "ui_tasks.h"
#include "ui_common.h"
#include <stdio.h>

lv_obj_t *ui_tasks_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = ui_common_screen("今日任务", model);
    if (model->task_count == 0) ui_common_label(screen, "今天还没有任务", 20, 130, 200, LV_TEXT_ALIGN_CENTER, false);
    int start = selected > 4 ? selected - 4 : 0;
    int shown = 0;
    for (int i = start; i < (int)model->task_count && shown < 5; i++, shown++) {
        const app_task_t *task = &model->tasks[i];
        lv_obj_t *card = ui_common_card(screen, 8, 38 + shown * 48, 224, 43, i == selected, true);
        char line[96]; snprintf(line, sizeof(line), "%s %s  %+ld分", task->completed_today ? "✓" : "○", task->name, (long)task->points);
        lv_obj_t *label = ui_common_label(card, line, 2, 5, 210, LV_TEXT_ALIGN_LEFT, false);
        if (task->completed_today || !task->self_complete) lv_obj_set_style_text_color(label, lv_color_hex(KP_MUTED), 0);
    }
    ui_common_footer(screen, "↑↓滚动  ✓打卡  ↑长按主页", model->pending_type != APP_PENDING_NONE);
    return screen;
}
