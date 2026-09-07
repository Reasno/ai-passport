#include "evidence_service.h"
#include "app_events.h"
#include "app_model.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mqtt_service.h"
#include "sound_service.h"
#include <stdio.h>
#include <string.h>

#define EVIDENCE_MAX_DURATION_MS 15000
#define EVIDENCE_TIMEOUT_MS 90000
#define EVIDENCE_CHUNK_BYTES 480
#define EVIDENCE_SAMPLES_PER_PACKET 160

static const char *TAG = "kp_evidence";

typedef struct {
    bool available;
    bool active;
    bool stop_requested;
    bool recording;
    bool uploading;
    bool waiting_review;
    uint32_t elapsed_ms;
    uint32_t max_duration_ms;
    int64_t started_ms;
    char task_id[APP_ID_LEN];
    char task_name[APP_NAME_LEN];
    char request_id[64];
} evidence_state_t;

static evidence_state_t s_state;
static SemaphoreHandle_t s_mutex;

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

static void post_status(const char *text, bool ok)
{
    app_event_t event = {.type = APP_EVT_STATUS_UPDATE, .ok = ok};
    strlcpy(event.text, text, sizeof(event.text));
    app_event_post(&event, 0);
}

static void post_error(const char *text)
{
    app_event_t event = {.type = APP_EVT_DATA_ERROR};
    strlcpy(event.text, text, sizeof(event.text));
    app_event_post(&event, 0);
}

static void state_lock(void) { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void state_unlock(void) { xSemaphoreGive(s_mutex); }

static void reset_state_locked(void)
{
    s_state.active = false;
    s_state.stop_requested = false;
    s_state.recording = false;
    s_state.uploading = false;
    s_state.waiting_review = false;
    s_state.elapsed_ms = 0;
    s_state.started_ms = 0;
    s_state.task_id[0] = 0;
    s_state.task_name[0] = 0;
    s_state.request_id[0] = 0;
}

static void build_sha256_hex(const unsigned char digest[32], char out[65])
{
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
    out[64] = 0;
}

static void evidence_task(void *arg)
{
    (void)arg;
    int16_t pcm[EVIDENCE_SAMPLES_PER_PACKET];
    uint8_t ulaw[EVIDENCE_SAMPLES_PER_PACKET];
    uint8_t chunk[EVIDENCE_CHUNK_BYTES];
    for (;;) {
        state_lock();
        bool active = s_state.active && !s_state.waiting_review;
        char task_id[APP_ID_LEN];
        char task_name[APP_NAME_LEN];
        strlcpy(task_id, s_state.task_id, sizeof(task_id));
        strlcpy(task_name, s_state.task_name, sizeof(task_name));
        state_unlock();
        if (!active) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!sound_service_audio_lock(pdMS_TO_TICKS(250))) {
            post_error("录音不可用，请稍后再试");
            state_lock();
            reset_state_locked();
            state_unlock();
            continue;
        }
        if (bsp_audio_set_format(8000, 16, 1) != ESP_OK) {
            sound_service_audio_unlock();
            post_error("录音初始化失败");
            state_lock();
            reset_state_locked();
            state_unlock();
            continue;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        char request_id[64];
        snprintf(request_id, sizeof(request_id), "evi_%lld", (long long)now_ms);
        if (!mqtt_service_publish_evidence_start(request_id, task_id, EVIDENCE_MAX_DURATION_MS, EVIDENCE_CHUNK_BYTES)) {
            sound_service_audio_unlock();
            post_error("语音会话启动失败，请检查网络");
            state_lock();
            reset_state_locked();
            state_unlock();
            continue;
        }
        state_lock();
        s_state.recording = true;
        s_state.uploading = false;
        s_state.waiting_review = false;
        s_state.started_ms = now_ms;
        strlcpy(s_state.request_id, request_id, sizeof(s_state.request_id));
        state_unlock();
        post_status("正在录音，松开 B2 发送", true);
        size_t chunk_fill = 0;
        uint32_t seq = 0;
        mbedtls_sha256_context sha_ctx;
        mbedtls_sha256_init(&sha_ctx);
        mbedtls_sha256_starts(&sha_ctx, 0);
        for (;;) {
            if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
                post_error("录音失败，请重试");
                break;
            }
            for (int i = 0; i < EVIDENCE_SAMPLES_PER_PACKET; i++) ulaw[i] = linear_to_ulaw(pcm[i]);
            mbedtls_sha256_update(&sha_ctx, ulaw, sizeof(ulaw));
            size_t copied = 0;
            while (copied < sizeof(ulaw)) {
                size_t remain = EVIDENCE_CHUNK_BYTES - chunk_fill;
                size_t take = sizeof(ulaw) - copied < remain ? sizeof(ulaw) - copied : remain;
                memcpy(chunk + chunk_fill, ulaw + copied, take);
                chunk_fill += take;
                copied += take;
                if (chunk_fill == EVIDENCE_CHUNK_BYTES) {
                    if (!mqtt_service_publish_evidence_chunk(request_id, ++seq, chunk, chunk_fill)) {
                        post_error("语音发送失败，请重试");
                        chunk_fill = 0;
                        goto transmit_end;
                    }
                    chunk_fill = 0;
                }
            }
            state_lock();
            s_state.elapsed_ms = (uint32_t)((esp_timer_get_time() / 1000) - s_state.started_ms);
            bool stop_requested = s_state.stop_requested || s_state.elapsed_ms >= s_state.max_duration_ms;
            state_unlock();
            if (stop_requested) break;
        }
        state_lock();
        s_state.recording = false;
        s_state.uploading = true;
        state_unlock();
        post_status("正在发送语音...", true);
        if (chunk_fill > 0 && !mqtt_service_publish_evidence_chunk(request_id, ++seq, chunk, chunk_fill)) {
            post_error("语音发送失败，请重试");
            goto transmit_end;
        }
        unsigned char digest[32];
        char sha_hex[65];
        mbedtls_sha256_finish(&sha_ctx, digest);
        build_sha256_hex(digest, sha_hex);
        state_lock();
        uint32_t duration_ms = s_state.elapsed_ms;
        state_unlock();
        if (!mqtt_service_publish_evidence_commit(request_id, task_id, seq, sha_hex, duration_ms)) {
            post_error("语音提交失败，请重试");
            goto transmit_end;
        }
        state_lock();
        s_state.uploading = false;
        s_state.waiting_review = true;
        s_state.stop_requested = false;
        state_unlock();
        app_model_begin_pending(APP_PENDING_EVIDENCE, request_id, task_id, esp_timer_get_time() / 1000 + EVIDENCE_TIMEOUT_MS);
        post_status("语音已发送，AI 正在检查...", true);
transmit_end:
        mbedtls_sha256_free(&sha_ctx);
        sound_service_audio_unlock();
        state_lock();
        if (!s_state.waiting_review) reset_state_locked();
        state_unlock();
    }
}

