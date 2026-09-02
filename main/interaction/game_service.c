#include "game_service.h"
#include "app_events.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define PAIR_HEAP 8192
#define RADAR_HEAP 16384
#define RPS_HEAP 24576
#define PAIR_TIMEOUT_MS 15000
#define INVITE_TIMEOUT_MS 15000
#define CHOICE_TIMEOUT_MS 20000
#define RPS_COUNTDOWN_SECONDS 1
#define RPS_RETRY_MS 400
#define RADAR_LOST_MS 5000

static const char *TAG = "kp_game";
static SemaphoreHandle_t s_lock;
static game_snapshot_t s_game;
static int64_t s_deadline, s_countdown_started, s_last_pair_tx, s_last_ping, s_last_radar_rx;
static int64_t s_last_rps_tx;
static bool s_choice_acked, s_invite_receiver;
extern esp_err_t espnow_service_store_peer(const uint8_t mac[6]);

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void notify(void) { app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0); }
static void status(const char *text) { strlcpy(s_game.status, text, sizeof(s_game.status)); }
static uint8_t bars_for_rssi(int8_t rssi)
{
    if (rssi >= -48) return 5;
    if (rssi >= -58) return 4;
    if (rssi >= -68) return 3;
    if (rssi >= -78) return 2;
    return 1;
}
static int8_t result_for(rps_choice_t own, rps_choice_t other)
{
    if (own == other) return 0;
    return ((own == RPS_ROCK && other == RPS_SCISSORS) ||
            (own == RPS_SCISSORS && other == RPS_PAPER) ||
            (own == RPS_PAPER && other == RPS_ROCK)) ? 1 : -1;
}
static void finish_if_ready(void)
{
    if (s_game.local_choice == RPS_NONE || s_game.remote_choice == RPS_NONE) return;
    bool first_result = s_game.state != GAME_STATE_RESULT;
    s_game.state = GAME_STATE_RESULT; s_game.result = result_for(s_game.local_choice, s_game.remote_choice);
    status(s_game.result > 0 ? "你赢了！" : s_game.result < 0 ? "你输了，下次加油！" : "平局，再来一次！");
    if (first_result) s_deadline = now_ms() + 2000;
    ESP_LOGI(TAG, "RPS entertainment result session=%u local=%u remote=%u result=%d (no points, no MQTT)",
             s_game.session, s_game.local_choice, s_game.remote_choice, s_game.result);
}
bool game_service_heap_allows_pairing(void) { return esp_get_free_heap_size() >= PAIR_HEAP; }
bool game_service_heap_allows_radar(void) { return esp_get_free_heap_size() >= RADAR_HEAP; }
bool game_service_heap_allows_rps(void) { return esp_get_free_heap_size() >= RPS_HEAP; }
esp_err_t game_service_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_game, 0, sizeof(s_game)); s_game.paired = espnow_service_has_peer(); status("准备好了");
    ESP_LOGI(TAG, "heap gates pair=%d radar=%d rps=%d free=%lu", game_service_heap_allows_pairing(),
             game_service_heap_allows_radar(), game_service_heap_allows_rps(), (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}
void game_service_snapshot(game_snapshot_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); *out = s_game; xSemaphoreGive(s_lock);
}
void game_service_start_pairing(void)
{
    if (!s_lock || !game_service_heap_allows_pairing() || !espnow_service_ready()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); s_game.state = GAME_STATE_PAIRING; status("正在寻找另一台 Passport..."); s_deadline = now_ms() + PAIR_TIMEOUT_MS; s_last_pair_tx = 0; xSemaphoreGive(s_lock);
    espnow_service_send(ESPNOW_MSG_PAIR_REQ, 0, 0, 0, 0, true); ESP_LOGI(TAG, "pairing start"); notify();
}
void game_service_cancel(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state == GAME_STATE_INVITE_SENT || s_game.state == GAME_STATE_INVITE_RECEIVED ||
        s_game.state == GAME_STATE_COUNTDOWN || s_game.state == GAME_STATE_WAITING_CHOICE ||
        s_game.state == GAME_STATE_RESULT) {
        /* Exit is user-visible state: send a short burst so one lost ESP-NOW frame
         * cannot leave the peer stranded in the game. */
        for (int i = 0; i < 3; i++)
            espnow_service_send(ESPNOW_MSG_RPS_CANCEL, s_game.session, 0, 0, 0, false);
    }
    s_game.state = GAME_STATE_IDLE; s_game.radar_active = false; status("已取消"); s_deadline = 0; xSemaphoreGive(s_lock); notify();
}
void game_service_set_radar(bool active)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_game.radar_active = active && s_game.paired && game_service_heap_allows_radar();
    s_game.peer_nearby = false; s_game.distance_bars = 0; s_last_ping = 0; s_last_radar_rx = 0;
    status(s_game.radar_active ? "正在扫描" : "雷达不可用"); xSemaphoreGive(s_lock); notify();
}
void game_service_invite_rps(void)
{
    if (!s_lock || !game_service_heap_allows_rps()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_game.paired || (s_game.state != GAME_STATE_IDLE && s_game.state != GAME_STATE_RESULT)) { xSemaphoreGive(s_lock); return; }
    s_game.session = (uint16_t)esp_random(); s_game.state = GAME_STATE_INVITE_SENT;
    s_game.local_choice = s_game.remote_choice = RPS_NONE; s_game.cursor_choice = RPS_ROCK;
    s_game.result = 0; s_choice_acked = false; s_invite_receiver = false;
    status("挑战已发出，等待接受...");
    s_deadline = now_ms() + INVITE_TIMEOUT_MS; s_last_rps_tx = now_ms();
    espnow_service_send(ESPNOW_MSG_RPS_INVITE, s_game.session, 0, 0, 0, false);
    ESP_LOGI(TAG, "RPS invite session=%u", s_game.session); xSemaphoreGive(s_lock); notify();
}
void game_service_respond_invite(bool accept)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); if (s_game.state != GAME_STATE_INVITE_RECEIVED) { xSemaphoreGive(s_lock); return; }
    espnow_service_send(accept ? ESPNOW_MSG_RPS_ACCEPT : ESPNOW_MSG_RPS_REJECT, s_game.session, 0, 0, 0, false);
    s_last_rps_tx = now_ms();
    if (accept) {
        s_game.state = GAME_STATE_COUNTDOWN; s_countdown_started = now_ms(); s_game.countdown = RPS_COUNTDOWN_SECONDS;
        s_game.local_choice = s_game.remote_choice = RPS_NONE; s_game.cursor_choice = RPS_ROCK;
        s_choice_acked = false; status("即将开始");
    }
    else { s_game.state = GAME_STATE_IDLE; status("已拒绝挑战"); }
    xSemaphoreGive(s_lock); notify();
}
void game_service_move_choice(int delta)
{
    if (!s_lock || delta == 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state == GAME_STATE_WAITING_CHOICE && s_game.local_choice == RPS_NONE) {
        int next = (int)s_game.cursor_choice - 1 + delta;
        while (next < 0) next += 3;
        s_game.cursor_choice = (rps_choice_t)((next % 3) + 1);
        xSemaphoreGive(s_lock); notify(); return;
    }
    xSemaphoreGive(s_lock);
}
void game_service_choose(rps_choice_t choice)
{
    if (!s_lock || choice < RPS_ROCK || choice > RPS_PAPER) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state != GAME_STATE_WAITING_CHOICE || s_game.local_choice != RPS_NONE) { xSemaphoreGive(s_lock); return; }
    s_game.local_choice = choice; s_choice_acked = false; s_last_rps_tx = now_ms();
    espnow_service_send(ESPNOW_MSG_RPS_CHOICE, s_game.session, 0, choice, 0, false);
    status("已出拳，等待对方..."); finish_if_ready(); xSemaphoreGive(s_lock); notify();
}
void game_service_tick(int64_t now)
{
    if (!s_lock) return;
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.state == GAME_STATE_PAIRING && now >= s_deadline) { s_game.state = GAME_STATE_IDLE; status("配对超时，请靠近后重试"); changed = true; }
    else if (s_game.state == GAME_STATE_PAIRING && now - s_last_pair_tx >= 1000) { s_last_pair_tx = now; espnow_service_send(ESPNOW_MSG_PAIR_REQ, 0, 0, 0, 0, true); }
    if (s_game.state == GAME_STATE_INVITE_SENT && now - s_last_rps_tx >= RPS_RETRY_MS) {
        s_last_rps_tx = now; espnow_service_send(ESPNOW_MSG_RPS_INVITE, s_game.session, 0, 0, 0, false);
    }
    if (s_invite_receiver && s_game.state == GAME_STATE_COUNTDOWN && now - s_last_rps_tx >= RPS_RETRY_MS) {
        s_last_rps_tx = now; espnow_service_send(ESPNOW_MSG_RPS_ACCEPT, s_game.session, 0, 0, 0, false);
    }
    if (s_game.state == GAME_STATE_WAITING_CHOICE && s_game.local_choice != RPS_NONE &&
        !s_choice_acked && now - s_last_rps_tx >= RPS_RETRY_MS) {
        s_last_rps_tx = now;
        espnow_service_send(ESPNOW_MSG_RPS_CHOICE, s_game.session, 0, s_game.local_choice, 0, false);
    }
    if (s_game.state == GAME_STATE_RESULT && s_game.local_choice != RPS_NONE &&
        now < s_deadline && now - s_last_rps_tx >= RPS_RETRY_MS) {
        s_last_rps_tx = now;
        espnow_service_send(ESPNOW_MSG_RPS_CHOICE, s_game.session, 0, s_game.local_choice, 0, false);
    }
    if (s_game.state == GAME_STATE_INVITE_SENT && now >= s_deadline) { s_game.state = GAME_STATE_IDLE; status("对方没有回应"); changed = true; }
    if (s_game.state == GAME_STATE_COUNTDOWN) {
        int elapsed = (int)((now - s_countdown_started) / 1000);
        uint8_t count = elapsed >= RPS_COUNTDOWN_SECONDS ? 0 : RPS_COUNTDOWN_SECONDS - elapsed;
        if (count != s_game.countdown) { s_game.countdown = count; changed = true; }
        if (!count) { s_game.state = GAME_STATE_WAITING_CHOICE; status("请出拳！"); s_deadline = now + CHOICE_TIMEOUT_MS; changed = true; }
    }
    if (s_game.state == GAME_STATE_WAITING_CHOICE && now >= s_deadline) { s_game.state = GAME_STATE_RESULT; s_game.result = s_game.local_choice != RPS_NONE ? 1 : -1; status(s_game.result > 0 ? "对方超时，你赢了！" : "出拳超时"); changed = true; }
    if (s_game.radar_active && now - s_last_ping >= 1000) { s_last_ping = now; espnow_service_send(ESPNOW_MSG_RADAR_PING, 0, 0, 0, 0, false); }
    if (s_game.radar_active && s_game.peer_nearby && now - s_last_radar_rx >= RADAR_LOST_MS) { s_game.peer_nearby = false; s_game.distance_bars = 0; changed = true; }
    if (s_deadline > now) s_game.seconds_left = (uint32_t)((s_deadline - now + 999) / 1000); else s_game.seconds_left = 0;
    xSemaphoreGive(s_lock); if (changed) notify();
}
void game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *p, int8_t rssi)
{
    if (!s_lock || !src || !p) return;
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (p->type == ESPNOW_MSG_PAIR_REQ && s_game.state == GAME_STATE_PAIRING) {
        if (espnow_service_store_peer(src) == ESP_OK) { s_game.paired = true; s_game.state = GAME_STATE_IDLE; status("配对成功！"); espnow_service_send(ESPNOW_MSG_PAIR_ACK, 0, 0, 0, 0, false); changed = true; }
    } else if (p->type == ESPNOW_MSG_PAIR_ACK && s_game.state == GAME_STATE_PAIRING) {
        if (espnow_service_store_peer(src) == ESP_OK) { s_game.paired = true; s_game.state = GAME_STATE_IDLE; status("配对成功！"); changed = true; }
    } else if (s_game.paired) {
        uint8_t peer[6]; if (!espnow_service_get_peer(peer) || memcmp(peer, src, 6) != 0) { xSemaphoreGive(s_lock); return; }
        if (p->type == ESPNOW_MSG_RADAR_PING) espnow_service_send(ESPNOW_MSG_RADAR_PONG, 0, p->sequence, 0, 0, false);
        else if (p->type == ESPNOW_MSG_RADAR_PONG && s_game.radar_active) { s_game.rssi = rssi; s_game.distance_bars = bars_for_rssi(rssi); s_game.peer_nearby = true; s_last_radar_rx = now_ms(); changed = true; }
        else if (p->type == ESPNOW_MSG_RPS_INVITE && s_game.state == GAME_STATE_INVITE_SENT) {
            /* Simultaneous invite tie-breaker: the lexicographically smaller device ID
             * becomes the receiver; the larger ID keeps its original session. */
            if (strcmp(CONFIG_KIDS_DEVICE_ID, CONFIG_KIDS_PEER_DEVICE_ID) < 0) {
                ESP_LOGI(TAG, "simultaneous invite: %s yields to %s", CONFIG_KIDS_DEVICE_ID,
                         CONFIG_KIDS_PEER_DEVICE_ID);
                s_game.session = p->session; s_game.state = GAME_STATE_COUNTDOWN;
                s_game.local_choice = s_game.remote_choice = RPS_NONE; s_game.cursor_choice = RPS_ROCK;
                s_game.countdown = RPS_COUNTDOWN_SECONDS; s_countdown_started = now_ms(); s_last_rps_tx = now_ms();
                s_choice_acked = false; s_invite_receiver = true; status("即将开始");
                espnow_service_send(ESPNOW_MSG_RPS_ACCEPT, s_game.session, 0, 0, 0, false);
                changed = true;
            }
        }
        else if (p->type == ESPNOW_MSG_RPS_INVITE &&
                 (s_game.state == GAME_STATE_IDLE || s_game.state == GAME_STATE_RESULT)) {
            s_game.session = p->session; s_game.state = GAME_STATE_INVITE_RECEIVED;
            s_game.local_choice = s_game.remote_choice = RPS_NONE; s_game.cursor_choice = RPS_ROCK;
            s_choice_acked = false; s_invite_receiver = true; status("收到挑战！");
            s_deadline = now_ms() + INVITE_TIMEOUT_MS; changed = true;
        }
        else if (p->type == ESPNOW_MSG_RPS_INVITE && s_invite_receiver && p->session == s_game.session &&
                 (s_game.state == GAME_STATE_COUNTDOWN || s_game.state == GAME_STATE_WAITING_CHOICE || s_game.state == GAME_STATE_RESULT)) {
            espnow_service_send(ESPNOW_MSG_RPS_ACCEPT, s_game.session, 0, 0, 0, false);
        }
        else if (p->type == ESPNOW_MSG_RPS_ACCEPT && s_game.state == GAME_STATE_INVITE_SENT && p->session == s_game.session) {
            s_game.state = GAME_STATE_COUNTDOWN; s_countdown_started = now_ms(); s_game.countdown = RPS_COUNTDOWN_SECONDS;
            s_last_rps_tx = now_ms(); status("即将开始"); changed = true;
        }
        else if ((p->type == ESPNOW_MSG_RPS_REJECT || p->type == ESPNOW_MSG_RPS_CANCEL) && p->session == s_game.session) {
            s_game.state = GAME_STATE_IDLE;
            status(p->type == ESPNOW_MSG_RPS_REJECT ? "对方拒绝了挑战" : "对方退出了游戏");
            changed = true;
        }
        else if (p->type == ESPNOW_MSG_RPS_CHOICE && p->session == s_game.session && p->choice >= RPS_ROCK && p->choice <= RPS_PAPER) {
            espnow_service_send(ESPNOW_MSG_RPS_CHOICE_ACK, s_game.session, 0, p->choice, 0, false);
            s_game.remote_choice = (rps_choice_t)p->choice;
            if (s_game.local_choice != RPS_NONE && !s_choice_acked)
                espnow_service_send(ESPNOW_MSG_RPS_CHOICE, s_game.session, 0, s_game.local_choice, 0, false);
            finish_if_ready(); changed = true;
        }
        else if (p->type == ESPNOW_MSG_RPS_CHOICE_ACK && p->session == s_game.session &&
                 p->choice == s_game.local_choice) { s_choice_acked = true; }
    }
    xSemaphoreGive(s_lock); if (changed) notify();
}
