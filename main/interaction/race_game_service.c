#include "race_game_service.h"
#include "app_events.h"
#include "nvs_cache.h"
#include "sound_service.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define RACE_PROTOCOL_VERSION 2U
#define RACE_COUNTDOWN_MS 3000
#define RACE_STATE_MS 100
#define RACE_RETRY_MS 150
#define RACE_INVITE_RETRY_MS 400
#define RACE_INVITE_TIMEOUT_MS 15000
#define RACE_PEER_TIMEOUT_MS 5000
#define RACE_RESULT_RETRY_MS 5000
#define RACE_PREDICT_MS 400
#define RACE_DRS_PENDING_MS 1000
#define RACE_EXTRAPOLATE_MS 400
#define RACE_EVENT_SLOTS 4
#define RACE_INPUT_LANE 1U
#define RACE_INPUT_DRS 2U
#define RACE_ACK_EVENT 1U

typedef struct {
    uint16_t seq;
    uint32_t data;
} race_pending_event_t;

static const char *TAG = "kp_race";
static SemaphoreHandle_t s_lock;
/* The inviting device hosts its race as car 0 and referees it; the invited device is car 1
 * and renders the host's STATE. */
static bool s_host;
static uint8_t s_own_mac[6];
/* Host: authoritative race. Peer: the shared track items. */
static race_t s_race;
static race_phase_t s_phase;
static race_result_t s_result;
static uint16_t s_session;
static uint32_t s_wins, s_losses;
static bool s_settle_pending;
static char s_status[48];
static int64_t s_deadline;
static int64_t s_last_peer_rx, s_last_tx, s_last_state_tx, s_last_event_tx, s_last_step, s_result_until;
static uint16_t s_tx_seq, s_rx_seq, s_rx_input_seq;
static bool s_has_rx_seq, s_has_rx_input;
static race_pending_event_t s_events[RACE_EVENT_SLOTS];
static uint8_t s_event_count;
static race_wire_state_t s_wire;
static int64_t s_wire_at;
static uint8_t s_pred_lane;
static int64_t s_pred_until, s_drs_pending_until;
static uint8_t s_fb_effect, s_fb_drs, s_fb_countdown;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int local_car(void) { return s_host ? 0 : 1; }
static void notify(void) { app_event_post(&(app_event_t){.type = APP_EVT_GAME_UPDATE}, 0); }
static void set_status(const char *text) { strlcpy(s_status, text, sizeof(s_status)); }
static bool seq_newer(uint16_t value, uint16_t previous) { return (int16_t)(value - previous) > 0; }
static uint32_t packet_data(const espnow_game_packet_t *p)
{
    return (uint32_t)p->choice | ((uint32_t)(uint8_t)p->value << 8) | ((uint32_t)p->reserved << 16);
}
static uint16_t next_seq(void)
{
    if (++s_tx_seq == 0) ++s_tx_seq;
    return s_tx_seq;
}
static void send(espnow_msg_type_t type, uint16_t seq, uint32_t data)
{
    espnow_service_send_data(type, s_session, seq, data);
}

static void reset_locked(uint16_t session, bool host)
{
    s_session = session;
    s_host = host;
    race_init(&s_race, (uint32_t)session * 2654435761U);
    s_result = RACE_RESULT_NONE;
    s_last_peer_rx = s_last_tx = s_last_state_tx = s_last_event_tx = s_last_step = s_result_until = 0;
    s_tx_seq = s_rx_seq = s_rx_input_seq = 0;
    s_has_rx_seq = s_has_rx_input = false;
    s_event_count = 0;
    race_wire_from_race(&s_race, &s_wire);
    s_wire_at = 0;
    s_pred_lane = 1;
    s_pred_until = s_drs_pending_until = 0;
    s_fb_effect = RACE_EFFECT_NONE;
    s_fb_drs = RACE_DRS_LOCKED;
    s_fb_countdown = 0;
}

static void enter_idle_locked(const char *status)
{
    s_phase = RACE_PHASE_IDLE;
    s_session = 0;
    s_event_count = 0;
    set_status(status);
}

