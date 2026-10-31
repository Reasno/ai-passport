#include "race_rules.h"
#include <string.h>

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static race_effect_t active_effect(const race_car_t *car, uint32_t clock_ms)
{
    return car->effect != RACE_EFFECT_NONE && clock_ms < car->effect_until_ms
               ? (race_effect_t)car->effect : RACE_EFFECT_NONE;
}

static bool drs_active(const race_car_t *car, uint32_t clock_ms)
{
    return car->drs_used && clock_ms < car->drs_until_ms;
}

void race_init(race_t *race, uint32_t seed)
{
    memset(race, 0, sizeof(*race));
    race->cars[0].lane = 1;
    race->cars[1].lane = 1;
    uint32_t state = seed ? seed : 0x9E3779B9U;
    for (int row = 0; row < RACE_ROWS; ++row) {
        int open_lane = (int)(next_random(&state) % RACE_LANES);
        uint8_t packed = 0;
        for (int lane = 0; lane < RACE_LANES; ++lane) {
            unsigned roll = next_random(&state) % 10U;
            if (lane == open_lane) continue;
            race_item_t item = roll < 4 ? RACE_ITEM_NITRO : roll < 8 ? RACE_ITEM_BUMP : RACE_ITEM_NONE;
            packed |= (uint8_t)(item << (lane * 2));
        }
        race->rows[row] = packed;
    }
}

uint32_t race_row_mm(int row)
{
    return RACE_ROW_FIRST_MM + (uint32_t)row * RACE_ROW_SPACING_MM;
}

race_item_t race_item_at(const race_t *race, int row, int lane)
{
    if (row < 0 || row >= RACE_ROWS || lane < 0 || lane >= RACE_LANES) return RACE_ITEM_NONE;
    return (race_item_t)((race->rows[row] >> (lane * 2)) & 0x03U);
}

void race_take_item(race_t *race, int row, int lane)
{
    if (row < 0 || row >= RACE_ROWS || lane < 0 || lane >= RACE_LANES) return;
    race->rows[row] &= (uint8_t)~(0x03U << (lane * 2));
}

void race_set_lane(race_t *race, int car, int lane)
{
    if (car < 0 || car > 1 || race->cars[car].finished) return;
    if (lane < 0) lane = 0;
    if (lane >= RACE_LANES) lane = RACE_LANES - 1;
    race->cars[car].lane = (uint8_t)lane;
}

race_drs_t race_drs_state(const race_t *race, int car)
{
    const race_car_t *self = &race->cars[car];
    const race_car_t *other = &race->cars[car ^ 1];
    if (self->drs_used) return drs_active(self, race->clock_ms) ? RACE_DRS_ACTIVE : RACE_DRS_USED;
    if (race->outcome != RACE_OUTCOME_PENDING || self->finished) return RACE_DRS_LOCKED;
    return other->pos_mm >= self->pos_mm + RACE_DRS_GAP_MM ? RACE_DRS_AVAILABLE : RACE_DRS_LOCKED;
}

bool race_request_drs(race_t *race, int car)
{
    if (car < 0 || car > 1 || race_drs_state(race, car) != RACE_DRS_AVAILABLE) return false;
    race->cars[car].drs_used = true;
    race->cars[car].drs_until_ms = race->clock_ms + RACE_DRS_MS;
    return true;
}

unsigned race_speed_percent(race_effect_t effect, bool drs)
{
    if (effect == RACE_EFFECT_SLOW) return 50;
    return 100U + (effect == RACE_EFFECT_NITRO ? RACE_NITRO_BONUS_PERCENT : 0U) +
           (drs ? RACE_DRS_BONUS_PERCENT : 0U);
}

unsigned race_car_speed_percent(const race_t *race, int car)
{
    const race_car_t *c = &race->cars[car];
    if (c->finished) return 0;
    return race_speed_percent(active_effect(c, race->clock_ms), drs_active(c, race->clock_ms));
}

