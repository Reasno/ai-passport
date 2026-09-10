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
    char hint[160];
    if (evidence->waiting_review) snprintf(hint, sizeof(hint), "语音已发送\nAI 正在检查");
    else if (evidence->uploading) snprintf(hint, sizeof(hint), "正在发送语音...\n请稍候");
    else if (evidence->recording) snprintf(hint, sizeof(hint), "录音中 %lu/%lu 秒\n松开 B2 提交",
                                           (unsigned long)(evidence->elapsed_ms / 1000),
                                           (unsigned long)(evidence->max_duration_ms / 1000));
    else snprintf(hint, sizeof(hint), "按住 B2 录音");
    ui_common_label(card, hint, 8, 76, 200, LV_TEXT_ALIGN_CENTER, true);
    if (evidence->recording) {
        lv_obj_set_style_bg_color(card, lv_color_hex(KP_CARD_ALT), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(KP_THEME), 0);
        lv_obj_set_style_border_width(card, 3, 0);
    }
    ui_common_footer(screen, evidence->active ? "松开B2提交" : "B1返回  按住B2录音", model->pending_type != APP_PENDING_NONE || evidence->uploading || evidence->waiting_review);
    return screen;
}