esp_err_t evidence_service_start(void)
{
    if (s_mutex) return ESP_OK;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(&s_state, 0, sizeof(s_state));
    s_state.available = true;
    s_state.max_duration_ms = EVIDENCE_MAX_DURATION_MS;
    if (xTaskCreatePinnedToCore(evidence_task, "kp_evidence", 4096, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool evidence_service_available(void)
{
    return s_mutex != NULL && s_state.available;
}

bool evidence_service_begin(const char *task_id, const char *task_name)
{
    if (!task_id || !task_name || !evidence_service_available()) return false;
    app_model_snapshot_t model;
    app_model_snapshot(&model);
    if (!model.mqtt_online || model.pending_type != APP_PENDING_NONE) return false;
    state_lock();
    if (s_state.active) {
        state_unlock();
        return false;
    }
    s_state.active = true;
    s_state.stop_requested = false;
    s_state.recording = false;
    s_state.uploading = false;
    s_state.waiting_review = false;
    s_state.elapsed_ms = 0;
    s_state.max_duration_ms = EVIDENCE_MAX_DURATION_MS;
    strlcpy(s_state.task_id, task_id, sizeof(s_state.task_id));
    strlcpy(s_state.task_name, task_name, sizeof(s_state.task_name));
    state_unlock();
    return true;
}

void evidence_service_stop(void)
{
    if (!s_mutex) return;
    state_lock();
    if (s_state.active && s_state.recording) s_state.stop_requested = true;
    state_unlock();
}

void evidence_service_finish(void)
{
    if (!s_mutex) return;
    state_lock();
    reset_state_locked();
    state_unlock();
}

void evidence_service_snapshot(evidence_service_snapshot_t *out)
{
    if (!out || !s_mutex) return;
    state_lock();
    out->available = s_state.available;
    out->active = s_state.active;
    out->recording = s_state.recording;
    out->uploading = s_state.uploading;
    out->waiting_review = s_state.waiting_review;
    out->elapsed_ms = s_state.elapsed_ms;
    out->max_duration_ms = s_state.max_duration_ms;
    strlcpy(out->task_id, s_state.task_id, sizeof(out->task_id));
    strlcpy(out->task_name, s_state.task_name, sizeof(out->task_name));
    state_unlock();
}
