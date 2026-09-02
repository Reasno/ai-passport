#pragma once

#include "esp_err.h"
#include "espnow_service.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUZZER_STATE_IDLE,
    BUZZER_STATE_INVITE_SENT,
    BUZZER_STATE_INVITE_RECEIVED,
    BUZZER_STATE_SYNCING,
    BUZZER_STATE_ARMED,
    BUZZER_STATE_GO,
    BUZZER_STATE_RESULT,
} buzzer_state_t;

typedef enum {
    BUZZER_RESULT_NONE,
    BUZZER_RESULT_WIN,
    BUZZER_RESULT_LOSE,
    BUZZER_RESULT_TIE,
    BUZZER_RESULT_TIMEOUT,
} buzzer_result_t;

typedef struct {
    buzzer_state_t state;
    buzzer_result_t result;
    bool paired;
    bool is_host;
    bool local_pressed;
    bool remote_pressed;
    bool local_false_start;
    uint8_t lights_on;
    uint16_t session;
    int32_t clock_offset_ms;
    uint16_t sync_rtt_ms;
    uint32_t seconds_left;
    char status[64];
} buzzer_game_snapshot_t;

esp_err_t buzzer_game_service_start(void);
void buzzer_game_service_tick(int64_t now_ms);
void buzzer_game_service_snapshot(buzzer_game_snapshot_t *out);
void buzzer_game_service_invite(void);
void buzzer_game_service_respond_invite(bool accept);
void buzzer_game_service_press(int64_t local_press_ms);
void buzzer_game_service_cancel(void);
void buzzer_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *packet);
bool buzzer_game_service_owns_packet(uint8_t type);