static void finish_locked(race_result_t result)
{
    s_phase = RACE_PHASE_RESULT;
    s_result = result;
    s_event_count = result == RACE_RESULT_ABORTED ? 0 : s_event_count;
    if (result == RACE_RESULT_WIN) { ++s_wins; s_settle_pending = true; }
    else if (result == RACE_RESULT_LOSE) { ++s_losses; s_settle_pending = true; }
    set_status(result == RACE_RESULT_WIN ? "你先冲过终点！" :
               result == RACE_RESULT_LOSE ? "对手先到终点" :
               result == RACE_RESULT_DRAW ? "同时冲线" : "连接中断，本局取消");
    sound_service_play(result == RACE_RESULT_WIN ? SOUND_FANFARE :
                       result == RACE_RESULT_DRAW ? SOUND_DING : SOUND_DU);
}

static race_result_t result_for_outcome(uint8_t outcome)
{
    if (outcome == RACE_OUTCOME_DRAW) return RACE_RESULT_DRAW;
    int winner = outcome == RACE_OUTCOME_HOST ? 0 : 1;
    return winner == local_car() ? RACE_RESULT_WIN : RACE_RESULT_LOSE;
}

static void persist(bool settle, uint32_t wins, uint32_t losses)
{
    if (!settle) return;
    esp_err_t err = nvs_cache_save_race_stats(wins, losses);
    if (err != ESP_OK) ESP_LOGW(TAG, "保存赛车战绩失败: %s", esp_err_to_name(err));
}

static void queue_event_locked(const race_event_t *event, int64_t now)
{
    if (s_event_count == RACE_EVENT_SLOTS) {
        memmove(s_events, s_events + 1, sizeof(s_events[0]) * (RACE_EVENT_SLOTS - 1));
        --s_event_count;
    }
    race_pending_event_t *slot = &s_events[s_event_count++];
    slot->seq = next_seq();
    slot->data = race_pack_event(event);
    send(ESPNOW_MSG_RACE_EVENT, slot->seq, slot->data);
    s_last_event_tx = now;
}

static void ack_event_locked(uint16_t seq)
{
    for (uint8_t i = 0; i < s_event_count; ++i) {
        if (s_events[i].seq != seq) continue;
        memmove(s_events + i, s_events + i + 1, sizeof(s_events[0]) * (s_event_count - i - 1));
        --s_event_count;
        return;
    }
}

static void send_countdown_locked(int64_t now)
{
    uint32_t remaining = s_deadline > now ? (uint32_t)(s_deadline - now) : 0;
    send(ESPNOW_MSG_RACE_COUNTDOWN, next_seq(), remaining);
    s_last_state_tx = now;
}

static void send_state_locked(int64_t now)
{
    race_wire_state_t wire;
    race_wire_from_race(&s_race, &wire);
    uint16_t seq = next_seq();
    uint32_t data = race_pack_state(&wire);
    for (int i = 0; i < 2; ++i) send(ESPNOW_MSG_RACE_STATE, seq, data);
    s_last_state_tx = now;
}

static void enter_countdown_locked(int64_t now)
{
    s_phase = RACE_PHASE_COUNTDOWN;
    s_deadline = now + RACE_COUNTDOWN_MS;
    s_last_peer_rx = now;
    set_status("准备出发");
    if (s_host) send_countdown_locked(now);
}

static void accept_as_peer_locked(int64_t now)
{
    enter_countdown_locked(now);
    send(ESPNOW_MSG_RACE_ACCEPT, 0, RACE_PROTOCOL_VERSION);
    s_last_tx = now;
}

static void start_racing_locked(int64_t now)
{
    s_phase = RACE_PHASE_RACING;
    s_last_step = now;
    set_status("比赛中");
    sound_service_play(SOUND_BUZZER_GO);
    if (s_host) send_state_locked(now);
}

static void local_view_locked(uint8_t *effect, uint8_t *drs)
{
    if (s_host) {
        race_wire_state_t wire;
        race_wire_from_race(&s_race, &wire);
        *effect = wire.effect[0];
        *drs = wire.drs[0];
    } else {
        *effect = s_wire.effect[1];
        *drs = s_wire.drs[1];
    }
}

