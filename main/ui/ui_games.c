#include "ui_games.h"
#include "find_service.h"
#include "ptt_service.h"
#include "ui_common.h"
#include "ui_pixel_icons.h"
#include "ui_text.h"
#include "esp_system.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RPS_ART_WIDTH 54
#define RPS_ART_HEIGHT 72
#define RPS_ART_BYTES (RPS_ART_WIDTH * RPS_ART_HEIGHT * 2)

extern const uint8_t rps_rock_data[] asm("_binary_rock_54x72_rgb565_start");
extern const uint8_t rps_scissors_data[] asm("_binary_scissors_54x72_rgb565_start");
extern const uint8_t rps_paper_data[] asm("_binary_paper_54x72_rgb565_start");

#define RPS_IMAGE(name, data_ptr) \
    static const lv_image_dsc_t name = { \
        .header.magic = LV_IMAGE_HEADER_MAGIC, \
        .header.cf = LV_COLOR_FORMAT_RGB565, \
        .header.flags = 0, \
        .header.w = RPS_ART_WIDTH, \
        .header.h = RPS_ART_HEIGHT, \
        .header.stride = RPS_ART_WIDTH * 2, \
        .data_size = RPS_ART_BYTES, \
        .data = data_ptr, \
    }

RPS_IMAGE(s_rps_rock, rps_rock_data);
RPS_IMAGE(s_rps_scissors, rps_scissors_data);
RPS_IMAGE(s_rps_paper, rps_paper_data);

