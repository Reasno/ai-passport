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
extern const uint8_t mole_logo_data[] asm("_binary_mole_54x72_rgb565_start");

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
RPS_IMAGE(s_mole_logo, mole_logo_data);

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
                      const lv_image_dsc_t *art, const char *title, const char *detail)
{
    lv_obj_t *card = ui_common_card(screen, 12, y, 216, 52, selected, enabled);
    lv_obj_t *image;
    if (art) {
        image = lv_image_create(card);
        lv_image_set_src(image, art);
        lv_image_set_scale(image, 128);
        lv_image_set_antialias(image, false);
    } else {
        image = icon == UI_PIXEL_ICON_RADAR
                    ? ui_radar_icon_create(card, 0, 0, enabled ? KP_THEME : KP_MUTED, 3)
                : icon == UI_PIXEL_ICON_BUZZER
                    ? ui_buzzer_icon_create(card, 0, 0, enabled, 3)
                    : ui_pixel_icon_create(card, icon, 0, 0, enabled ? KP_THEME : KP_MUTED, 3);
    }
    if (image) lv_obj_align(image, LV_ALIGN_LEFT_MID, 14, 0);
    ui_common_label(card, title, 56, 2, 148, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_t *d = ui_common_label_small(card, detail, 56, 28, 148, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(d, lv_color_hex(enabled ? KP_MUTED_LIGHT : KP_MUTED), 0);
}

lv_obj_t *ui_games_build(const app_model_snapshot_t *model, const game_snapshot_t *game, int selected)
{
    lv_obj_t *screen = ui_common_screen("互动游戏", model);
    const char *pair = game->paired ? "已配对 长按B3重配" : "未配对 长按B3配对";
    ui_common_label_small(screen, pair, 12, 23, 216, LV_TEXT_ALIGN_CENTER);
    /* Ring works over either transport, so the entry only needs one of them alive. */
    bool find_ready = game->paired || model->mqtt_online;
    const char *find_detail = game->paired ? "响铃和近距离信号"
                              : model->mqtt_online ? "仅WiFi响铃 无测距" : "请先完成配对";
    game_card(screen, 39, selected == 0, game_service_heap_allows_radar() && find_ready,
              UI_PIXEL_ICON_RADAR, NULL, "找" KP_PEER_LABEL, find_detail);
    game_card(screen, 93, selected == 1, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_RPS, NULL, "石头剪刀布", game->paired ? "纯娱乐 不增减积分" : "请先完成配对");
    game_card(screen, 147, selected == 2, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_BUZZER, NULL, "抢答器", game->paired ? "B3 抢答 纯娱乐" : "请先完成配对");
    game_card(screen, 201, selected == 3, game_service_heap_allows_rps() && game->paired,
              UI_PIXEL_ICON_RPS, &s_mole_logo, "打地鼠", game->paired ? "合作瞄准并换弹" : "请先完成配对");
    char heap[48]; snprintf(heap, sizeof(heap), "可用内存 %lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_obj_t *h = ui_common_label_small(screen, heap, 12, 258, 216, LV_TEXT_ALIGN_CENTER);
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

static lv_obj_t *mole_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *hits;
    lv_obj_t *timer;
    lv_obj_t *role;
    lv_obj_t *ammo;
    lv_obj_t *mole;
    lv_obj_t *reticle;
    lv_obj_t *phase_box;
    lv_obj_t *phase_title;
    lv_obj_t *phase_detail;
    lv_obj_t *footer;
    mole_game_snapshot_t shown;
    bool has_shown;
} mole_ui_cache_t;

static mole_ui_cache_t s_mole_ui;

static lv_obj_t *mole_group(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void mole_create_grid(lv_obj_t *screen)
{
    lv_obj_t *grid = mole_rect(screen, 21, 55, 198, 198, 0x16283A);
    lv_obj_set_style_border_width(grid, 3, 0);
    lv_obj_set_style_border_color(grid, lv_color_hex(KP_MUTED_LIGHT), 0);
    mole_rect(grid, 65, 0, 2, 198, 0x526075);
    mole_rect(grid, 131, 0, 2, 198, 0x526075);
    mole_rect(grid, 0, 65, 198, 2, 0x526075);
    mole_rect(grid, 0, 131, 198, 2, 0x526075);

    s_mole_ui.mole = mole_group(grid, 40, 41);
    lv_obj_t *body = mole_rect(s_mole_ui.mole, 0, 6, 40, 35, 0xA96F45);
    lv_obj_set_style_radius(body, 18, 0);
    lv_obj_t *ear_l = mole_rect(s_mole_ui.mole, 4, 0, 10, 10, 0xC78A58);
    lv_obj_t *ear_r = mole_rect(s_mole_ui.mole, 26, 0, 10, 10, 0xC78A58);
    lv_obj_set_style_radius(ear_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(ear_r, LV_RADIUS_CIRCLE, 0);
    mole_rect(s_mole_ui.mole, 10, 16, 4, 4, 0x15202E);
    mole_rect(s_mole_ui.mole, 26, 16, 4, 4, 0x15202E);
    lv_obj_t *nose = mole_rect(s_mole_ui.mole, 17, 24, 6, 5, 0xFF9AA8);
    lv_obj_set_style_radius(nose, 2, 0);

    s_mole_ui.reticle = mole_group(grid, 52, 52);
    uint32_t rc = 0xFFD84A;
    mole_rect(s_mole_ui.reticle, 0, 0, 16, 3, rc); mole_rect(s_mole_ui.reticle, 0, 0, 3, 16, rc);
    mole_rect(s_mole_ui.reticle, 36, 0, 16, 3, rc); mole_rect(s_mole_ui.reticle, 49, 0, 3, 16, rc);
    mole_rect(s_mole_ui.reticle, 0, 49, 16, 3, rc); mole_rect(s_mole_ui.reticle, 0, 36, 3, 16, rc);
    mole_rect(s_mole_ui.reticle, 36, 49, 16, 3, rc); mole_rect(s_mole_ui.reticle, 49, 36, 3, 16, rc);
    mole_rect(s_mole_ui.reticle, 24, 24, 4, 4, rc);
}

static void mole_set_label(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void ui_mole_update(const mole_game_snapshot_t *game)
{
    if (!game || !s_mole_ui.screen) return;
    const mole_game_snapshot_t *old = &s_mole_ui.shown;
    bool first = !s_mole_ui.has_shown;
    char text[128];

    if (first || old->hits != game->hits) {
        snprintf(text, sizeof(text), "命中 %u/5", game->hits);
        mole_set_label(s_mole_ui.hits, text);
    }
    if (first || old->remaining_ds != game->remaining_ds) {
        snprintf(text, sizeof(text), "%u.%us", game->remaining_ds / 10, game->remaining_ds % 10);
        mole_set_label(s_mole_ui.timer, text);
    }
    if (first || old->is_host != game->is_host)
        mole_set_label(s_mole_ui.role, game->is_host ? "上下瞄准并开枪" : "左右瞄准并换弹");
    if (first || old->ammo_loaded != game->ammo_loaded) {
        mole_set_label(s_mole_ui.ammo, game->ammo_loaded ? "弹夹已装" : "待换弹");
        lv_obj_set_style_text_color(s_mole_ui.ammo,
                                   lv_color_hex(game->ammo_loaded ? KP_GREEN : KP_RED), 0);
    }
    if (first || old->mole_cell != game->mole_cell)
        lv_obj_set_pos(s_mole_ui.mole, (game->mole_cell % 3) * 66 + 13,
                       (game->mole_cell / 3) * 66 + 14);
    if (first || old->reticle_cell != game->reticle_cell)
        lv_obj_set_pos(s_mole_ui.reticle, (game->reticle_cell % 3) * 66 + 7,
                       (game->reticle_cell / 3) * 66 + 7);

    if (first || old->phase != game->phase || old->result != game->result ||
        old->remaining_ds / 10 != game->remaining_ds / 10 ||
        strcmp(old->status, game->status) != 0) {
        const char *title = "";
        const char *detail = "";
        const char *footer = game->is_host ? "B1上 B2下 B3开枪" : "B1左 B2右 B3换弹";
        uint32_t border = KP_YELLOW;
        if (game->phase == MOLE_PHASE_IDLE) {
            title = game->is_host ? "上下瞄准并开枪" : "左右瞄准并换弹";
            snprintf(text, sizeof(text), "%s  %lu胜 %lu负", game->status,
                     (unsigned long)game->wins, (unsigned long)game->losses);
            detail = text;
            footer = game->is_host ? "B3开始  长按B1主页" : "等待邀请  长按B1主页";
        } else if (game->phase == MOLE_PHASE_COUNTDOWN) {
            static char countdown[24];
            snprintf(countdown, sizeof(countdown), "准备 %u", (game->remaining_ds + 9) / 10);
            title = countdown;
            detail = "30秒内合作击中5只";
            footer = "准备开始  长按B1主页";
        } else if (game->phase == MOLE_PHASE_RESULT) {
            title = game->result == MOLE_RESULT_WIN ? "胜利！" :
                    game->result == MOLE_RESULT_LOSE ? "挑战失败" : "本局已中止";
            detail = game->status;
            border = game->result == MOLE_RESULT_WIN ? KP_GREEN :
                     game->result == MOLE_RESULT_LOSE ? KP_RED : KP_YELLOW;
            footer = game->is_host ? "B3再来一局 长按B1主页" : "等待对方 长按B1主页";
        }
        if (game->phase == MOLE_PHASE_PLAYING) lv_obj_add_flag(s_mole_ui.phase_box, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(s_mole_ui.phase_box, LV_OBJ_FLAG_HIDDEN);
            mole_set_label(s_mole_ui.phase_title, title);
            mole_set_label(s_mole_ui.phase_detail, detail);
            lv_obj_set_style_border_color(s_mole_ui.phase_box, lv_color_hex(border), 0);
        }
        mole_set_label(s_mole_ui.footer, footer);
    }
    s_mole_ui.shown = *game;
    s_mole_ui.has_shown = true;
}

lv_obj_t *ui_mole_build(const app_model_snapshot_t *model, const mole_game_snapshot_t *game)
{
    memset(&s_mole_ui, 0, sizeof(s_mole_ui));
    s_mole_ui.screen = ui_common_screen("打地鼠", model);
    s_mole_ui.hits = ui_common_label_small(s_mole_ui.screen, "", 12, 25, 108, LV_TEXT_ALIGN_LEFT);
    s_mole_ui.timer = ui_common_label_small(s_mole_ui.screen, "", 120, 25, 108, LV_TEXT_ALIGN_RIGHT);
    mole_create_grid(s_mole_ui.screen);
    s_mole_ui.role = ui_common_label_small(s_mole_ui.screen, "", 12, 261, 112, LV_TEXT_ALIGN_LEFT);
    s_mole_ui.ammo = ui_common_label_small(s_mole_ui.screen, "", 124, 261, 104, LV_TEXT_ALIGN_RIGHT);
    s_mole_ui.phase_box = ui_common_card(s_mole_ui.screen, 36, 105, 168, 98, false, true);
    lv_obj_set_style_border_width(s_mole_ui.phase_box, 3, 0);
    s_mole_ui.phase_title = ui_common_label(s_mole_ui.phase_box, "", 4, 12, 160, LV_TEXT_ALIGN_CENTER, true);
    s_mole_ui.phase_detail = ui_common_label_small(s_mole_ui.phase_box, "", 4, 54, 160, LV_TEXT_ALIGN_CENTER);
    ui_common_footer(s_mole_ui.screen, "", false);
    lv_obj_t *footer_bar = lv_obj_get_child(s_mole_ui.screen, -1);
    s_mole_ui.footer = footer_bar ? lv_obj_get_child(footer_bar, 0) : NULL;
    ui_mole_update(game);
    return s_mole_ui.screen;
}

void ui_mole_forget(void)
{
    memset(&s_mole_ui, 0, sizeof(s_mole_ui));
}
