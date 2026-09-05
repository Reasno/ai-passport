#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>

typedef enum {
    SOUND_TICK,
    SOUND_DING,
    SOUND_DU,
    SOUND_FANFARE,
    SOUND_FIND_RING,
    SOUND_BUZZER_GO,
    SOUND_MOLE_RELOAD,
    SOUND_MOLE_SHOOT,
    SOUND_MOLE_HIT,
} sound_effect_t;
esp_err_t sound_service_start(void);
void sound_service_play(sound_effect_t effect);
void sound_service_stop_ring(void);
bool sound_service_audio_lock(TickType_t wait);
void sound_service_audio_unlock(void);
