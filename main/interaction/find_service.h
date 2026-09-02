#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** Which transports a single ring/ack actually went out on. The two stacks are never
 *  mutually exclusive: MQTT is best-effort, ESP-NOW is always attempted. */
typedef struct {
    bool mqtt;
    bool espnow;
} find_channels_t;

esp_err_t find_service_start(void);

/** Ring the paired sibling over every transport at once. Returns the transports used. */
find_channels_t find_service_ring(void);

/** Answer a ring. target_device_id may be NULL/empty to fall back to the configured peer. */
find_channels_t find_service_ack(const char *target_device_id);

/** Drive the ESP-NOW retry burst. Safe to call from the UI tick at any cadence. */
void find_service_tick(int64_t now_ms);

/** Transports currently believed usable, for status text ("WiFi+直连" / "仅直连"). */
find_channels_t find_service_available(void);

/** Short Chinese label describing a channel set. Never returns NULL. */
const char *find_service_channel_label(find_channels_t channels);

/** Inbound ESP-NOW ring/ack, already validated to come from the paired sibling. */
void find_service_on_espnow(bool ring, uint32_t ts);

/** Inbound MQTT ring/ack. ts==0 means the payload carried no token (legacy sender). */
void find_service_on_mqtt(bool ring, const char *sender, uint32_t ts);
