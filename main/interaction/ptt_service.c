#include "ptt_service.h"
#include "app_events.h"
#include "bsp_audio.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "espnow_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sound_service.h"
#include <string.h>

#define PTT_MAGIC 0x5450
#define PTT_VERSION 1
#define PTT_TYPE_AUDIO 1
#define PTT_MIN_HEAP (80 * 1024)

typedef struct { uint8_t src[6]; ptt_audio_packet_t packet; } ptt_rx_event_t;
static const char *TAG = "kp_ptt";
static QueueHandle_t s_rx_queue;
static volatile bool s_available;
static volatile bool s_transmitting;
static uint16_t s_sequence;

static uint16_t packet_crc(const ptt_audio_packet_t *packet)
{
    ptt_audio_packet_t copy = *packet;
    copy.checksum = 0;
    return esp_crc16_le(0xffff, (const uint8_t *)&copy, sizeof(copy));
}
static uint8_t linear_to_ulaw(int16_t sample)
{
    static const int16_t end[8] = {0xFF,0x1FF,0x3FF,0x7FF,0xFFF,0x1FFF,0x3FFF,0x7FFF};
    int pcm = sample;
    int mask = pcm < 0 ? 0x7F : 0xFF;
    if (pcm < 0) pcm = -pcm;
    if (pcm > 32635) pcm = 32635;
    pcm += 0x84;
    int segment = 0;
    while (segment < 8 && pcm > end[segment]) segment++;
    uint8_t value = segment >= 8 ? 0x7F : (uint8_t)((segment << 4) | ((pcm >> (segment + 3)) & 0x0F));
    return value ^ mask;
}
static int16_t ulaw_to_linear(uint8_t value)
{
    value = ~value;
    int t = ((value & 0x0F) << 3) + 0x84;
    t <<= (value & 0x70) >> 4;
    return (int16_t)((value & 0x80) ? (0x84 - t) : (t - 0x84));
}

bool ptt_service_heap_allows(void) { return esp_get_free_heap_size() >= PTT_MIN_HEAP; }
bool ptt_service_available(void) { return s_available && ptt_service_heap_allows() && espnow_service_has_peer(); }
bool ptt_service_is_transmitting(void) { return s_transmitting; }
void ptt_service_set_transmitting(bool active)
{
    if (active && !ptt_service_available()) return;
    s_transmitting = active;
    app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0);
}
bool ptt_service_is_packet(const uint8_t *data, int len)
{
    if (!data || len != sizeof(ptt_audio_packet_t)) return false;
    const ptt_audio_packet_t *packet = (const ptt_audio_packet_t *)data;
    return packet->magic == PTT_MAGIC && packet->version == PTT_VERSION && packet->type == PTT_TYPE_AUDIO;
}
void ptt_service_on_packet(const uint8_t src[6], const uint8_t *data, int len)
{
    if (!s_rx_queue || !ptt_service_is_packet(data, len)) return;
    uint8_t peer[6];
    if (!espnow_service_get_peer(peer) || memcmp(peer, src, 6) != 0) return;
    ptt_rx_event_t event;
    memcpy(event.src, src, 6);
    memcpy(&event.packet, data, sizeof(event.packet));
    xQueueSend(s_rx_queue, &event, 0);
}
static void mark_rx_unavailable(const char *reason)
{
    if (s_available) ESP_LOGE(TAG, "音频RX不可用: %s; Find页PTT将灰化", reason);
    s_available = false;
    s_transmitting = false;
    app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0);
}
static void transmit_session(void)
{
    if (!sound_service_audio_lock(pdMS_TO_TICKS(250))) return;
    if (bsp_audio_set_format(8000, 16, 1) != ESP_OK) {
        mark_rx_unavailable("8kHz codec format failed");
        sound_service_audio_unlock();
        return;
    }
    int16_t pcm[PTT_SAMPLES_PER_PACKET];
    ptt_audio_packet_t packet = {.magic = PTT_MAGIC, .version = PTT_VERSION, .type = PTT_TYPE_AUDIO};
    ESP_LOGI(TAG, "PTT TX start: 8kHz mono, 20ms, G.711 mu-law");
    while (s_transmitting) {
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            mark_rx_unavailable("bsp_audio_read failed");
            break;
        }
        packet.sequence = ++s_sequence;
        for (int i = 0; i < PTT_SAMPLES_PER_PACKET; i++) packet.audio[i] = linear_to_ulaw(pcm[i]);
        packet.checksum = packet_crc(&packet);
        esp_err_t err = espnow_service_send_raw_to_peer(&packet, sizeof(packet));
        if (err != ESP_OK) ESP_LOGW(TAG, "PTT packet send failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "PTT TX stop");
    sound_service_audio_unlock();
}
static void play_packet(const ptt_audio_packet_t *packet)
{
    if (packet->checksum != packet_crc(packet) || s_transmitting) return;
    if (!sound_service_audio_lock(pdMS_TO_TICKS(10))) return;
    if (bsp_audio_set_format(8000, 16, 1) == ESP_OK) {
        int16_t pcm[PTT_SAMPLES_PER_PACKET];
        for (int i = 0; i < PTT_SAMPLES_PER_PACKET; i++) pcm[i] = ulaw_to_linear(packet->audio[i]);
        bsp_audio_set_volume(100);
        if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) ESP_LOGW(TAG, "PTT playback write failed");
    }
    sound_service_audio_unlock();
}
static void ptt_task(void *arg)
{
    (void)arg;
    ptt_rx_event_t event;
    for (;;) {
        if (s_transmitting) { transmit_session(); continue; }
        if (xQueueReceive(s_rx_queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) play_packet(&event.packet);
    }
}
esp_err_t ptt_service_start(void)
{
    s_rx_queue = xQueueCreate(6, sizeof(ptt_rx_event_t));
    if (!s_rx_queue) return ESP_ERR_NO_MEM;
    s_available = true;
    if (xTaskCreatePinnedToCore(ptt_task, "kp_ptt", 3072, NULL, 4, NULL, 0) != pdPASS) {
        s_available = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "PTT ready; RX will be validated on first long-press");
    return ESP_OK;
}
