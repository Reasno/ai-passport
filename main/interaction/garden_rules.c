#include "garden_rules.h"
#include <string.h>

#define DAY_S 86400

typedef struct {
    uint16_t dry_minutes[3];
    uint8_t fertilizer;
    uint8_t pest_stage;
    uint16_t weed_permille;
    uint16_t pest_permille;
} crop_rules_t;

static const crop_rules_t CROPS[GARDEN_CROPS] = {
    [GARDEN_RADISH] = {{300, 300, 300}, 0, GARDEN_STAGE_SEEDLING, 300, 700},
    [GARDEN_LETTUCE] = {{300, 300, 300}, 1, GARDEN_STAGE_SEEDLING, 400, 100},
    [GARDEN_BASIL] = {{300, 300, 300}, 1, GARDEN_STAGE_GROWING, 250, 250},
    [GARDEN_BEAN] = {{300, 180, 120}, 0, GARDEN_STAGE_GROWING, 650, 700},
    [GARDEN_TOMATO] = {{300, 180, 120}, 1, GARDEN_STAGE_GROWING, 100, 400},
    [GARDEN_ROSEMARY] = {{300, 420, 900}, 0, GARDEN_STAGE_GROWING, 850, 150},
};
/* Cumulative per-mille thresholds for one harvested material draw. */
static const uint16_t MATERIAL_DRAW[GARDEN_MATERIALS] = {382, 707, 1000};
#define EXTRA_MATERIAL_PERMILLE 200U

static uint32_t next_random(garden_t *g)
{
    uint32_t x = g->rng ? g->rng : 0x6D2B79F5U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}
static uint32_t random_below(garden_t *g, uint32_t n) { return next_random(g) % n; }
static bool chance(garden_t *g, uint32_t permille) { return random_below(g, 1000) < permille; }

static void shuffle_seed_bag(garden_t *g)
{
    for (int i = 0; i < GARDEN_CROPS; ++i) g->seed_bag[i] = (uint8_t)i;
    for (int i = GARDEN_CROPS - 1; i > 0; --i) {
        int j = (int)random_below(g, (uint32_t)i + 1);
        uint8_t t = g->seed_bag[i];
        g->seed_bag[i] = g->seed_bag[j];
        g->seed_bag[j] = t;
    }
    g->seed_bag_pos = 0;
}
static uint8_t take_seed(garden_t *g)
{
    if (g->seed_bag_pos >= GARDEN_CROPS) shuffle_seed_bag(g);
    return g->seed_bag[g->seed_bag_pos++];
}

static void clear_plot(garden_t *g)
{
    g->planted = 0;
    g->stage = GARDEN_STAGE_SEED;
    g->progress = 0;
    g->water = 0;
    g->flags = 0;
}

void garden_init(garden_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x6D2B79F5U;
    shuffle_seed_bag(g);
    clear_plot(g);
    g->crop = take_seed(g);
    /* One-time starter kit: enough to treat every possible need of the first plant. */
    for (int i = 0; i < GARDEN_MATERIALS; ++i) g->bag[i] = 1;
}

uint8_t garden_crop_fertilizer(int crop) { return crop >= 0 && crop < GARDEN_CROPS ? CROPS[crop].fertilizer : 0; }
uint16_t garden_crop_weed_permille(int crop) { return crop >= 0 && crop < GARDEN_CROPS ? CROPS[crop].weed_permille : 0; }
uint16_t garden_crop_pest_permille(int crop) { return crop >= 0 && crop < GARDEN_CROPS ? CROPS[crop].pest_permille : 0; }
uint8_t garden_crop_pest_stage(int crop) { return crop >= 0 && crop < GARDEN_CROPS ? CROPS[crop].pest_stage : 0; }
unsigned garden_crop_dry_minutes(int crop, int stage)
{
    if (crop < 0 || crop >= GARDEN_CROPS || stage < 0 || stage > GARDEN_STAGE_GROWING) return 0;
    return CROPS[crop].dry_minutes[stage];
}

static int seconds_of_day(int64_t t, int32_t utc_offset_s)
{
    int64_t local = (t + utc_offset_s) % DAY_S;
    return (int)(local < 0 ? local + DAY_S : local);
}
bool garden_is_day(int64_t now, int32_t utc_offset_s)
{
    int sod = seconds_of_day(now, utc_offset_s);
    return sod >= GARDEN_DAY_START_S && sod < GARDEN_DAY_END_S;
}

bool garden_is_growing(const garden_t *g) { return g->planted && g->stage < GARDEN_STAGE_MATURE; }

