#include "garden_service.h"
#include "app_events.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NS "kp_garden"
#define KEY_STATE "state_v1"
/* Any clock earlier than 2025-01-01 has not been set since power loss. */
#define CLOCK_VALID_FROM 1735689600LL
#define GIFT_RETRY_MS 300
#define GIFT_TIMEOUT_MS 5000
#define QUERY_INTERVAL_MS 5000
/* Small backward corrections (for example an SNTP adjustment) are absorbed silently. */
#define CLOCK_JITTER_S 120

static const char *TAG = "kp_garden";
static SemaphoreHandle_t s_lock;
static garden_t s_garden;
static bool s_clock_lost;
static uint8_t s_phase;
static uint8_t s_gift_material;
static int64_t s_deadline_ms, s_next_tx_ms;
/* Set from the Wi-Fi task when the peer's radio acknowledged one of our frames. */
static volatile bool s_delivered;

static const char *const MATERIAL_NAMES[GARDEN_MATERIALS] = {"肥料", "除草剂", "除虫剂"};

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool clock_now(int64_t *now)
{
    time_t t = time(NULL);
    *now = (int64_t)t;
    return *now >= CLOCK_VALID_FROM;
}

static void save_locked(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_STATE, &s_garden, sizeof(s_garden));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
}

/* Settles growth up to the clock. Returns false when the clock is unset or behind the
 * last settlement; the plant then keeps its state and no interval is guessed or replayed. */
static bool settle_locked(int64_t *now_out)
{
    int64_t now;
    if (!clock_now(&now)) {
        s_clock_lost = true;
        return false;
    }
    if (now_out) *now_out = now;
    if (!s_garden.planted) {
        /* Nothing grows on an empty plot, so its settlement simply follows the clock. */
        s_garden.settled_at = now;
        return true;
    }
    if (now < s_garden.settled_at) return s_garden.settled_at - now <= CLOCK_JITTER_S;
    if (s_clock_lost) {
        /* 校时完成后从原存档补算；不能先覆盖结算时间，否则离线进度会丢失。 */
        s_clock_lost = false;
        ESP_LOGI(TAG, "Clock restored; replay garden from %lld to %lld",
                 (long long)s_garden.settled_at, (long long)now);
    }
    return garden_settle(&s_garden, now, GARDEN_UTC_OFFSET_S) == GARDEN_OK;
}

static void notify(void)
{
    app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0);
}

esp_err_t garden_service_start(void)
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_garden);
        loaded = nvs_get_blob(h, KEY_STATE, &s_garden, &len) == ESP_OK && len == sizeof(s_garden) &&
                 s_garden.crop < GARDEN_CROPS && s_garden.stage <= GARDEN_STAGE_MATURE;
        nvs_close(h);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!loaded) {
        /* First launch only: one seed plus the one-time starter kit. */
        garden_init(&s_garden, esp_random());
        int64_t now;
        if (clock_now(&now)) s_garden.settled_at = now;
        save_locked();
    } else if (settle_locked(NULL)) {
        save_locked();
    }
    if (s_garden.pending_mat) {
        s_phase = GARDEN_GIFT_PENDING;
        s_gift_material = (uint8_t)(s_garden.pending_mat - 1);
        s_next_tx_ms = 0;
    }
    ESP_LOGI(TAG, "start loaded=%d crop=%u planted=%u stage=%u bag=%u/%u/%u pending=%u", loaded,
             s_garden.crop, s_garden.planted, s_garden.stage, s_garden.bag[0], s_garden.bag[1],
             s_garden.bag[2], s_garden.pending_mat);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void garden_service_snapshot(garden_view_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now = 0;
    out->clock_ok = settle_locked(&now);
    out->day = out->clock_ok && garden_is_day(now, GARDEN_UTC_OFFSET_S);
    out->garden = s_garden;
    out->gift_phase = s_phase;
    out->gift_material = s_gift_material;
    xSemaphoreGive(s_lock);
}

/* Time-dependent actions apply to a freshly settled plant and are saved at once. */
static garden_result_t timed_action(garden_result_t (*action)(garden_t *))
{
    if (!s_lock) return GARDEN_ERR_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    garden_result_t r = settle_locked(NULL) ? action(&s_garden) : GARDEN_ERR_TIME;
    if (r == GARDEN_OK) save_locked();
    xSemaphoreGive(s_lock);
    return r;
}

static garden_result_t sow_now(garden_t *g) { return garden_sow(g, g->settled_at); }
garden_result_t garden_service_sow(void) { return timed_action(sow_now); }
garden_result_t garden_service_water(void) { return timed_action(garden_water); }

static garden_material_t s_use_material;
static garden_result_t use_now(garden_t *g) { return garden_use(g, s_use_material); }
garden_result_t garden_service_use(garden_material_t material)
{
    s_use_material = material;
    return timed_action(use_now);
}

garden_result_t garden_service_reroll(void)
{
    if (!s_lock) return GARDEN_ERR_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    garden_result_t r = garden_reroll(&s_garden);
    if (r == GARDEN_OK) save_locked();
    xSemaphoreGive(s_lock);
    return r;
}

garden_result_t garden_service_harvest(garden_harvest_t *out)
{
    if (!s_lock) return GARDEN_ERR_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    (void)settle_locked(NULL);
    garden_result_t r = garden_harvest(&s_garden, out);
    if (r == GARDEN_OK) save_locked();
    xSemaphoreGive(s_lock);
    return r;
}

