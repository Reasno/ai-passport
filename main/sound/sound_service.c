#include "sound_service.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

#define RATE 16000
#define CHUNK 160
static const char *TAG = "kp_sound";
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_audio_lock;
static volatile bool s_stop_ring;

bool sound_service_audio_lock(TickType_t wait)
{
    return s_audio_lock && xSemaphoreTake(s_audio_lock, wait) == pdTRUE;
}
void sound_service_audio_unlock(void)
{
    if (s_audio_lock) xSemaphoreGive(s_audio_lock);
}

static void tone(float start_hz, float end_hz, int duration_ms, bool decay, bool interruptible)
{
    int16_t pcm[CHUNK];
    int total = RATE * duration_ms / 1000;
    float phase = 0;
    for (int base = 0; base < total; base += CHUNK) {
        if (interruptible && s_stop_ring) return;
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
static void find_ring(void)
{
    for (int round = 0; round < 3 && !s_stop_ring; round++) {
        tone(880, 880, 130, true, true);
        vTaskDelay(pdMS_TO_TICKS(80));
        if (s_stop_ring) break;
        tone(1175, 1175, 160, true, true);
        if (round < 2) vTaskDelay(pdMS_TO_TICKS(280));
    }
}
static void sound_task(void *arg)
{
    (void)arg;
    sound_effect_t effect;
    while (xQueueReceive(s_queue, &effect, portMAX_DELAY)) {
        if (!sound_service_audio_lock(portMAX_DELAY)) continue;
        if (bsp_audio_set_format(RATE, 16, 1) != ESP_OK) {
            ESP_LOGE(TAG, "音频格式初始化失败");
            sound_service_audio_unlock();
            continue;
        }
        bsp_audio_set_volume(100);
        if (effect == SOUND_TICK) tone(800, 800, 50, true, false);
        else if (effect == SOUND_DING) tone(440, 880, 200, false, false);
        else if (effect == SOUND_DU) tone(200, 200, 150, true, false);
        else if (effect == SOUND_FIND_RING) find_ring();
        else if (effect == SOUND_BUZZER_GO) tone(1600, 1600, 35, true, false);
        else { tone(523, 523, 150, false, false); tone(659, 659, 150, false, false); tone(784, 784, 200, false, false); }
        sound_service_audio_unlock();
    }
}
esp_err_t sound_service_start(void)
{
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) return err;
    s_audio_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(6, sizeof(sound_effect_t));
    if (!s_audio_lock || !s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(sound_task, "kp_sound", 3072, NULL, 3, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void sound_service_play(sound_effect_t effect)
{
    if (effect == SOUND_FIND_RING) s_stop_ring = false;
    if (s_queue) xQueueSend(s_queue, &effect, 0);
}
void sound_service_stop_ring(void)
{
    s_stop_ring = true;
}
