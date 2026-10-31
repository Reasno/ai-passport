#include "ui_race.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>

/* Track canvas: 216 x 186 at (12, 46). Lanes are 72 px wide; the local car stays
 * fixed near the bottom and the road scrolls 1 px per 0.4 m, showing ~50 m ahead. */
#define TRACK_X 12
#define TRACK_Y 46
#define TRACK_W 216
#define TRACK_H 186
#define LANE_W 72
#define CAR_Y 132
#define MM_PER_PX 400
#define DASH_PERIOD 40
#define KERB_PERIOD 24
#ifdef CONFIG_KIDS_THEME_SISTER
#define PEER_COLOR 0x4B9EFF
#else
#define PEER_COLOR 0xF06AA8
#endif

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *track;
    lv_obj_t *speed;
    lv_obj_t *gap;
    lv_obj_t *remain;
    lv_obj_t *progress;
    lv_obj_t *dot_self;
    lv_obj_t *dot_peer;
    lv_obj_t *drs_badge;
    lv_obj_t *drs_label;
    lv_obj_t *drs_hint;
    lv_obj_t *panel;
    lv_obj_t *panel_title;
    lv_obj_t *panel_detail;
    lv_obj_t *footer;
    race_game_snapshot_t shown;
    bool has_shown;
} race_ui_t;

static race_ui_t s_ui;
/* Snapshot used by the draw callback; kept static so the LVGL task never touches a stack copy. */
static race_game_snapshot_t s_draw;

static void fill(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color, lv_opa_t opa)
{
    /* The track is rendered in 20-row bands; skip rectangles outside the current band. */
    const lv_area_t *clip = &layer->_clip_area;
    if (x2 < clip->x1 || x1 > clip->x2 || y2 < clip->y1 || y1 > clip->y2) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opa;
    lv_area_t area = {x1, y1, x2, y2};
    lv_draw_rect(layer, &dsc, &area);
}

static int32_t lane_center(int32_t ox, uint8_t lane)
{
    return ox + LANE_W / 2 + (int32_t)(lane > 2 ? 2 : lane) * LANE_W;
}

static void draw_car(lv_layer_t *layer, int32_t cx, int32_t top, uint32_t color, lv_opa_t opa, bool exhaust)
{
    fill(layer, cx - 11, top, cx + 10, top + 3, color, opa);
    fill(layer, cx - 5, top + 4, cx + 4, top + 35, color, opa);
    fill(layer, cx - 9, top + 14, cx + 8, top + 25, color, opa);
    fill(layer, cx - 12, top + 33, cx + 11, top + 37, color, opa);
    fill(layer, cx - 2, top + 10, cx + 1, top + 15, 0xD6E8FF, opa);
    fill(layer, cx - 15, top + 4, cx - 11, top + 12, 0x0A0F18, opa);
    fill(layer, cx + 10, top + 4, cx + 14, top + 12, 0x0A0F18, opa);
    fill(layer, cx - 15, top + 24, cx - 11, top + 32, 0x0A0F18, opa);
    fill(layer, cx + 10, top + 24, cx + 14, top + 32, 0x0A0F18, opa);
    if (exhaust) {
        fill(layer, cx - 3, top + 38, cx + 2, top + 46, KP_YELLOW, opa);
        fill(layer, cx - 2, top + 47, cx + 1, top + 53, KP_YELLOW, LV_OPA_40);
    }
}

static void draw_item(lv_layer_t *layer, int32_t cx, int32_t y, race_item_t item)
{
    if (item == RACE_ITEM_NITRO) {
        fill(layer, cx - 4, y - 14, cx + 3, y - 11, KP_MUTED_LIGHT, LV_OPA_COVER);
        fill(layer, cx - 8, y - 10, cx + 7, y + 13, KP_GREEN, LV_OPA_COVER);
        fill(layer, cx - 8, y - 2, cx + 7, y + 3, KP_TEXT, LV_OPA_COVER);
    } else if (item == RACE_ITEM_BUMP) {
        fill(layer, cx - 26, y - 5, cx + 25, y + 4, KP_YELLOW, LV_OPA_COVER);
        for (int32_t x = cx - 22; x < cx + 25; x += 13) fill(layer, x, y - 5, x + 5, y + 4, 0x0D1B2A, LV_OPA_COVER);
    }
}

