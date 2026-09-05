#pragma once

#include "esp_err.h"
#include "espnow_service.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOLE_PHASE_IDLE = 0,
    MOLE_PHASE_INVITE_SENT = 1,
    MOLE_PHASE_INVITE_RECEIVED = 2,
    MOLE_PHASE_COUNTDOWN = 3,
    MOLE_PHASE_PLAYING = 4,
    MOLE_PHASE_RESULT = 5,
} mole_phase_t;

typedef enum {
    MOLE_RESULT_NONE = 0,
    MOLE_RESULT_WIN = 1,
    MOLE_RESULT_LOSE = 2,
    MOLE_RESULT_ABORTED = 3,
} mole_result_t;

typedef enum {
    MOLE_INPUT_UP = 1,
    MOLE_INPUT_DOWN,
    MOLE_INPUT_SHOOT,
    MOLE_INPUT_LEFT,
    MOLE_INPUT_RIGHT,
    MOLE_INPUT_RELOAD,
} mole_input_t;

typedef struct {
    mole_phase_t phase;
    mole_result_t result;
    bool paired;
    bool is_host;
    bool peer_connected;
    bool ammo_loaded;
    uint8_t reticle_cell;
    uint8_t mole_cell;
    uint8_t mole_generation;
    uint8_t hits;
    uint16_t remaining_ds;
    uint16_t session;
    uint32_t wins;
    uint32_t losses;
    char status[64];
} mole_game_snapshot_t;

esp_err_t mole_game_service_start(void);
void mole_game_service_tick(int64_t now_ms);
void mole_game_service_snapshot(mole_game_snapshot_t *out);
void mole_game_service_begin_session(void);
void mole_game_service_respond_invite(bool accept);
void mole_game_service_host_input(mole_input_t input);
void mole_game_service_client_input(mole_input_t input);
void mole_game_service_cancel(void);
void mole_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *packet);
bool mole_game_service_owns_packet(uint8_t type);
