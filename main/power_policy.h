#pragma once
#include <stdbool.h>
#include <stdint.h>

#define POWER_DIM_MS 30000
#define POWER_SLEEP_MS 60000
/* STANDBY keeps connections with the display off; DEEP_SLEEP is power-saving mode only. */
typedef enum { POWER_ACTIVE, POWER_DIM, POWER_STANDBY, POWER_DEEP_SLEEP } power_state_t;
power_state_t power_policy_state(int64_t idle_ms, bool busy, bool saver);
