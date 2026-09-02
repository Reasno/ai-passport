#include "ui_games.h"
#include "ptt_service.h"
#include "ui_common.h"
#include "ui_pixel_icons.h"
#include "esp_system.h"
#include <stdint.h>
#include <stdio.h>

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
    lv_obj_t *card = ui_common_card(screen, x, 29, 66, 98, false, true);
    lv_obj_t *marker = lv_obj_get_child(card, 0);
    if (marker) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(card, lv_color_hex(selected ? KP_CARD_ALT : KP_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? KP_THEME : 0x263951), 0);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    lv_obj_t *image = lv_image_create(card);
    lv_image_set_src(image, image_source);
    lv_image_set_antialias(image, false);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *caption = ui_common_label_small(card, label, 6, 78, 54, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(caption, lv_color_hex(selected ? KP_TEXT : KP_MUTED_LIGHT), 0);
}

static void game_card(lv_obj_t *screen, int y, bool selected, bool enabled, ui_pixel_icon_t icon,
                      const char *title, const char *detail)
{
    lv_obj_t *card = ui_common_card(screen, 12, y, 216, 66, selected, enabled);
    if (icon == UI_PIXEL_ICON_RADAR) ui_radar_icon_create(card, 14, 13, enabled ? KP_THEME : KP_MUTED, 3);
    else ui_pixel_icon_create(card, icon, 14, 13, enabled ? KP_THEME : KP_MUTED, 3);
    ui_common_label(card, title, 56, 7, 148, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_t *d = ui_common_label_small(card, detail, 56, 38, 148, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(d, lv_color_hex(enabled ? KP_MUTED_LIGHT : KP_MUTED), 0);
}

lv_obj_t *ui_games_build(const app_model_snapshot_t *model, const game_snapshot_t *game, int selected)
{
    lv_obj_t *screen = ui_common_screen("互动游戏", model);
    const char *pair = game->paired ? "已配对 长按中键重配" : "未配对 长按中键配对";
    ui_common_label_small(screen, pair, 12, 31, 216, LV_TEXT_ALIGN_CENTER);
    game_card(screen, 52, selected == 0, game_service_heap_allows_radar() && game->paired,
              UI_PIXEL_ICON_RADAR, "找伙伴", game->paired ? "响铃和近距离信号" : "请先完成配对");
    game_card(screen, 126, selected == 1, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_RPS, "石头剪刀布", game->paired ? "纯娱乐 不增减积分" : "请先完成配对");
    char heap[48]; snprintf(heap, sizeof(heap), "可用内存 %lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_obj_t *h = ui_common_label_small(screen, heap, 12, 211, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(h, lv_color_hex(KP_MUTED), 0);
    if (game->state == GAME_STATE_PAIRING) ui_common_message(screen, game->status, false);
    ui_common_footer(screen, "上下选择 中键进入 长按中键配对", false);
    return screen;
}

lv_obj_t *ui_find_build(const app_model_snapshot_t *model, const game_snapshot_t *game,
                        const char *find_status, bool waiting)
{
    lv_obj_t *screen = ui_common_screen("找伙伴", model);
    ui_radar_icon_create(screen, 91, 32, KP_THEME, 5);
    const char *who = game->peer_nearby ? "伙伴就在附近" : "等待近距离信号";
    ui_common_label(screen, who, 20, 99, 200, LV_TEXT_ALIGN_CENTER, true);
    char rssi[48];
    snprintf(rssi, sizeof(rssi), game->peer_nearby ? "信号 %d dBm" : "信号扫描中...", game->rssi);
    ui_common_label_small(screen, rssi, 20, 136, 200, LV_TEXT_ALIGN_CENTER);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *bar = lv_obj_create(screen); lv_obj_set_pos(bar, 55 + i * 28, 184 - i * 5);
        lv_obj_set_size(bar, 18, 10 + i * 5); lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0); lv_obj_set_style_bg_color(bar, lv_color_hex(i < game->distance_bars ? KP_GREEN : KP_CARD), 0);
    }
    const char *status = find_status && find_status[0] ? find_status : (model->mqtt_online ? "可让伙伴响铃" : "MQTT离线\n响铃不可用");
    lv_obj_t *s = ui_common_label_small(screen, status, 12, 213, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(s, lv_color_hex(waiting ? KP_YELLOW : (model->mqtt_online ? KP_MUTED_LIGHT : KP_RED)), 0);
    bool ptt = ptt_service_available();
    lv_obj_t *p = ui_common_label_small(screen, ptt ? (ptt_service_is_transmitting() ? "正在说话 松开下键结束" : "长按下键对讲") : "对讲不可用", 12, 234, 216, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(p, lv_color_hex(ptt ? KP_GREEN : KP_MUTED), 0);
    ui_common_footer(screen, "中键响铃 长按下键对讲 长按上键主页", false);
    return screen;
}

lv_obj_t *ui_rps_build(const app_model_snapshot_t *model, const game_snapshot_t *game)
{
    lv_obj_t *screen = ui_common_screen("石头剪刀布", model);
    rps_choice_card(screen, 8, &s_rps_rock, "石头", game->local_choice == RPS_ROCK);
    rps_choice_card(screen, 87, &s_rps_scissors, "剪刀", game->local_choice == RPS_SCISSORS);
    rps_choice_card(screen, 166, &s_rps_paper, "布", game->local_choice == RPS_PAPER);
    ui_common_label(screen, game->status, 12, 137, 216, LV_TEXT_ALIGN_CENTER, false);
    if (game->state == GAME_STATE_WAITING_CHOICE) {
        ui_common_label_small(screen, "上键石头  中键剪刀  下键布", 6, 178, 228, LV_TEXT_ALIGN_CENTER);
    } else if (game->state == GAME_STATE_INVITE_RECEIVED) {
        ui_common_label(screen, "上键拒绝 中键接受", 20, 178, 200, LV_TEXT_ALIGN_CENTER, false);
    } else if (game->state == GAME_STATE_RESULT) {
        ui_common_label_small(screen, "纯娱乐 不增减积分", 12, 178, 216, LV_TEXT_ALIGN_CENTER);
        ui_common_label(screen, "中键返回互动游戏", 12, 203, 216, LV_TEXT_ALIGN_CENTER, false);
    } else {
        char left[40]; snprintf(left, sizeof(left), "剩余 %lu 秒", (unsigned long)game->seconds_left);
        ui_common_label(screen, left, 20, 178, 200, LV_TEXT_ALIGN_CENTER, false);
    }
    ui_common_footer(screen, "本地娱乐对战", false);
    return screen;
}
