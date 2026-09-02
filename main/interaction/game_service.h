#pragma once
#include "espnow_service.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { GAME_STATE_IDLE, GAME_STATE_PAIRING, GAME_STATE_INVITE_SENT, GAME_STATE_INVITE_RECEIVED,
               GAME_STATE_COUNTDOWN, GAME_STATE_WAITING_CHOICE, GAME_STATE_REVEAL, GAME_STATE_RESULT } game_state_t;
typedef enum { RPS_NONE, RPS_ROCK = 1, RPS_SCISSORS = 2, RPS_PAPER = 3 } rps_choice_t;
typedef struct {
    game_state_t state; bool paired; bool radar_active; bool peer_nearby;
    int8_t rssi; uint8_t distance_bars; uint16_t session;
    rps_choice_t local_choice, remote_choice, cursor_choice; int8_t result;
    uint8_t countdown; uint32_t seconds_left; char status[64];
} game_snapshot_t;
esp_err_t game_service_start(void);
void game_service_tick(int64_t now_ms);
void game_service_snapshot(game_snapshot_t *out);
bool game_service_heap_allows_pairing(void);
bool game_service_heap_allows_radar(void);
bool game_service_heap_allows_rps(void);
void game_service_start_pairing(void);
void game_service_cancel(void);
void game_service_set_radar(bool active);
void game_service_invite_rps(void);
void game_service_respond_invite(bool accept);
void game_service_move_choice(int delta);
void game_service_choose(rps_choice_t choice);
void game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *packet, int8_t rssi);