static void rps_choice_card(lv_obj_t *screen, int x, const lv_image_dsc_t *image_source,
                            const char *label, bool selected)
{
    lv_obj_t *card = ui_common_card(screen, x, 29, 66, 116, false, true);
    lv_obj_t *marker = lv_obj_get_child(card, 0);
    if (marker) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(card, lv_color_hex(selected ? KP_CARD_ALT : KP_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? KP_THEME : 0x263951), 0);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    lv_obj_t *image = lv_image_create(card);
    lv_image_set_src(image, image_source);
    lv_image_set_antialias(image, false);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *caption = ui_common_label_small(card, label, 0, 0, 66, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(caption, lv_pct(100));
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_color(caption, lv_color_hex(selected ? KP_TEXT : KP_MUTED_LIGHT), 0);
}

static void game_card(lv_obj_t *screen, int y, bool selected, bool enabled, ui_pixel_icon_t icon,
                      const char *title, const char *detail)
{
    lv_obj_t *card = ui_common_card(screen, 12, y, 216, 52, selected, enabled);
    lv_obj_t *image = icon == UI_PIXEL_ICON_RADAR
                          ? ui_radar_icon_create(card, 0, 0, enabled ? KP_THEME : KP_MUTED, 3)
                      : icon == UI_PIXEL_ICON_BUZZER
                          ? ui_buzzer_icon_create(card, 0, 0, enabled, 3)
                          : ui_pixel_icon_create(card, icon, 0, 0, enabled ? KP_THEME : KP_MUTED, 3);
    if (image) lv_obj_align(image, LV_ALIGN_LEFT_MID, 14, 0);
    ui_common_label(card, title, 56, 2, 148, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_t *d = ui_common_label_small(card, detail, 56, 28, 148, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(d, lv_color_hex(enabled ? KP_MUTED_LIGHT : KP_MUTED), 0);
}

lv_obj_t *ui_games_build(const app_model_snapshot_t *model, const game_snapshot_t *game, int selected)
{
    lv_obj_t *screen = ui_common_screen("互动游戏", model);
    const char *pair = game->paired ? "已配对 长按B3重配" : "未配对 长按B3配对";
    ui_common_label_small(screen, pair, 12, 31, 216, LV_TEXT_ALIGN_CENTER);
    /* Ring works over either transport, so the entry only needs one of them alive. */
    bool find_ready = game->paired || model->mqtt_online;
    const char *find_detail = game->paired ? "响铃和近距离信号"
                              : model->mqtt_online ? "仅WiFi响铃 无测距" : "请先完成配对";
    game_card(screen, 47, selected == 0, game_service_heap_allows_radar() && find_ready,
              UI_PIXEL_ICON_RADAR, "找" KP_PEER_LABEL, find_detail);
    game_card(screen, 105, selected == 1, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_RPS, "石头剪刀布", game->paired ? "纯娱乐 不增减积分" : "请先完成配对");
    game_card(screen, 163, selected == 2, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_BUZZER, "抢答器", game->paired ? "B3 抢答 纯娱乐" : "请先完成配对");
    char heap[48]; snprintf(heap, sizeof(heap), "可用内存 %lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_obj_t *h = ui_common_label_small(screen, heap, 12, 222, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(h, lv_color_hex(KP_MUTED), 0);
    if (game->state == GAME_STATE_PAIRING) ui_common_message(screen, game->status, false);
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B1主页", false);
    return screen;
}

lv_obj_t *ui_find_build(const app_model_snapshot_t *model, const game_snapshot_t *game,
                        const char *find_status, bool waiting)
{
    lv_obj_t *screen = ui_common_screen("找" KP_PEER_LABEL, model);
    lv_obj_t *radar = ui_radar_icon_create(screen, 0, 0, KP_THEME, 5);
    if (radar) lv_obj_align(radar, LV_ALIGN_TOP_MID, 0, 26);
    const char *who = game->peer_nearby ? KP_PEER_LABEL "就在附近" : "等待近距离信号";
    ui_common_label(screen, who, 20, 102, 200, LV_TEXT_ALIGN_CENTER, true);
    char rssi[48];
    snprintf(rssi, sizeof(rssi), game->peer_nearby ? "信号 %d dBm" : "信号扫描中...", game->rssi);
    ui_common_label_small(screen, rssi, 20, 138, 200, LV_TEXT_ALIGN_CENTER);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *bar = lv_obj_create(screen); lv_obj_set_pos(bar, 55 + i * 28, 184 - i * 5);
        lv_obj_set_size(bar, 18, 10 + i * 5); lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0); lv_obj_set_style_bg_color(bar, lv_color_hex(i < game->distance_bars ? KP_GREEN : KP_CARD), 0);
    }
    /* Status sits in a two-line box ending at y=234, clearing the PTT hint below it. */
    find_channels_t live = find_service_available();
    bool can_ring = live.mqtt || live.espnow;
    char ready[80];
    if (can_ring) snprintf(ready, sizeof(ready), "可让" KP_PEER_LABEL "响铃 (%s)", find_service_channel_label(live));
    else strlcpy(ready, "暂无可用通道\n请先配对或联网", sizeof(ready));
    const char *status = find_status && find_status[0] ? find_status : ready;
    lv_color_t status_color = lv_color_hex(waiting ? KP_YELLOW : (can_ring ? KP_MUTED_LIGHT : KP_RED));
    /* Avoid LVGL WDT deadloops on embedded newlines: split into two single-line labels. */
    const char *nl = strchr(status, '\n');
    if (!nl) {
        lv_obj_t *s = ui_common_label_small(screen, status, 12, 198, 216, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_color(s, status_color, 0);
    } else {
        char line1[96], line2[96];
        size_t n1 = (size_t)(nl - status);
        if (n1 >= sizeof(line1)) n1 = sizeof(line1) - 1;
        memcpy(line1, status, n1); line1[n1] = 0;
        strlcpy(line2, nl + 1, sizeof(line2));
        lv_obj_t *s1 = ui_common_label_small(screen, line1, 12, 198, 216, LV_TEXT_ALIGN_CENTER);
        lv_obj_t *s2 = ui_common_label_small(screen, line2, 12, 214, 216, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_color(s1, status_color, 0);
        lv_obj_set_style_text_color(s2, status_color, 0);
    }
    bool ptt = ptt_service_available();
    lv_obj_t *p = ui_common_label_small(screen, ptt ? (ptt_service_is_transmitting() ? "正在说话 松开B2结束" : "长按B2对讲") : "对讲不可用", 12, 240, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(p, lv_color_hex(ptt ? KP_GREEN : KP_MUTED), 0);
    ui_common_footer(screen, waiting ? "等待对方回应  按B3取消" : "长按B1主页  长按B2对讲  B3响铃", false);
    return screen;
}

lv_obj_t *ui_rps_build(const app_model_snapshot_t *model, const game_snapshot_t *game)
{
    lv_obj_t *screen = ui_common_screen("石头剪刀布", model);
    rps_choice_t shown = game->local_choice != RPS_NONE ? game->local_choice : game->cursor_choice;
    rps_choice_card(screen, 8, &s_rps_rock, "石头", shown == RPS_ROCK);
    rps_choice_card(screen, 87, &s_rps_scissors, "剪刀", shown == RPS_SCISSORS);
    rps_choice_card(screen, 166, &s_rps_paper, "布", shown == RPS_PAPER);
    ui_common_label(screen, game->status, 12, 153, 216, LV_TEXT_ALIGN_CENTER, false);
    char record[48];
    snprintf(record, sizeof(record), "本机战绩 %lu胜 %lu负",
             (unsigned long)game->wins, (unsigned long)game->losses);
    ui_common_label_small(screen, record, 12, 176, 216, LV_TEXT_ALIGN_CENTER);
    const char *footer = "长按B1主页";
    if (game->state == GAME_STATE_WAITING_CHOICE) {
        ui_common_label_small(screen, game->local_choice == RPS_NONE ? "选择后按确认出拳" : "已锁定出拳，等待对方", 6, 194, 228, LV_TEXT_ALIGN_CENTER);
        footer = game->local_choice == RPS_NONE
                     ? "B1 B2选择  B3确认  长按B1主页"
                     : "等待对方  长按B1主页";
    } else if (game->state == GAME_STATE_INVITE_RECEIVED) {
        ui_common_label(screen, "B2拒绝 B3接受", 20, 194, 200, LV_TEXT_ALIGN_CENTER, false);
        footer = "B2拒绝  B3接受  长按B1主页";
    } else if (game->state == GAME_STATE_RESULT) {
        footer = "长按B1主页  B3再来一局";
    } else if (game->state == GAME_STATE_COUNTDOWN) {
        /* The centered status already says 即将开始; deliberately show no seconds. */
    } else {
        char left[40];
        snprintf(left, sizeof(left), "剩余 %lu 秒", (unsigned long)game->seconds_left);
        ui_common_label(screen, left, 20, 194, 200, LV_TEXT_ALIGN_CENTER, false);
    }
    ui_common_footer(screen, footer, false);
    return screen;
}

static lv_obj_t *buzzer_light(lv_obj_t *parent, int x, bool on)
{
    lv_obj_t *light = lv_obj_create(parent);
    lv_obj_set_pos(light, x, 22);
    lv_obj_set_size(light, 48, 48);
    lv_obj_set_style_radius(light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(light, lv_color_hex(on ? 0xFF334D : 0x364254), 0);
    lv_obj_set_style_border_color(light, lv_color_hex(on ? 0xFF7A8C : 0x526075), 0);
    lv_obj_set_style_border_width(light, 3, 0);
    lv_obj_clear_flag(light, LV_OBJ_FLAG_SCROLLABLE);
    return light;
}

lv_obj_t *ui_buzzer_build(const app_model_snapshot_t *model, const buzzer_game_snapshot_t *game)
{
    lv_obj_t *screen = ui_common_screen("抢答器", model);
    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_pos(card, 12, 38);
    lv_obj_set_size(card, 216, 92);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(KP_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(KP_THEME), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    /* Card: (12, 38), 216 × 92.  Lights: diameter 48, gap 14.
     * Group width = 48 × 3 + 14 × 2 = 172.
     * Relative start x = (216 - 172) / 2 = 22; y = (92 - 48) / 2 = 22.
     * Clear the card's theme padding above so these coordinates are measured
     * from the card itself rather than from its padded content area. */
    for (int i = 0; i < 3; ++i) buzzer_light(card, 22 + i * 62, i < game->lights_on);

    const char *role = game->is_host ? "HOST" : "CLIENT";
    if (game->state == BUZZER_STATE_IDLE) role = "READY";
    lv_obj_t *role_label = ui_common_label_small(screen, role, 12, 136, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(role_label, lv_color_hex(KP_THEME), 0);
    ui_common_label(screen, game->status, 12, 158, 216, LV_TEXT_ALIGN_CENTER, false);
    char record[48];
    snprintf(record, sizeof(record), "本机战绩 %lu胜 %lu负",
             (unsigned long)game->wins, (unsigned long)game->losses);
    ui_common_label_small(screen, record, 12, 181, 216, LV_TEXT_ALIGN_CENTER);

    const char *hint = "等待邀请";
    const char *footer = "长按B1主页";
    if (game->state == BUZZER_STATE_INVITE_RECEIVED) {
        hint = "B2 拒绝  B3 接受";
        footer = "B2拒绝  B3接受  长按B1主页";
    } else if (game->state == BUZZER_STATE_ARMED) {
        hint = "红灯全灭前按 B3 会判负";
        footer = "只用B3抢答  长按B1主页";
    } else if (game->state == BUZZER_STATE_GO) {
        hint = "现在按 B3！";
        footer = "B3抢答  长按B1主页";
    } else if (game->state == BUZZER_STATE_RESULT) {
        hint = "主机已按同步时间裁决";
        footer = "B3再来一局  长按B1主页";
    } else if (game->state == BUZZER_STATE_SYNCING) {
        hint = "NTP-like 多次采样中";
    } else if (game->state == BUZZER_STATE_INVITE_SENT) {
        hint = "等待对方接受";
    }
    lv_obj_t *h = ui_common_label_small(screen, hint, 12, 205, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(h, lv_color_hex(KP_MUTED_LIGHT), 0);
    ui_common_footer(screen, footer, false);
    return screen;
}
