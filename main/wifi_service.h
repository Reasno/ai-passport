#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t wifi_service_start(void);
bool wifi_service_is_started(void);
