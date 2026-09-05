#include "mole_game_service.h"
#include "app_events.h"
#include "nvs_cache.h"
#include "sound_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

#define MOLE_PROTOCOL_VERSION 3U
#define MOLE_DURATION_MS 30000
#define MOLE_PERIOD_MS 3000
#define MOLE_COUNTDOWN_MS 3000
#define MOLE_TARGET_HITS 3
#define MOLE_HEARTBEAT_MS 250
#define MOLE_RETRY_MS 150
#define MOLE_INVITE_RETRY_MS 400
#define MOLE_INVITE_TIMEOUT_MS 15000
#define MOLE_PEER_TIMEOUT_MS 5000
#define MOLE_RESULT_RETRY_MS 5000
#define MOLE_INPUT_DEBOUNCE_MS 80
#define MOLE_INPUT_ARBITRATION_MS 50

static const char *TAG = "kp_mole";
static SemaphoreHandle_t s_lock;
static mole_game_snapshot_t s_game;
static int64_t s_countdown_deadline;
static int64_t s_play_deadline;
static int64_t s_mole_deadline;
static int64_t s_invite_deadline;
static int64_t s_last_peer_rx;
static int64_t s_last_state_tx;
static int64_t s_last_accept_tx;
static int64_t s_last_input_ms;
static int64_t s_result_retry_until;
static uint16_t s_state_sequence;
static uint16_t s_last_state_rx_sequence;
static uint16_t s_last_client_input_sequence;
static uint16_t s_client_input_sequence;
static uint16_t s_critical_sequence;
static uint32_t s_last_state_data;
static bool s_has_state_sequence;
static bool s_has_client_input_sequence;
static bool s_critical_acked;
static bool s_settlement_pending;
static bool s_settled;
static bool s_shot_pending;
static bool s_reload_pending;
static int64_t s_input_window_deadline;