static void feedback_locked(void)
{
    uint8_t effect, drs;
    local_view_locked(&effect, &drs);
    if (effect != s_fb_effect) {
        if (effect == RACE_EFFECT_NITRO) sound_service_play(SOUND_DING);
        else if (effect == RACE_EFFECT_SLOW) sound_service_play(SOUND_DU);
    }
    if (drs == RACE_DRS_ACTIVE && s_fb_drs != RACE_DRS_ACTIVE) sound_service_play(SOUND_DING);
    s_fb_effect = effect;
    s_fb_drs = drs;
}

static void host_step_locked(int64_t now, bool *changed)
{
    race_step_report_t report;
    uint32_t dt = now > s_last_step ? (uint32_t)(now - s_last_step) : 0;
    s_last_step = now;
    race_step(&s_race, dt, &report);
    for (uint8_t i = 0; i < report.claimed_count; ++i) {
        race_event_t event = {.kind = RACE_EVENT_NITRO_TAKEN, .row = report.claimed_row[i],
                              .lane = report.claimed_lane[i], .car = report.claimed_car[i]};
        queue_event_locked(&event, now);
    }
    if (s_race.outcome != RACE_OUTCOME_PENDING) {
        send_state_locked(now);
        race_event_t event = {.kind = RACE_EVENT_RESULT, .outcome = s_race.outcome};
        queue_event_locked(&event, now);
        s_result_until = now + RACE_RESULT_RETRY_MS;
        finish_locked(result_for_outcome(s_race.outcome));
        *changed = true;
    } else if (now - s_last_state_tx >= RACE_STATE_MS) {
        send_state_locked(now);
    }
}

esp_err_t race_game_service_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    reset_locked(0, false);
    enter_idle_locked("准备好了");
    esp_err_t err = esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) ESP_LOGW(TAG, "读取本机 MAC 失败: %s", esp_err_to_name(err));
    err = nvs_cache_load_race_stats(&s_wins, &s_losses);
    if (err != ESP_OK) ESP_LOGW(TAG, "加载赛车战绩失败: %s", esp_err_to_name(err));
    return ESP_OK;
}

bool race_game_service_owns_packet(uint8_t type)
{
    return type >= ESPNOW_MSG_RACE_INVITE && type <= ESPNOW_MSG_RACE_CANCEL;
}

void race_game_service_snapshot(race_game_snapshot_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now = now_ms();
    memset(out, 0, sizeof(*out));
    out->phase = s_phase;
    out->result = s_result;
    out->paired = espnow_service_has_peer();
    out->wins = s_wins;
    out->losses = s_losses;
    strlcpy(out->status, s_status, sizeof(out->status));
    memcpy(out->rows, s_race.rows, sizeof(out->rows));
    if (s_phase == RACE_PHASE_COUNTDOWN)
        out->countdown_s = s_deadline > now ? (uint8_t)((s_deadline - now + 999) / 1000) : 0;
    race_wire_state_t wire = s_wire;
    int64_t age = 0;
    if (s_host) race_wire_from_race(&s_race, &wire);
    else if (s_phase == RACE_PHASE_RACING && s_wire_at > 0)
        age = now - s_wire_at > RACE_EXTRAPOLATE_MS ? RACE_EXTRAPOLATE_MS : now - s_wire_at;
    for (int i = 0; i < 2; ++i) {
        int car = i == 0 ? local_car() : local_car() ^ 1;
        out->lane[i] = wire.lane[car];
        out->effect[i] = wire.effect[car];
        out->drs[i] = wire.drs[car];
        if (s_host) {
            out->pos_mm[i] = s_race.cars[car].pos_mm;
        } else {
            uint32_t pos = (uint32_t)wire.meters[car] * 1000U;
            unsigned pct = race_speed_percent((race_effect_t)wire.effect[car], wire.drs[car] == RACE_DRS_ACTIVE);
            if (pos < RACE_TRACK_MM) pos += (uint32_t)(age * RACE_BASE_SPEED_MM_S / 1000 * pct / 100);
            out->pos_mm[i] = pos > RACE_TRACK_MM ? RACE_TRACK_MM : pos;
        }
    }
    if (!s_host && now < s_pred_until) out->lane[0] = s_pred_lane;
    out->drs_pending = !s_host && now < s_drs_pending_until && out->drs[0] == RACE_DRS_AVAILABLE;
    xSemaphoreGive(s_lock);
}

