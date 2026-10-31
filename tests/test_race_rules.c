#include "race_rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void clear_track(race_t *race)
{
    memset(race->rows, 0, sizeof(race->rows));
}

static void run_until(race_t *race, uint32_t until_ms)
{
    while (race->clock_ms < until_ms && race->outcome == RACE_OUTCOME_PENDING) race_step(race, 50, NULL);
}

static void test_track_is_deterministic_and_always_leaves_an_open_lane(void)
{
    race_t a, b, c;
    race_init(&a, 12345);
    race_init(&b, 12345);
    race_init(&c, 54321);
    assert(memcmp(a.rows, b.rows, sizeof(a.rows)) == 0);
    assert(memcmp(a.rows, c.rows, sizeof(a.rows)) != 0);
    for (uint32_t seed = 1; seed < 400; ++seed) {
        race_init(&a, seed);
        for (int row = 0; row < RACE_ROWS; ++row) {
            int open = 0;
            for (int lane = 0; lane < RACE_LANES; ++lane) {
                race_item_t item = race_item_at(&a, row, lane);
                assert(item == RACE_ITEM_NONE || item == RACE_ITEM_NITRO || item == RACE_ITEM_BUMP);
                if (item == RACE_ITEM_NONE) open++;
            }
            assert(open >= 1);
        }
    }
    assert(race_row_mm(0) == 60000U);
    assert(race_row_mm(RACE_ROWS - 1) == RACE_TRACK_MM - 60000U);
    assert(a.cars[0].lane == 1 && a.cars[1].lane == 1);
}

static void test_equal_cars_on_empty_track_draw_in_thirty_seconds(void)
{
    race_t race;
    race_init(&race, 7);
    clear_track(&race);
    race_set_lane(&race, 0, 0);
    race_set_lane(&race, 1, 2);
    run_until(&race, 31000);
    assert(race.outcome == RACE_OUTCOME_DRAW);
    assert(race.cars[0].finish_ms == 30000U && race.cars[1].finish_ms == 30000U);
    assert(race.cars[0].pos_mm == RACE_TRACK_MM);
}

static void test_nitro_goes_to_the_first_car_and_is_shared_within_100ms(void)
{
    race_t race;
    race_step_report_t report;
    /* 4.5 m apart at 30 m/s is 150 ms: outside the sharing window, so only the leader gets it. */
    race_init(&race, 9);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_NITRO << 2;
    race.cars[0].pos_mm = race_row_mm(0) - 600;
    race.cars[1].pos_mm = race_row_mm(0) - 5100;
    race_step(&race, 50, &report);
    assert(report.nitro[0] && !report.nitro[1]);
    assert(report.claimed_count == 1 && report.claimed_row[0] == 0 && report.claimed_lane[0] == 1);
    assert(report.claimed_car[0] == 0);
    assert(race_item_at(&race, 0, 1) == RACE_ITEM_NONE);
    run_until(&race, 250);
    assert(race.cars[1].pos_mm > race_row_mm(0));
    assert(race_car_speed_percent(&race, 0) == 150 && race_car_speed_percent(&race, 1) == 100);

    /* Exact simultaneous arrival gives both cars the nitro. */
    race_init(&race, 9);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_NITRO << 2;
    race.cars[0].pos_mm = race.cars[1].pos_mm = race_row_mm(0) - 900;
    race_step(&race, 50, &report);
    assert(report.nitro[0] && report.nitro[1] && report.claimed_count == 2);
    assert(race_item_at(&race, 0, 1) == RACE_ITEM_NONE);
    assert(race_car_speed_percent(&race, 0) == 150 && race_car_speed_percent(&race, 1) == 150);

    /* The trailing car may arrive in a later step and still share within 100 ms. */
    race_init(&race, 9);
    clear_track(&race);
    race.rows[2] = RACE_ITEM_NITRO << 2;
    race.cars[1].pos_mm = race_row_mm(2) - 300;
    race.cars[0].pos_mm = race_row_mm(2) - 2850;
    race_step(&race, 50, &report);
    assert(report.nitro[1] && !report.nitro[0]);
    race_step(&race, 50, &report);
    assert(report.nitro[0] && !report.nitro[1]);
    assert(race_car_speed_percent(&race, 0) == 150 && race_car_speed_percent(&race, 1) == 150);

    /* Exactly 100 ms behind is outside the window. */
    race_init(&race, 9);
    clear_track(&race);
    race.rows[2] = RACE_ITEM_NITRO << 2;
    race.cars[1].pos_mm = race_row_mm(2) - 300;
    race.cars[0].pos_mm = race_row_mm(2) - 3300;
    run_until(&race, 200);
    assert(race_car_speed_percent(&race, 1) == 150 && race_car_speed_percent(&race, 0) == 100);

    /* Sharing needs the same lane: a car one lane over drives past the taken can. */
    race_init(&race, 9);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_NITRO << 2;
    race_set_lane(&race, 1, 2);
    race.cars[0].pos_mm = race.cars[1].pos_mm = race_row_mm(0) - 900;
    race_step(&race, 50, &report);
    assert(report.nitro[0] && !report.nitro[1] && report.claimed_count == 1);
}

