#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define PTT_SAMPLES_PER_PACKET 160
#define PTT_AUDIO_PACKET_SIZE 168

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t sequence;
    uint16_t checksum;
    uint8_t audio[PTT_SAMPLES_PER_PACKET];
} ptt_audio_packet_t;

_Static_assert(sizeof(ptt_audio_packet_t) == PTT_AUDIO_PACKET_SIZE, "PTT packet size changed");

esp_err_t ptt_service_start(void);
bool ptt_service_available(void);
bool ptt_service_heap_allows(void);
bool ptt_service_is_transmitting(void);
void ptt_service_set_transmitting(bool active);
bool ptt_service_is_packet(const uint8_t *data, int len);
void ptt_service_on_packet(const uint8_t src[6], const uint8_t *data, int len);
