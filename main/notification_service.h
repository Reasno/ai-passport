#pragma once

#include "esp_err.h"
#include "notification_store.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t notification_service_init(void);
/* Called by the MQTT event task. A valid payload atomically replaces the full
 * unread snapshot and posts APP_EVT_NOTIFICATION_SNAPSHOT for the UI task. */
esp_err_t notification_service_apply_json(const char *json, size_t len);
bool notification_service_current(notification_item_t *item, size_t *position, size_t *count);
bool notification_service_next(void);
bool notification_service_mark_read(const char *notification_id);
