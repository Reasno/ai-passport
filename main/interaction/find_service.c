/* Dual-stack "find my sibling" ring.
 *
 * A ring is published on MQTT (so Home Assistant can see it) *and* sent straight over
 * ESP-NOW (so it still works outdoors with no Wi-Fi). Both copies carry the same 32-bit
 * `ts` token, and the receiver keys de-duplication on that token, so whichever copy
 * arrives first rings the device exactly once.
 */
#include "find_service.h"
#include "app_events.h"
#include "app_model.h"
#include "espnow_service.h"
#include "mqtt_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define DEDUP_SLOTS 8
#define DEDUP_WINDOW_MS 15000
/* Wi-Fi may be mid-scan while disconnected, so repeat the ESP-NOW copy a few times.
 * The receiver collapses them by ts, so extra copies never cause a second ring. */
#define BURST_COPIES 3
#define BURST_GAP_MS 250

typedef struct {
    uint32_t ts[DEDUP_SLOTS];
    int64_t seen_ms[DEDUP_SLOTS];
    uint8_t next;
} dedup_table_t;

typedef struct {
    bool active;
    uint32_t ts;
    uint8_t remaining;
    int64_t next_ms;
} burst_t;

static const char *TAG = "kp_find";
static SemaphoreHandle_t s_lock;
static dedup_table_t s_ring_seen, s_ack_seen;
static burst_t s_ring_burst, s_ack_burst;
static app_model_snapshot_t s_find_model;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static uint32_t new_token(void)
{
    uint32_t ts = (uint32_t)now_ms();
    return ts ? ts : 1; /* 0 is reserved for "no token supplied". */
}

/* Returns true when this ts has not been handled recently, and claims it. */
static bool claim(dedup_table_t *table, uint32_t ts, int64_t now)
{
    if (ts == 0) return true; /* Legacy sender without a token: cannot de-duplicate. */
    for (int i = 0; i < DEDUP_SLOTS; i++) {
        if (table->ts[i] == ts && now - table->seen_ms[i] < DEDUP_WINDOW_MS) return false;
    }
    table->ts[table->next] = ts;
    table->seen_ms[table->next] = now;
    table->next = (uint8_t)((table->next + 1) % DEDUP_SLOTS);
    return true;
}

static void arm_burst(burst_t *burst, uint32_t ts, int64_t now)
{
    burst->active = BURST_COPIES > 1;
    burst->ts = ts;
    burst->remaining = BURST_COPIES > 1 ? (uint8_t)(BURST_COPIES - 1) : 0;
    burst->next_ms = now + BURST_GAP_MS;
}

static void post_find(bool ring, const char *sender)
{
    app_event_t event = {.type = ring ? APP_EVT_FIND_RING : APP_EVT_FIND_ACK};
    strlcpy(event.text, sender && sender[0] ? sender : CONFIG_KIDS_PEER_DEVICE_ID, sizeof(event.text));
    app_event_post(&event, 0);
}

/* Both inbound transports converge here so a single ts can only ring once. */
static void handle_inbound(bool ring, const char *sender, uint32_t ts, const char *via)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = claim(ring ? &s_ring_seen : &s_ack_seen, ts, now_ms());
    xSemaphoreGive(s_lock);
    if (!fresh) {
        ESP_LOGI(TAG, "%s via=%s ts=%lu duplicate, ignored", ring ? "ring" : "ack", via, (unsigned long)ts);
        return;
    }
    ESP_LOGI(TAG, "%s via=%s ts=%lu from=%s", ring ? "ring" : "ack", via, (unsigned long)ts,
             sender && sender[0] ? sender : "(peer)");
    post_find(ring, sender);
}

esp_err_t find_service_start(void)
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "dual-stack find ready burst=%dx%dms window=%dms", BURST_COPIES, BURST_GAP_MS, DEDUP_WINDOW_MS);
    return ESP_OK;
}

find_channels_t find_service_available(void)
{
    app_model_snapshot(&s_find_model);
    return (find_channels_t){
        .mqtt = s_find_model.mqtt_online,
        .espnow = espnow_service_ready() && espnow_service_has_peer(),
    };
}

const char *find_service_channel_label(find_channels_t channels)
{
    if (channels.mqtt && channels.espnow) return "WiFi+直连";
    if (channels.mqtt) return "仅 WiFi";
    if (channels.espnow) return "仅直连";
    return "无可用通道";
}

/* MQTT and ESP-NOW are attempted independently; neither failure suppresses the other. */
static find_channels_t send_both(bool ring, const char *target, uint32_t ts)
{
    find_channels_t used = {0};
    used.mqtt = ring ? mqtt_service_publish_find_ring(ts) : mqtt_service_publish_find_ack(target, ts);
    esp_err_t err = espnow_service_send_find(ring ? ESPNOW_MSG_FIND_RING : ESPNOW_MSG_FIND_ACK, ts);
    used.espnow = err == ESP_OK;
    if (used.espnow && s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        arm_burst(ring ? &s_ring_burst : &s_ack_burst, ts, now_ms());
        xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "%s ts=%lu mqtt=%d espnow=%d(%s)", ring ? "ring" : "ack", (unsigned long)ts, used.mqtt,
             used.espnow, esp_err_to_name(err));
    return used;
}

find_channels_t find_service_ring(void) { return send_both(true, NULL, new_token()); }

find_channels_t find_service_ack(const char *target_device_id)
{
    const char *target = target_device_id && target_device_id[0] ? target_device_id : CONFIG_KIDS_PEER_DEVICE_ID;
    return send_both(false, target, new_token());
}

static void burst_tick(burst_t *burst, bool ring, int64_t now)
{
    if (!burst->active || now < burst->next_ms) return;
    espnow_service_send_find(ring ? ESPNOW_MSG_FIND_RING : ESPNOW_MSG_FIND_ACK, burst->ts);
    if (--burst->remaining == 0) burst->active = false;
    else burst->next_ms = now + BURST_GAP_MS;
}

void find_service_tick(int64_t now)
{
    if (!s_lock) return;
    if (!s_ring_burst.active && !s_ack_burst.active) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    burst_tick(&s_ring_burst, true, now);
    burst_tick(&s_ack_burst, false, now);
    xSemaphoreGive(s_lock);
}

void find_service_on_espnow(bool ring, uint32_t ts)
{
    handle_inbound(ring, CONFIG_KIDS_PEER_DEVICE_ID, ts, "espnow");
}

void find_service_on_mqtt(bool ring, const char *sender, uint32_t ts)
{
    handle_inbound(ring, sender, ts, "mqtt");
}