static void apply_item(race_t *race, int car, int row, uint32_t cross_ms, uint32_t end_ms,
                       race_step_report_t *report)
{
    race_car_t *c = &race->cars[car];
    race_item_t item = race_item_at(race, row, c->lane);
    const race_share_t *other = &race->share[car ^ 1];
    bool shared = item == RACE_ITEM_NONE && other->open && other->row == row && other->lane == c->lane &&
                  cross_ms - other->taken_ms < RACE_SHARE_WINDOW_MS;
    if (item == RACE_ITEM_NITRO || shared) {
        if (item == RACE_ITEM_NITRO) {
            race_take_item(race, row, c->lane);
            race->share[car] = (race_share_t){.taken_ms = cross_ms, .row = (uint8_t)row, .lane = c->lane, .open = true};
        }
        c->effect = RACE_EFFECT_NITRO;
        c->effect_until_ms = end_ms + RACE_EFFECT_MS;
        if (report) {
            report->nitro[car] = true;
            if (report->claimed_count < RACE_REPORT_CLAIMS) {
                report->claimed_row[report->claimed_count] = (uint8_t)row;
                report->claimed_lane[report->claimed_count] = c->lane;
                report->claimed_car[report->claimed_count] = (uint8_t)car;
                report->claimed_count++;
            }
        }
    } else if (item == RACE_ITEM_BUMP) {
        c->effect = RACE_EFFECT_SLOW;
        c->effect_until_ms = end_ms + RACE_EFFECT_MS;
        /* A speed bump ends an open DRS window; the single use is still spent. */
        if (drs_active(c, race->clock_ms)) c->drs_until_ms = race->clock_ms;
        if (report) report->bump[car] = true;
    }
}

static void update_outcome(race_t *race)
{
    const race_car_t *a = &race->cars[0];
    const race_car_t *b = &race->cars[1];
    if (race->outcome != RACE_OUTCOME_PENDING) return;
    if (a->finished && b->finished) {
        uint32_t diff = a->finish_ms > b->finish_ms ? a->finish_ms - b->finish_ms : b->finish_ms - a->finish_ms;
        race->outcome = diff < RACE_TIE_WINDOW_MS ? RACE_OUTCOME_DRAW
                        : a->finish_ms < b->finish_ms ? RACE_OUTCOME_HOST : RACE_OUTCOME_PEER;
    } else if (a->finished && race->clock_ms >= a->finish_ms + RACE_TIE_WINDOW_MS) {
        race->outcome = RACE_OUTCOME_HOST;
    } else if (b->finished && race->clock_ms >= b->finish_ms + RACE_TIE_WINDOW_MS) {
        race->outcome = RACE_OUTCOME_PEER;
    }
}

static void substep(race_t *race, uint32_t step_ms, race_step_report_t *report)
{
    uint32_t start = race->clock_ms;
    uint32_t end = start + step_ms;
    uint32_t prev[2], next[2], speed[2];
    for (int car = 0; car < 2; ++car) {
        prev[car] = race->cars[car].pos_mm;
        speed[car] = RACE_BASE_SPEED_MM_S * race_car_speed_percent(race, car) / 100U;
        next[car] = prev[car] + speed[car] * step_ms / 1000U;
    }
    for (int row = 0; row < RACE_ROWS; ++row) {
        uint32_t mark = race_row_mm(row);
        bool crossed[2];
        uint32_t cross_ms[2] = {0, 0};
        for (int car = 0; car < 2; ++car) {
            crossed[car] = speed[car] > 0 && prev[car] < mark && next[car] >= mark;
            if (crossed[car]) cross_ms[car] = start + (uint32_t)((uint64_t)(mark - prev[car]) * 1000U / speed[car]);
        }
        if (!crossed[0] && !crossed[1]) continue;
        int first = crossed[0] ? 0 : 1;
        if (crossed[0] && crossed[1]) {
            /* Process the earlier crossing first; the later car can still share its nitro. */
            uint64_t t0 = (uint64_t)(mark - prev[0]) * speed[1];
            uint64_t t1 = (uint64_t)(mark - prev[1]) * speed[0];
            first = t1 < t0 ? 1 : 0;
        }
        apply_item(race, first, row, cross_ms[first], end, report);
        if (crossed[first ^ 1]) apply_item(race, first ^ 1, row, cross_ms[first ^ 1], end, report);
    }
    for (int car = 0; car < 2; ++car) {
        race_car_t *c = &race->cars[car];
        if (!c->finished && next[car] >= RACE_TRACK_MM) {
            c->finished = true;
            c->finish_ms = start + (uint32_t)((uint64_t)(RACE_TRACK_MM - prev[car]) * 1000U / speed[car]);
            next[car] = RACE_TRACK_MM;
        }
        c->pos_mm = next[car];
    }
    race->clock_ms = end;
    update_outcome(race);
}

