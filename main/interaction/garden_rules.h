#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pure rules for the Pocket Garden: one plant per player, three growth steps of 180
 * standard minutes each, daytime-only growth and water use, and a material bag.
 * Time is Unix seconds; the caller passes the local UTC offset so the rules never
 * depend on the C library time zone. */
#define GARDEN_CROPS 6
#define GARDEN_MATERIALS 3
#define GARDEN_RECV_IDS 8

/* Progress counts percent-seconds: one second at 100 % speed adds 100. */
#define GARDEN_STAGE_UNITS (180U * 60U * 100U)
#define GARDEN_WEED_NODE (45U * 60U * 100U)
#define GARDEN_PEST_NODE (108U * 60U * 100U)
/* Divisible by every dry time (2, 3, 5, 7 and 15 h) so daytime water use is an integer per second. */
#define GARDEN_WATER_FULL 756000U
#define GARDEN_DAY_START_S (8 * 3600)
#define GARDEN_DAY_END_S (21 * 3600 + 1800)
/* The garden day window follows Asia/Shanghai local time (UTC+8). */
#define GARDEN_UTC_OFFSET_S (8 * 3600)
#define GARDEN_MIN_SPEED 35U

typedef enum {
    GARDEN_RADISH = 0,
    GARDEN_LETTUCE,
    GARDEN_BASIL,
    GARDEN_BEAN,
    GARDEN_TOMATO,
    GARDEN_ROSEMARY,
} garden_crop_t;

typedef enum {
    GARDEN_STAGE_SEED = 0,
    GARDEN_STAGE_SEEDLING,
    GARDEN_STAGE_GROWING,
    GARDEN_STAGE_MATURE,
} garden_stage_t;

typedef enum {
    GARDEN_MAT_FERTILIZER = 0,
    GARDEN_MAT_HERBICIDE,
    GARDEN_MAT_INSECTICIDE,
} garden_material_t;

typedef enum {
    GARDEN_OK = 0,
    GARDEN_ERR_STATE,        /* the action does not apply to the current plot */
    GARDEN_ERR_NOT_NEEDED,   /* no matching care need */
    GARDEN_ERR_NO_MATERIAL,  /* the bag holds none of that material */
    GARDEN_ERR_WATER_FULL,   /* water is already 100 % */
    GARDEN_ERR_TIME,         /* clock is earlier than the last settlement */
} garden_result_t;

enum {
    GARDEN_FLAG_WEED_DRAWN = 1U << 0,
    GARDEN_FLAG_PEST_DRAWN = 1U << 1,
    GARDEN_FLAG_WEED_SEEN = 1U << 2,
    GARDEN_FLAG_PEST_SEEN = 1U << 3,
    GARDEN_FLAG_WEED = 1U << 4,
    GARDEN_FLAG_PEST = 1U << 5,
    GARDEN_FLAG_FERT_SEEN = 1U << 6,
    GARDEN_FLAG_FERT_OWED = 1U << 7,
};

/* The whole persisted garden. Field order keeps the struct free of padding. */
typedef struct {
    int64_t settled_at;
    uint32_t rng;
    uint32_t progress;
    uint32_t water;
    uint16_t bag[GARDEN_MATERIALS];
    uint16_t album[GARDEN_CROPS];
    uint16_t recv_ids[GARDEN_RECV_IDS];
    uint16_t pending_id;
    uint16_t next_gift_id;
    uint8_t crop;
    uint8_t planted;
    uint8_t stage;
    uint8_t flags;
    uint8_t seed_bag[GARDEN_CROPS];
    uint8_t seed_bag_pos;
    uint8_t recv_pos;
    /* Material index + 1 of the gift awaiting confirmation; 0 when none. */
    uint8_t pending_mat;
    /* Material index + 1 of the latest received gift; 0 when none. */
    uint8_t last_recv_mat;
    uint8_t reserved[8];
} garden_t;

_Static_assert(sizeof(garden_t) == 80, "garden_t is persisted as one NVS blob");

typedef struct {
    uint8_t crop;
    uint8_t count;
    uint8_t next_crop;
    bool first;
    uint8_t materials[GARDEN_MATERIALS];
} garden_harvest_t;

void garden_init(garden_t *g, uint32_t seed);
bool garden_is_day(int64_t now, int32_t utc_offset_s);
/* Replays daytime growth, water use and care events from settled_at to now. */
garden_result_t garden_settle(garden_t *g, int64_t now, int32_t utc_offset_s);

unsigned garden_speed_percent(const garden_t *g);
/* Speed actually applied right now: 0 at night, while dry, or when not growing. */
unsigned garden_effective_speed(const garden_t *g, bool day);
unsigned garden_water_percent(const garden_t *g);
bool garden_needs(const garden_t *g, garden_material_t material);
bool garden_is_growing(const garden_t *g);
/* Full-water daytime hours for the current stage, in minutes. */
unsigned garden_dry_minutes(const garden_t *g);
/* Daytime seconds until water reaches 0 at the current stage. */
uint32_t garden_seconds_to_dry(const garden_t *g);
/* Daytime seconds to maturity at the current speed, ignoring future events. */
uint32_t garden_seconds_to_mature(const garden_t *g);
/* Wall-clock seconds to maturity from now, counting skipped nights as elapsed time. */
uint32_t garden_seconds_to_mature_wall(const garden_t *g, int64_t now, int32_t utc_offset_s);

garden_result_t garden_sow(garden_t *g, int64_t now);
garden_result_t garden_reroll(garden_t *g);
garden_result_t garden_water(garden_t *g);
garden_result_t garden_use(garden_t *g, garden_material_t material);
garden_result_t garden_harvest(garden_t *g, garden_harvest_t *out);

/* Gift ids are 1..GARDEN_GIFT_ID_MAX. The receiver remembers the latest ids so a repeated
 * delivery is acknowledged without a second credit. */
#define GARDEN_GIFT_ID_MAX 0x7FFFU
typedef enum { GARDEN_GIFT_UNKNOWN = 0, GARDEN_GIFT_CREDITED, GARDEN_GIFT_CLOSED } garden_gift_status_t;
/* Credits a gift once per id; returns false for a known id (credited or closed). */
bool garden_receive_gift(garden_t *g, uint16_t id, garden_material_t material);
garden_gift_status_t garden_gift_status(const garden_t *g, uint16_t id);
/* Answers a sender's reconciliation query: an unknown id is closed so a late copy of
 * that gift can no longer be credited. */
garden_gift_status_t garden_gift_query(garden_t *g, uint16_t id);
/* Sender side: returns this device's next gift id. Ids are sequential so the receiver's
 * recent-id window never confuses two gifts from the same sender. */
uint16_t garden_next_gift_id(garden_t *g);
/* Sender side: moves one item out of the bag into the single pending slot. */
garden_result_t garden_gift_lock(garden_t *g, garden_material_t material, uint16_t id);
/* Sender side: clears the pending slot; refund returns the item to the bag. */
void garden_gift_finish(garden_t *g, bool refund);

uint8_t garden_crop_fertilizer(int crop);
uint16_t garden_crop_weed_permille(int crop);
uint16_t garden_crop_pest_permille(int crop);
uint8_t garden_crop_pest_stage(int crop);
unsigned garden_crop_dry_minutes(int crop, int stage);