void race_game_service_invite(void)
{
    if (!s_lock || !espnow_service_has_peer()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != RACE_PHASE_IDLE && s_phase != RACE_PHASE_RESULT) {
        xSemaphoreGive(s_lock);
        return;
    }
    uint16_t session;
    do { session = (uint16_t)esp_random(); } while (session == 0 || session == s_session);
    reset_locked(session, true);
    s_phase = RACE_PHASE_INVITE_SENT;
    s_deadline = now_ms() + RACE_INVITE_TIMEOUT_MS;
    set_status("正在邀请对手...");
    xSemaphoreGive(s_lock);
    notify();
}

void race_game_service_respond_invite(bool accept)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != RACE_PHASE_INVITE_RECEIVED) {
        xSemaphoreGive(s_lock);
        return;
    }
    int64_t now = now_ms();
    if (!accept) {
        send(ESPNOW_MSG_RACE_REJECT, 0, 0);
        enter_idle_locked("已拒绝邀请");
    } else {
        accept_as_peer_locked(now);
    }
    xSemaphoreGive(s_lock);
    notify();
}

void race_game_service_steer(int delta)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != RACE_PHASE_RACING) {
        xSemaphoreGive(s_lock);
        return;
    }
    int64_t now = now_ms();
    if (s_host) {
        race_set_lane(&s_race, 0, s_race.cars[0].lane + delta);
        send_state_locked(now);
    } else {
        int lane = (now < s_pred_until ? s_pred_lane : s_wire.lane[1]) + delta;
        lane = lane < 0 ? 0 : lane >= RACE_LANES ? RACE_LANES - 1 : lane;
        s_pred_lane = (uint8_t)lane;
        s_pred_until = now + RACE_PREDICT_MS;
        uint16_t seq = next_seq();
        for (int i = 0; i < 2; ++i)
            send(ESPNOW_MSG_RACE_INPUT, seq, RACE_INPUT_LANE | (uint32_t)lane << 2);
    }
    xSemaphoreGive(s_lock);
}

void race_game_service_request_drs(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != RACE_PHASE_RACING) {
        xSemaphoreGive(s_lock);
        return;
    }
    int64_t now = now_ms();
    if (s_host) {
        if (race_request_drs(&s_race, 0)) send_state_locked(now);
        else sound_service_play(SOUND_DU);
    } else if (s_wire.drs[1] == RACE_DRS_AVAILABLE) {
        s_drs_pending_until = now + RACE_DRS_PENDING_MS;
        uint16_t seq = next_seq();
        for (int i = 0; i < 3; ++i) send(ESPNOW_MSG_RACE_INPUT, seq, RACE_INPUT_DRS);
    } else {
        sound_service_play(SOUND_DU);
    }
    xSemaphoreGive(s_lock);
}

void race_game_service_cancel(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool live = s_phase != RACE_PHASE_IDLE && s_phase != RACE_PHASE_RESULT && s_session != 0;
    if (live)
        for (int i = 0; i < 3; ++i) send(ESPNOW_MSG_RACE_CANCEL, 0, 1U);
    bool changed = s_phase != RACE_PHASE_IDLE;
    enter_idle_locked("已退出比赛");
    xSemaphoreGive(s_lock);
    if (changed) notify();
}