static void track_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    const int32_t ox = box.x1, oy = box.y1;
    const race_game_snapshot_t *s = &s_draw;
    const uint32_t self_mm = s->pos_mm[0];
    const int32_t scroll = (int32_t)(self_mm / MM_PER_PX);

    fill(layer, ox, oy, ox + TRACK_W - 1, oy + TRACK_H - 1, 0x2A3240, LV_OPA_COVER);
    int32_t kerb_shift = scroll % KERB_PERIOD;
    for (int32_t y = -KERB_PERIOD + kerb_shift; y < TRACK_H; y += KERB_PERIOD) {
        int32_t y1 = oy + (y < 0 ? 0 : y), y2 = oy + (y + KERB_PERIOD / 2 > TRACK_H ? TRACK_H : y + KERB_PERIOD / 2) - 1;
        if (y2 < y1) continue;
        fill(layer, ox, y1, ox + 3, y2, KP_RED, LV_OPA_COVER);
        fill(layer, ox + TRACK_W - 4, y1, ox + TRACK_W - 1, y2, KP_RED, LV_OPA_COVER);
    }
    int32_t dash_shift = scroll % DASH_PERIOD;
    for (int lane = 1; lane < RACE_LANES; ++lane) {
        int32_t x = ox + lane * LANE_W - 1;
        for (int32_t y = -DASH_PERIOD + dash_shift; y < TRACK_H; y += DASH_PERIOD) {
            int32_t y1 = oy + (y < 0 ? 0 : y), y2 = oy + (y + 20 > TRACK_H ? TRACK_H : y + 20) - 1;
            if (y2 >= y1) fill(layer, x, y1, x + 1, y2, KP_MUTED_LIGHT, LV_OPA_COVER);
        }
    }
    int32_t finish_y = CAR_Y - (int32_t)(RACE_TRACK_MM / MM_PER_PX) + scroll;
    if (finish_y > -8 && finish_y < TRACK_H) {
        for (int32_t x = 0; x < TRACK_W; x += 8) {
            fill(layer, ox + x, oy + finish_y, ox + x + 3, oy + finish_y + 3, KP_TEXT, LV_OPA_COVER);
            fill(layer, ox + x + 4, oy + finish_y + 4, ox + x + 7, oy + finish_y + 7, KP_TEXT, LV_OPA_COVER);
        }
    }
    for (int row = 0; row < RACE_ROWS; ++row) {
        int32_t y = CAR_Y - ((int32_t)race_row_mm(row) - (int32_t)self_mm) / MM_PER_PX;
        if (y < -20 || y > TRACK_H + 20) continue;
        for (int lane = 0; lane < RACE_LANES; ++lane) {
            race_item_t item = (race_item_t)((s->rows[row] >> (lane * 2)) & 0x03U);
            if (item != RACE_ITEM_NONE) draw_item(layer, lane_center(ox, (uint8_t)lane), oy + y, item);
        }
    }
    int32_t peer_y = CAR_Y - ((int32_t)s->pos_mm[1] - (int32_t)self_mm) / MM_PER_PX;
    if (peer_y > -38 && peer_y < TRACK_H) {
        draw_car(layer, lane_center(ox, s->lane[1]), oy + peer_y, PEER_COLOR, LV_OPA_50, false);
    } else {
        int32_t edge = peer_y <= -38 ? oy + 2 : oy + TRACK_H - 6;
        int32_t cx = lane_center(ox, s->lane[1]);
        fill(layer, cx - 8, edge, cx + 7, edge + 3, PEER_COLOR, LV_OPA_COVER);
    }
    bool boosted = s->effect[0] == RACE_EFFECT_NITRO || s->drs[0] == RACE_DRS_ACTIVE;
    draw_car(layer, lane_center(ox, s->lane[0]), oy + CAR_Y, KP_THEME, LV_OPA_COVER, boosted);
}

static void set_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static unsigned speed_kmh(const race_game_snapshot_t *s)
{
    if (s->phase != RACE_PHASE_RACING) return 0;
    return RACE_BASE_KMH * race_speed_percent((race_effect_t)s->effect[0], s->drs[0] == RACE_DRS_ACTIVE) / 100U;
}

