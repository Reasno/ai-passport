#pragma once
#include "esp_err.h"
typedef enum { SOUND_TICK, SOUND_DING, SOUND_DU, SOUND_FANFARE } sound_effect_t;
esp_err_t sound_service_start(void);
void sound_service_play(sound_effect_t effect);
