#include "ui_ledger.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_text.h"
#include "notification_store.h"
#include <stdio.h>
#include <time.h>

lv_obj_t *ui_ledger_build(const app_model_snapshot_t *model, const ledger_snapshot_t *ledger)
{
#if CONFIG_KIDS_ACTOR_SISTER
    const char *title = "妹妹积分流水";
#else
    const char *title = "哥哥积分流水";
#endif
    lv_obj_t *screen = ui_common_screen(title, model);
    if (!ledger->page.count) {
        ui_common_label(screen, ledger->loading ? "正在读取流水" :
            ledger->error ? "读取失败，请联网重试" : "暂无积分流水",
            14, 120, 212, LV_TEXT_ALIGN_CENTER, false);
    }
    for (size_t i = 0; i < ledger->page.count; ++i) {
        const ledger_entry_t *entry = &ledger->page.items[i];
        lv_obj_t *card = ui_common_card(screen, 8, 28 + i * 118, 224, 114, false, true);
        /* Timestamp is always Asia/Shanghai, even before SNTP synchronization. */
        time_t seconds = (time_t)(entry->created_at_ms / 1000 + 8 * 3600);
        struct tm local;
        gmtime_r(&seconds, &local);
        char date[24], amount[40];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
        ui_common_label_small(card, date, 4, 0, 208, LV_TEXT_ALIGN_LEFT);
        snprintf(amount, sizeof(amount), "余额%ld", (long)entry->balance);
        ui_common_label_small(card, amount, 4, 19, 124, LV_TEXT_ALIGN_LEFT);
        snprintf(amount, sizeof(amount), "%+ld", (long)entry->points_delta);
        lv_obj_t *delta = ui_common_label_small(card, amount, 128, 19, 84, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_style_text_color(delta, lv_color_hex(entry->points_delta < 0 ? KP_RED : KP_GREEN), 0);
        char text[LEDGER_TEXT_LEN];
        notification_utf8_copy(text, sizeof(text), entry->summary);
        ui_text_font_fallback(UI_FONT_BODY, text);
        lv_obj_t *body = lv_label_create(card);
        lv_obj_set_pos(body, 4, 42);
        lv_obj_set_size(body, 204, 56);
        lv_obj_set_style_text_font(body, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(body, lv_color_hex(KP_TEXT), 0);
        lv_label_set_long_mode(body, LV_LABEL_LONG_SCROLL);
        lv_label_set_text(body, text);
    }
    const char *status = ledger->loading ? "正在读取..." :
        ledger->error ? "读取失败，B3重试" : !model->mqtt_online ? "离线，显示已读取流水" :
        ledger->page.has_more ? "B2继续查看更早流水" : "已到最早流水";
    ui_common_label_small(screen, status, 8, 268, 224, LV_TEXT_ALIGN_CENTER);
    ui_common_footer(screen, "B1最新 B2更早 B3刷新", false);
    return screen;
}
