#include "power_policy.h"

power_state_t power_policy_state(int64_t idle_ms, bool busy)
{
    if (busy || idle_ms < POWER_DIM_MS) return POWER_ACTIVE;
    return idle_ms < POWER_SLEEP_MS ? POWER_DIM : POWER_DEEP_SLEEP;
}
