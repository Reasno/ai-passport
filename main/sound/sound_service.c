#include "sound_service.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

#define RATE 16000
#define CHUNK 160
static const char *TAG = "kp_sound";
static QueueHandle_t s_queue;

static void tone(float start_hz, float end_hz, int duration_ms, bool decay)
{
    int16_t pcm[CHUNK];
    int total = RATE * duration_ms / 1000;
    float phase = 0;
    for (int base = 0; base < total; base += CHUNK) {
        int n = total - base > CHUNK ? CHUNK : total - base;
        for (int i = 0; i < n; i++) {
            float progress = (float)(base + i) / (float)total;
            float hz = start_hz + (end_hz - start_hz) * progress;
            phase += 2.0f * 3.14159265f * hz / RATE;
            float envelope = decay ? (1.0f - progress) : 0.65f;
            pcm[i] = (int16_t)(sinf(phase) * 6500.0f * envelope);
        }
        bsp_audio_write(pcm, n * sizeof(int16_t));
    }
}
static void sound_task(void *arg)
{
    (void)arg;
    if (bsp_audio_set_format(RATE, 16, 1) != ESP_OK) { ESP_LOGE(TAG, "音频格式初始化失败"); vTaskDelete(NULL); }
    bsp_audio_set_volume(55);
    sound_effect_t effect;
    while (xQueueReceive(s_queue, &effect, portMAX_DELAY)) {
        if (effect == SOUND_TICK) tone(800, 800, 50, true);
        else if (effect == SOUND_DING) tone(440, 880, 200, false);
        else if (effect == SOUND_DU) tone(200, 200, 150, true);
        else { tone(523, 523, 150, false); tone(659, 659, 150, false); tone(784, 784, 200, false); }
    }
}
esp_err_t sound_service_start(void)
{
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) return err;
    s_queue = xQueueCreate(4, sizeof(sound_effect_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(sound_task, "kp_sound", 3072, NULL, 3, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void sound_service_play(sound_effect_t effect) { if (s_queue) xQueueSend(s_queue, &effect, 0); }
