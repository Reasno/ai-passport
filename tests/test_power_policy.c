#include "power_policy.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    for (int saver = 0; saver <= 1; saver++) {
        assert(power_policy_state(-1, false, saver) == POWER_ACTIVE);
        assert(power_policy_state(0, false, saver) == POWER_ACTIVE);
        assert(power_policy_state(29999, false, saver) == POWER_ACTIVE);
        assert(power_policy_state(30000, false, saver) == POWER_DIM);
        assert(power_policy_state(59999, false, saver) == POWER_DIM);
        assert(power_policy_state(INT64_MAX, true, saver) == POWER_ACTIVE);
    }
    assert(power_policy_state(60000, false, false) == POWER_STANDBY);
    assert(power_policy_state(INT64_MAX, false, false) == POWER_STANDBY);
    assert(power_policy_state(60000, false, true) == POWER_DEEP_SLEEP);
    assert(power_policy_state(INT64_MAX, false, true) == POWER_DEEP_SLEEP);
    return 0;
}
