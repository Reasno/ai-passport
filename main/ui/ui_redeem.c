#include "ui_redeem.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>
static const char *IDS[2] = {"tv_time", "lottery_ticket"};
static const char *FALLBACK[2] = {"看电视一次", "大转盘抽奖"};
int ui_redeem_model_index(const app_model_snapshot_t *model, int selected)
{
    if (selected < 0 || selected > 1) return -1;
    for (size_t i = 0; i < model->reward_count; i++) if (strcmp(model->rewards[i].id, IDS[selected]) == 0) return (int)i;
    return -1;
}
lv_obj_t *ui_redeem_build(const app_model_snapshot_t *model, int selected)
{
    lv_obj_t *screen = ui_common_screen("兑换奖品", model);
    for (int i = 0; i < 2; i++) {
        int idx = ui_redeem_model_index(model, i); int price = idx >= 0 ? model->rewards[idx].price : 0;
        bool enabled = idx >= 0 && model->rewards[idx].enabled && model->mqtt_online && model->balance_valid && model->balance >= price && model->pending_type == APP_PENDING_NONE;
        lv_obj_t *card = ui_common_card(screen, 14, 48 + i * 92, 212, 76, selected == i, enabled);
        const char *name = idx >= 0 && model->rewards[idx].name[0] ? model->rewards[idx].name : FALLBACK[i];
        ui_common_label(card, name, 4, 7, 194, LV_TEXT_ALIGN_LEFT, false);
        char detail[80]; if (idx < 0) snprintf(detail, sizeof(detail), "奖品暂未开放"); else snprintf(detail, sizeof(detail), "今日价格：%d分%s", price, enabled ? "" : "  暂不可换");
        lv_obj_t *label = ui_common_label(card, detail, 4, 38, 194, LV_TEXT_ALIGN_LEFT, false); if (!enabled) lv_obj_set_style_text_color(label, lv_color_hex(KP_MUTED), 0);
    }
    char balance[48]; snprintf(balance, sizeof(balance), model->balance_valid ? "当前余额：%ld分" : "当前余额：—", (long)model->balance);
    lv_obj_t *bal = ui_common_label(screen, balance, 20, 244, 200, LV_TEXT_ALIGN_CENTER, false); lv_obj_set_style_text_color(bal, lv_color_hex(KP_YELLOW), 0);
    if (!model->mqtt_online) ui_common_label(screen, "当前离线，请连接家里的 Wi-Fi 再试", 8, 267, 224, LV_TEXT_ALIGN_CENTER, false);
    ui_common_footer(screen, "↑↓切换  ✓兑换  ↑长按主页", model->pending_type != APP_PENDING_NONE);
    return screen;
}
