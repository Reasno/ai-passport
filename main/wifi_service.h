#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t wifi_service_start(void);
bool wifi_service_is_started(void);
/* Terminal stop: suppress retries and shut down MQTT, SNTP and Wi-Fi. */
esp_err_t wifi_service_stop(void);
