#include "power_policy.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    for (int saver = 0; saver <= 1; saver++) {
        for (int usb = 0; usb <= 1; usb++) {
            assert(power_policy_state(-1, false, saver, usb) == POWER_ACTIVE);
            assert(power_policy_state(0, false, saver, usb) == POWER_ACTIVE);
            assert(power_policy_state(29999, false, saver, usb) == POWER_ACTIVE);
            assert(power_policy_state(30000, false, saver, usb) == POWER_DIM);
            assert(power_policy_state(59999, false, saver, usb) == POWER_DIM);
            assert(power_policy_state(INT64_MAX, true, saver, usb) == POWER_ACTIVE);
            power_state_t idle_state = saver && !usb ? POWER_DEEP_SLEEP : POWER_STANDBY;
            assert(power_policy_state(60000, false, saver, usb) == idle_state);
            assert(power_policy_state(INT64_MAX, false, saver, usb) == idle_state);
        }
    }
    return 0;
}
