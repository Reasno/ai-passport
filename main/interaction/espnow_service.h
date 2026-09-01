#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESPNOW_GAME_PACKET_SIZE 16

typedef enum {
    ESPNOW_MSG_PAIR_REQ = 1,
    ESPNOW_MSG_PAIR_ACK,
    ESPNOW_MSG_RADAR_PING,
    ESPNOW_MSG_RADAR_PONG,
    ESPNOW_MSG_RPS_INVITE,
    ESPNOW_MSG_RPS_ACCEPT,
    ESPNOW_MSG_RPS_REJECT,
    ESPNOW_MSG_RPS_CHOICE,
    ESPNOW_MSG_RPS_CANCEL,
} espnow_msg_type_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t actor;
    uint8_t choice;
    uint16_t session;
    uint16_t sequence;
    int8_t value;
    uint8_t flags;
    uint16_t checksum;
    uint16_t reserved;
} espnow_game_packet_t;

_Static_assert(sizeof(espnow_game_packet_t) == ESPNOW_GAME_PACKET_SIZE, "ESP-NOW packet must remain 16 bytes");

esp_err_t espnow_service_start(void);
bool espnow_service_ready(void);
bool espnow_service_has_peer(void);
bool espnow_service_get_peer(uint8_t mac[6]);
esp_err_t espnow_service_clear_peer(void);
esp_err_t espnow_service_store_peer(const uint8_t mac[6]);
esp_err_t espnow_service_send(espnow_msg_type_t type, uint16_t session, uint16_t sequence,
                              uint8_t choice, int8_t value, bool broadcast);
esp_err_t espnow_service_send_raw_to_peer(const void *data, size_t len);