static void test_nitro_and_bump_timing(void)
{
    race_t race;
    race_init(&race, 3);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_BUMP << 2;
    race.cars[0].pos_mm = race_row_mm(0) - 1000;
    race_step_report_t report;
    race_step(&race, 50, &report);
    assert(report.bump[0]);
    assert(race_item_at(&race, 0, 1) == RACE_ITEM_BUMP);
    assert(race_car_speed_percent(&race, 0) == 50);
    run_until(&race, 2000);
    assert(race.clock_ms == 2000);
    assert(race_car_speed_percent(&race, 0) == 50);
    race_step(&race, 50, NULL);
    assert(race.clock_ms == 2050);
    assert(race_car_speed_percent(&race, 0) == 100);

    /* A later item replaces the current effect instead of stacking with it. */
    race_init(&race, 3);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_NITRO << 2;
    race.rows[1] = RACE_ITEM_BUMP << 2;
    race.cars[0].pos_mm = race_row_mm(0) - 100;
    race_step(&race, 50, NULL);
    assert(race_car_speed_percent(&race, 0) == 150);
    race.cars[0].pos_mm = race_row_mm(1) - 100;
    race_step(&race, 50, NULL);
    assert(race_car_speed_percent(&race, 0) == 50);
}

static void test_drs_needs_a_gap_and_is_single_use(void)
{
    race_t race;
    race_init(&race, 11);
    clear_track(&race);
    assert(race_drs_state(&race, 0) == RACE_DRS_LOCKED);
    assert(!race_request_drs(&race, 0));
    race.cars[0].pos_mm = 100000;
    race.cars[1].pos_mm = 104999;
    assert(race_drs_state(&race, 0) == RACE_DRS_LOCKED);
    race.cars[1].pos_mm = 105000;
    assert(race_drs_state(&race, 0) == RACE_DRS_AVAILABLE);
    assert(race_drs_state(&race, 1) == RACE_DRS_LOCKED);
    assert(!race_request_drs(&race, 1));
    assert(race_request_drs(&race, 0));
    assert(race_drs_state(&race, 0) == RACE_DRS_ACTIVE);
    assert(race_car_speed_percent(&race, 0) == 170);
    uint32_t start_clock = race.clock_ms;
    uint32_t start_pos = race.cars[0].pos_mm;
    run_until(&race, start_clock + RACE_DRS_MS);
    assert(RACE_DRS_MS == 4000U);
    assert(race.cars[0].pos_mm - start_pos == 204000U);
    /* Overtaking during the window keeps the sprint; it then becomes spent for this race. */
    assert(race.cars[0].pos_mm > race.cars[1].pos_mm);
    assert(race_drs_state(&race, 0) == RACE_DRS_USED);
    assert(race_car_speed_percent(&race, 0) == 100);
    race.cars[1].pos_mm = race.cars[0].pos_mm + 50000;
    assert(race_drs_state(&race, 0) == RACE_DRS_USED);
    assert(!race_request_drs(&race, 0));
    assert(race_drs_state(&race, 1) == RACE_DRS_LOCKED);
    race.cars[0].pos_mm = race.cars[1].pos_mm + RACE_DRS_GAP_MM;
    assert(race_drs_state(&race, 1) == RACE_DRS_AVAILABLE);
}

static void test_drs_stacks_with_nitro_and_bump_cancels_both(void)
{
    assert(race_speed_percent(RACE_EFFECT_NONE, false) == 100);
    assert(race_speed_percent(RACE_EFFECT_NITRO, false) == 150);
    assert(race_speed_percent(RACE_EFFECT_NONE, true) == 170);
    assert(race_speed_percent(RACE_EFFECT_NITRO, true) == 220);
    assert(race_speed_percent(RACE_EFFECT_SLOW, true) == 50);

    race_t race;
    race_init(&race, 5);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_NITRO << 2;
    race.rows[1] = RACE_ITEM_BUMP << 2;
    race.cars[1].pos_mm = race_row_mm(0) + 40000;
    race.cars[0].pos_mm = race_row_mm(0) - 100;
    assert(race_request_drs(&race, 0));
    race_step(&race, 50, NULL);
    assert(race_car_speed_percent(&race, 0) == 220);
    /* Picking nitro up during DRS stacks without shortening the DRS window. */
    assert(race_drs_state(&race, 0) == RACE_DRS_ACTIVE);
    assert(race.cars[0].drs_until_ms == RACE_DRS_MS);
    race.cars[0].pos_mm = race_row_mm(1) - 100;
    race_step(&race, 50, NULL);
    assert(race_car_speed_percent(&race, 0) == 50);
    assert(race_drs_state(&race, 0) == RACE_DRS_USED);
    run_until(&race, race.clock_ms + RACE_EFFECT_MS + 50);
    assert(race_car_speed_percent(&race, 0) == 100);
}

