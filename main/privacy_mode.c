#include "privacy_mode.h"
#include "sdkconfig.h"

static bool s_privacy_mode;

bool privacy_mode_is_active(void)
{
    return s_privacy_mode;
}

void privacy_mode_toggle(void)
{
    s_privacy_mode = !s_privacy_mode;
}

const char *privacy_mode_display_name(void)
{
    if (!s_privacy_mode) return CONFIG_KP_CHILD_NAME;
#ifdef CONFIG_KIDS_ACTOR_BROTHER
    return "李小帅";
#elif defined(CONFIG_KIDS_ACTOR_SISTER)
    return "王小美";
#else
    return CONFIG_KP_CHILD_NAME;
#endif
}