unsigned garden_speed_percent(const garden_t *g)
{
    int speed = 100;
    if (g->flags & GARDEN_FLAG_FERT_OWED) speed -= 15;
    if (g->flags & GARDEN_FLAG_WEED) speed -= 20;
    if (g->flags & GARDEN_FLAG_PEST) speed -= 30;
    return speed < (int)GARDEN_MIN_SPEED ? GARDEN_MIN_SPEED : (unsigned)speed;
}
unsigned garden_effective_speed(const garden_t *g, bool day)
{
    if (!garden_is_growing(g) || !day || g->water == 0) return 0;
    return garden_speed_percent(g);
}

unsigned garden_dry_minutes(const garden_t *g)
{
    return garden_is_growing(g) ? CROPS[g->crop].dry_minutes[g->stage] : 0;
}
static uint32_t drain_per_second(const garden_t *g)
{
    return GARDEN_WATER_FULL / (CROPS[g->crop].dry_minutes[g->stage] * 60U);
}
unsigned garden_water_percent(const garden_t *g)
{
    if (g->water == 0) return 0;
    unsigned pct = (unsigned)(((uint64_t)g->water * 100U + GARDEN_WATER_FULL / 2) / GARDEN_WATER_FULL);
    return pct == 0 ? 1 : pct;
}
static uint32_t ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
uint32_t garden_seconds_to_dry(const garden_t *g)
{
    return garden_is_growing(g) ? ceil_div(g->water, drain_per_second(g)) : 0;
}
uint32_t garden_seconds_to_mature(const garden_t *g)
{
    if (!garden_is_growing(g)) return 0;
    uint32_t units = (GARDEN_STAGE_UNITS - g->progress) +
                     GARDEN_STAGE_UNITS * (uint32_t)(GARDEN_STAGE_MATURE - 1 - g->stage);
    return ceil_div(units, garden_speed_percent(g));
}
uint32_t garden_seconds_to_mature_wall(const garden_t *g, int64_t now, int32_t utc_offset_s)
{
    uint32_t grow = garden_seconds_to_mature(g);
    if (grow == 0) return 0;
    int sod = seconds_of_day(now, utc_offset_s);
    int64_t wall = 0;
    if (sod < GARDEN_DAY_START_S) {
        wall = GARDEN_DAY_START_S - sod;
        sod = GARDEN_DAY_START_S;
    } else if (sod >= GARDEN_DAY_END_S) {
        wall = (int64_t)DAY_S - sod + GARDEN_DAY_START_S;
        sod = GARDEN_DAY_START_S;
    }
    uint32_t today = (uint32_t)(GARDEN_DAY_END_S - sod);
    if (grow <= today) return (uint32_t)(wall + grow);
    grow -= today;
    wall += today;
    uint32_t day_len = (uint32_t)(GARDEN_DAY_END_S - GARDEN_DAY_START_S);
    uint32_t full_days = grow / day_len;
    wall += (int64_t)full_days * DAY_S;
    grow -= full_days * day_len;
    if (grow == 0) return (uint32_t)wall;
    return (uint32_t)(wall + (DAY_S - day_len) + grow);
}

bool garden_needs(const garden_t *g, garden_material_t material)
{
    if (!garden_is_growing(g)) return false;
    if (material == GARDEN_MAT_FERTILIZER) return g->flags & GARDEN_FLAG_FERT_OWED;
    if (material == GARDEN_MAT_HERBICIDE) return g->flags & GARDEN_FLAG_WEED;
    return g->flags & GARDEN_FLAG_PEST;
}

/* Progress value of the next node in this stage: an event or the stage end. */
static uint32_t next_node(const garden_t *g)
{
    uint32_t node = GARDEN_STAGE_UNITS;
    if (g->stage == GARDEN_STAGE_SEEDLING && (g->flags & GARDEN_FLAG_WEED_DRAWN) &&
        !(g->flags & GARDEN_FLAG_WEED_SEEN) && g->progress < GARDEN_WEED_NODE && GARDEN_WEED_NODE < node)
        node = GARDEN_WEED_NODE;
    if (g->stage == CROPS[g->crop].pest_stage && (g->flags & GARDEN_FLAG_PEST_DRAWN) &&
        !(g->flags & GARDEN_FLAG_PEST_SEEN) && g->progress < GARDEN_PEST_NODE && GARDEN_PEST_NODE < node)
        node = GARDEN_PEST_NODE;
    return node;
}
static void apply_nodes(garden_t *g)
{
    if (g->stage == GARDEN_STAGE_SEEDLING && (g->flags & GARDEN_FLAG_WEED_DRAWN) &&
        !(g->flags & GARDEN_FLAG_WEED_SEEN) && g->progress >= GARDEN_WEED_NODE)
        g->flags |= GARDEN_FLAG_WEED_SEEN | GARDEN_FLAG_WEED;
    if (g->stage == CROPS[g->crop].pest_stage && (g->flags & GARDEN_FLAG_PEST_DRAWN) &&
        !(g->flags & GARDEN_FLAG_PEST_SEEN) && g->progress >= GARDEN_PEST_NODE)
        g->flags |= GARDEN_FLAG_PEST_SEEN | GARDEN_FLAG_PEST;
    if (g->progress < GARDEN_STAGE_UNITS) return;
    g->progress = 0;
    g->stage++;
    if (g->stage == GARDEN_STAGE_GROWING && CROPS[g->crop].fertilizer && !(g->flags & GARDEN_FLAG_FERT_SEEN))
        g->flags |= GARDEN_FLAG_FERT_SEEN | GARDEN_FLAG_FERT_OWED;
    /* A mature plant waits for harvest; open needs no longer matter. */
    if (g->stage == GARDEN_STAGE_MATURE)
        g->flags &= (uint8_t)~(GARDEN_FLAG_WEED | GARDEN_FLAG_PEST | GARDEN_FLAG_FERT_OWED);
}

