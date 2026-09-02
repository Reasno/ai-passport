#include "buzzer_game_service.h"
#include "app_events.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <limits.h>
#include <string.h>

#define INVITE_TIMEOUT_MS 15000
#define SYNC_RETRY_MS 180
#define SYNC_SAMPLES 5
#define PLAN_LEAD_MS 800
#define LIGHT_STEP_MS 1000
#define RANDOM_MIN_MS 500
#define RANDOM_SPAN_MS 1501
#define PRESS_TIMEOUT_MS 10000
#define RETRY_MS 120
#define DECISION_GRACE_MS 1200
#define RESULT_RETRY_LIMIT_MS 10000

static const char *TAG = "kp_buzzer";
static SemaphoreHandle_t s_lock;
static buzzer_game_snapshot_t s_game;
static int64_t s_deadline;
static int64_t s_last_tx;
static int64_t s_first_press_seen;
static int64_t s_result_retry_until;
static uint32_t s_plan_base_host;
static uint32_t s_go_host;
static uint32_t s_local_press_host;
static uint32_t s_remote_press_host;
static uint32_t s_last_sync_t0;
static int32_t s_best_offset;
static uint16_t s_best_rtt;
static uint8_t s_sync_count;
static uint16_t s_sync_nonce;
static uint16_t s_random_delay;
static uint8_t s_result_code;
static bool s_plan_received;
static bool s_offset_received;
static bool s_plan_acked;
static bool s_press_acked;
static bool s_result_acked;

