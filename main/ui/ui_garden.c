#include "ui_garden.h"
#include "espnow_service.h"
#include "garden_assets.h"
#include "garden_service.h"
#include "sound_service.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef enum {
    GP_HOME,
    GP_SOW,
    GP_CARE,
    GP_USE,
    GP_BAG,
    GP_GIFT_CONFIRM,
    GP_GIFT_STATUS,
    GP_HARVEST,
    GP_ALBUM,
    GP_BOOK,
} garden_page_t;

#define ROW_X 12
#define ROW_W 216
#define WATER_FRAME_MS 150

static const char *const CROP_NAMES[GARDEN_CROPS] = {"樱桃萝卜", "散叶生菜", "甜罗勒", "矮生菜豆", "番茄", "迷迭香"};
static const char *const STAGE_NAMES[4] = {"种子", "幼苗", "成长", "成熟"};
static const char *const MATERIAL_NAMES[GARDEN_MATERIALS] = {"肥料", "除草剂", "除虫剂"};
static const garden_icon_t MATERIAL_ICONS[GARDEN_MATERIALS] = {GARDEN_ICON_FERTILIZER, GARDEN_ICON_HERBICIDE,
                                                               GARDEN_ICON_INSECTICIDE};
static const char *const CARE_TIPS[GARDEN_CROPS] = {
    "常来浇水，不用施肥。小苗容易有虫。",
    "常来浇水，长大时记得施肥。虫子少，留意杂草。",
    "常来浇水，长大时记得施肥。留意杂草和虫子。",
    "越大越爱喝水，要勤浇水。不用施肥，常要除草除虫。",
    "长大后要勤浇水，也要施肥。杂草少，留意虫子。",
    "越大越省水，不用施肥。小苗容易长草，记得除草。",
};
static const char *const BOOK_HEADS[4] = {"认识它", "水与阳光", "土壤与照顾", "游戏里怎么养"};
static const char *const FACTS[GARDEN_CROPS][4] = {
    {"樱桃萝卜是小型根菜，常见品种圆圆的、红皮白肉。适宜条件下，播种后约3–5周就能采收。",
     "喜欢凉爽天气和阳光。土壤保持适度湿润，水分忽干忽湿容易让萝卜开裂。",
     "土壤疏松，萝卜才容易长好。幼苗叶子上的小圆孔，可能是跳甲啃出来的。",
     "常规种植：各阶段5小时干燥，只计白天。每轮至少补水一次，不额外施肥。"},
    {"散叶生菜的叶片向外舒展，常采收鲜嫩叶片。先摘外层叶、留下中心小叶，就能继续长新叶。",
     "喜欢凉爽、光线充足的环境。要稳定供水；天气太热时，容易长出花茎，叶片也会变苦。",
     "土壤要保水又能排水。除草时浅浅松土，避免伤到靠近地面的细根。",
     "常规种植：各阶段5小时干燥，只计白天。成长开始施肥1次。"},
    {"甜罗勒是一年生芳香草本，叶片带有香气。常采嫩叶调味，也能做成青酱。",
     "喜欢温暖和充足阳光，怕霜冻。土壤保持适度湿润，盆底要能排水。",
     "适量施肥，定期剪去嫩枝顶端。在一对叶片上方剪枝，有助于长出更多侧枝。",
     "常规种植：各阶段5小时干燥，只计白天。每轮至少补水一次，成长开始施肥1次，虫草均需留意。"},
    {"矮生菜豆株型紧凑，通常不用搭架。开花后长出豆荚，荚内一粒粒的小豆子是种子。",
     "喜欢温暖和阳光，土壤要能排水。生长、开花和结荚时，都需要适当的水分。",
     "根部可与根瘤菌合作，帮助获得氮养分。除草时浅浅松土，保护表层细根。",
     "常规种植：种子5小时、幼苗3小时、成长2小时干燥，只计白天。结荚时耗水最快。"},
    {"番茄先开花，再结出多汁的果实。果实成熟后可以采来食用，里面有许多小种子。",
     "喜欢温暖和充足阳光。浇水要均匀，让水深入根部；土壤长期积水会影响生长。",
     "适量补充养分，施肥过多会使枝叶过旺。支架能托住枝条，让果实离开地面。",
     "常规种植：种子5小时、幼苗3小时、成长2小时干燥，只计白天。成长开始施肥1次。"},
    {"迷迭香是常绿小灌木，叶片细长，带有香气。它能生长多年，嫩枝和叶片常用来调味。",
     "喜欢阳光和排水好的土。根系健壮后较耐旱，小苗和盆栽在干燥时仍需补水。",
     "嫩枝可以用来扦插，长出新植株。定期轻剪嫩枝，有助于长出更多分枝。",
     "常规种植：种子5小时、幼苗7小时、成长15小时干燥，只计白天。幼苗重点除草，越长越耐旱。"},
};

static garden_page_t s_page;
static int s_focus;
static garden_material_t s_material;
static garden_harvest_t s_harvest;
static int s_book_crop, s_book_page;
static bool s_water_anim;
/* Large snapshots stay out of the 8 KB UI task stack. */
static garden_view_t s_view;

/* One decoded 48x48 RGB565A8 sprite shared by every garden screen. Only one screen
 * exists at a time and every rebuild happens under the LVGL lock. */