static int64_t monotonic_ms(void) { return esp_timer_get_time() / 1000; }
static void notify(void) { app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0); }
static uint32_t packet_data(const espnow_game_packet_t *p)
{
    return (uint32_t)p->choice | ((uint32_t)(uint8_t)p->value << 8) |
           ((uint32_t)p->reserved << 16);
}
static bool sequence_newer(uint16_t value, uint16_t previous)
{
    return (int16_t)(value - previous) > 0;
}
static void set_status(const char *text)
{
    strlcpy(s_game.status, text, sizeof(s_game.status));
}
static uint16_t remaining_ds(int64_t now)
{
    int64_t deadline = 0;
    if (s_game.phase == MOLE_PHASE_INVITE_SENT || s_game.phase == MOLE_PHASE_INVITE_RECEIVED) deadline = s_invite_deadline;
    else if (s_game.phase == MOLE_PHASE_COUNTDOWN) deadline = s_countdown_deadline;
    else if (s_game.phase == MOLE_PHASE_PLAYING) deadline = s_play_deadline;
    if (deadline <= now) return 0;
    int64_t ds = (deadline - now + 99) / 100;
    return ds > 511 ? 511 : (uint16_t)ds;
}
static uint8_t phase_to_wire(mole_phase_t phase)
{
    switch (phase) {
    case MOLE_PHASE_COUNTDOWN: return 1U;
    case MOLE_PHASE_PLAYING: return 2U;
    case MOLE_PHASE_RESULT: return 3U;
    default: return 0U;
    }
}
static mole_phase_t phase_from_wire(uint8_t phase)
{
    switch (phase & 0x03U) {
    case 1U: return MOLE_PHASE_COUNTDOWN;
    case 2U: return MOLE_PHASE_PLAYING;
    case 3U: return MOLE_PHASE_RESULT;
    default: return MOLE_PHASE_IDLE;
    }
}
static uint32_t pack_state(int64_t now)
{
    uint32_t data = phase_to_wire(s_game.phase);
    data |= (uint32_t)(s_game.reticle_cell & 0x0f) << 2;
    data |= (uint32_t)(s_game.mole_cell & 0x0f) << 6;
    data |= (uint32_t)(s_game.ammo_loaded ? 1U : 0U) << 10;
    data |= (uint32_t)(s_game.hits & 0x07) << 11;
    data |= (uint32_t)(s_game.result & 0x03) << 14;
    data |= (uint32_t)(remaining_ds(now) & 0x01ffU) << 16;
    data |= (uint32_t)(s_game.mole_generation & 0x7fU) << 25;
    return data;
}
static void send_state_locked(int64_t now, bool new_revision, bool critical)
{
    if (!s_game.is_host || s_game.phase == MOLE_PHASE_IDLE) return;
    if (new_revision || s_state_sequence == 0) ++s_state_sequence;
    s_last_state_data = pack_state(now);
    espnow_service_send_data(ESPNOW_MSG_MOLE_GAME_STATE, s_game.session,
                             s_state_sequence, s_last_state_data);
    s_last_state_tx = now;
    if (critical) {
        s_critical_sequence = s_state_sequence;
        s_critical_acked = false;
    }
}
static void choose_next_mole_locked(void)
{
    uint8_t old = s_game.mole_cell;
    uint8_t next = (uint8_t)(esp_random() % 8U);
    if (next >= old) ++next;
    s_game.mole_cell = next;
    ++s_game.mole_generation;
}
static void reset_session_locked(bool host, uint16_t session)
{
    uint32_t wins = s_game.wins;
    uint32_t losses = s_game.losses;
    memset(&s_game, 0, sizeof(s_game));
    s_game.wins = wins;
    s_game.losses = losses;
    s_game.paired = espnow_service_has_peer();
    s_game.peer_connected = s_game.paired;
    s_game.is_host = host;
    s_game.session = session;
    s_game.reticle_cell = 4;
    s_game.mole_cell = (uint8_t)(esp_random() % 9U);
    s_game.ammo_loaded = true;
    s_state_sequence = 0;
    s_last_state_rx_sequence = 0;
    s_last_client_input_sequence = 0;
    s_has_state_sequence = false;
    s_has_client_input_sequence = false;
    s_last_peer_rx = 0;
    s_last_accept_tx = 0;
    s_last_input_ms = 0;
    s_critical_sequence = 0;
    s_critical_acked = false;
    s_settlement_pending = false;
    s_settled = false;
    s_shot_pending = false;
    s_reload_pending = false;
    s_input_window_deadline = 0;
}
static void enter_idle_locked(const char *status)
{
    uint32_t wins = s_game.wins;
    uint32_t losses = s_game.losses;
    memset(&s_game, 0, sizeof(s_game));
    s_game.wins = wins;
    s_game.losses = losses;
    s_game.paired = espnow_service_has_peer();
    s_game.peer_connected = s_game.paired;
    s_game.reticle_cell = 4;
    s_game.ammo_loaded = true;
    set_status(status);
}
static void finish_host_locked(mole_result_t result, int64_t now)
{
    if (!s_game.is_host || s_game.phase == MOLE_PHASE_RESULT) return;
    s_game.phase = MOLE_PHASE_RESULT;
    s_game.result = result;
    s_game.remaining_ds = 0;
    if (result == MOLE_RESULT_WIN) set_status("合作成功！");
    else if (result == MOLE_RESULT_LOSE) set_status("时间到，再试一次");
    else set_status("连接中断，本局取消");
    if (!s_settled && (result == MOLE_RESULT_WIN || result == MOLE_RESULT_LOSE)) {
        if (result == MOLE_RESULT_WIN) ++s_game.wins;
        else ++s_game.losses;
        s_settled = true;
        s_settlement_pending = true;
    }
    s_result_retry_until = now + MOLE_RESULT_RETRY_MS;
    send_state_locked(now, true, true);
}
static bool apply_host_input_locked(mole_input_t input, int64_t now)
{
    if (!s_game.is_host || s_game.phase != MOLE_PHASE_PLAYING || now >= s_play_deadline) return false;
    uint8_t old = s_game.reticle_cell;
    switch (input) {
    case MOLE_INPUT_UP:
        if (s_game.reticle_cell >= 3) s_game.reticle_cell -= 3;
        break;
    case MOLE_INPUT_DOWN:
        if (s_game.reticle_cell <= 5) s_game.reticle_cell += 3;
        break;
    case MOLE_INPUT_LEFT:
        if (s_game.reticle_cell % 3 != 0) --s_game.reticle_cell;
        break;
    case MOLE_INPUT_RIGHT:
        if (s_game.reticle_cell % 3 != 2) ++s_game.reticle_cell;
        break;
    case MOLE_INPUT_RELOAD:
        if (!s_game.ammo_loaded) s_game.ammo_loaded = true;
        break;
    case MOLE_INPUT_SHOOT:
        if (!s_game.ammo_loaded) return false;
        s_game.ammo_loaded = false;
        sound_service_play(SOUND_MOLE_SHOOT);
        if (s_game.reticle_cell == s_game.mole_cell) {
            sound_service_play(SOUND_MOLE_HIT);
            ++s_game.hits;
            if (s_game.hits >= MOLE_TARGET_HITS) {
                finish_host_locked(MOLE_RESULT_WIN, now);
                return true;
            }
            choose_next_mole_locked();
            s_mole_deadline = now + MOLE_PERIOD_MS;
        }
        break;
    default:
        return false;
    }
    if (old == s_game.reticle_cell &&
        (input == MOLE_INPUT_UP || input == MOLE_INPUT_DOWN ||
         input == MOLE_INPUT_LEFT || input == MOLE_INPUT_RIGHT)) return false;
    send_state_locked(now, true, false);
    return true;
}