static int64_t monotonic_ms(void) { return esp_timer_get_time() / 1000; }
static uint32_t time32(int64_t value) { return (uint32_t)value; }
static bool time_reached(uint32_t now, uint32_t target) { return (int32_t)(now - target) >= 0; }
static uint32_t host_now32(int64_t local_now)
{
    return time32(local_now + (s_game.is_host ? 0 : s_game.clock_offset_ms));
}
static void set_status(const char *text) { strlcpy(s_game.status, text, sizeof(s_game.status)); }
static void notify(void) { app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0); }
static uint32_t packet_data(const espnow_game_packet_t *p)
{
    return (uint32_t)p->choice | ((uint32_t)(uint8_t)p->value << 8) | ((uint32_t)p->reserved << 16);
}
static void send_data(espnow_msg_type_t type, uint16_t sequence, uint32_t data)
{
    espnow_service_send_data(type, s_game.session, sequence, data);
}
static void reset_round(bool host)
{
    uint16_t session = s_game.session;
    bool paired = espnow_service_has_peer();
    memset(&s_game, 0, sizeof(s_game));
    s_game.session = session;
    s_game.paired = paired;
    s_game.is_host = host;
    s_best_rtt = UINT16_MAX;
    s_plan_received = s_offset_received = s_plan_acked = s_press_acked = s_result_acked = false;
    s_local_press_host = s_remote_press_host = 0;
    s_first_press_seen = s_result_retry_until = 0;
    s_result_code = 0;
}
static void enter_idle(const char *text)
{
    bool paired = espnow_service_has_peer();
    memset(&s_game, 0, sizeof(s_game));
    s_game.paired = paired;
    s_game.state = BUZZER_STATE_IDLE;
    set_status(text);
    s_deadline = 0;
}
static void send_result(void)
{
    send_data(ESPNOW_MSG_BUZZER_RESULT, 0, s_result_code);
    s_last_tx = monotonic_ms();
}
static void finish_host(uint8_t code)
{
    s_result_code = code;
    s_game.state = BUZZER_STATE_RESULT;
    if (code == 1) { s_game.result = BUZZER_RESULT_WIN; set_status("你赢了！"); }
    else if (code == 2) { s_game.result = BUZZER_RESULT_LOSE; set_status("对方赢了"); }
    else if (code == 3) { s_game.result = BUZZER_RESULT_TIE; set_status("同时按下，平局！"); }
    else { s_game.result = BUZZER_RESULT_TIMEOUT; set_status("本局超时"); }
    s_result_retry_until = monotonic_ms() + RESULT_RETRY_LIMIT_MS;
    s_result_acked = false;
    send_result();
    ESP_LOGI(TAG, "host verdict session=%u code=%u local=%lu remote=%lu go=%lu",
             s_game.session, code, (unsigned long)s_local_press_host,
             (unsigned long)s_remote_press_host, (unsigned long)s_go_host);
}
static void decide_host(void)
{
    bool local = s_game.local_pressed;
    bool remote = s_game.remote_pressed;
    bool local_foul = local && !time_reached(s_local_press_host, s_go_host);
    bool remote_foul = remote && !time_reached(s_remote_press_host, s_go_host);
    if (local_foul || remote_foul) {
        if (local_foul && remote_foul) {
            if (s_local_press_host == s_remote_press_host) finish_host(3);
            else finish_host(time_reached(s_local_press_host, s_remote_press_host) ? 1 : 2);
        } else finish_host(local_foul ? 2 : 1);
        return;
    }
    if (local && remote) {
        if (s_local_press_host == s_remote_press_host) finish_host(3);
        else finish_host(time_reached(s_local_press_host, s_remote_press_host) ? 2 : 1);
    } else if (local) finish_host(1);
    else if (remote) finish_host(2);
    else finish_host(4);
}
static void begin_host_sync(void)
{
    s_game.state = BUZZER_STATE_SYNCING;
    s_sync_count = 0;
    s_best_rtt = UINT16_MAX;
    set_status("正在同步时钟...");
    s_last_sync_t0 = time32(monotonic_ms());
    s_sync_nonce = 1;
    send_data(ESPNOW_MSG_BUZZER_SYNC_REQ, s_sync_nonce, s_last_sync_t0);
    s_last_tx = monotonic_ms();
}
static void send_plan(int64_t now)
{
    /* Offset and plan are each repeated: ESP-NOW delivery order is not assumed. */
    send_data(ESPNOW_MSG_BUZZER_SYNC_SET, 0, (uint32_t)s_best_offset);
    send_data(ESPNOW_MSG_BUZZER_START, s_random_delay, s_plan_base_host);
    s_last_tx = now;
}
static void create_host_plan(int64_t now)
{
    s_random_delay = RANDOM_MIN_MS + (uint16_t)(esp_random() % RANDOM_SPAN_MS);
    s_plan_base_host = time32(now + PLAN_LEAD_MS);
    s_go_host = s_plan_base_host + 3 * LIGHT_STEP_MS + s_random_delay;
    s_game.clock_offset_ms = 0;
    s_game.sync_rtt_ms = s_best_rtt;
    s_game.state = BUZZER_STATE_ARMED;
    set_status("准备，别抢跑");
    s_deadline = now + PLAN_LEAD_MS + 3 * LIGHT_STEP_MS + s_random_delay + PRESS_TIMEOUT_MS;
    send_plan(now);
    ESP_LOGI(TAG, "plan session=%u base=%lu go=%lu random=%u offset=%ld rtt=%u",
             s_game.session, (unsigned long)s_plan_base_host, (unsigned long)s_go_host,
             s_random_delay, (long)s_best_offset, s_best_rtt);
}

