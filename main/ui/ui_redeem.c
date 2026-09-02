#include "ui_redeem.h"
#include "ui_common.h"
#include "ui_pixel_icons.h"
#include <stdio.h>
#include <string.h>
static const char *IDS[2] = {"tv_time", "lottery_ticket"};
static const char *FALLBACK[2] = {"看电视一次", "幸运抽奖券"};
int ui_redeem_model_index(const app_model_snapshot_t *model, int selected)
{
    if (selected < 0 || selected > 1) return -1;
    for (size_t i = 0; i < model->reward_count; i++) {
        if (strcmp(model->rewards[i].id, IDS[selected]) == 0) return (int)i;
    }
    return -1;
}
lv_obj_t *ui_redeem_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = ui_common_screen("兑换奖品", model);
    for (int i = 0; i < 2; i++) {
        int idx = ui_redeem_model_index(model, i); int price = idx >= 0 ? model->rewards[idx].price : 0;
        bool enabled = idx >= 0 && model->rewards[idx].enabled && model->mqtt_online && model->balance_valid && model->balance >= price && model->pending_type == APP_PENDING_NONE;
        lv_obj_t *card = ui_common_card(screen, 12, 30 + i * 88, 216, 76, selected == i, enabled);
        ui_pixel_icon_create(card, i ? UI_PIXEL_ICON_TICKET : UI_PIXEL_ICON_TV, 14, 15, enabled ? KP_THEME : KP_MUTED, 3);
        const char *name = idx >= 0 && model->rewards[idx].name[0] ? model->rewards[idx].name : FALLBACK[i];
        lv_obj_t *name_label = ui_common_label(card, name, 56, 8, 148, LV_TEXT_ALIGN_LEFT, false);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_height(name_label, 22);
        char detail[32];
        if (idx < 0) snprintf(detail, sizeof(detail), "今日价格：--");
        else snprintf(detail, sizeof(detail), "今日价格：%d分", price);
        lv_obj_t *d = ui_common_label_small(card, detail, 56, 40, 148, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_height(d, 18);
        lv_obj_set_style_text_color(d, lv_color_hex(enabled ? KP_YELLOW : KP_MUTED), 0);
    }
    char balance[48]; snprintf(balance, sizeof(balance), model->balance_valid ? "当前余额：%ld分" : "当前余额：--", (long)model->balance);
    /* The pending message occupies the lower band, so the balance line is skipped while
     * it is visible: no two texts may ever share the same rows. */
    if (model->pending_type == APP_PENDING_LOTTERY) ui_common_message(screen, "兑换成功\n正在等待开奖...", false);
    else {
        ui_common_label(screen, balance, 20, 212, 200, LV_TEXT_ALIGN_CENTER, true);
        if (!model->mqtt_online) ui_common_label(screen, "离线状态仅可浏览", 8, 251, 224, LV_TEXT_ALIGN_CENTER, false);
    }
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B1主页", model->pending_type != APP_PENDING_NONE); return screen;
}