static uint8_t s_sprite_buf[GARDEN_ART_SIZE * GARDEN_ART_SIZE * 3];
static const lv_image_dsc_t s_sprite = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565A8,
    .header.w = GARDEN_ART_SIZE,
    .header.h = GARDEN_ART_SIZE,
    .header.stride = GARDEN_ART_SIZE * 2,
    .data_size = sizeof(s_sprite_buf),
    .data = s_sprite_buf,
};
static lv_obj_t *s_water_img;
static lv_timer_t *s_water_timer;
static int s_water_frame;

static void decode_rle(const garden_rle_t *rle, int x0, int y0, int w)
{
    uint16_t *rgb = (uint16_t *)s_sprite_buf;
    uint8_t *alpha = s_sprite_buf + GARDEN_ART_SIZE * GARDEN_ART_SIZE * 2;
    int i = 0;
    for (size_t b = 0; b < rle->size; ++b) {
        int index = rle->data[b] & 0x1F;
        int run = (rle->data[b] >> 5) + 1;
        for (; run > 0; --run, ++i) {
            if (!index) continue;
            int x = x0 + i % w, y = y0 + i / w;
            if (x < 0 || y < 0 || x >= GARDEN_ART_SIZE || y >= GARDEN_ART_SIZE) continue;
            rgb[y * GARDEN_ART_SIZE + x] = garden_palette[index];
            alpha[y * GARDEN_ART_SIZE + x] = 0xFF;
        }
    }
}

static const lv_image_dsc_t *ui_garden_sprite(int crop, int stage, int variant, bool weed, bool pest, bool moon)
{
    memset(s_sprite_buf, 0, sizeof(s_sprite_buf));
    decode_rle(&garden_sprites[crop][stage][variant], 0, 0, GARDEN_ART_SIZE);
    if (weed) decode_rle(&garden_overlays[GARDEN_OVERLAY_WEED], 0, 28, GARDEN_ICON_SIZE);
    if (pest) decode_rle(&garden_overlays[GARDEN_OVERLAY_PEST], 32, 14, GARDEN_ICON_SIZE);
    if (moon) decode_rle(&garden_overlays[GARDEN_OVERLAY_MOON], 31, 1, GARDEN_ICON_SIZE);
    return &s_sprite;
}

const lv_image_dsc_t *ui_garden_menu_icon(void)
{
    return ui_garden_sprite(GARDEN_TOMATO, GARDEN_STAGE_MATURE, GARDEN_ART_HEALTHY, false, false, false);
}

static void refresh(void) { garden_service_snapshot(&s_view); }
static const garden_t *g(void) { return &s_view.garden; }

static lv_obj_t *image_at(lv_obj_t *parent, const lv_image_dsc_t *src, int cx, int cy, int scale)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_image_set_antialias(img, false);
    if (scale != 256) lv_image_set_scale(img, scale);
    /* Scaling pivots on the centre and keeps the source-size object bounds. */
    lv_obj_set_pos(img, cx - (int)src->header.w / 2, cy - (int)src->header.h / 2);
    return img;
}

static lv_obj_t *plant_image(lv_obj_t *parent, int cx, int cy)
{
    const garden_t *p = g();
    bool growing = garden_is_growing(p);
    int stage = p->planted ? p->stage : GARDEN_STAGE_SEED;
    int variant = growing && p->water == 0 ? GARDEN_ART_DRY
                  : growing && (p->flags & GARDEN_FLAG_FERT_OWED) ? GARDEN_ART_DEFICIENT
                                                                  : GARDEN_ART_HEALTHY;
    bool overlays = growing && stage != GARDEN_STAGE_SEED;
    const lv_image_dsc_t *src = ui_garden_sprite(p->crop, stage, variant, overlays && (p->flags & GARDEN_FLAG_WEED),
                                                 overlays && (p->flags & GARDEN_FLAG_PEST),
                                                 growing && s_view.clock_ok && !s_view.day);
    return image_at(parent, src, cx, cy, 512);
}

static void water_anim_deleted(lv_event_t *e)
{
    /* A rebuild may already own a newer animation; only the current one stops the timer. */
    if (lv_event_get_target(e) != s_water_img) return;
    if (s_water_timer) lv_timer_delete(s_water_timer);
    s_water_timer = NULL;
    s_water_img = NULL;
}
static void water_anim_step(lv_timer_t *timer)
{
    (void)timer;
    if (!s_water_img) return;
    if (++s_water_frame >= 4) {
        lv_obj_delete(s_water_img);
        return;
    }
    lv_image_set_src(s_water_img, &garden_icons[GARDEN_ICON_WATERING_0 + s_water_frame]);
}
/* Four watering frames over the plant; the state is already updated, keys stay live. */
static void start_water_anim(lv_obj_t *parent, int cx, int cy)
{
    s_water_anim = false;
    s_water_frame = 0;
    if (s_water_timer) lv_timer_delete(s_water_timer);
    s_water_img = image_at(parent, &garden_icons[GARDEN_ICON_WATERING_0], cx, cy, 512);
    lv_obj_add_event_cb(s_water_img, water_anim_deleted, LV_EVENT_DELETE, NULL);
    s_water_timer = lv_timer_create(water_anim_step, WATER_FRAME_MS, NULL);
}

