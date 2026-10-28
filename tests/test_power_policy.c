#include "power_policy.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    assert(power_policy_state(-1, false) == POWER_ACTIVE);
    assert(power_policy_state(0, false) == POWER_ACTIVE);
    assert(power_policy_state(29999, false) == POWER_ACTIVE);
    assert(power_policy_state(30000, false) == POWER_DIM);
    assert(power_policy_state(59999, false) == POWER_DIM);
    assert(power_policy_state(60000, false) == POWER_DEEP_SLEEP);
    assert(power_policy_state(INT64_MAX, false) == POWER_DEEP_SLEEP);
    assert(power_policy_state(INT64_MAX, true) == POWER_ACTIVE);
    return 0;
}