static void update_drs(const race_game_snapshot_t *s)
{
    uint8_t drs = s->drs[0];
    bool racing = s->phase == RACE_PHASE_RACING;
    uint32_t bg = KP_CARD, fg = KP_MUTED;
    const char *hint = "落后 5 m 以上可用";
    if (racing && s->drs_pending) { bg = KP_YELLOW; fg = KP_BG; hint = "DRS 申请中..."; }
    else if (racing && drs == RACE_DRS_AVAILABLE) { bg = KP_GREEN; fg = KP_BG; hint = "可用  按B3冲刺"; }
    else if (racing && drs == RACE_DRS_ACTIVE)
        { bg = KP_YELLOW; fg = KP_BG; hint = s->effect[0] == RACE_EFFECT_NITRO ? "冲刺中 + 氮气" : "冲刺中"; }
    else if (drs == RACE_DRS_USED) hint = "本局已用完";
    lv_obj_set_style_bg_color(s_ui.drs_badge, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(s_ui.drs_label, lv_color_hex(fg), 0);
    set_text(s_ui.drs_hint, hint);
    lv_obj_set_style_text_color(s_ui.drs_hint, lv_color_hex(bg == KP_CARD ? KP_MUTED : bg), 0);
}

static void update_panel(const race_game_snapshot_t *s)
{
    char detail[64];
    const char *title = "", *body = s->status, *footer = "B1左  B2右  B3 DRS  长按B1退出";
    uint32_t border = KP_YELLOW;
    switch (s->phase) {
    case RACE_PHASE_IDLE:
        title = "极速赛车";
        snprintf(detail, sizeof(detail), "战绩 %lu胜 %lu负", (unsigned long)s->wins, (unsigned long)s->losses);
        body = detail;
        footer = "B3邀请对手  长按B1退出";
        break;
    case RACE_PHASE_INVITE_SENT:
        title = "已发送邀请";
        body = "等待对手接受...";
        footer = "等待中  长按B1退出";
        break;
    case RACE_PHASE_INVITE_RECEIVED:
        title = "收到赛车邀请";
        body = "B3接受  B2拒绝";
        footer = "B2拒绝  B3接受  长按B1退出";
        break;
    case RACE_PHASE_COUNTDOWN:
        if (s->countdown_s > 0) snprintf(detail, sizeof(detail), "红灯 %u", s->countdown_s);
        else strlcpy(detail, "出发！", sizeof(detail));
        title = detail;
        body = "抢氮气  躲减速带";
        footer = "准备出发  长按B1退出";
        border = KP_RED;
        break;
    case RACE_PHASE_RESULT:
        title = s->result == RACE_RESULT_WIN ? "你赢了" : s->result == RACE_RESULT_LOSE ? "惜败" :
                s->result == RACE_RESULT_DRAW ? "平局" : "比赛中止";
        border = s->result == RACE_RESULT_WIN ? KP_GREEN : s->result == RACE_RESULT_LOSE ? KP_RED : KP_YELLOW;
        footer = "B3再来一局  长按B1退出";
        break;
    case RACE_PHASE_RACING:
        break;
    }
    if (s->phase == RACE_PHASE_RACING) {
        lv_obj_add_flag(s_ui.panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ui.panel, LV_OBJ_FLAG_HIDDEN);
        set_text(s_ui.panel_title, title);
        set_text(s_ui.panel_detail, body);
        lv_obj_set_style_border_color(s_ui.panel, lv_color_hex(border), 0);
    }
    set_text(s_ui.footer, footer);
}

void ui_race_update(const race_game_snapshot_t *s)
{
    if (!s || !s_ui.screen) return;
    const race_game_snapshot_t *old = &s_ui.shown;
    bool first = !s_ui.has_shown;
    char text[48];

    unsigned kmh = speed_kmh(s);
    if (first || kmh != speed_kmh(old)) {
        snprintf(text, sizeof(text), "%u km/h", kmh);
        set_text(s_ui.speed, text);
    }
    uint32_t self_m = s->pos_mm[0] / 1000U, peer_m = s->pos_mm[1] / 1000U;
    if (first || self_m != old->pos_mm[0] / 1000U || peer_m != old->pos_mm[1] / 1000U || s->phase != old->phase) {
        uint32_t left = self_m >= RACE_TRACK_MM / 1000U ? 0 : RACE_TRACK_MM / 1000U - self_m;
        snprintf(text, sizeof(text), "距终点 %lu m", (unsigned long)left);
        set_text(s_ui.remain, text);
        if (peer_m > self_m) {
            snprintf(text, sizeof(text), "落后 %lu m", (unsigned long)(peer_m - self_m));
            lv_obj_set_style_text_color(s_ui.gap, lv_color_hex(KP_RED), 0);
        } else if (self_m > peer_m) {
            snprintf(text, sizeof(text), "领先 %lu m", (unsigned long)(self_m - peer_m));
            lv_obj_set_style_text_color(s_ui.gap, lv_color_hex(KP_GREEN), 0);
        } else {
            strlcpy(text, "并驾齐驱", sizeof(text));
            lv_obj_set_style_text_color(s_ui.gap, lv_color_hex(KP_MUTED_LIGHT), 0);
        }
        set_text(s_ui.gap, text);
        lv_obj_set_x(s_ui.dot_self, (int32_t)(s->pos_mm[0] / (RACE_TRACK_MM / 196U)));
        lv_obj_set_x(s_ui.dot_peer, (int32_t)(s->pos_mm[1] / (RACE_TRACK_MM / 196U)));
    }
    if (first || s->drs[0] != old->drs[0] || s->drs_pending != old->drs_pending || s->phase != old->phase ||
        s->effect[0] != old->effect[0])
        update_drs(s);
    if (first || s->phase != old->phase || s->result != old->result || s->countdown_s != old->countdown_s ||
        s->wins != old->wins || s->losses != old->losses || strcmp(s->status, old->status) != 0)
        update_panel(s);
    if (first || memcmp(s->pos_mm, old->pos_mm, sizeof(s->pos_mm)) != 0 ||
        memcmp(s->lane, old->lane, sizeof(s->lane)) != 0 || memcmp(s->rows, old->rows, sizeof(s->rows)) != 0 ||
        s->effect[0] != old->effect[0] || s->drs[0] != old->drs[0]) {
        s_draw = *s;
        lv_obj_invalidate(s_ui.track);
    }
    s_ui.shown = *s;
    s_ui.has_shown = true;
}

static lv_obj_t *plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *ui_race_build(const app_model_snapshot_t *model, const race_game_snapshot_t *game)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.screen = ui_common_screen("极速赛车", model);
    s_ui.remain = ui_common_label_small(s_ui.screen, "", 12, 21, 110, LV_TEXT_ALIGN_LEFT);
    s_ui.speed = ui_common_label_small(s_ui.screen, "", 124, 21, 104, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_text_color(s_ui.speed, lv_color_hex(KP_MUTED_LIGHT), 0);
    s_ui.progress = plain(s_ui.screen, 14, 40, 204, 4, KP_CARD, 2);
    plain(s_ui.screen, 220, 37, 6, 10, KP_TEXT, 0);
    s_ui.dot_peer = plain(s_ui.progress, 0, -2, 8, 8, PEER_COLOR, LV_RADIUS_CIRCLE);
    s_ui.dot_self = plain(s_ui.progress, 0, -2, 8, 8, KP_THEME, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s_ui.progress, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_ui.track = lv_obj_create(s_ui.screen);
    lv_obj_set_pos(s_ui.track, TRACK_X, TRACK_Y);
    lv_obj_set_size(s_ui.track, TRACK_W, TRACK_H);
    lv_obj_set_style_pad_all(s_ui.track, 0, 0);
    lv_obj_set_style_border_width(s_ui.track, 0, 0);
    lv_obj_set_style_radius(s_ui.track, 0, 0);
    lv_obj_set_style_bg_opa(s_ui.track, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_ui.track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.track, track_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_ui.drs_badge = plain(s_ui.screen, 12, 238, 44, 20, KP_CARD, 10);
    s_ui.drs_label = ui_common_label_small(s_ui.drs_badge, "DRS", 0, 1, 44, LV_TEXT_ALIGN_CENTER);
    s_ui.drs_hint = ui_common_label_small(s_ui.screen, "", 62, 239, 166, LV_TEXT_ALIGN_LEFT);
    s_ui.gap = ui_common_label_small(s_ui.screen, "", 12, 262, 216, LV_TEXT_ALIGN_CENTER);

    s_ui.panel = ui_common_card(s_ui.screen, 30, 92, 180, 96, false, true);
    lv_obj_set_style_border_width(s_ui.panel, 3, 0);
    s_ui.panel_title = ui_common_label(s_ui.panel, "", 4, 12, 172, LV_TEXT_ALIGN_CENTER, true);
    s_ui.panel_detail = ui_common_label_small(s_ui.panel, "", 4, 54, 172, LV_TEXT_ALIGN_CENTER);
    ui_common_footer(s_ui.screen, "", false);
    lv_obj_t *footer_bar = lv_obj_get_child(s_ui.screen, -1);
    s_ui.footer = footer_bar ? lv_obj_get_child(footer_bar, 0) : NULL;
    ui_race_update(game);
    return s_ui.screen;
}

void ui_race_forget(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
}