static lv_obj_t *wrap_text(lv_obj_t *parent, const char *text, int x, int y, int w, const lv_font_t *font,
                           uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    char safe[256];
    strlcpy(safe, text ? text : "", sizeof(safe));
    ui_text_font_fallback(font, safe);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, safe);
    return label;
}
static lv_obj_t *small_line(lv_obj_t *parent, const char *text, int y, uint32_t color)
{
    lv_obj_t *label = ui_common_label_small(parent, text, ROW_X, y, ROW_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

/* One selectable row: optional 16 px icon shown at 2x, a label and a right-aligned detail. */
static void row(lv_obj_t *screen, int y, int h, bool selected, bool muted, int icon, const char *label,
                const char *detail)
{
    lv_obj_t *card = ui_common_card(screen, ROW_X, y, ROW_W, h, selected, true);
    int text_x = 10;
    if (icon >= 0) {
        lv_obj_t *img = image_at(card, &garden_icons[icon], 24, (h - 8) / 2, 512);
        if (muted) lv_obj_set_style_image_opa(img, LV_OPA_50, 0);
        text_x = 46;
    }
    lv_obj_t *l = ui_common_label(card, label, text_x, (h - 8 - 21) / 2, 112, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_color(l, lv_color_hex(muted ? KP_MUTED_LIGHT : KP_TEXT), 0);
    if (detail && detail[0]) {
        lv_obj_t *d = ui_common_label_small(card, detail, 96, (h - 8 - 17) / 2, ROW_W - 8 - 100, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_style_text_color(d, lv_color_hex(muted ? KP_MUTED : KP_MUTED_LIGHT), 0);
    }
}

static void speed_reasons(char *out, size_t size)
{
    out[0] = 0;
    const garden_t *p = g();
    if (p->flags & GARDEN_FLAG_FERT_OWED) strlcat(out, "缺肥", size);
    if (p->flags & GARDEN_FLAG_WEED) strlcat(out, out[0] ? "、杂草" : "杂草", size);
    if (p->flags & GARDEN_FLAG_PEST) strlcat(out, out[0] ? "、有虫" : "有虫", size);
}

/* The single most important status line, with its colour. */
static uint32_t status_line(char *out, size_t size, bool care_wording)
{
    const garden_t *p = g();
    if (!s_view.clock_ok) { strlcpy(out, "时间未就绪，请校准设备时间", size); return KP_RED; }
    if (!p->planted) { strlcpy(out, "种子准备好了，选择播种", size); return KP_MUTED_LIGHT; }
    if (p->stage == GARDEN_STAGE_MATURE) { strlcpy(out, "已成熟，随时可以收获", size); return KP_GREEN; }
    if (!s_view.day) { strlcpy(out, "太晚了 明天再来看吧", size); return KP_MUTED_LIGHT; }
    if (p->water == 0) {
        strlcpy(out, !care_wording ? "缺水暂停，浇水可恢复"
                     : p->stage == GARDEN_STAGE_SEED ? "土壤干燥，暂停生长" : "缺水枯萎，暂停生长", size);
        return KP_RED;
    }
    unsigned speed = garden_speed_percent(p);
    if (speed >= 100) { strlcpy(out, "生长正常", size); return KP_GREEN; }
    char reasons[48];
    speed_reasons(reasons, sizeof(reasons));
    snprintf(out, size, "%s %u%%：%s", speed >= 65 ? "变慢" : "很慢", speed, reasons);
    return KP_YELLOW;
}

static const char *water_word(unsigned pct) { return pct == 0 ? "缺水" : pct <= 30 ? "偏干" : "湿润"; }

static void hours_text(char *out, size_t size, uint32_t seconds)
{
    /* Rounded up to the next half hour so the estimate is never early. */
    unsigned halves = (unsigned)((seconds + 1799) / 1800);
    if (halves < 1) halves = 1;
    if (halves % 2) snprintf(out, size, "%u.5", halves / 2);
    else snprintf(out, size, "%u", halves / 2);
}

static bool care_needed(void)
{
    const garden_t *p = g();
    return garden_is_growing(p) && (garden_water_percent(p) <= 30 || garden_needs(p, GARDEN_MAT_FERTILIZER) ||
                                    garden_needs(p, GARDEN_MAT_HERBICIDE) || garden_needs(p, GARDEN_MAT_INSECTICIDE));
}

static lv_obj_t *build_home(const app_model_snapshot_t *model)
{
    const garden_t *p = g();
    lv_obj_t *screen = ui_common_screen("口袋花园", model);
    lv_obj_t *panel = ui_common_card(screen, ROW_X, 24, ROW_W, 178, false, true);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x15291F), 0);
    plant_image(screen, 120, 72);
    if (s_water_anim) start_water_anim(screen, 160, 44);
    char line[80];
    snprintf(line, sizeof(line), "%s  %s", CROP_NAMES[p->crop], p->planted ? STAGE_NAMES[p->stage] : "种子");
    lv_obj_t *name = ui_common_label(screen, line, ROW_X, 122, ROW_W, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_color(name, lv_color_hex(KP_THEME), 0);
    if (!p->planted) strlcpy(line, "尚未播种", sizeof(line));
    else if (p->stage == GARDEN_STAGE_MATURE) strlcpy(line, "已完成三个生长阶段", sizeof(line));
    else {
        unsigned water = garden_water_percent(p);
        snprintf(line, sizeof(line), "本阶段 %u%%  水分 %u%% %s",
                 (unsigned)(p->progress * 100U / GARDEN_STAGE_UNITS), water, water_word(water));
    }
    small_line(screen, line, 145, KP_MUTED_LIGHT);
    uint32_t color = status_line(line, sizeof(line), false);
    small_line(screen, line, 163, color);
    if (s_view.clock_ok && garden_is_growing(p) && p->water > 0) {
        char hours[12];
        hours_text(hours, sizeof(hours),
                   garden_seconds_to_mature_wall(p, (int64_t)time(NULL), GARDEN_UTC_OFFSET_S));
        snprintf(line, sizeof(line), "预计%s小时后成熟", hours);
        small_line(screen, line, 181, KP_MUTED);
    }
    const char *first = !p->planted ? "播种" : p->stage == GARDEN_STAGE_MATURE ? "收获" : "照料";
    const char *first_detail = !p->planted ? "种子 ×1" : p->stage == GARDEN_STAGE_MATURE ? "可领取"
                               : care_needed() ? "有需求" : "";
    const char *labels[] = {first, "背包", "图鉴", "返回游戏列表"};
    int start = s_focus > 2 ? s_focus - 2 : 0;
    for (int i = start; i < start + 3; ++i)
        row(screen, 206 + (i - start) * 28, 26, s_focus == i, false, -1,
            labels[i], i == 0 ? first_detail : "");
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_sow(const app_model_snapshot_t *model)
{
    const garden_t *p = g();
    lv_obj_t *screen = ui_common_screen("开始种植", model);
    image_at(screen, ui_garden_sprite(p->crop, GARDEN_STAGE_SEED, GARDEN_ART_HEALTHY, false, false, false), 120, 66, 512);
    char line[48];
    snprintf(line, sizeof(line), "%s种子", CROP_NAMES[p->crop]);
    lv_obj_t *name = ui_common_label(screen, line, ROW_X, 116, ROW_W, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_color(name, lv_color_hex(KP_THEME), 0);
    small_line(screen, "预计一天成熟", 140, KP_MUTED_LIGHT);
    wrap_text(screen, CARE_TIPS[p->crop], ROW_X, 160, ROW_W, UI_FONT_SMALL, KP_TEXT, LV_TEXT_ALIGN_CENTER);
    snprintf(line, sizeof(line), "种%s", CROP_NAMES[p->crop]);
    row(screen, 206, 26, s_focus == 0, false, -1, line, "");
    row(screen, 234, 26, s_focus == 1, false, -1, "换个种子", "免费");
    row(screen, 262, 26, s_focus == 2, false, -1, "返回", "");
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_care(const app_model_snapshot_t *model)
{
    const garden_t *p = g();
    lv_obj_t *screen = ui_common_screen("照料", model);
    char line[80];
    snprintf(line, sizeof(line), "%s  %s", CROP_NAMES[p->crop], STAGE_NAMES[p->stage]);
    lv_obj_t *name = ui_common_label(screen, line, ROW_X, 24, ROW_W, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_color(name, lv_color_hex(KP_THEME), 0);
    uint32_t color = status_line(line, sizeof(line), true);
    small_line(screen, line, 48, color);
    unsigned water = garden_water_percent(p);
    if (garden_is_growing(p) && s_view.day && water > 0 && water <= 30) {
        snprintf(line, sizeof(line), "约%u分钟后需要浇水", (unsigned)((garden_seconds_to_dry(p) + 59) / 60));
    } else line[0] = 0;
    small_line(screen, line, 66, KP_MUTED);
    char detail[32];
    bool full = water >= 100 && p->water >= GARDEN_WATER_FULL;
    static const char *const labels[GARDEN_MATERIALS] = {"施肥", "除草", "除虫"};
    int start = s_focus > 3 ? s_focus - 3 : 0;
    for (int i = start; i < start + 4; ++i) {
        int y = 88 + (i - start) * 48;
        if (i == 0) {
            snprintf(detail, sizeof(detail), full ? "%u%% 已满" : "%u%% 免费", water);
            row(screen, y, 46, s_focus == i, full, GARDEN_ICON_WATER, "浇水", detail);
            continue;
        }
        if (i == 4) {
            row(screen, y, 46, s_focus == i, false, -1, "返回", "");
            continue;
        }
        int m = i - 1;
        bool need = garden_needs(p, (garden_material_t)m);
        if (need && m == GARDEN_MAT_FERTILIZER) snprintf(detail, sizeof(detail), "需1  材料 ×%u", p->bag[m]);
        else if (need) snprintf(detail, sizeof(detail), "材料 ×%u", p->bag[m]);
        else strlcpy(detail, "无需", sizeof(detail));
        row(screen, y, 46, s_focus == i, !need, MATERIAL_ICONS[m], labels[m], detail);
    }
    if (s_water_anim) start_water_anim(screen, 204, 34);
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_use(const app_model_snapshot_t *model)
{
    lv_obj_t *screen = ui_common_screen("使用材料", model);
    image_at(screen, &garden_icons[MATERIAL_ICONS[s_material]], 120, 62, 768);
    char line[64];
    snprintf(line, sizeof(line), "使用%s ×1？", MATERIAL_NAMES[s_material]);
    ui_common_label(screen, line, ROW_X, 98, ROW_W, LV_TEXT_ALIGN_CENTER, false);
    snprintf(line, sizeof(line), "当前剩余 %u 件", g()->bag[s_material]);
    small_line(screen, line, 128, KP_MUTED_LIGHT);
    small_line(screen, "处理这次养护需求", 148, KP_MUTED_LIGHT);
    row(screen, 206, 34, s_focus == 0, false, -1, "使用", "");
    row(screen, 244, 34, s_focus == 1, false, -1, "取消", "");
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static bool bag_has_recent(void) { return g()->last_recv_mat != 0; }

static lv_obj_t *build_bag(const app_model_snapshot_t *model)
{
    const garden_t *p = g();
    lv_obj_t *screen = ui_common_screen("背包", model);
    char line[64];
    if (s_view.gift_phase == GARDEN_GIFT_PENDING) strlcpy(line, "有 1 件赠送待确认", sizeof(line));
    else if (s_view.gift_phase == GARDEN_GIFT_SENDING) strlcpy(line, "正在送出，请稍候", sizeof(line));
    else strlcpy(line, "选择材料，送给" KP_PEER_LABEL, sizeof(line));
    small_line(screen, line, 26, KP_MUTED_LIGHT);
    char detail[32];
    int back_index = bag_has_recent() ? 4 : 3;
    int start = s_focus > 3 ? s_focus - 3 : 0;
    for (int m = start; m <= back_index && m < start + 4; ++m) {
        int y = 48 + (m - start) * 50;
        if (m == back_index) {
            row(screen, y, 46, s_focus == m, false, -1, "返回", "");
            continue;
        }
        if (m == 3) {
            snprintf(detail, sizeof(detail), "%s ×1", MATERIAL_NAMES[p->last_recv_mat - 1]);
            row(screen, y, 46, s_focus == m, false, -1, "最近收礼", detail);
            continue;
        }
        bool locked = p->pending_mat == m + 1;
        if (locked) snprintf(detail, sizeof(detail), "×%u  待确认1", p->bag[m]);
        else snprintf(detail, sizeof(detail), "×%u", p->bag[m]);
        row(screen, y, 46, s_focus == m, p->bag[m] == 0, MATERIAL_ICONS[m], MATERIAL_NAMES[m], detail);
    }
    small_line(screen, "收获可获得材料，可送给家人", 262, KP_MUTED);
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_gift_confirm(const app_model_snapshot_t *model)
{
    lv_obj_t *screen = ui_common_screen("确认赠送", model);
    image_at(screen, &garden_icons[MATERIAL_ICONS[s_material]], 120, 58, 768);
    lv_obj_t *to = ui_common_label(screen, "送给" KP_PEER_LABEL, ROW_X, 92, ROW_W, LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_style_text_color(to, lv_color_hex(KP_THEME), 0);
    char line[64];
    uint8_t mac[6];
    if (espnow_service_get_peer(mac)) snprintf(line, sizeof(line), "已配对设备 %02X%02X", mac[4], mac[5]);
    else strlcpy(line, "已配对设备", sizeof(line));
    small_line(screen, line, 118, KP_MUTED);
    snprintf(line, sizeof(line), "%s ×1  当前拥有 %u 件", MATERIAL_NAMES[s_material], g()->bag[s_material]);
    small_line(screen, line, 140, KP_TEXT);
    small_line(screen, "对方开机后自动收下", 160, KP_MUTED_LIGHT);
    row(screen, 206, 34, s_focus == 0, false, -1, "取消", "");
    row(screen, 244, 34, s_focus == 1, false, -1, "送出", "");
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static int gift_status_count(void)
{
    if (s_view.gift_phase == GARDEN_GIFT_SENDING) return 0;
    return s_view.gift_phase == GARDEN_GIFT_FAILED ? 2 : 1;
}

static lv_obj_t *build_gift_status(const app_model_snapshot_t *model)
{
    uint8_t phase = s_view.gift_phase;
    int m = s_view.gift_material < GARDEN_MATERIALS ? s_view.gift_material : s_material;
    const char *title = phase == GARDEN_GIFT_SENDING ? "正在送出" : phase == GARDEN_GIFT_PENDING ? "待确认"
                        : phase == GARDEN_GIFT_SENT ? "赠送结果" : "未送出";
    lv_obj_t *screen = ui_common_screen(title, model);
    image_at(screen, &garden_icons[MATERIAL_ICONS[m]], 120, 58, 768);
    char head[48], line[64];
    const char *sub1 = "", *sub2 = "";
    uint32_t head_color = KP_TEXT;
    if (phase == GARDEN_GIFT_SENDING) {
        snprintf(head, sizeof(head), "%s ×1", MATERIAL_NAMES[m]);
        sub1 = "正在连接" KP_PEER_LABEL "的设备";
        sub2 = "对方将自动收到";
    } else if (phase == GARDEN_GIFT_SENT) {
        snprintf(head, sizeof(head), "已送出 %s ×1", MATERIAL_NAMES[m]);
        head_color = KP_GREEN;
        sub1 = "对方已自动入账";
    } else if (phase == GARDEN_GIFT_PENDING) {
        strlcpy(head, "待确认 ×1", sizeof(head));
        head_color = KP_YELLOW;
        sub1 = "请等" KP_PEER_LABEL "开机后核对";
        sub2 = "物品暂不能使用或再送";
    } else if (phase == GARDEN_GIFT_RETURNED) {
        strlcpy(head, "未送出", sizeof(head));
        sub1 = "核对结果：对方未收到";
        sub2 = "物品已回到背包";
    } else {
        strlcpy(head, "连接失败", sizeof(head));
        head_color = KP_RED;
        sub1 = "请等" KP_PEER_LABEL "开机后再送";
        sub2 = "已开机请靠近并唤醒，库存不变";
    }
    wrap_text(screen, head, ROW_X, 92, ROW_W, UI_FONT_BODY, head_color, LV_TEXT_ALIGN_CENTER);
    small_line(screen, sub1, 120, KP_MUTED_LIGHT);
    small_line(screen, sub2, 140, KP_MUTED_LIGHT);
    if (phase == GARDEN_GIFT_SENT) {
        snprintf(line, sizeof(line), "当前剩余 %u 件", g()->bag[m]);
        small_line(screen, line, 140, KP_MUTED_LIGHT);
    }
    if (phase == GARDEN_GIFT_FAILED) {
        row(screen, 206, 34, s_focus == 0, false, -1, "重试", "");
        row(screen, 244, 34, s_focus == 1, false, -1, "返回背包", "");
    } else if (phase != GARDEN_GIFT_SENDING) {
        row(screen, 244, 34, true, false, -1, "返回背包", "");
    }
    ui_common_footer(screen, phase == GARDEN_GIFT_SENDING ? "正在连接，请稍候" : "B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_harvest(const app_model_snapshot_t *model)
{
    lv_obj_t *screen = ui_common_screen("收获完成", model);
    image_at(screen, ui_garden_sprite(s_harvest.crop, GARDEN_STAGE_MATURE, GARDEN_ART_HEALTHY, false, false, false),
             120, 66, 512);
    char line[96];
    snprintf(line, sizeof(line), "本次收获 %s +1", CROP_NAMES[s_harvest.crop]);
    wrap_text(screen, line, ROW_X, 118, ROW_W, UI_FONT_BODY, KP_GREEN, LV_TEXT_ALIGN_CENTER);
    if (s_harvest.first) strlcpy(line, "图鉴已点亮", sizeof(line));
    else snprintf(line, sizeof(line), "累计收获 %u 次", s_harvest.count);
    small_line(screen, line, 146, KP_YELLOW);
    small_line(screen, "获得园艺材料", 166, KP_MUTED_LIGHT);
    line[0] = 0;
    for (int m = 0; m < GARDEN_MATERIALS; ++m) {
        if (!s_harvest.materials[m]) continue;
        char part[24];
        snprintf(part, sizeof(part), "%s%s +%u", line[0] ? "  " : "", MATERIAL_NAMES[m], s_harvest.materials[m]);
        strlcat(line, part, sizeof(line));
    }
    small_line(screen, line, 184, KP_TEXT);
    snprintf(line, sizeof(line), "下一粒种子：%s", CROP_NAMES[s_harvest.next_crop]);
    small_line(screen, line, 206, KP_MUTED_LIGHT);
    row(screen, 244, 34, true, false, -1, "回到花园", "");
    ui_common_footer(screen, "B3确认", false);
    return screen;
}

static lv_obj_t *build_album(const app_model_snapshot_t *model)
{
    const garden_t *p = g();
    lv_obj_t *screen = ui_common_screen("图鉴", model);
    int collected = 0;
    for (int c = 0; c < GARDEN_CROPS; ++c) collected += p->album[c] > 0;
    char line[48];
    snprintf(line, sizeof(line), "已收集 %d / %d", collected, GARDEN_CROPS);
    small_line(screen, line, 26, KP_YELLOW);
    /* Four rows fit; the window keeps the focused crop on screen. */
    int start = s_focus > 3 ? s_focus - 3 : 0;
    for (int i = 0; i < 4; ++i) {
        int c = start + i;
        if (c == GARDEN_CROPS) {
            row(screen, 48 + i * 52, 48, s_focus == c, false, -1, "返回", "");
            continue;
        }
        char detail[24];
        if (p->album[c]) snprintf(detail, sizeof(detail), "收获 ×%u", p->album[c]);
        else strlcpy(detail, "未解锁", sizeof(detail));
        row(screen, 48 + i * 52, 48, s_focus == c, !p->album[c], -1, CROP_NAMES[c], detail);
    }
    if (s_focus == GARDEN_CROPS) strlcpy(line, "返回花园", sizeof(line));
    else if (p->album[s_focus] == 0) strlcpy(line, "未解锁  收获后开启", sizeof(line));
    else snprintf(line, sizeof(line), "第 %d / %d 种  确定展开", s_focus + 1, GARDEN_CROPS);
    small_line(screen, line, 260, KP_MUTED);
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B3返回", false);
    return screen;
}

static lv_obj_t *build_book(const app_model_snapshot_t *model)
{
    lv_obj_t *screen = ui_common_screen(CROP_NAMES[s_book_crop], model);
    lv_obj_t *head = ui_common_label(screen, BOOK_HEADS[s_book_page], ROW_X, 24, ROW_W, LV_TEXT_ALIGN_LEFT, false);
    lv_obj_set_style_text_color(head, lv_color_hex(KP_THEME), 0);
    int text_y = 54;
    if (s_book_page == 0) {
        image_at(screen, ui_garden_sprite(s_book_crop, GARDEN_STAGE_MATURE, GARDEN_ART_HEALTHY, false, false, false),
                 120, 96, 512);
        text_y = 148;
    }
    wrap_text(screen, FACTS[s_book_crop][s_book_page], ROW_X, text_y, ROW_W, UI_FONT_BODY, KP_TEXT,
              LV_TEXT_ALIGN_LEFT);
    if (s_book_page == 0) {
        char line[32];
        snprintf(line, sizeof(line), "本人收获 %u 次", g()->album[s_book_crop]);
        small_line(screen, line, 266, KP_MUTED_LIGHT);
    }
    char foot[48];
    snprintf(foot, sizeof(foot), "B1 B2翻页  %d/4  B3返回", s_book_page + 1);
    ui_common_footer(screen, foot, false);
    return screen;
}

void ui_garden_enter(void)
{
    s_page = GP_HOME;
    s_focus = 0;
}

lv_obj_t *ui_garden_build(const app_model_snapshot_t *model)
{
    refresh();
    /* A gift result can change page content while the player is elsewhere in the garden. */
    if (s_page == GP_GIFT_STATUS && s_view.gift_phase == GARDEN_GIFT_IDLE) s_page = GP_BAG;
    switch (s_page) {
    case GP_SOW: return build_sow(model);
    case GP_CARE: return build_care(model);
    case GP_USE: return build_use(model);
    case GP_BAG: return build_bag(model);
    case GP_GIFT_CONFIRM: return build_gift_confirm(model);
    case GP_GIFT_STATUS: return build_gift_status(model);
    case GP_HARVEST: return build_harvest(model);
    case GP_ALBUM: return build_album(model);
    case GP_BOOK: return build_book(model);
    default: return build_home(model);
    }
}

bool ui_garden_live_page(void) { return s_page == GP_HOME || s_page == GP_CARE; }

static void say(ui_garden_feedback_t *fb, const char *text, bool error, int sound)
{
    strlcpy(fb->message, text, sizeof(fb->message));
    fb->error = error;
    fb->sound = sound;
}

static void go_page(garden_page_t page, int focus)
{
    s_page = page;
    s_focus = focus;
}

static int option_count(void)
{
    switch (s_page) {
    case GP_HOME: return 4;
    case GP_SOW: return 3;
    case GP_CARE: return 5;
    case GP_USE: case GP_GIFT_CONFIRM: return 2;
    case GP_BAG: return bag_has_recent() ? 5 : 4;
    case GP_GIFT_STATUS: return gift_status_count();
    case GP_HARVEST: return 1;
    case GP_ALBUM: return GARDEN_CROPS + 1;
    default: return 1;
    }
}

static void speed_message(ui_garden_feedback_t *fb, const char *first)
{
    const garden_t *p = g();
    char text[64];
    if (!s_view.day) snprintf(text, sizeof(text), "%s\n夜间自动休息", first);
    else if (p->water == 0) snprintf(text, sizeof(text), "%s\n仍需浇水", first);
    else snprintf(text, sizeof(text), "%s\n当前速度 %u%%", first, garden_speed_percent(p));
    say(fb, text, false, SOUND_DING);
}

static void time_error(ui_garden_feedback_t *fb) { say(fb, "时间未就绪\n请校准设备时间", true, SOUND_DU); }
static void no_material(ui_garden_feedback_t *fb) { say(fb, "材料不够\n收获可获得\n也可请家人赠送", true, SOUND_DU); }

static void open_care(void)
{
    const garden_t *p = g();
    int focus = garden_water_percent(p) <= 30 ? 0
                : garden_needs(p, GARDEN_MAT_INSECTICIDE) ? 3
                : garden_needs(p, GARDEN_MAT_HERBICIDE) ? 2
                : garden_needs(p, GARDEN_MAT_FERTILIZER) ? 1 : 0;
    go_page(GP_CARE, focus);
}

static void key_home(ui_garden_feedback_t *fb)
{
    const garden_t *p = g();
    if (s_focus == 1) { go_page(GP_BAG, 0); return; }
    if (s_focus == 2) { go_page(GP_ALBUM, 0); return; }
    if (!p->planted) { go_page(GP_SOW, 0); return; }
    if (p->stage == GARDEN_STAGE_MATURE) {
        if (garden_service_harvest(&s_harvest) == GARDEN_OK) {
            go_page(GP_HARVEST, 0);
            fb->sound = SOUND_FANFARE;
        }
        return;
    }
    if (!s_view.clock_ok) { time_error(fb); return; }
    open_care();
}

static void key_sow(ui_garden_feedback_t *fb)
{
    if (s_focus == 2) { go_page(GP_HOME, 0); return; }
    if (s_focus == 1) {
        if (garden_service_reroll() == GARDEN_OK) {
            refresh();
            char text[48];
            snprintf(text, sizeof(text), "已换成%s种子", CROP_NAMES[g()->crop]);
            say(fb, text, false, SOUND_TICK);
        }
        return;
    }
    garden_result_t r = garden_service_sow();
    if (r == GARDEN_ERR_TIME) { time_error(fb); return; }
    if (r != GARDEN_OK) return;
    s_water_anim = true;
    say(fb, "播种成功\n已自动浇水", false, SOUND_DING);
    go_page(GP_HOME, 0);
}

static void key_care(ui_garden_feedback_t *fb)
{
    if (s_focus == 0) {
        garden_result_t r = garden_service_water();
        refresh();
        if (r == GARDEN_ERR_WATER_FULL) say(fb, "水分已充足", false, SOUND_TICK);
        else if (r == GARDEN_ERR_TIME) time_error(fb);
        else if (r != GARDEN_OK) { say(fb, "已经成熟啦\n回花园收获", false, SOUND_TICK); go_page(GP_HOME, 0); }
        else { s_water_anim = true; speed_message(fb, "已补水至100%"); }
        return;
    }
    garden_material_t m = (garden_material_t)(s_focus - 1);
    if (!garden_needs(g(), m)) { say(fb, "暂时不需要", false, SOUND_TICK); return; }
    if (g()->bag[m] == 0) { no_material(fb); return; }
    s_material = m;
    go_page(GP_USE, 0);
}

static void key_use(ui_garden_feedback_t *fb)
{
    int back_focus = s_material + 1;
    if (s_focus == 0) {
        garden_result_t r = garden_service_use(s_material);
        refresh();
        if (r == GARDEN_OK) {
            char first[32];
            snprintf(first, sizeof(first), "已使用%s ×1", MATERIAL_NAMES[s_material]);
            speed_message(fb, first);
        } else if (r == GARDEN_ERR_NO_MATERIAL) no_material(fb);
        else if (r == GARDEN_ERR_TIME) time_error(fb);
        else say(fb, "暂时不需要", false, SOUND_TICK);
    }
    go_page(GP_CARE, back_focus);
}

static void key_bag(ui_garden_feedback_t *fb)
{
    const garden_t *p = g();
    if (s_focus == 3) {
        char text[48];
        snprintf(text, sizeof(text), "最近收到\n%s ×1", MATERIAL_NAMES[p->last_recv_mat - 1]);
        say(fb, text, false, SOUND_TICK);
        return;
    }
    s_material = (garden_material_t)s_focus;
    if (s_view.gift_phase == GARDEN_GIFT_SENDING || s_view.gift_phase == GARDEN_GIFT_PENDING) {
        go_page(GP_GIFT_STATUS, 0);
        return;
    }
    if (p->bag[s_material] == 0) { say(fb, "材料为零\n收获可获得\n也可请家人赠送", true, SOUND_DU); return; }
    if (!espnow_service_has_peer()) {
        say(fb, "请先完成\n设备配对", false, SOUND_DU);
        fb->open_pairing = true;
        return;
    }
    go_page(GP_GIFT_CONFIRM, 0);
}

static void send_gift(ui_garden_feedback_t *fb)
{
    garden_result_t r = garden_service_gift(s_material);
    if (r == GARDEN_ERR_NO_MATERIAL) { no_material(fb); go_page(GP_BAG, s_material); return; }
    fb->sound = SOUND_TICK;
    go_page(GP_GIFT_STATUS, 0);
}

static void back_to_bag(void)
{
    garden_service_gift_seen();
    go_page(GP_BAG, s_material);
}

bool ui_garden_key(bsp_btn_t key, ui_garden_feedback_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->sound = -1;
    refresh();
    if (key != BSP_BTN_OK) {
        int delta = key == BSP_BTN_UP ? -1 : 1;
        if (s_page == GP_BOOK) {
            int next = s_book_page + delta;
            if (next >= 0 && next < 4) { s_book_page = next; fb->sound = SOUND_TICK; }
            return false;
        }
        int n = option_count();
        if (n > 1) { s_focus = (s_focus + delta + n) % n; fb->sound = SOUND_TICK; }
        return false;
    }
    /* Visible return actions and the long-press shortcut share the same destination
     * and focus restoration. Handle them before indexing crop/material arrays. */
    if ((s_page == GP_HOME && s_focus == 3) ||
        (s_page == GP_CARE && s_focus == 4) ||
        (s_page == GP_BAG && s_focus == (bag_has_recent() ? 4 : 3)) ||
        (s_page == GP_ALBUM && s_focus == GARDEN_CROPS) ||
        s_page == GP_BOOK)
        return ui_garden_back(fb);
    switch (s_page) {
    case GP_HOME: key_home(fb); break;
    case GP_SOW: key_sow(fb); break;
    case GP_CARE: key_care(fb); break;
    case GP_USE: key_use(fb); break;
    case GP_BAG: key_bag(fb); break;
    case GP_GIFT_CONFIRM:
        if (s_focus == 1) send_gift(fb);
        else go_page(GP_BAG, s_material);
        break;
    case GP_GIFT_STATUS:
        if (s_view.gift_phase == GARDEN_GIFT_FAILED && s_focus == 0) send_gift(fb);
        else if (s_view.gift_phase != GARDEN_GIFT_SENDING) back_to_bag();
        break;
    case GP_HARVEST: go_page(GP_HOME, 0); break;
    case GP_ALBUM:
        /* Each entry unlocks with the first successful harvest of that crop. */
        if (g()->album[s_focus] == 0) {
            say(fb, "未解锁\n收获这种植物后开启", false, SOUND_TICK);
            return false;
        }
        s_book_crop = s_focus;
        s_book_page = 0;
        go_page(GP_BOOK, 0);
        fb->sound = SOUND_TICK;
        break;
    default: break;
    }
    return false;
}

bool ui_garden_back(ui_garden_feedback_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->sound = SOUND_TICK;
    switch (s_page) {
    case GP_HOME: return true;
    case GP_USE: go_page(GP_CARE, s_material + 1); break;
    case GP_BAG: go_page(GP_HOME, 1); break;
    case GP_GIFT_CONFIRM: go_page(GP_BAG, s_material); break;
    case GP_GIFT_STATUS: back_to_bag(); break;
    case GP_ALBUM: go_page(GP_HOME, 2); break;
    case GP_BOOK: go_page(GP_ALBUM, s_book_crop); break;
    default: go_page(GP_HOME, 0); break;
    }
    return false;
}