garden_result_t garden_service_gift(garden_material_t material)
{
    if (!s_lock) return GARDEN_ERR_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    garden_result_t r = s_phase == GARDEN_GIFT_SENDING || s_phase == GARDEN_GIFT_PENDING
                            ? GARDEN_ERR_STATE
                            : garden_gift_lock(&s_garden, material, garden_next_gift_id(&s_garden));
    if (r == GARDEN_OK) {
        /* The item leaves the bag before the first frame, so a restart cannot duplicate it. */
        save_locked();
        s_phase = GARDEN_GIFT_SENDING;
        s_gift_material = (uint8_t)material;
        s_delivered = false;
        s_deadline_ms = now_ms() + GIFT_TIMEOUT_MS;
        s_next_tx_ms = 0;
    }
    xSemaphoreGive(s_lock);
    if (r == GARDEN_OK) ESP_LOGI(TAG, "gift start id=%u material=%d", s_garden.pending_id, material);
    return r;
}

void garden_service_gift_seen(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != GARDEN_GIFT_SENDING && s_phase != GARDEN_GIFT_PENDING) s_phase = GARDEN_GIFT_IDLE;
    xSemaphoreGive(s_lock);
}

bool garden_service_busy(void) { return s_phase == GARDEN_GIFT_SENDING; }

void garden_service_tick(int64_t now)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = false;
    uint16_t id = s_garden.pending_id;
    if (s_phase == GARDEN_GIFT_SENDING && now >= s_deadline_ms) {
        if (!s_delivered) {
            /* No frame reached the peer's radio: nothing was delivered, so restore the item. */
            garden_gift_finish(&s_garden, true);
            save_locked();
            s_phase = GARDEN_GIFT_FAILED;
        } else {
            s_phase = GARDEN_GIFT_PENDING;
            s_next_tx_ms = now + QUERY_INTERVAL_MS;
        }
        changed = true;
        ESP_LOGI(TAG, "gift id=%u timeout delivered=%d", id, s_delivered);
    }
    bool send_gift = s_phase == GARDEN_GIFT_SENDING && now >= s_next_tx_ms;
    bool send_query = s_phase == GARDEN_GIFT_PENDING && now >= s_next_tx_ms;
    if (send_gift) s_next_tx_ms = now + GIFT_RETRY_MS;
    if (send_query) s_next_tx_ms = now + QUERY_INTERVAL_MS;
    uint8_t material = s_gift_material;
    xSemaphoreGive(s_lock);
    if (send_gift) espnow_service_send(ESPNOW_MSG_GARDEN_GIFT, id, 0, material, 0, false);
    if (send_query) espnow_service_send(ESPNOW_MSG_GARDEN_QUERY, id, 0, 0, 0, false);
    if (changed) notify();
}

bool garden_service_owns_packet(uint8_t type)
{
    return type == ESPNOW_MSG_GARDEN_GIFT || type == ESPNOW_MSG_GARDEN_ACK || type == ESPNOW_MSG_GARDEN_QUERY;
}

static void post_received(uint8_t material, bool day)
{
    if (!day) {
        /* Night delivery is silent: the bag simply shows it next time. */
        notify();
        return;
    }
    app_event_t event = {.type = APP_EVT_STATUS_UPDATE, .ok = true};
    snprintf(event.text, sizeof(event.text), "已收到" CONFIG_KP_PEER_LABEL "的\n%s ×1", MATERIAL_NAMES[material]);
    app_event_post(&event, 0);
}

void garden_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *p)
{
    (void)src;
    if (!s_lock || !p) return;
    uint16_t id = p->session;
    if (p->type == ESPNOW_MSG_GARDEN_GIFT || p->type == ESPNOW_MSG_GARDEN_QUERY) {
        if (p->type == ESPNOW_MSG_GARDEN_GIFT && p->choice >= GARDEN_MATERIALS) return;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool credited = false;
        garden_gift_status_t status;
        if (p->type == ESPNOW_MSG_GARDEN_GIFT) {
            credited = garden_receive_gift(&s_garden, id, (garden_material_t)p->choice);
            status = garden_gift_status(&s_garden, id);
        } else {
            garden_gift_status_t before = garden_gift_status(&s_garden, id);
            status = garden_gift_query(&s_garden, id);
            credited = false;
            if (before == GARDEN_GIFT_UNKNOWN) save_locked();
        }
        /* The credit is stored before the receipt leaves, so an ACK always means "in the bag". */
        if (credited) save_locked();
        int64_t now = 0;
        bool day = clock_now(&now) && garden_is_day(now, GARDEN_UTC_OFFSET_S);
        xSemaphoreGive(s_lock);
        espnow_service_send(ESPNOW_MSG_GARDEN_ACK, id, 0, (uint8_t)status, 0, false);
        if (credited) {
            ESP_LOGI(TAG, "gift received id=%u material=%u", id, p->choice);
            post_received(p->choice, day);
        }
        return;
    }
    /* ACK for our pending gift. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = false;
    if ((s_phase == GARDEN_GIFT_SENDING || s_phase == GARDEN_GIFT_PENDING) && s_garden.pending_mat &&
        id == s_garden.pending_id) {
        if (p->choice == GARDEN_GIFT_CREDITED) {
            garden_gift_finish(&s_garden, false);
            s_phase = GARDEN_GIFT_SENT;
            changed = true;
        } else if (p->choice == GARDEN_GIFT_CLOSED) {
            /* The receiver never credited it and has now closed the id: safe to restore. */
            garden_gift_finish(&s_garden, true);
            s_phase = GARDEN_GIFT_RETURNED;
            changed = true;
        }
        if (changed) save_locked();
    }
    xSemaphoreGive(s_lock);
    if (changed) {
        ESP_LOGI(TAG, "gift id=%u resolved status=%u", id, p->choice);
        notify();
    }
}

void garden_service_on_send_status(bool delivered)
{
    if (delivered && s_phase == GARDEN_GIFT_SENDING) s_delivered = true;
}
