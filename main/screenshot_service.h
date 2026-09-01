#pragma once

#include "esp_err.h"

/** Start the optional USB Serial/JTAG SCREENSHOT and PAGE debug command receiver. */
esp_err_t screenshot_service_start(void);