/* Advances one daytime span [t, end) with water and growth, stopping at each node. */
static void run_day(garden_t *g, int64_t t, int64_t end)
{
    while (t < end && garden_is_growing(g)) {
        if (g->water == 0) return;
        uint32_t speed = garden_speed_percent(g);
        uint32_t drain = drain_per_second(g);
        uint32_t node = next_node(g);
        int64_t dt = end - t;
        uint32_t to_dry = ceil_div(g->water, drain);
        uint32_t to_node = ceil_div(node - g->progress, speed);
        if (dt > to_dry) dt = to_dry;
        if (dt > to_node) dt = to_node;
        uint64_t gained = (uint64_t)speed * (uint64_t)dt;
        g->progress = gained >= node - g->progress ? node : g->progress + (uint32_t)gained;
        uint64_t used = (uint64_t)drain * (uint64_t)dt;
        g->water = used >= g->water ? 0 : g->water - (uint32_t)used;
        t += dt;
        apply_nodes(g);
    }
}

garden_result_t garden_settle(garden_t *g, int64_t now, int32_t utc_offset_s)
{
    if (now < g->settled_at) return GARDEN_ERR_TIME;
    int64_t t = g->settled_at;
    /* A dry plant is frozen until watered, so the remaining span changes nothing. */
    while (t < now && garden_is_growing(g) && g->water > 0) {
        int sod = seconds_of_day(t, utc_offset_s);
        if (sod < GARDEN_DAY_START_S) {
            t += GARDEN_DAY_START_S - sod;
        } else if (sod >= GARDEN_DAY_END_S) {
            t += DAY_S - sod + GARDEN_DAY_START_S;
        } else {
            int64_t end = t + (GARDEN_DAY_END_S - sod);
            if (end > now) end = now;
            run_day(g, t, end);
            t = end;
        }
    }
    g->settled_at = now;
    return GARDEN_OK;
}

garden_result_t garden_sow(garden_t *g, int64_t now)
{
    if (g->planted) return GARDEN_ERR_STATE;
    clear_plot(g);
    g->planted = 1;
    g->water = GARDEN_WATER_FULL;
    if (chance(g, CROPS[g->crop].weed_permille)) g->flags |= GARDEN_FLAG_WEED_DRAWN;
    if (chance(g, CROPS[g->crop].pest_permille)) g->flags |= GARDEN_FLAG_PEST_DRAWN;
    g->settled_at = now;
    return GARDEN_OK;
}

garden_result_t garden_reroll(garden_t *g)
{
    if (g->planted) return GARDEN_ERR_STATE;
    g->crop = (uint8_t)((g->crop + 1 + random_below(g, GARDEN_CROPS - 1)) % GARDEN_CROPS);
    return GARDEN_OK;
}

garden_result_t garden_water(garden_t *g)
{
    if (!garden_is_growing(g)) return GARDEN_ERR_STATE;
    if (g->water >= GARDEN_WATER_FULL) return GARDEN_ERR_WATER_FULL;
    g->water = GARDEN_WATER_FULL;
    return GARDEN_OK;
}

garden_result_t garden_use(garden_t *g, garden_material_t material)
{
    if ((unsigned)material >= GARDEN_MATERIALS) return GARDEN_ERR_STATE;
    if (!garden_needs(g, material)) return GARDEN_ERR_NOT_NEEDED;
    if (g->bag[material] == 0) return GARDEN_ERR_NO_MATERIAL;
    g->bag[material]--;
    if (material == GARDEN_MAT_FERTILIZER) g->flags &= (uint8_t)~GARDEN_FLAG_FERT_OWED;
    else if (material == GARDEN_MAT_HERBICIDE) g->flags &= (uint8_t)~GARDEN_FLAG_WEED;
    else g->flags &= (uint8_t)~GARDEN_FLAG_PEST;
    return GARDEN_OK;
}

