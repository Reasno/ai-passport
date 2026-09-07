#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t mqtt_service_start(void);
void mqtt_service_on_ip_ready(void);
bool mqtt_service_publish_complete_task(const char *task_id);
bool mqtt_service_publish_redeem(const char *reward_id);
bool mqtt_service_publish_evidence_start(const char *request_id, const char *task_id, uint32_t max_duration_ms, size_t chunk_bytes);
bool mqtt_service_publish_evidence_chunk(const char *request_id, uint32_t seq, const uint8_t *audio, size_t len);
bool mqtt_service_publish_evidence_commit(const char *request_id, const char *task_id, uint32_t total_chunks, const char *audio_sha256, uint32_t duration_ms);
/* Find ring/ack carry the same 32-bit ts as the parallel ESP-NOW copy so the receiver
 * can collapse the two transports into a single ring. */
bool mqtt_service_publish_find_ring(uint32_t ts);
bool mqtt_service_publish_find_ack(const char *target_device_id, uint32_t ts);
void mqtt_service_resubscribe(void);
