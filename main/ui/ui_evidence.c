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
    else if (evidence->recording) snprintf(hint, sizeof(hint), "正在录音 %lu/%lu 秒\n松开 B2 发送",
                                           (unsigned long)(evidence->elapsed_ms / 1000),
                                           (unsigned long)(evidence->max_duration_ms / 1000));
    else snprintf(hint, sizeof(hint), "长按 B2 录音\n松开后提交证据");
    ui_common_label(card, hint, 8, 76, 200, LV_TEXT_ALIGN_CENTER, true);
    if (evidence->recording) {
        lv_obj_set_style_border_color(card, lv_color_hex(KP_RED), 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_pos(dot, 103, 58);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(KP_RED), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_t *progress = lv_bar_create(card);
        lv_obj_set_size(progress, 184, 8);
        lv_obj_set_pos(progress, 16, 158);
        lv_bar_set_range(progress, 0, (int32_t)(evidence->max_duration_ms ? evidence->max_duration_ms : 1));
        lv_bar_set_value(progress, (int32_t)evidence->elapsed_ms, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress, lv_color_hex(0x263951), LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress, lv_color_hex(KP_RED), LV_PART_INDICATOR);
    }
    ui_common_footer(screen, evidence->active ? "录音中请松开B2发送" : "B1返回  长按B2录音", model->pending_type != APP_PENDING_NONE || evidence->uploading || evidence->waiting_review);
    return screen;
}
