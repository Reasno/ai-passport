#pragma once

#include "esp_err.h"
#include "espnow_service.h"
#include "race_rules.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RACE_PHASE_IDLE = 0,
    RACE_PHASE_INVITE_SENT,
    RACE_PHASE_INVITE_RECEIVED,
    RACE_PHASE_COUNTDOWN,
    RACE_PHASE_RACING,
    RACE_PHASE_RESULT,
} race_phase_t;

typedef enum {
    RACE_RESULT_NONE = 0,
    RACE_RESULT_WIN,
    RACE_RESULT_LOSE,
    RACE_RESULT_DRAW,
    RACE_RESULT_ABORTED,
} race_result_t;

/* Per-device view: index 0 is the local car, index 1 the opponent. */
typedef struct {
    race_phase_t phase;
    race_result_t result;
    bool paired;
    bool drs_pending;
    uint8_t countdown_s;
    uint8_t lane[2];
    uint8_t effect[2];
    uint8_t drs[2];
    uint32_t pos_mm[2];
    uint8_t rows[RACE_ROWS];
    uint32_t wins;
    uint32_t losses;
    char status[48];
} race_game_snapshot_t;

esp_err_t race_game_service_start(void);
void race_game_service_tick(int64_t now_ms);
void race_game_service_snapshot(race_game_snapshot_t *out);
void race_game_service_invite(void);
void race_game_service_respond_invite(bool accept);
void race_game_service_steer(int delta);
void race_game_service_request_drs(void);
void race_game_service_cancel(void);
bool race_game_service_owns_packet(uint8_t type);
void race_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *packet);
