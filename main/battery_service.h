#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    bool valid;
    int percent;
} battery_snapshot_t;

esp_err_t battery_service_start(void);
/* Cooperative stop; no I2C read can remain in flight on success. */
esp_err_t battery_service_stop(void);
void battery_service_snapshot(battery_snapshot_t *out);
