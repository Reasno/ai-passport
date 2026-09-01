#include "ui_lottery.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *s_cells[9];
static const char *CELLS[9] = {"麦当劳", "20元", "10分", "2元", "开始", "5分", "谢谢", "麦当劳", "2分"};
lv_obj_t *ui_lottery_build(const app_model_snapshot_t *model, int highlight)
{
    lv_obj_t *screen = ui_common_screen("幸运抽奖", model);
    if (model->lottery_ready) {
        ui_common_label(screen, "恭 喜！", 20, 52, 200, LV_TEXT_ALIGN_CENTER, true);
        char prize[96]; snprintf(prize, sizeof(prize), "抽中 %s", model->lottery_label[0] ? model->lottery_label : model->lottery_prize_id);
        lv_obj_t *p = ui_common_label(screen, prize, 15, 106, 210, LV_TEXT_ALIGN_CENTER, false); lv_obj_set_style_text_color(p, lv_color_hex(KP_YELLOW), 0);
        ui_common_label(screen, model->lottery_points_delta ? "已自动入账到账户" : "请去找爸爸妈妈兑奖", 10, 178, 220, LV_TEXT_ALIGN_CENTER, false);
        ui_common_label(screen, model->lottery_message, 10, 218, 220, LV_TEXT_ALIGN_CENTER, false);
        ui_common_footer(screen, "✓确认返回兑换页", false);
        return screen;
    }
    for (int i = 0; i < 9; i++) {
        int x = 45 + (i % 3) * 50, y = 52 + (i / 3) * 50;
        lv_obj_t *card = ui_common_card(screen, x, y, 48, 48, i == highlight, true); s_cells[i] = card;
        ui_common_label(card, CELLS[i], 0, 8, 38, LV_TEXT_ALIGN_CENTER, false);
    }
    ui_common_label(screen, model->pending_type == APP_PENDING_LOTTERY ? "正在开奖，请等待结果..." : "高亮框快速移动中...", 10, 224, 220, LV_TEXT_ALIGN_CENTER, false);
    ui_common_footer(screen, "抽奖进行中，请等待", true);
    return screen;
}

void ui_lottery_set_highlight(int highlight)
{
    for (int i = 0; i < 9; i++) if (s_cells[i]) { lv_obj_set_style_bg_color(s_cells[i], lv_color_hex(i == highlight ? KP_YELLOW : KP_CARD), 0); lv_obj_set_style_border_width(s_cells[i], i == highlight ? 2 : 1, 0); lv_obj_set_style_border_color(s_cells[i], lv_color_hex(i == highlight ? 0xFFFFFF : KP_THEME), 0); }
}
