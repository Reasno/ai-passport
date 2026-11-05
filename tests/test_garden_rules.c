#include "garden_rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CST (8 * 3600)
/* 2026-11-04 00:00 local (UTC+8). */
#define DAY0 ((int64_t)1793750400 - CST)
#define AT(day, h, m) (DAY0 + (int64_t)(day) * 86400 + (h) * 3600 + (m) * 60)

static void plant(garden_t *g, int crop, uint8_t drawn, int64_t now)
{
    garden_init(g, 99);
    g->crop = (uint8_t)crop;
    assert(garden_sow(g, now) == GARDEN_OK);
    g->flags = drawn;
}

static void test_init_gives_one_seed_and_starter_kit(void)
{
    garden_t g;
    garden_init(&g, 7);
    assert(!g.planted && g.crop < GARDEN_CROPS);
    for (int i = 0; i < GARDEN_MATERIALS; ++i) assert(g.bag[i] == 1);
    for (int i = 0; i < GARDEN_CROPS; ++i) assert(g.album[i] == 0);
    garden_t again;
    garden_init(&again, 7);
    assert(memcmp(&g, &again, sizeof(g)) == 0);
}

static void test_day_window(void)
{
    assert(!garden_is_day(AT(0, 7, 59), CST));
    assert(garden_is_day(AT(0, 8, 0), CST));
    assert(garden_is_day(AT(0, 21, 29), CST));
    assert(!garden_is_day(AT(0, 21, 30), CST));
}

