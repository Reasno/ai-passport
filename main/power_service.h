#pragma once
#include "esp_err.h"
#include <stdbool.h>
/* Loads the persisted power-saving mode. When it is off, idle time dims the
 * display and then enters connected standby; when it is on, idle time ends in
 * terminal deep sleep that only the hardware power button can leave.
 * A USB host connection keeps connected standby even in power-saving mode. */
esp_err_t power_service_start(void);
/* Wake immediately for remote Find alerts without consuming a later key. */
void power_service_wake(void);
/* Returns true when this key only woke a dim/off display and must be consumed. */
bool power_service_key_activity(void);
/* UI worker only: toggles and persists power-saving mode; returns the new state. */
bool power_service_toggle_saver(void);
/* Any task may read the current power-saving mode, for example for status indicators. */
bool power_service_saver_enabled(void);
/* UI worker only: no timer/button callback may perform terminal shutdown.
 * busy defers deep sleep while recording, submitting, or showing a lottery result.
 * USB host disconnection starts a fresh idle interval without changing the
 * persisted mode. No terminal shutdown runs while the USB host is connected. */
void power_service_poll(bool busy);