garden_result_t garden_harvest(garden_t *g, garden_harvest_t *out)
{
    if (!g->planted || g->stage != GARDEN_STAGE_MATURE) return GARDEN_ERR_STATE;
    garden_harvest_t result = {.crop = g->crop};
    if (g->album[g->crop] < UINT16_MAX) g->album[g->crop]++;
    result.count = g->album[g->crop] > UINT8_MAX ? UINT8_MAX : (uint8_t)g->album[g->crop];
    result.first = g->album[g->crop] == 1;
    int items = 1 + (chance(g, EXTRA_MATERIAL_PERMILLE) ? 1 : 0);
    for (int i = 0; i < items; ++i) {
        uint32_t roll = random_below(g, 1000);
        int m = 0;
        while (m < GARDEN_MATERIALS - 1 && roll >= MATERIAL_DRAW[m]) m++;
        result.materials[m]++;
        if (g->bag[m] < UINT16_MAX) g->bag[m]++;
    }
    clear_plot(g);
    g->crop = take_seed(g);
    result.next_crop = g->crop;
    if (out) *out = result;
    return GARDEN_OK;
}

static int find_gift(const garden_t *g, uint16_t id)
{
    for (int i = 0; i < GARDEN_RECV_IDS; ++i)
        if (g->recv_ids[i] && (g->recv_ids[i] & GARDEN_GIFT_ID_MAX) == id) return i;
    return -1;
}
static void remember_gift(garden_t *g, uint16_t entry)
{
    g->recv_ids[g->recv_pos % GARDEN_RECV_IDS] = entry;
    g->recv_pos = (uint8_t)((g->recv_pos + 1) % GARDEN_RECV_IDS);
}
garden_gift_status_t garden_gift_status(const garden_t *g, uint16_t id)
{
    int i = find_gift(g, id);
    if (i < 0) return GARDEN_GIFT_UNKNOWN;
    return (g->recv_ids[i] & 0x8000U) ? GARDEN_GIFT_CREDITED : GARDEN_GIFT_CLOSED;
}
bool garden_receive_gift(garden_t *g, uint16_t id, garden_material_t material)
{
    if (id == 0 || id > GARDEN_GIFT_ID_MAX || (unsigned)material >= GARDEN_MATERIALS) return false;
    if (find_gift(g, id) >= 0) return false;
    remember_gift(g, (uint16_t)(id | 0x8000U));
    if (g->bag[material] < UINT16_MAX) g->bag[material]++;
    g->last_recv_mat = (uint8_t)(material + 1);
    return true;
}
garden_gift_status_t garden_gift_query(garden_t *g, uint16_t id)
{
    garden_gift_status_t status = garden_gift_status(g, id);
    if (status == GARDEN_GIFT_UNKNOWN && id && id <= GARDEN_GIFT_ID_MAX) {
        remember_gift(g, id);
        status = GARDEN_GIFT_CLOSED;
    }
    return status;
}

uint16_t garden_next_gift_id(garden_t *g)
{
    if (g->next_gift_id == 0 || g->next_gift_id > GARDEN_GIFT_ID_MAX)
        g->next_gift_id = (uint16_t)(1 + random_below(g, GARDEN_GIFT_ID_MAX));
    uint16_t id = g->next_gift_id;
    g->next_gift_id = id >= GARDEN_GIFT_ID_MAX ? 1 : (uint16_t)(id + 1);
    return id;
}

garden_result_t garden_gift_lock(garden_t *g, garden_material_t material, uint16_t id)
{
    if ((unsigned)material >= GARDEN_MATERIALS || id == 0 || id > GARDEN_GIFT_ID_MAX) return GARDEN_ERR_STATE;
    if (g->pending_mat) return GARDEN_ERR_STATE;
    if (g->bag[material] == 0) return GARDEN_ERR_NO_MATERIAL;
    g->bag[material]--;
    g->pending_mat = (uint8_t)(material + 1);
    g->pending_id = id;
    return GARDEN_OK;
}
void garden_gift_finish(garden_t *g, bool refund)
{
    if (!g->pending_mat) return;
    int m = g->pending_mat - 1;
    if (refund && g->bag[m] < UINT16_MAX) g->bag[m]++;
    g->pending_mat = 0;
    g->pending_id = 0;
}