esp_err_t buzzer_game_service_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    enter_idle("准备好了");
    return ESP_OK;
}
void buzzer_game_service_snapshot(buzzer_game_snapshot_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_game;
    xSemaphoreGive(s_lock);
}
bool buzzer_game_service_owns_packet(uint8_t type)
{
    return type >= ESPNOW_MSG_BUZZER_INVITE && type <= ESPNOW_MSG_BUZZER_CANCEL;
}
void buzzer_game_service_invite(void)
{
    if (!s_lock || !espnow_service_has_peer()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state != BUZZER_STATE_IDLE && s_game.state != BUZZER_STATE_RESULT) {
        xSemaphoreGive(s_lock); return;
    }
    s_game.session = (uint16_t)esp_random();
    reset_round(true);
    s_game.state = BUZZER_STATE_INVITE_SENT;
    set_status("邀请已发出，等待接受...");
    s_deadline = monotonic_ms() + INVITE_TIMEOUT_MS;
    s_last_tx = 0;
    xSemaphoreGive(s_lock);
    notify();
}
void buzzer_game_service_respond_invite(bool accept)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state != BUZZER_STATE_INVITE_RECEIVED) { xSemaphoreGive(s_lock); return; }
    espnow_service_send(accept ? ESPNOW_MSG_BUZZER_ACCEPT : ESPNOW_MSG_BUZZER_REJECT,
                        s_game.session, 0, 0, 0, false);
    if (accept) { s_game.state = BUZZER_STATE_SYNCING; set_status("等待主机同步..."); s_deadline = monotonic_ms() + INVITE_TIMEOUT_MS; }
    else enter_idle("已拒绝邀请");
    xSemaphoreGive(s_lock);
    notify();
}
void buzzer_game_service_press(int64_t local_press_ms)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if ((s_game.state != BUZZER_STATE_ARMED && s_game.state != BUZZER_STATE_GO) || s_game.local_pressed) {
        xSemaphoreGive(s_lock); return;
    }
    s_local_press_host = host_now32(local_press_ms);
    s_game.local_pressed = true;
    s_game.local_false_start = !time_reached(s_local_press_host, s_go_host);
    set_status(s_game.local_false_start ? "抢跑！等待主机裁决" : "已按下，等待裁决...");
    s_last_tx = local_press_ms;
    s_press_acked = s_game.is_host;
    if (!s_game.is_host) send_data(ESPNOW_MSG_BUZZER_PRESS, 0, s_local_press_host);
    if (!s_first_press_seen) s_first_press_seen = local_press_ms;
    if (s_game.is_host && s_game.local_false_start) finish_host(2);
    xSemaphoreGive(s_lock);
    notify();
}
void buzzer_game_service_cancel(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state != BUZZER_STATE_IDLE) {
        for (int i = 0; i < 3; ++i)
            espnow_service_send(ESPNOW_MSG_BUZZER_CANCEL, s_game.session, 0, 0, 0, false);
    }
    enter_idle("已退出游戏");
    xSemaphoreGive(s_lock);
    notify();
}
void buzzer_game_service_tick(int64_t now)
{
    if (!s_lock) return;
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_game.paired = espnow_service_has_peer();
    if (s_game.state == BUZZER_STATE_INVITE_SENT && now - s_last_tx >= 400) {
        espnow_service_send(ESPNOW_MSG_BUZZER_INVITE, s_game.session, 0, 0, 0, false);
        s_last_tx = now;
    }
    if ((s_game.state == BUZZER_STATE_INVITE_SENT || s_game.state == BUZZER_STATE_INVITE_RECEIVED ||
         s_game.state == BUZZER_STATE_SYNCING) && now >= s_deadline) {
        enter_idle("连接超时，请重试"); changed = true;
    }
    if (s_game.is_host && s_game.state == BUZZER_STATE_SYNCING && now - s_last_tx >= SYNC_RETRY_MS) {
        s_last_sync_t0 = time32(now);
        ++s_sync_nonce;
        send_data(ESPNOW_MSG_BUZZER_SYNC_REQ, s_sync_nonce, s_last_sync_t0);
        s_last_tx = now;
    }
    if (s_game.is_host && s_game.state == BUZZER_STATE_ARMED && !s_plan_acked && now - s_last_tx >= RETRY_MS)
        send_plan(now);
    if (!s_game.is_host && s_game.local_pressed && !s_press_acked &&
        (s_game.state == BUZZER_STATE_ARMED || s_game.state == BUZZER_STATE_GO) && now - s_last_tx >= RETRY_MS) {
        send_data(ESPNOW_MSG_BUZZER_PRESS, 0, s_local_press_host); s_last_tx = now;
    }
    if (s_game.is_host && s_game.state == BUZZER_STATE_RESULT && !s_result_acked &&
        now < s_result_retry_until && now - s_last_tx >= RETRY_MS) send_result();
    if (s_game.state == BUZZER_STATE_ARMED || s_game.state == BUZZER_STATE_GO) {
        uint32_t host_now = host_now32(now);
        uint8_t lights = 0;
        if (time_reached(host_now, s_plan_base_host)) lights = 1;
        if (time_reached(host_now, s_plan_base_host + LIGHT_STEP_MS)) lights = 2;
        if (time_reached(host_now, s_plan_base_host + 2 * LIGHT_STEP_MS)) lights = 3;
        if (time_reached(host_now, s_go_host)) {
            lights = 0;
            if (s_game.state != BUZZER_STATE_GO) { s_game.state = BUZZER_STATE_GO; set_status("GO！快按 B3"); changed = true; }
        }
        if (lights != s_game.lights_on) { s_game.lights_on = lights; changed = true; }
        if (s_game.is_host && s_first_press_seen && now - s_first_press_seen >= DECISION_GRACE_MS) {
            decide_host(); changed = true;
        } else if (s_game.is_host && now >= s_deadline) { decide_host(); changed = true; }
    }
    uint32_t left = s_deadline > now ? (uint32_t)((s_deadline - now + 999) / 1000) : 0;
    if (left != s_game.seconds_left) { s_game.seconds_left = left; }
    xSemaphoreGive(s_lock);
    if (changed) notify();
}
void buzzer_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *p)
{
    if (!s_lock || !src || !p || !buzzer_game_service_owns_packet(p->type)) return;
    uint8_t peer[6];
    if (!espnow_service_get_peer(peer) || memcmp(peer, src, 6) != 0) return;
    bool changed = false;
    int64_t now = monotonic_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t data = packet_data(p);
    if (p->type == ESPNOW_MSG_BUZZER_INVITE) {
        if (s_game.state == BUZZER_STATE_INVITE_SENT) {
            /* Same deterministic role rule as RPS: larger configured ID remains inviter/Host. */
            if (strcmp(CONFIG_KIDS_DEVICE_ID, CONFIG_KIDS_PEER_DEVICE_ID) < 0) {
                s_game.session = p->session; reset_round(false);
                s_game.state = BUZZER_STATE_INVITE_RECEIVED; set_status("收到三灯抢答邀请");
                s_deadline = now + INVITE_TIMEOUT_MS; changed = true;
            }
        } else if (s_game.state == BUZZER_STATE_IDLE || s_game.state == BUZZER_STATE_RESULT) {
            s_game.session = p->session; reset_round(false);
            s_game.state = BUZZER_STATE_INVITE_RECEIVED; set_status("收到三灯抢答邀请");
            s_deadline = now + INVITE_TIMEOUT_MS; changed = true;
        } else if (!s_game.is_host && p->session == s_game.session && s_game.state >= BUZZER_STATE_SYNCING) {
            espnow_service_send(ESPNOW_MSG_BUZZER_ACCEPT, s_game.session, 0, 0, 0, false);
        }
    } else if (p->session != s_game.session) {
        xSemaphoreGive(s_lock); return;
    } else if (p->type == ESPNOW_MSG_BUZZER_ACCEPT && s_game.is_host &&
               s_game.state == BUZZER_STATE_INVITE_SENT) {
        begin_host_sync(); s_deadline = now + INVITE_TIMEOUT_MS; changed = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_REJECT) {
        enter_idle("对方拒绝了邀请"); changed = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_SYNC_REQ && !s_game.is_host &&
               s_game.state == BUZZER_STATE_SYNCING) {
        send_data(ESPNOW_MSG_BUZZER_SYNC_RESP, p->sequence, time32(now));
    } else if (p->type == ESPNOW_MSG_BUZZER_SYNC_RESP && s_game.is_host &&
               s_game.state == BUZZER_STATE_SYNCING && p->sequence == s_sync_nonce) {
        uint32_t t2 = time32(now);
        uint16_t rtt = (uint16_t)(t2 - s_last_sync_t0);
        int32_t offset = (int32_t)(s_last_sync_t0 + rtt / 2 - data);
        if (rtt < s_best_rtt) { s_best_rtt = rtt; s_best_offset = offset; }
        if (++s_sync_count >= SYNC_SAMPLES) create_host_plan(now);
        else {
            s_last_sync_t0 = time32(now);
            ++s_sync_nonce;
            send_data(ESPNOW_MSG_BUZZER_SYNC_REQ, s_sync_nonce, s_last_sync_t0);
            s_last_tx = now;
        }
        changed = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_SYNC_SET && !s_game.is_host &&
               (s_game.state == BUZZER_STATE_SYNCING || s_game.state == BUZZER_STATE_ARMED)) {
        s_game.clock_offset_ms = (int32_t)data;
        s_offset_received = true;
        if (s_plan_received) {
            s_game.state = BUZZER_STATE_ARMED;
            set_status("准备，别抢跑");
            espnow_service_send(ESPNOW_MSG_BUZZER_START_ACK, s_game.session, s_random_delay, 0, 0, false);
            changed = true;
        }
    } else if (p->type == ESPNOW_MSG_BUZZER_START && !s_game.is_host &&
               (s_game.state == BUZZER_STATE_SYNCING || s_game.state == BUZZER_STATE_ARMED)) {
        s_plan_base_host = data; s_random_delay = p->sequence;
        s_go_host = s_plan_base_host + 3 * LIGHT_STEP_MS + s_random_delay;
        s_plan_received = true;
        s_deadline = now + PLAN_LEAD_MS + 3 * LIGHT_STEP_MS + s_random_delay + PRESS_TIMEOUT_MS;
        if (s_offset_received) {
            s_game.state = BUZZER_STATE_ARMED;
            set_status("准备，别抢跑");
            espnow_service_send(ESPNOW_MSG_BUZZER_START_ACK, s_game.session, p->sequence, 0, 0, false);
        }
        changed = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_START_ACK && s_game.is_host &&
               s_game.state == BUZZER_STATE_ARMED && p->sequence == s_random_delay) {
        s_plan_acked = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_PRESS && s_game.is_host &&
               (s_game.state == BUZZER_STATE_ARMED || s_game.state == BUZZER_STATE_GO)) {
        send_data(ESPNOW_MSG_BUZZER_PRESS_ACK, 0, data);
        if (!s_game.remote_pressed) {
            s_remote_press_host = data; s_game.remote_pressed = true;
            if (!s_first_press_seen) s_first_press_seen = now;
            if (!time_reached(s_remote_press_host, s_go_host)) finish_host(1);
            changed = true;
        }
    } else if (p->type == ESPNOW_MSG_BUZZER_PRESS_ACK && !s_game.is_host && data == s_local_press_host) {
        s_press_acked = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_RESULT && !s_game.is_host) {
        s_result_code = (uint8_t)data;
        s_game.state = BUZZER_STATE_RESULT;
        if (s_result_code == 1) { s_game.result = BUZZER_RESULT_LOSE; set_status("对方赢了"); }
        else if (s_result_code == 2) { s_game.result = BUZZER_RESULT_WIN; set_status("你赢了！"); }
        else if (s_result_code == 3) { s_game.result = BUZZER_RESULT_TIE; set_status("同时按下，平局！"); }
        else { s_game.result = BUZZER_RESULT_TIMEOUT; set_status("本局超时"); }
        send_data(ESPNOW_MSG_BUZZER_RESULT_ACK, 0, data); changed = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_RESULT_ACK && s_game.is_host && data == s_result_code) {
        s_result_acked = true;
    } else if (p->type == ESPNOW_MSG_BUZZER_CANCEL) {
        enter_idle("对方退出了游戏"); changed = true;
    }
    xSemaphoreGive(s_lock);
    if (changed) notify();
}