void race_game_service_tick(int64_t now)
{
    if (!s_lock) return;
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase == RACE_PHASE_INVITE_SENT || s_phase == RACE_PHASE_INVITE_RECEIVED) {
        if (now >= s_deadline) {
            enter_idle_locked("邀请超时，请重试");
            changed = true;
        } else if (s_phase == RACE_PHASE_INVITE_SENT && now - s_last_tx >= RACE_INVITE_RETRY_MS) {
            send(ESPNOW_MSG_RACE_INVITE, 0, RACE_PROTOCOL_VERSION);
            s_last_tx = now;
        }
    }
    if ((s_phase == RACE_PHASE_COUNTDOWN || s_phase == RACE_PHASE_RACING) &&
        now - s_last_peer_rx >= RACE_PEER_TIMEOUT_MS) {
        finish_locked(RACE_RESULT_ABORTED);
        changed = true;
    }
    if (s_phase == RACE_PHASE_COUNTDOWN) {
        uint8_t seconds = s_deadline > now ? (uint8_t)((s_deadline - now + 999) / 1000) : 0;
        if (seconds != s_fb_countdown) {
            if (seconds > 0) sound_service_play(SOUND_TICK);
            s_fb_countdown = seconds;
            changed = true;
        }
        if (s_host) {
            if (now >= s_deadline) {
                start_racing_locked(now);
                changed = true;
            } else if (now - s_last_state_tx >= RACE_RETRY_MS) {
                send_countdown_locked(now);
            }
        } else if (!s_has_rx_seq && now - s_last_tx >= RACE_RETRY_MS) {
            send(ESPNOW_MSG_RACE_ACCEPT, 0, RACE_PROTOCOL_VERSION);
            s_last_tx = now;
        }
    } else if (s_phase == RACE_PHASE_RACING) {
        if (s_host) host_step_locked(now, &changed);
        if (s_phase == RACE_PHASE_RACING) feedback_locked();
    }
    if (s_host && s_event_count > 0 && now - s_last_event_tx >= RACE_RETRY_MS &&
        (s_phase == RACE_PHASE_RACING || (s_phase == RACE_PHASE_RESULT && now < s_result_until))) {
        for (uint8_t i = 0; i < s_event_count; ++i) send(ESPNOW_MSG_RACE_EVENT, s_events[i].seq, s_events[i].data);
        s_last_event_tx = now;
    }
    bool settle = s_settle_pending;
    s_settle_pending = false;
    uint32_t wins = s_wins, losses = s_losses;
    xSemaphoreGive(s_lock);
    persist(settle, wins, losses);
    if (changed) notify();
}

static bool handle_invite_locked(const uint8_t src[6], const espnow_game_packet_t *p, uint32_t data, int64_t now)
{
    if ((data & 0xffU) != RACE_PROTOCOL_VERSION) return false;
    bool changed = false;
    if (s_phase == RACE_PHASE_INVITE_SENT) {
        /* Crossed invites: one device keeps hosting its own race and ignores the other
         * invite; the other device drops its invite and joins as the peer. */
        if (p->session != s_session && !race_keeps_own_invite(s_own_mac, src)) {
            reset_locked(p->session, false);
            accept_as_peer_locked(now);
            changed = true;
        }
    } else if (s_phase == RACE_PHASE_IDLE || s_phase == RACE_PHASE_RESULT) {
        reset_locked(p->session, false);
        s_phase = RACE_PHASE_INVITE_RECEIVED;
        s_deadline = now + RACE_INVITE_TIMEOUT_MS;
        set_status("收到赛车邀请");
        changed = true;
    }
    s_last_peer_rx = now;
    return changed;
}