/* Design example: radish sown at 07:30, dry at 13:00, refilled at 17:00, mature at 21:00. */
static void test_radish_example_from_design(void)
{
    garden_t g;
    plant(&g, GARDEN_RADISH, 0, AT(0, 7, 30));
    assert(garden_water_percent(&g) == 100);
    assert(garden_settle(&g, AT(0, 12, 59), CST) == GARDEN_OK);
    assert(g.water > 0 && g.stage == GARDEN_STAGE_SEEDLING);
    assert(garden_settle(&g, AT(0, 13, 0), CST) == GARDEN_OK);
    assert(g.water == 0 && g.stage == GARDEN_STAGE_SEEDLING);
    uint32_t frozen = g.progress;
    assert(garden_settle(&g, AT(0, 17, 0), CST) == GARDEN_OK);
    assert(g.progress == frozen && garden_effective_speed(&g, true) == 0);
    assert(garden_water(&g) == GARDEN_OK);
    assert(garden_water(&g) == GARDEN_ERR_WATER_FULL);
    assert(garden_settle(&g, AT(0, 20, 59), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_GROWING);
    assert(garden_settle(&g, AT(0, 21, 0), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_MATURE && garden_water_percent(&g) == 20);
    uint32_t water = g.water;
    assert(garden_settle(&g, AT(3, 12, 0), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_MATURE && g.water == water);
}

/* 20:30 to next day 08:30: only 1.5 h of growth and water use. */
static void test_night_freezes_growth_and_water(void)
{
    garden_t g;
    plant(&g, GARDEN_LETTUCE, 0, AT(0, 20, 30));
    assert(garden_settle(&g, AT(1, 8, 30), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_SEED);
    assert(g.progress == 90U * 60U * 100U);
    assert(garden_water_percent(&g) == 70);
}

static void test_events_trigger_at_nodes_and_slow_growth(void)
{
    garden_t g;
    plant(&g, GARDEN_BASIL, GARDEN_FLAG_WEED_DRAWN | GARDEN_FLAG_PEST_DRAWN, AT(0, 8, 0));
    /* Seed stage: 180 min. Weed appears 45 min into the seedling stage. */
    assert(garden_settle(&g, AT(0, 11, 44), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_SEEDLING && !(g.flags & GARDEN_FLAG_WEED));
    assert(garden_settle(&g, AT(0, 11, 45), CST) == GARDEN_OK);
    assert(g.flags & GARDEN_FLAG_WEED);
    assert(garden_speed_percent(&g) == 80);
    assert(garden_needs(&g, GARDEN_MAT_HERBICIDE));
    assert(!garden_needs(&g, GARDEN_MAT_INSECTICIDE));
    assert(garden_use(&g, GARDEN_MAT_INSECTICIDE) == GARDEN_ERR_NOT_NEEDED);
    assert(garden_use(&g, GARDEN_MAT_HERBICIDE) == GARDEN_OK);
    assert(g.bag[GARDEN_MAT_HERBICIDE] == 0 && garden_speed_percent(&g) == 100);
    assert(garden_water(&g) == GARDEN_OK);
    /* Remaining seedling 135 min, then growing starts with a fertilizer need. */
    assert(garden_settle(&g, AT(0, 14, 0), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_GROWING && g.progress == 0);
    assert(garden_needs(&g, GARDEN_MAT_FERTILIZER) && garden_speed_percent(&g) == 85);
    /* Pest appears after 108 standard minutes in growing: 108 / 0.85 = 127.06 min. */
    assert(garden_water(&g) == GARDEN_OK);
    assert(garden_settle(&g, AT(0, 16, 7), CST) == GARDEN_OK);
    assert(!(g.flags & GARDEN_FLAG_PEST));
    assert(garden_settle(&g, AT(0, 16, 8), CST) == GARDEN_OK);
    assert(g.flags & GARDEN_FLAG_PEST);
    assert(garden_speed_percent(&g) == 55);
    /* Events never repeat once handled. */
    assert(garden_use(&g, GARDEN_MAT_INSECTICIDE) == GARDEN_OK);
    assert(garden_use(&g, GARDEN_MAT_FERTILIZER) == GARDEN_OK);
    assert(garden_water(&g) == GARDEN_OK);
    assert(garden_settle(&g, AT(0, 21, 29), CST) == GARDEN_OK);
    assert(g.stage == GARDEN_STAGE_MATURE && g.flags == (GARDEN_FLAG_WEED_DRAWN | GARDEN_FLAG_PEST_DRAWN |
                                                          GARDEN_FLAG_WEED_SEEN | GARDEN_FLAG_PEST_SEEN |
                                                          GARDEN_FLAG_FERT_SEEN));
}

static void test_speed_floor(void)
{
    garden_t g;
    plant(&g, GARDEN_TOMATO, 0, AT(0, 8, 0));
    g.flags |= GARDEN_FLAG_FERT_OWED | GARDEN_FLAG_WEED | GARDEN_FLAG_PEST;
    assert(garden_speed_percent(&g) == 35);
    g.flags &= (uint8_t)~GARDEN_FLAG_FERT_OWED;
    assert(garden_speed_percent(&g) == 50);
    assert(garden_effective_speed(&g, false) == 0);
}

static void test_stage_water_rates(void)
{
    garden_t g;
    plant(&g, GARDEN_ROSEMARY, 0, AT(0, 8, 0));
    assert(garden_seconds_to_dry(&g) == 5U * 3600U);
    g.stage = GARDEN_STAGE_SEEDLING;
    assert(garden_seconds_to_dry(&g) == 7U * 3600U);
    g.stage = GARDEN_STAGE_GROWING;
    assert(garden_seconds_to_dry(&g) == 15U * 3600U);
    g.crop = GARDEN_BEAN;
    assert(garden_seconds_to_dry(&g) == 2U * 3600U);
    g.stage = GARDEN_STAGE_SEEDLING;
    assert(garden_seconds_to_dry(&g) == 3U * 3600U);
    assert(garden_seconds_to_mature(&g) == 2U * 3U * 3600U);
}

/* Chunked settlement equals one long replay for every crop and event mix. */
static void test_replay_is_split_invariant(void)
{
    for (int crop = 0; crop < GARDEN_CROPS; ++crop) {
        for (uint8_t drawn = 0; drawn < 4; ++drawn) {
            garden_t a, b;
            plant(&a, crop, drawn, AT(0, 6, 17));
            b = a;
            assert(garden_settle(&a, AT(2, 13, 0), CST) == GARDEN_OK);
            for (int64_t t = AT(0, 6, 17); t < AT(2, 13, 0);) {
                t += 977;
                if (t > AT(2, 13, 0)) t = AT(2, 13, 0);
                assert(garden_settle(&b, t, CST) == GARDEN_OK);
            }
            assert(memcmp(&a, &b, sizeof(a)) == 0);
        }
    }
}

static void test_clock_going_back_is_rejected(void)
{
    garden_t g;
    plant(&g, GARDEN_RADISH, 0, AT(0, 9, 0));
    assert(garden_settle(&g, AT(0, 10, 0), CST) == GARDEN_OK);
    garden_t before = g;
    assert(garden_settle(&g, AT(0, 9, 30), CST) == GARDEN_ERR_TIME);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
}

static void test_harvest_and_seed_bag(void)
{
    garden_t g;
    garden_init(&g, 1234);
    int seen[GARDEN_CROPS] = {0};
    assert(garden_harvest(&g, NULL) == GARDEN_ERR_STATE);
    for (int round = 0; round < GARDEN_CROPS * 50; ++round) {
        if (round % GARDEN_CROPS == 0) memset(seen, 0, sizeof(seen));
        seen[g.crop]++;
        assert(seen[g.crop] == 1);
        assert(garden_sow(&g, AT(0, 8, 0)) == GARDEN_OK);
        assert(garden_sow(&g, AT(0, 8, 0)) == GARDEN_ERR_STATE);
        assert(garden_reroll(&g) == GARDEN_ERR_STATE);
        g.stage = GARDEN_STAGE_MATURE;
        uint16_t before = g.bag[0] + g.bag[1] + g.bag[2];
        int crop = g.crop;
        garden_harvest_t h;
        assert(garden_harvest(&g, &h) == GARDEN_OK);
        int got = h.materials[0] + h.materials[1] + h.materials[2];
        assert(got == 1 || got == 2);
        assert(g.bag[0] + g.bag[1] + g.bag[2] == before + got);
        assert(h.crop == crop && g.album[crop] == h.count && h.first == (h.count == 1));
        assert(!g.planted && g.crop == h.next_crop);
        assert(garden_harvest(&g, NULL) == GARDEN_ERR_STATE);
    }
}

static void test_material_and_extra_rates(void)
{
    garden_t g;
    garden_init(&g, 42);
    long counts[GARDEN_MATERIALS] = {0}, total = 0, rounds = 200000;
    for (long i = 0; i < rounds; ++i) {
        g.planted = 1;
        g.stage = GARDEN_STAGE_MATURE;
        garden_harvest_t h;
        assert(garden_harvest(&g, &h) == GARDEN_OK);
        for (int m = 0; m < GARDEN_MATERIALS; ++m) { counts[m] += h.materials[m]; total += h.materials[m]; }
    }
    double per_round = (double)total / rounds;
    assert(per_round > 1.19 && per_round < 1.21);
    assert((double)counts[0] / total > 0.375 && (double)counts[0] / total < 0.389);
    assert((double)counts[1] / total > 0.318 && (double)counts[1] / total < 0.332);
    assert((double)counts[2] / total > 0.286 && (double)counts[2] / total < 0.300);
}

static void test_reroll_always_changes_crop_uniformly(void)
{
    garden_t g;
    garden_init(&g, 5);
    int hits[GARDEN_CROPS] = {0};
    g.crop = GARDEN_TOMATO;
    for (int i = 0; i < 60000; ++i) {
        uint8_t before = g.crop;
        assert(garden_reroll(&g) == GARDEN_OK);
        assert(g.crop != before && g.crop < GARDEN_CROPS);
        if (before == GARDEN_TOMATO) hits[g.crop]++;
        g.crop = GARDEN_TOMATO;
    }
    assert(hits[GARDEN_TOMATO] == 0);
    for (int c = 0; c < GARDEN_CROPS; ++c)
        if (c != GARDEN_TOMATO) assert(hits[c] > 11400 && hits[c] < 12600);
}

static void test_event_probabilities_follow_crop_table(void)
{
    for (int crop = 0; crop < GARDEN_CROPS; ++crop) {
        garden_t g;
        garden_init(&g, 77U + (uint32_t)crop);
        int weed = 0, pest = 0, n = 40000;
        for (int i = 0; i < n; ++i) {
            g.planted = 0;
            g.crop = (uint8_t)crop;
            assert(garden_sow(&g, 0) == GARDEN_OK);
            weed += (g.flags & GARDEN_FLAG_WEED_DRAWN) != 0;
            pest += (g.flags & GARDEN_FLAG_PEST_DRAWN) != 0;
        }
        double w = weed * 1000.0 / n, p = pest * 1000.0 / n;
        assert(w > garden_crop_weed_permille(crop) - 12 && w < garden_crop_weed_permille(crop) + 12);
        assert(p > garden_crop_pest_permille(crop) - 12 && p < garden_crop_pest_permille(crop) + 12);
    }
}

static void test_gift_dedup_and_pending(void)
{
    garden_t sender, receiver;
    garden_init(&sender, 1);
    garden_init(&receiver, 2);
    assert(garden_gift_lock(&sender, GARDEN_MAT_FERTILIZER, 17) == GARDEN_OK);
    assert(sender.bag[0] == 0 && sender.pending_mat == 1);
    assert(garden_gift_lock(&sender, GARDEN_MAT_HERBICIDE, 18) == GARDEN_ERR_STATE);
    assert(garden_receive_gift(&receiver, 17, GARDEN_MAT_FERTILIZER));
    assert(!garden_receive_gift(&receiver, 17, GARDEN_MAT_FERTILIZER));
    assert(receiver.bag[0] == 2 && receiver.last_recv_mat == 1);
    assert(garden_gift_status(&receiver, 17) == GARDEN_GIFT_CREDITED);
    garden_gift_finish(&sender, false);
    assert(sender.bag[0] == 0 && !sender.pending_mat);
    /* Unknown id: the query closes it so a late copy cannot also be credited. */
    assert(garden_gift_lock(&sender, GARDEN_MAT_HERBICIDE, 19) == GARDEN_OK);
    assert(garden_gift_query(&receiver, 19) == GARDEN_GIFT_CLOSED);
    assert(!garden_receive_gift(&receiver, 19, GARDEN_MAT_HERBICIDE));
    assert(receiver.bag[1] == 1);
    garden_gift_finish(&sender, true);
    assert(sender.bag[1] == 1);
    garden_t empty;
    garden_init(&empty, 3);
    empty.bag[2] = 0;
    assert(garden_gift_lock(&empty, GARDEN_MAT_INSECTICIDE, 20) == GARDEN_ERR_NO_MATERIAL);
    for (uint16_t id = 100; id < 100 + GARDEN_RECV_IDS; ++id) assert(garden_receive_gift(&receiver, id, 0));
    assert(garden_gift_status(&receiver, 17) == GARDEN_GIFT_UNKNOWN);
}

/* Wall-clock estimates count the nightly pause as elapsed time. */
static void test_wall_time_to_mature_skips_night(void)
{
    garden_t g;
    /* 9 daytime hours left at 07:30: 30 min to 08:00, mature at 17:00. */
    plant(&g, GARDEN_RADISH, 0, AT(0, 7, 30));
    assert(garden_seconds_to_mature(&g) == 9U * 3600U);
    assert(garden_seconds_to_mature_wall(&g, AT(0, 7, 30), CST) == 9U * 3600U + 1800U);
    assert(garden_seconds_to_mature_wall(&g, AT(0, 10, 0), CST) == 9U * 3600U);

    /* 18:00: 3.5 h today, 10.5 h night, 5.5 h tomorrow. */
    assert(garden_seconds_to_mature_wall(&g, AT(0, 18, 0), CST) ==
           (3U * 3600U + 1800U) + (10U * 3600U + 1800U) + (5U * 3600U + 1800U));

    /* At the 21:30 boundary the whole night is skipped first. */
    assert(garden_seconds_to_mature_wall(&g, AT(0, 21, 30), CST) ==
           10U * 3600U + 1800U + 9U * 3600U);

    /* 02:00: 6 h until 08:00, then the full daytime estimate. */
    assert(garden_seconds_to_mature_wall(&g, AT(1, 2, 0), CST) == 6U * 3600U + 9U * 3600U);

    /* Slowest growth spans more than one skipped night. */
    plant(&g, GARDEN_RADISH, GARDEN_FLAG_FERT_OWED | GARDEN_FLAG_WEED | GARDEN_FLAG_PEST, AT(0, 10, 0));
    assert(garden_seconds_to_mature(&g) == 92572U);
    assert(garden_seconds_to_mature_wall(&g, AT(0, 10, 0), CST) == 168172U);

    /* A mature or empty plot has no remaining time. */
    g.stage = GARDEN_STAGE_MATURE;
    assert(garden_seconds_to_mature_wall(&g, AT(0, 10, 0), CST) == 0);
    garden_init(&g, 5);
    assert(garden_seconds_to_mature_wall(&g, AT(0, 10, 0), CST) == 0);
}

int main(void)
{
    test_init_gives_one_seed_and_starter_kit();
    test_day_window();
    test_radish_example_from_design();
    test_night_freezes_growth_and_water();
    test_wall_time_to_mature_skips_night();
    test_events_trigger_at_nodes_and_slow_growth();
    test_speed_floor();
    test_stage_water_rates();
    test_replay_is_split_invariant();
    test_clock_going_back_is_rejected();
    test_harvest_and_seed_bag();
    test_material_and_extra_rates();
    test_reroll_always_changes_crop_uniformly();
    test_event_probabilities_follow_crop_table();
    test_gift_dedup_and_pending();
    puts("garden rules tests passed");
    return 0;
}
