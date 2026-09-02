#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t mqtt_service_start(void);
void mqtt_service_on_ip_ready(void);
bool mqtt_service_publish_complete_task(const char *task_id);
bool mqtt_service_publish_redeem(const char *reward_id);
/* Find ring/ack carry the same 32-bit ts as the parallel ESP-NOW copy so the receiver
 * can collapse the two transports into a single ring. */
bool mqtt_service_publish_find_ring(uint32_t ts);
bool mqtt_service_publish_find_ack(const char *target_device_id, uint32_t ts);
void mqtt_service_resubscribe(void);