static void test_finish_order_and_tie_window(void)
{
    race_t race;
    race_init(&race, 21);
    clear_track(&race);
    race.cars[1].pos_mm = 3000;
    run_until(&race, 31000);
    assert(race.outcome == RACE_OUTCOME_PEER);
    assert(race.cars[1].finish_ms == 29900U);

    race_init(&race, 21);
    clear_track(&race);
    race.cars[0].pos_mm = 1500;
    run_until(&race, 31000);
    assert(race.cars[0].finish_ms == 29950U);
    assert(race.outcome == RACE_OUTCOME_DRAW);

    /* The leader is declared once the tie window has passed. */
    race_init(&race, 21);
    clear_track(&race);
    race.cars[0].pos_mm = RACE_TRACK_MM - 1500;
    race.cars[1].pos_mm = 100000;
    race_step(&race, 50, NULL);
    assert(race.cars[0].finished && race.outcome == RACE_OUTCOME_PENDING);
    race_step(&race, 100, NULL);
    assert(race.outcome == RACE_OUTCOME_HOST);
    race_set_lane(&race, 0, 2);
    assert(race.cars[0].lane == 1);
    assert(race_car_speed_percent(&race, 0) == 0);
}

static void test_large_steps_do_not_skip_items(void)
{
    race_t race;
    race_init(&race, 13);
    clear_track(&race);
    race.rows[0] = RACE_ITEM_BUMP << 2;
    race.cars[0].pos_mm = race_row_mm(0) - 20000;
    race_step(&race, 2000, NULL);
    assert(race.cars[0].effect == RACE_EFFECT_SLOW);
    assert(race.cars[0].pos_mm < race_row_mm(0) + 30000);
}

static void test_wire_round_trip(void)
{
    race_wire_state_t in = {
        .meters = {900, 437}, .lane = {2, 0}, .effect = {RACE_EFFECT_NITRO, RACE_EFFECT_SLOW},
        .drs = {RACE_DRS_USED, RACE_DRS_AVAILABLE},
    };
    race_wire_state_t out;
    race_unpack_state(race_pack_state(&in), &out);
    assert(memcmp(&in, &out, sizeof(in)) == 0);
    in.meters[0] = 5000;
    race_unpack_state(race_pack_state(&in), &out);
    assert(out.meters[0] == 1023);

    race_event_t ev = {.kind = RACE_EVENT_NITRO_TAKEN, .row = 26, .lane = 2, .car = 1, .outcome = RACE_OUTCOME_DRAW};
    race_event_t back;
    race_unpack_event(race_pack_event(&ev), &back);
    assert(memcmp(&ev, &back, sizeof(ev)) == 0);

    race_t race;
    race_init(&race, 17);
    clear_track(&race);
    race.cars[0].pos_mm = 12345;
    race.cars[1].pos_mm = 20000;
    race_set_lane(&race, 1, 7);
    race_wire_from_race(&race, &out);
    assert(out.meters[0] == 12 && out.meters[1] == 20 && out.lane[1] == 2);
    assert(out.drs[0] == RACE_DRS_AVAILABLE && out.drs[1] == RACE_DRS_LOCKED);
}

static void test_crossed_invites_pick_exactly_one_host(void)
{
    const uint8_t a[6] = {0x02, 0x00, 0x00, 0x00, 0x10, 0x0C};
    const uint8_t b[6] = {0x02, 0x00, 0x00, 0x00, 0x10, 0xB8};
    const uint8_t c[6] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(race_keeps_own_invite(a, b) && !race_keeps_own_invite(b, a));
    assert(race_keeps_own_invite(c, a) && !race_keeps_own_invite(a, c));
}

int main(void)
{
    test_track_is_deterministic_and_always_leaves_an_open_lane();
    test_equal_cars_on_empty_track_draw_in_thirty_seconds();
    test_nitro_goes_to_the_first_car_and_is_shared_within_100ms();
    test_nitro_and_bump_timing();
    test_drs_needs_a_gap_and_is_single_use();
    test_drs_stacks_with_nitro_and_bump_cancels_both();
    test_finish_order_and_tie_window();
    test_large_steps_do_not_skip_items();
    test_wire_round_trip();
    test_crossed_invites_pick_exactly_one_host();
    puts("race rules: PASS");
    return 0;
}