static bool handle_session_packet_locked(const espnow_game_packet_t *p, uint32_t data, int64_t now)
{
    bool changed = false;
    s_last_peer_rx = now;
    switch (p->type) {
    case ESPNOW_MSG_RACE_ACCEPT:
        if (s_host && (data & 0xffU) == RACE_PROTOCOL_VERSION) {
            if (s_phase == RACE_PHASE_INVITE_SENT) { enter_countdown_locked(now); changed = true; }
            else if (s_phase == RACE_PHASE_COUNTDOWN) send_countdown_locked(now);
        }
        break;
    case ESPNOW_MSG_RACE_REJECT:
        if (s_phase == RACE_PHASE_INVITE_SENT) { enter_idle_locked("对手拒绝了邀请"); changed = true; }
        break;
    case ESPNOW_MSG_RACE_COUNTDOWN:
        if (!s_host && s_phase == RACE_PHASE_COUNTDOWN && (!s_has_rx_seq || seq_newer(p->sequence, s_rx_seq))) {
            s_has_rx_seq = true;
            s_rx_seq = p->sequence;
            s_deadline = now + (int64_t)(data > RACE_COUNTDOWN_MS ? RACE_COUNTDOWN_MS : data);
            send(ESPNOW_MSG_RACE_ACK, p->sequence, 0);
        }
        break;
    case ESPNOW_MSG_RACE_STATE:
        if (!s_host && (s_phase == RACE_PHASE_COUNTDOWN || s_phase == RACE_PHASE_RACING) &&
            (!s_has_rx_seq || seq_newer(p->sequence, s_rx_seq))) {
            s_has_rx_seq = true;
            s_rx_seq = p->sequence;
            race_unpack_state(data, &s_wire);
            s_wire_at = now;
            if (s_phase == RACE_PHASE_COUNTDOWN) { start_racing_locked(now); changed = true; }
            send(ESPNOW_MSG_RACE_ACK, p->sequence, 0);
        }
        break;
    case ESPNOW_MSG_RACE_EVENT:
        if (!s_host && (s_phase == RACE_PHASE_RACING || s_phase == RACE_PHASE_RESULT ||
                        s_phase == RACE_PHASE_COUNTDOWN)) {
            race_event_t event;
            race_unpack_event(data, &event);
            if (event.kind == RACE_EVENT_NITRO_TAKEN) race_take_item(&s_race, event.row, event.lane);
            else if (event.kind == RACE_EVENT_RESULT && s_phase != RACE_PHASE_RESULT &&
                     event.outcome != RACE_OUTCOME_PENDING) {
                finish_locked(result_for_outcome(event.outcome));
                changed = true;
            }
            send(ESPNOW_MSG_RACE_ACK, p->sequence, RACE_ACK_EVENT);
        }
        break;
    case ESPNOW_MSG_RACE_ACK:
        if (s_host && (data & RACE_ACK_EVENT)) ack_event_locked(p->sequence);
        break;
    case ESPNOW_MSG_RACE_INPUT:
        if (s_host && s_phase == RACE_PHASE_RACING &&
            (!s_has_rx_input || seq_newer(p->sequence, s_rx_input_seq))) {
            s_has_rx_input = true;
            s_rx_input_seq = p->sequence;
            if ((data & 0x03U) == RACE_INPUT_LANE) race_set_lane(&s_race, 1, (int)((data >> 2) & 0x03U));
            else if ((data & 0x03U) == RACE_INPUT_DRS) race_request_drs(&s_race, 1);
            send_state_locked(now);
        }
        break;
    case ESPNOW_MSG_RACE_CANCEL:
        if (s_phase != RACE_PHASE_IDLE && s_phase != RACE_PHASE_RESULT) {
            finish_locked(RACE_RESULT_ABORTED);
            set_status("对手退出，本局取消");
            changed = true;
        }
        break;
    default:
        break;
    }
    return changed;
}

void race_game_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *p)
{
    if (!s_lock || !src || !p || !race_game_service_owns_packet(p->type)) return;
    uint8_t peer[6];
    if (!espnow_service_get_peer(peer) || memcmp(peer, src, 6) != 0) return;
    int64_t now = now_ms();
    uint32_t data = packet_data(p);
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (p->type == ESPNOW_MSG_RACE_INVITE) changed = handle_invite_locked(src, p, data, now);
    else if (s_session != 0 && p->session == s_session) changed = handle_session_packet_locked(p, data, now);
    bool settle = s_settle_pending;
    s_settle_pending = false;
    uint32_t wins = s_wins, losses = s_losses;
    xSemaphoreGive(s_lock);
    persist(settle, wins, losses);
    if (changed) notify();
}
