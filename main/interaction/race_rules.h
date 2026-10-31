#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pure rules for the two-car racing game. Car 0 is the host: the device that sent the
 * accepted invitation and referees the race. Car 1 is the invited peer. Distances are
 * millimetres, time is milliseconds. */
#define RACE_LANES 3
#define RACE_ROWS 27
#define RACE_TRACK_MM 900000U
#define RACE_ROW_FIRST_MM 60000U
#define RACE_ROW_SPACING_MM 30000U
#define RACE_BASE_SPEED_MM_S 30000U
#define RACE_BASE_KMH 120U
#define RACE_EFFECT_MS 2000U
#define RACE_DRS_MS 4000U
/* Speed bonuses add up: nitro + DRS run at 100 + 50 + 70 = 220 % of base speed. */
#define RACE_NITRO_BONUS_PERCENT 50U
#define RACE_DRS_BONUS_PERCENT 70U
#define RACE_DRS_GAP_MM 5000U
#define RACE_TIE_WINDOW_MS 100U
/* A car reaching a nitro can within this time after the other car took it gets it too. */
#define RACE_SHARE_WINDOW_MS 100U
#define RACE_REPORT_CLAIMS 4
/* Longer steps are simulated as this limit so a car never skips a row of items. */
#define RACE_MAX_STEP_MS 250U

typedef enum { RACE_ITEM_NONE = 0, RACE_ITEM_NITRO = 1, RACE_ITEM_BUMP = 2 } race_item_t;
typedef enum { RACE_EFFECT_NONE = 0, RACE_EFFECT_NITRO = 1, RACE_EFFECT_SLOW = 2 } race_effect_t;
typedef enum { RACE_DRS_LOCKED = 0, RACE_DRS_AVAILABLE = 1, RACE_DRS_ACTIVE = 2, RACE_DRS_USED = 3 } race_drs_t;
typedef enum { RACE_OUTCOME_PENDING = 0, RACE_OUTCOME_HOST = 1, RACE_OUTCOME_PEER = 2, RACE_OUTCOME_DRAW = 3 } race_outcome_t;

typedef struct {
    uint32_t pos_mm;
    uint32_t effect_until_ms;
    uint32_t drs_until_ms;
    uint32_t finish_ms;
    uint8_t lane;
    uint8_t effect;
    bool drs_used;
    bool finished;
} race_car_t;

typedef struct {
    uint32_t taken_ms;
    uint8_t row;
    uint8_t lane;
    bool open;
} race_share_t;

typedef struct {
    /* Two bits per lane: bits 0-1 lane 0, bits 2-3 lane 1, bits 4-5 lane 2. */
    uint8_t rows[RACE_ROWS];
    race_car_t cars[2];
    /* The latest nitro each car took, still claimable by the other car for RACE_SHARE_WINDOW_MS. */
    race_share_t share[2];
    uint32_t clock_ms;
    uint8_t outcome;
} race_t;

typedef struct {
    bool nitro[2];
    bool bump[2];
    uint8_t claimed_count;
    uint8_t claimed_row[RACE_REPORT_CLAIMS];
    uint8_t claimed_lane[RACE_REPORT_CLAIMS];
    uint8_t claimed_car[RACE_REPORT_CLAIMS];
} race_step_report_t;

/* Display state of both cars as carried by one 32-bit STATE payload. */
typedef struct {
    uint16_t meters[2];
    uint8_t lane[2];
    uint8_t effect[2];
    uint8_t drs[2];
} race_wire_state_t;

typedef enum { RACE_EVENT_NONE = 0, RACE_EVENT_NITRO_TAKEN = 1, RACE_EVENT_RESULT = 2 } race_event_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t row;
    uint8_t lane;
    uint8_t car;
    uint8_t outcome;
} race_event_t;

void race_init(race_t *race, uint32_t seed);
uint32_t race_row_mm(int row);
race_item_t race_item_at(const race_t *race, int row, int lane);
void race_take_item(race_t *race, int row, int lane);
void race_set_lane(race_t *race, int car, int lane);
race_drs_t race_drs_state(const race_t *race, int car);
/* Opens DRS when the car trails by at least RACE_DRS_GAP_MM and has not used it. */
bool race_request_drs(race_t *race, int car);
/* Speed as a percentage of the base speed. Slowdown overrides every boost. */
unsigned race_speed_percent(race_effect_t effect, bool drs_active);
unsigned race_car_speed_percent(const race_t *race, int car);
void race_step(race_t *race, uint32_t dt_ms, race_step_report_t *report);

uint32_t race_pack_state(const race_wire_state_t *state);
void race_unpack_state(uint32_t data, race_wire_state_t *state);
void race_wire_from_race(const race_t *race, race_wire_state_t *state);
uint32_t race_pack_event(const race_event_t *event);
void race_unpack_event(uint32_t data, race_event_t *event);
/* Crossed invitations: the device with the lower MAC keeps hosting its own invitation and the
 * other device joins it as the peer. */
bool race_keeps_own_invite(const uint8_t own_mac[6], const uint8_t other_mac[6]);