void race_step(race_t *race, uint32_t dt_ms, race_step_report_t *report)
{
    if (report) memset(report, 0, sizeof(*report));
    while (dt_ms > 0 && race->outcome == RACE_OUTCOME_PENDING) {
        uint32_t step = dt_ms > RACE_MAX_STEP_MS ? RACE_MAX_STEP_MS : dt_ms;
        substep(race, step, report);
        dt_ms -= step;
    }
}

uint32_t race_pack_state(const race_wire_state_t *s)
{
    uint32_t data = 0;
    for (int car = 0; car < 2; ++car) {
        uint32_t meters = s->meters[car] > 1023U ? 1023U : s->meters[car];
        uint32_t fields = meters | (uint32_t)(s->lane[car] & 0x03U) << 10 |
                          (uint32_t)(s->effect[car] & 0x03U) << 12 | (uint32_t)(s->drs[car] & 0x03U) << 14;
        data |= fields << (car * 16);
    }
    return data;
}

void race_unpack_state(uint32_t data, race_wire_state_t *s)
{
    for (int car = 0; car < 2; ++car) {
        uint32_t fields = (data >> (car * 16)) & 0xFFFFU;
        s->meters[car] = (uint16_t)(fields & 0x03FFU);
        s->lane[car] = (uint8_t)((fields >> 10) & 0x03U);
        s->effect[car] = (uint8_t)((fields >> 12) & 0x03U);
        s->drs[car] = (uint8_t)((fields >> 14) & 0x03U);
    }
}

void race_wire_from_race(const race_t *race, race_wire_state_t *s)
{
    for (int car = 0; car < 2; ++car) {
        const race_car_t *c = &race->cars[car];
        s->meters[car] = (uint16_t)(c->pos_mm / 1000U);
        s->lane[car] = c->lane;
        s->effect[car] = (uint8_t)active_effect(c, race->clock_ms);
        s->drs[car] = (uint8_t)race_drs_state(race, car);
    }
}

uint32_t race_pack_event(const race_event_t *e)
{
    return (uint32_t)(e->kind & 0x03U) | (uint32_t)(e->row & 0x1FU) << 2 |
           (uint32_t)(e->lane & 0x03U) << 7 | (uint32_t)(e->car & 0x01U) << 9 |
           (uint32_t)(e->outcome & 0x03U) << 10;
}

void race_unpack_event(uint32_t data, race_event_t *e)
{
    e->kind = (uint8_t)(data & 0x03U);
    e->row = (uint8_t)((data >> 2) & 0x1FU);
    e->lane = (uint8_t)((data >> 7) & 0x03U);
    e->car = (uint8_t)((data >> 9) & 0x01U);
    e->outcome = (uint8_t)((data >> 10) & 0x03U);
}

bool race_keeps_own_invite(const uint8_t own_mac[6], const uint8_t other_mac[6])
{
    return memcmp(own_mac, other_mac, 6) < 0;
}
