#include "power_policy.h"

power_state_t power_policy_state(int64_t idle_ms, bool busy, bool saver, bool usb_connected)
{
    if (busy || idle_ms < POWER_DIM_MS) return POWER_ACTIVE;
    if (idle_ms < POWER_SLEEP_MS) return POWER_DIM;
    return saver && !usb_connected ? POWER_DEEP_SLEEP : POWER_STANDBY;
}
