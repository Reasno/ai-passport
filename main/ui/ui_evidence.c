#include "ui_evidence.h"
#include "ui_common.h"
#include "ui_pixel_icons.h"
#include "ui_text.h"
#include <stdio.h>

lv_obj_t *ui_evidence_build(const app_model_snapshot_t *model, const evidence_service_snapshot_t *evidence)
{
    lv_obj_t *screen = ui_common_screen("语音证据", model);
    lv_obj_t *card = ui_common_card(screen, 12, 36, 216, 188, false, true);
    ui_pixel_icon_create(card, UI_PIXEL_ICON_TASK, 88, 10, KP_THEME, 3);
    ui_common_label(card, evidence->task_name[0] ? evidence->task_name : "提交语音证据", 8, 50, 200, LV_TEXT_ALIGN_CENTER, true);
    char hint[160];
    if (evidence->waiting_review) snprintf(hint, sizeof(hint), "语音已发送\nAI 正在检查");
    else if (evidence->uploading) snprintf(hint, sizeof(hint), "正在发送语音...\n请稍候");
    else if (evidence->recording) snprintf(hint, sizeof(hint), "正在录音 %lu/%lu 秒\n松开 B2 发送",
                                           (unsigned long)(evidence->elapsed_ms / 1000),
                                           (unsigned long)(evidence->max_duration_ms / 1000));
    else snprintf(hint, sizeof(hint), "长按 B2 开始说话\n松开发送，最多 15 秒");
    ui_common_label(card, hint, 8, 92, 200, LV_TEXT_ALIGN_CENTER, false);
    ui_common_label_small(card, "直接说你做了什么\n或补充完成细节", 16, 142, 184, LV_TEXT_ALIGN_CENTER);
    ui_common_footer(screen, evidence->active ? "录音中请松开B2发送" : "B1返回  长按B2录音", model->pending_type != APP_PENDING_NONE || evidence->uploading || evidence->waiting_review);
    return screen;
}
