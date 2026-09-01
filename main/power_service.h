#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t power_service_start(void);
/* Wake immediately for remote Find alerts without consuming a later key. */
void power_service_wake(void);
/* Returns true when this key only woke a dim/off display and must be consumed. */
bool power_service_key_activity(void);
