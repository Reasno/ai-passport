#pragma once

#include <stdbool.h>

bool privacy_mode_is_active(void);
void privacy_mode_toggle(void);
const char *privacy_mode_display_name(void);