esp_err_t mole_game_service_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    enter_idle_locked("准备好了");
    esp_err_t err = nvs_cache_load_mole_stats(&s_game.wins, &s_game.losses);
    if (err != ESP_OK) ESP_LOGW(TAG, "加载打地鼠战绩失败: %s", esp_err_to_name(err));
    return ESP_OK;
}
void mole_game_service_snapshot(mole_game_snapshot_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_game;
    if (s_game.phase == MOLE_PHASE_COUNTDOWN || s_game.phase == MOLE_PHASE_PLAYING)
        out->remaining_ds = remaining_ds(monotonic_ms());
    xSemaphoreGive(s_lock);
}
bool mole_game_service_owns_packet(uint8_t type)
{
    return type >= ESPNOW_MSG_MOLE_INVITE && type <= ESPNOW_MSG_MOLE_CANCEL;
}
void mole_game_service_begin_session(void)
{
    if (!s_lock || !espnow_service_has_peer()) return;
    int64_t now = monotonic_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.phase != MOLE_PHASE_IDLE && s_game.phase != MOLE_PHASE_RESULT) {
        xSemaphoreGive(s_lock);
        return;
    }
    uint16_t session;
    do { session = (uint16_t)esp_random(); } while (session == 0 || session == s_game.session);
    reset_session_locked(true, session);
    s_game.phase = MOLE_PHASE_INVITE_SENT;
    set_status("正在邀请对方...");
    s_invite_deadline = now + MOLE_INVITE_TIMEOUT_MS;
    s_last_state_tx = 0;
    xSemaphoreGive(s_lock);
    notify();
}
void mole_game_service_respond_invite(bool accept)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.phase != MOLE_PHASE_INVITE_RECEIVED) {
        xSemaphoreGive(s_lock);
        return;
    }
    if (accept) {
        int64_t now = monotonic_ms();
        s_game.phase = MOLE_PHASE_COUNTDOWN;
        s_countdown_deadline = now + MOLE_COUNTDOWN_MS;
        set_status("已接受，等待开始");
        espnow_service_send_data(ESPNOW_MSG_MOLE_ACCEPT, s_game.session, 0, MOLE_PROTOCOL_VERSION);
        s_last_accept_tx = now;
        ESP_LOGI(TAG, "tx ACCEPT session=%u", s_game.session);
    } else {
        espnow_service_send_data(ESPNOW_MSG_MOLE_REJECT, s_game.session, 0, 0);
        enter_idle_locked("已拒绝邀请");
    }
    xSemaphoreGive(s_lock);
    notify();
}
void mole_game_service_host_input(mole_input_t input)
{
    if (!s_lock || !s_game.is_host) return;
    int64_t now = monotonic_ms();
    bool changed;
    bool settle;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.phase != MOLE_PHASE_PLAYING) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_last_input_ms = now;
    if (input == MOLE_INPUT_SHOOT && s_game.phase == MOLE_PHASE_PLAYING) {
        s_shot_pending = true;
        if (s_input_window_deadline == 0) s_input_window_deadline = now + MOLE_INPUT_ARBITRATION_MS;
        changed = false;
    } else {
        changed = apply_host_input_locked(input, now);
    }
    settle = s_settlement_pending;
    s_settlement_pending = false;
    uint32_t wins = s_game.wins, losses = s_game.losses;
    xSemaphoreGive(s_lock);
    if (settle) nvs_cache_save_mole_stats(wins, losses);
    if (changed) notify();
}
void mole_game_service_client_input(mole_input_t input)
{
    if (!s_lock || s_game.is_host) return;
    int64_t now = monotonic_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.phase != MOLE_PHASE_PLAYING || now - s_last_input_ms < MOLE_INPUT_DEBOUNCE_MS) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_last_input_ms = now;
    uint16_t input_sequence = ++s_client_input_sequence;
    uint8_t wire_action = input == MOLE_INPUT_LEFT ? 1U : input == MOLE_INPUT_RIGHT ? 2U : 3U;
    uint32_t data = (uint32_t)wire_action | ((uint32_t)input_sequence << 16);
    for (int i = 0; i < 3; ++i)
        espnow_service_send_data(ESPNOW_MSG_MOLE_INPUT, s_game.session, input_sequence, data);
    if (input == MOLE_INPUT_RELOAD) sound_service_play(SOUND_MOLE_RELOAD);
    xSemaphoreGive(s_lock);
}
void mole_game_service_cancel(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_game.phase != MOLE_PHASE_IDLE && s_game.session != 0) {
        for (int i = 0; i < 3; ++i)
            espnow_service_send_data(ESPNOW_MSG_MOLE_CANCEL, s_game.session, 0, 1U);
    }
    enter_idle_locked("已退出游戏");
    xSemaphoreGive(s_lock);
    notify();
}
void mole_game_service_tick(int64_t now)
{
    if (!s_lock) return;
    bool changed = false;
    bool settle = false;
    uint32_t wins = 0, losses = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t previous_remaining_ds = s_game.remaining_ds;
    s_game.paired = espnow_service_has_peer();
    if (s_game.phase == MOLE_PHASE_INVITE_SENT && now - s_last_state_tx >= MOLE_INVITE_RETRY_MS) {
        uint32_t rules = MOLE_PROTOCOL_VERSION | (30U << 8) | (MOLE_TARGET_HITS << 16) | (30U << 24);
        espnow_service_send_data(ESPNOW_MSG_MOLE_INVITE, s_game.session, 0, rules);
        s_last_state_tx = now;
    }
    if (!s_game.is_host && s_game.phase == MOLE_PHASE_COUNTDOWN && !s_has_state_sequence &&
        now - s_last_accept_tx >= MOLE_RETRY_MS) {
        espnow_service_send_data(ESPNOW_MSG_MOLE_ACCEPT, s_game.session, 0, MOLE_PROTOCOL_VERSION);
        s_last_accept_tx = now;
        ESP_LOGI(TAG, "retry ACCEPT session=%u", s_game.session);
    }
    if ((s_game.phase == MOLE_PHASE_INVITE_SENT || s_game.phase == MOLE_PHASE_INVITE_RECEIVED) && now >= s_invite_deadline) {
        enter_idle_locked("连接超时，请重试");
        changed = true;
    }
    if (s_game.phase != MOLE_PHASE_IDLE && s_game.phase != MOLE_PHASE_RESULT &&
        s_game.phase != MOLE_PHASE_INVITE_SENT && s_game.phase != MOLE_PHASE_INVITE_RECEIVED &&
        s_last_peer_rx > 0 && now - s_last_peer_rx >= MOLE_PEER_TIMEOUT_MS) {
        if (s_game.is_host) finish_host_locked(MOLE_RESULT_ABORTED, now);
        else {
            s_game.phase = MOLE_PHASE_RESULT;
            s_game.result = MOLE_RESULT_ABORTED;
            set_status("连接中断，本局取消");
        }
        s_game.peer_connected = false;
        changed = true;
    }
    if (s_game.is_host && s_game.phase == MOLE_PHASE_COUNTDOWN && now >= s_countdown_deadline) {
        s_game.phase = MOLE_PHASE_PLAYING;
        s_play_deadline = now + MOLE_DURATION_MS;
        s_mole_deadline = now + MOLE_PERIOD_MS;
        set_status("合作打中 3 只地鼠");
        send_state_locked(now, true, true);
        changed = true;
    }
    if (s_game.is_host && s_game.phase == MOLE_PHASE_PLAYING) {
        if (now >= s_play_deadline) {
            finish_host_locked(MOLE_RESULT_LOSE, now);
            changed = true;
        } else if (now >= s_mole_deadline) {
            choose_next_mole_locked();
            s_mole_deadline = now + MOLE_PERIOD_MS;
            send_state_locked(now, true, false);
            changed = true;
        } else if (s_critical_acked && now - s_last_state_tx >= MOLE_HEARTBEAT_MS) {
            send_state_locked(now, true, false);
        }
    }
    if (s_game.is_host && s_game.phase == MOLE_PHASE_PLAYING && s_input_window_deadline > 0 &&
        now >= s_input_window_deadline) {
        if (s_shot_pending) changed = apply_host_input_locked(MOLE_INPUT_SHOOT, now) || changed;
        if (s_reload_pending) changed = apply_host_input_locked(MOLE_INPUT_RELOAD, now) || changed;
        s_shot_pending = false;
        s_reload_pending = false;
        s_input_window_deadline = 0;
    } else if (s_game.phase != MOLE_PHASE_PLAYING) {
        s_shot_pending = false;
        s_reload_pending = false;
        s_input_window_deadline = 0;
    }
    if (s_game.is_host && !s_critical_acked && s_critical_sequence != 0 &&
        now - s_last_state_tx >= MOLE_RETRY_MS &&
        (s_game.phase != MOLE_PHASE_RESULT || now < s_result_retry_until)) {
        espnow_service_send_data(ESPNOW_MSG_MOLE_GAME_STATE, s_game.session,
                                 s_critical_sequence, s_last_state_data);
        s_last_state_tx = now;
    }
    s_game.remaining_ds = remaining_ds(now);
    if ((s_game.phase == MOLE_PHASE_COUNTDOWN || s_game.phase == MOLE_PHASE_PLAYING) &&
        previous_remaining_ds / 10 != s_game.remaining_ds / 10) changed = true;
    if (s_settlement_pending) {
        settle = true;
        s_settlement_pending = false;
        wins = s_game.wins;
        losses = s_game.losses;
    }
    xSemaphoreGive(s_lock);
    if (settle) {
        esp_err_t err = nvs_cache_save_mole_stats(wins, losses);
        if (err != ESP_OK) ESP_LOGW(TAG, "保存打地鼠战绩失败: %s", esp_err_to_name(err));
    }
    if (changed) notify();
}
void mole_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *p)
{
    if (!s_lock || !src || !p || !mole_game_service_owns_packet(p->type)) return;
    uint8_t peer[6];
    if (!espnow_service_get_peer(peer) || memcmp(peer, src, 6) != 0) return;
    int64_t now = monotonic_ms();
    uint32_t data = packet_data(p);
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (p->type == ESPNOW_MSG_MOLE_INVITE) {
        if ((data & 0xffU) != MOLE_PROTOCOL_VERSION) {
            xSemaphoreGive(s_lock);
            return;
        }
        if (s_game.phase == MOLE_PHASE_INVITE_SENT) {
            if (strcmp(CONFIG_KIDS_DEVICE_ID, CONFIG_KIDS_PEER_DEVICE_ID) < 0) {
                reset_session_locked(false, p->session);
                s_game.phase = MOLE_PHASE_INVITE_RECEIVED;
                set_status("收到打地鼠邀请");
                s_invite_deadline = now + MOLE_INVITE_TIMEOUT_MS;
                changed = true;
            }
        } else if (s_game.phase == MOLE_PHASE_IDLE || s_game.phase == MOLE_PHASE_RESULT) {
            reset_session_locked(false, p->session);
            s_game.phase = MOLE_PHASE_INVITE_RECEIVED;
            set_status("收到打地鼠邀请");
            s_invite_deadline = now + MOLE_INVITE_TIMEOUT_MS;
            changed = true;
        }
        s_last_peer_rx = now;
    } else if (p->session != s_game.session || s_game.session == 0) {
        xSemaphoreGive(s_lock);
        return;
    } else {
        s_last_peer_rx = now;
        s_game.peer_connected = true;
        if (p->type == ESPNOW_MSG_MOLE_ACCEPT && s_game.is_host &&
            (data & 0xffU) == MOLE_PROTOCOL_VERSION &&
            (s_game.phase == MOLE_PHASE_INVITE_SENT || s_game.phase == MOLE_PHASE_COUNTDOWN)) {
            ESP_LOGI(TAG, "rx ACCEPT session=%u phase=%u", p->session, (unsigned)s_game.phase);
            if (s_game.phase == MOLE_PHASE_COUNTDOWN) {
                send_state_locked(now, false, true);
            } else {
                s_game.phase = MOLE_PHASE_COUNTDOWN;
                s_game.reticle_cell = 4;
                s_game.hits = 0;
                s_game.ammo_loaded = true;
                s_game.mole_generation = 0;
                s_countdown_deadline = now + MOLE_COUNTDOWN_MS;
                set_status("准备：3");
                send_state_locked(now, true, true);
                changed = true;
            }
        } else if (p->type == ESPNOW_MSG_MOLE_REJECT && s_game.is_host && s_game.phase == MOLE_PHASE_INVITE_SENT) {
            enter_idle_locked("对方拒绝了邀请");
            changed = true;
        } else if (p->type == ESPNOW_MSG_MOLE_INPUT && s_game.is_host &&
                   s_game.phase == MOLE_PHASE_PLAYING) {
            uint16_t input_sequence = (uint16_t)(data >> 16);
            if (!s_has_client_input_sequence || sequence_newer(input_sequence, s_last_client_input_sequence)) {
                s_has_client_input_sequence = true;
                s_last_client_input_sequence = input_sequence;
                uint8_t wire_action = (uint8_t)(data & 0x07U);
                mole_input_t input = wire_action == 1 ? MOLE_INPUT_LEFT :
                                     wire_action == 2 ? MOLE_INPUT_RIGHT : MOLE_INPUT_RELOAD;
                if (wire_action == 3) {
                    s_reload_pending = true;
                    if (s_input_window_deadline == 0)
                        s_input_window_deadline = now + MOLE_INPUT_ARBITRATION_MS;
                } else if (wire_action == 1 || wire_action == 2) {
                    changed = apply_host_input_locked(input, now);
                }
            }
        } else if (p->type == ESPNOW_MSG_MOLE_GAME_STATE && !s_game.is_host) {
            if (!s_has_state_sequence || sequence_newer(p->sequence, s_last_state_rx_sequence)) {
                mole_game_snapshot_t old = s_game;
                s_has_state_sequence = true;
                s_last_state_rx_sequence = p->sequence;
                s_game.phase = phase_from_wire((uint8_t)data);
                s_game.reticle_cell = (uint8_t)((data >> 2) & 0x0fU);
                s_game.mole_cell = (uint8_t)((data >> 6) & 0x0fU);
                s_game.ammo_loaded = ((data >> 10) & 1U) != 0;
                s_game.hits = (uint8_t)((data >> 11) & 0x07U);
                s_game.result = (mole_result_t)((data >> 14) & 0x03U);
                s_game.remaining_ds = (uint16_t)((data >> 16) & 0x01ffU);
                s_game.mole_generation = (uint8_t)(data >> 25);
                if (s_game.phase == MOLE_PHASE_COUNTDOWN)
                    s_countdown_deadline = now + (int64_t)s_game.remaining_ds * 100;
                else if (s_game.phase == MOLE_PHASE_PLAYING)
                    s_play_deadline = now + (int64_t)s_game.remaining_ds * 100;
                if (s_game.phase == MOLE_PHASE_COUNTDOWN) set_status("准备倒计时");
                else if (s_game.phase == MOLE_PHASE_PLAYING) set_status("合作打中 3 只地鼠");
                else if (s_game.result == MOLE_RESULT_WIN) set_status("合作成功！");
                else if (s_game.result == MOLE_RESULT_LOSE) set_status("时间到，再试一次");
                else if (s_game.result == MOLE_RESULT_ABORTED) set_status("连接中断，本局取消");
                changed = old.phase != s_game.phase || old.result != s_game.result ||
                          old.reticle_cell != s_game.reticle_cell || old.mole_cell != s_game.mole_cell ||
                          old.mole_generation != s_game.mole_generation || old.ammo_loaded != s_game.ammo_loaded ||
                          old.hits != s_game.hits || old.remaining_ds / 10 != s_game.remaining_ds / 10;
                espnow_service_send_data(ESPNOW_MSG_MOLE_STATE_ACK, s_game.session,
                                         p->sequence, p->sequence);
            } else if (p->sequence == s_last_state_rx_sequence) {
                espnow_service_send_data(ESPNOW_MSG_MOLE_STATE_ACK, s_game.session,
                                         p->sequence, p->sequence);
            }
        } else if (p->type == ESPNOW_MSG_MOLE_STATE_ACK && s_game.is_host &&
                   ((uint16_t)data == s_critical_sequence ||
                    sequence_newer((uint16_t)data, s_critical_sequence))) {
            s_critical_acked = true;
        } else if (p->type == ESPNOW_MSG_MOLE_CANCEL) {
            s_game.phase = MOLE_PHASE_RESULT;
            s_game.result = MOLE_RESULT_ABORTED;
            set_status("对方退出，本局取消");
            changed = true;
        }
    }
    xSemaphoreGive(s_lock);
    if (changed) notify();
}
