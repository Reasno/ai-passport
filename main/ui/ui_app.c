#include "ui_app.h"
#include "app_events.h"
#include "app_model.h"
#include "buzzer_game_service.h"
#include "find_service.h"
#include "game_service.h"
#include "mole_game_service.h"
#include "mqtt_service.h"
#include "nvs_cache.h"
#include "power_service.h"
#include "privacy_mode.h"
#include "ptt_service.h"
#include "sound_service.h"
#include "ui_common.h"
#include "ui_confirm.h"
#include "ui_games.h"
#include "ui_home.h"
#include "ui_lottery.h"
#include "lottery_assets.h"
#include "ui_redeem.h"
#include "ui_tasks.h"
#include "ui_text.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

typedef enum { PAGE_HOME, PAGE_TASKS, PAGE_CONFIRM, PAGE_REDEEM, PAGE_LOTTERY, PAGE_GAMES, PAGE_FIND, PAGE_RPS, PAGE_BUZZER, PAGE_MOLE } page_t;
static const char *TAG = "kp_ui";
static page_t s_page = PAGE_HOME, s_confirm_return = PAGE_HOME;
static int s_selected; static confirm_kind_t s_confirm_kind;
static char s_confirm_id[APP_ID_LEN], s_confirm_name[APP_NAME_LEN]; static int s_confirm_points;
static char s_message[128]; static bool s_message_error; static int64_t s_message_until;
static int s_lottery_rotation; static bool s_lottery_animating; static int64_t s_lottery_reveal_at;
static bool s_suppress_wake_key;
static bool s_mole_page_created;
static bool s_buzzer_press_consumed;
static bool s_mole_b3_block_click;
static int64_t s_b3_press_started;
static bool s_b3_privacy_consumed;
#if CONFIG_ENABLE_SCREENSHOT
typedef enum { DEBUG_LOTTERY_IDLE, DEBUG_LOTTERY_SPIN, DEBUG_LOTTERY_RESULT } debug_lottery_t;
typedef enum { DEBUG_BUZZER_NONE, DEBUG_BUZZER_ARMED, DEBUG_BUZZER_GO, DEBUG_BUZZER_RESULT } debug_buzzer_t;
static bool s_debug_preview;
static debug_lottery_t s_debug_lottery;
static debug_buzzer_t s_debug_buzzer;
static int s_debug_prize_index;
#endif
static bool s_find_waiting, s_find_ringing, s_find_flash, s_ignore_key_until_release, s_ignore_ring_click;
static lv_obj_t *s_find_overlay;
static int64_t s_find_deadline, s_find_ring_deadline, s_find_next_flash;
static char s_find_status[64], s_find_sender[64];
/* Large cross-module snapshots are static to preserve the measured-safe 4 KB UI stack. */
static app_model_snapshot_t s_ui_model;
static game_snapshot_t s_game;
static buzzer_game_snapshot_t s_buzzer;
static mole_game_snapshot_t s_mole;

static app_model_snapshot_t *model_snapshot(void) { app_model_snapshot(&s_ui_model); return &s_ui_model; }
static game_snapshot_t *game_snapshot(void) { game_service_snapshot(&s_game); return &s_game; }
static buzzer_game_snapshot_t *buzzer_snapshot(void) { buzzer_game_service_snapshot(&s_buzzer); return &s_buzzer; }
static mole_game_snapshot_t *mole_snapshot(void) { mole_game_service_snapshot(&s_mole); return &s_mole; }
static void set_message(const char *text, bool error)
{
    ui_text_limit_lines(text, s_message, sizeof(s_message), UI_TEXT_STANDARD_MAX_CHARS);
    s_message_error = error;
    s_message_until = esp_timer_get_time() / 1000 + 2400;
}
static void render(void)
{
    app_model_snapshot_t *model = model_snapshot();
    game_snapshot_t *game = game_snapshot();
    buzzer_game_snapshot_t *buzzer = buzzer_snapshot();
    mole_game_snapshot_t *mole = mole_snapshot();
#if CONFIG_ENABLE_SCREENSHOT
    /* Preview-only snapshots never mutate ESP-NOW, MQTT, pairing, radar or RPS state. */
    if (s_debug_preview && s_page == PAGE_FIND) {
        game->paired = true;
        game->peer_nearby = true;
        game->rssi = -54;
        game->distance_bars = 4;
    } else if (s_debug_preview && s_page == PAGE_LOTTERY && s_debug_lottery == DEBUG_LOTTERY_RESULT) {
        /* Each LOTTERY_RESULT request steps to the next prize so a capture run covers
         * every image plus both hint variants. */
        model->lottery_ready = true;
        model->lottery_points_delta = 0;
        strlcpy(model->lottery_prize_id, lottery_prize_id_at(s_debug_prize_index),
                sizeof(model->lottery_prize_id));
        model->lottery_label[0] = 0;
    } else if (s_debug_preview && s_page == PAGE_RPS) {
        game->state = GAME_STATE_WAITING_CHOICE;
        game->seconds_left = 10;
        strlcpy(game->status, "请选择出拳", sizeof(game->status));
    } else if (s_debug_preview && s_page == PAGE_BUZZER) {
        memset(buzzer, 0, sizeof(*buzzer));
        buzzer->paired = true;
        buzzer->is_host = true;
        if (s_debug_buzzer == DEBUG_BUZZER_GO) {
            buzzer->state = BUZZER_STATE_GO;
            buzzer->lights_on = 0;
            strlcpy(buzzer->status, "GO！快按 B3", sizeof(buzzer->status));
        } else if (s_debug_buzzer == DEBUG_BUZZER_RESULT) {
            buzzer->state = BUZZER_STATE_RESULT;
            buzzer->result = BUZZER_RESULT_WIN;
            buzzer->local_pressed = true;
            strlcpy(buzzer->status, "你赢了！", sizeof(buzzer->status));
        } else {
            /* BUZZER is an alias of the most useful full-page review state. */
            buzzer->state = BUZZER_STATE_ARMED;
            buzzer->lights_on = 3;
            strlcpy(buzzer->status, "准备，别抢跑", sizeof(buzzer->status));
        }
    }
#endif
    if (s_page == PAGE_MOLE && s_mole_page_created) {
        if (bsp_lvgl_lock(100)) {
            ui_mole_update(mole);
            bsp_lvgl_unlock();
        }
        return;
    }
    if (!bsp_lvgl_lock(1000)) return;
    lv_obj_t *screen = NULL;
    if (s_page == PAGE_HOME) screen = ui_home_build(model, s_selected);
    else if (s_page == PAGE_TASKS) screen = ui_tasks_build(model, s_selected);
    else if (s_page == PAGE_REDEEM) screen = ui_redeem_build(model, s_selected);
    else if (s_page == PAGE_CONFIRM) screen = ui_confirm_build(model, s_confirm_kind, s_confirm_name, s_confirm_points, s_selected);
    else if (s_page == PAGE_LOTTERY) screen = ui_lottery_build(model, s_lottery_rotation,
#if CONFIG_ENABLE_SCREENSHOT
                                                               s_lottery_animating || (s_debug_preview && s_debug_lottery == DEBUG_LOTTERY_SPIN)
#else
                                                               s_lottery_animating
#endif
                                                               );
    else if (s_page == PAGE_GAMES) screen = ui_games_build(model, game, s_selected);
    else if (s_page == PAGE_FIND) screen = ui_find_build(model, game, s_find_status, s_find_waiting);
    else if (s_page == PAGE_RPS) screen = ui_rps_build(model, game);
    else if (s_page == PAGE_BUZZER) screen = ui_buzzer_build(model, buzzer);
    else {
        screen = ui_mole_build(model, mole);
        s_mole_page_created = true;
    }
    if (s_message[0] && esp_timer_get_time() / 1000 < s_message_until) ui_common_message(screen, s_message, s_message_error);
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    bsp_lvgl_unlock();
}
static void set_find_overlay_visible(bool visible)
{
    if (!s_find_overlay || !bsp_lvgl_lock(100)) return;
    if (visible) {
        ui_common_find_overlay_tint(s_find_overlay, s_find_flash);
        lv_obj_remove_flag(s_find_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_find_overlay);
    } else {
        lv_obj_add_flag(s_find_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_lvgl_unlock();
}
static void show_find_ring_overlay(void)
{
    if (!s_find_overlay) {
        ESP_LOGE(TAG, "Find overlay unavailable; ringing without visual overlay");
        return;
    }
    set_find_overlay_visible(true);
}
static void ui_app_init(void)
{
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Find overlay preallocation skipped: LVGL lock timeout");
        return;
    }
    s_find_overlay = ui_common_find_overlay(lv_layer_top(), false);
    if (s_find_overlay) {
        lv_obj_add_flag(s_find_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_mem_monitor_t monitor;
        lv_mem_monitor(&monitor);
        ESP_LOGI(TAG, "Find overlay preallocated; LVGL free=%lu biggest=%lu",
                 (unsigned long)monitor.free_size, (unsigned long)monitor.free_biggest_size);
    } else {
        ESP_LOGE(TAG, "Find overlay preallocation failed; ringing will continue without overlay");
    }
    bsp_lvgl_unlock();
}
static void go(page_t page, int selected)
{
#if CONFIG_ENABLE_SCREENSHOT
    s_debug_preview = false;
#endif
    if (s_page == PAGE_FIND && page != PAGE_FIND) {
        game_service_set_radar(false);
        ptt_service_set_transmitting(false);
    }
    if (s_page == PAGE_MOLE && page != PAGE_MOLE) {
        ui_mole_forget();
        s_mole_page_created = false;
    }
    if (s_page != PAGE_MOLE && page == PAGE_MOLE) s_mole_page_created = false;
    s_page = page; s_selected = selected; ESP_LOGI(TAG, "页面=%d 选中=%d", page, selected); render();
}
static void key_move(int delta, int count)
{
    if (count <= 0) return;
    s_selected = (s_selected + delta + count) % count; sound_service_play(SOUND_TICK); render();
}
static void begin_confirm(confirm_kind_t kind, page_t back, const char *id, const char *name, int points)
{
    s_confirm_kind = kind; s_confirm_return = back; strlcpy(s_confirm_id, id, sizeof(s_confirm_id));
    ui_text_limit_lines(name, s_confirm_name, sizeof(s_confirm_name), UI_TEXT_STANDARD_MAX_CHARS);
    s_confirm_points = points; go(PAGE_CONFIRM, 0);
}
static void handle_short_key(bsp_btn_t key)
{
    app_model_snapshot_t *model = model_snapshot(); game_snapshot_t *game = game_snapshot();
    buzzer_game_snapshot_t *buzzer = buzzer_snapshot();
    mole_game_snapshot_t *mole = mole_snapshot();
    if (model->pending_type != APP_PENDING_NONE && s_page != PAGE_LOTTERY && s_page != PAGE_GAMES && s_page != PAGE_FIND && s_page != PAGE_RPS && s_page != PAGE_BUZZER && s_page != PAGE_MOLE) return;
    int delta = key == BSP_BTN_UP ? -1 : 1;
    if (s_page == PAGE_HOME) {
        if (key != BSP_BTN_OK) key_move(delta, 3);
        else if (s_selected == 0) go(PAGE_TASKS, 0); else if (s_selected == 1) go(PAGE_REDEEM, 0); else go(PAGE_GAMES, 0);
    } else if (s_page == PAGE_TASKS) {
        if (key != BSP_BTN_OK) key_move(delta, model->task_count);
        else if (!model->task_count) { set_message("今天还没有任务", false); render(); }
        else { const app_task_t *t = &model->tasks[s_selected];
            if (!model->mqtt_online) set_message("当前离线，请联网再试", true);
            else if (t->completed_today) set_message("这个任务已经完成啦", false);
            else if (!t->self_complete) set_message("这个任务不在这里完成", false);
            else { begin_confirm(CONFIRM_TASK, PAGE_TASKS, t->id, t->name, t->points); return; } render(); }
    } else if (s_page == PAGE_REDEEM) {
        if (key != BSP_BTN_OK) key_move(delta, 2);
        else { int idx = ui_redeem_model_index(model, s_selected);
            if (!model->mqtt_online) set_message("当前离线\n请连接Wi-Fi再试", true);
            else if (idx < 0 || !model->rewards[idx].enabled) set_message("奖品暂未开放", true);
            else if (!model->balance_valid || model->balance < model->rewards[idx].price) set_message("积分不够，继续努力哦", true);
            else { begin_confirm(CONFIRM_REDEEM, PAGE_REDEEM, model->rewards[idx].id, model->rewards[idx].name, model->rewards[idx].price); return; } render(); }
    } else if (s_page == PAGE_CONFIRM) {
        if (key != BSP_BTN_OK) key_move(delta, 2);
        else if (s_selected == 0) go(s_confirm_return, 0);
        else { bool sent = s_confirm_kind == CONFIRM_TASK ? mqtt_service_publish_complete_task(s_confirm_id) : mqtt_service_publish_redeem(s_confirm_id);
            if (sent) { set_message("正在处理中，请稍候...", false); sound_service_play(SOUND_TICK); go(s_confirm_return, 0); }
            else { set_message("无法提交\n请检查网络后重试", true); sound_service_play(SOUND_DU); render(); } }
    } else if (s_page == PAGE_LOTTERY && model->lottery_ready && !s_lottery_animating && key == BSP_BTN_OK) go(PAGE_REDEEM, 1);
    else if (s_page == PAGE_GAMES) {
        if (key != BSP_BTN_OK) key_move(delta, 4);
        /* Find only needs one live transport; both competitive games require pairing. */
        else if (s_selected == 0) {
            if (!game->paired && !model->mqtt_online) { set_message("请长按B3先配对", false); render(); }
            else if (!game_service_heap_allows_radar()) { set_message("内存不足\n找" KP_PEER_LABEL "暂不可用", true); render(); }
            else { game_service_set_radar(true); s_find_status[0] = 0; s_find_waiting = false; go(PAGE_FIND, 0); }
        } else if (!game->paired) { set_message("请长按B3先配对", false); render(); }
        else if (!game_service_heap_allows_rps()) { set_message("内存不足，对战不可用", true); render(); }
        else if (s_selected == 1) { game_service_invite_rps(); go(PAGE_RPS, 0); }
        else if (s_selected == 2) { buzzer_game_service_invite(); go(PAGE_BUZZER, 0); }
        else {
            if (mole->is_host) mole_game_service_begin_session();
            go(PAGE_MOLE, 0);
        }
    } else if (s_page == PAGE_FIND && key == BSP_BTN_OK) {
        /* Both transports fire together: MQTT when online (so HA sees it) plus an
         * unconditional ESP-NOW copy, which is the only path when we are away from home. */
        find_channels_t used = find_service_ring();
        if (used.mqtt || used.espnow) {
            snprintf(s_find_status, sizeof(s_find_status), "已响铃 (%s)\n等待" KP_PEER_LABEL "回应...",
                     find_service_channel_label(used));
            s_find_waiting = true; s_find_deadline = esp_timer_get_time() / 1000 + 30000;
            sound_service_play(SOUND_TICK);
        } else {
            strlcpy(s_find_status, "两条通道都不可用\n请先配对或联网", sizeof(s_find_status));
            sound_service_play(SOUND_DU);
        }
        render();
    } else if (s_page == PAGE_RPS) {
        if (game->state == GAME_STATE_INVITE_RECEIVED) {
            if (key == BSP_BTN_OK) game_service_respond_invite(true);
            else if (key == BSP_BTN_DOWN) { game_service_respond_invite(false); go(PAGE_GAMES, 1); }
        } else if (game->state == GAME_STATE_WAITING_CHOICE) {
            if (key == BSP_BTN_OK) game_service_choose(game->cursor_choice);
            else game_service_move_choice(key == BSP_BTN_UP ? -1 : 1);
        }
        else if (game->state == GAME_STATE_RESULT && key == BSP_BTN_OK) game_service_invite_rps();
        else if (game->state == GAME_STATE_IDLE && key == BSP_BTN_OK) go(PAGE_GAMES, 1);
    } else if (s_page == PAGE_BUZZER) {
        if (buzzer->state == BUZZER_STATE_INVITE_RECEIVED) {
            if (key == BSP_BTN_OK) buzzer_game_service_respond_invite(true);
            else if (key == BSP_BTN_DOWN) { buzzer_game_service_respond_invite(false); go(PAGE_GAMES, 2); }
        } else if (buzzer->state == BUZZER_STATE_RESULT && key == BSP_BTN_OK) {
            buzzer_game_service_invite();
        } else if (buzzer->state == BUZZER_STATE_IDLE && key == BSP_BTN_OK) {
            go(PAGE_GAMES, 2);
        }
    } else if (s_page == PAGE_MOLE) {
        if (mole->phase == MOLE_PHASE_PLAYING) {
            if (mole->is_host) {
                if (key == BSP_BTN_UP) mole_game_service_host_input(MOLE_INPUT_UP);
                else if (key == BSP_BTN_DOWN) mole_game_service_host_input(MOLE_INPUT_DOWN);
                else mole_game_service_host_input(MOLE_INPUT_SHOOT);
            } else {
                if (key == BSP_BTN_UP) mole_game_service_client_input(MOLE_INPUT_LEFT);
                else if (key == BSP_BTN_DOWN) mole_game_service_client_input(MOLE_INPUT_RIGHT);
                else mole_game_service_client_input(MOLE_INPUT_RELOAD);
            }
        } else if ((mole->phase == MOLE_PHASE_IDLE || mole->phase == MOLE_PHASE_RESULT) &&
                   mole->is_host && key == BSP_BTN_OK) {
            mole_game_service_begin_session();
        }
    }
}
#if CONFIG_ENABLE_SCREENSHOT
static void show_debug_page(app_debug_page_t target)
{
    static const page_t pages[APP_DEBUG_PAGE_COUNT] = {
        PAGE_HOME, PAGE_TASKS, PAGE_REDEEM, PAGE_LOTTERY, PAGE_GAMES, PAGE_FIND, PAGE_RPS,
        PAGE_BUZZER, PAGE_BUZZER, PAGE_BUZZER, PAGE_BUZZER,
        PAGE_LOTTERY, PAGE_LOTTERY,
    };
    if (target < 0 || target >= APP_DEBUG_PAGE_COUNT) return;
    s_debug_preview = true;
    s_debug_lottery = target == APP_DEBUG_PAGE_LOTTERY_SPIN ? DEBUG_LOTTERY_SPIN
                    : target == APP_DEBUG_PAGE_LOTTERY_RESULT ? DEBUG_LOTTERY_RESULT
                    : DEBUG_LOTTERY_IDLE;
    s_debug_buzzer = target == APP_DEBUG_PAGE_BUZZER_GO ? DEBUG_BUZZER_GO
                   : target == APP_DEBUG_PAGE_BUZZER_RESULT ? DEBUG_BUZZER_RESULT
                   : (target == APP_DEBUG_PAGE_BUZZER || target == APP_DEBUG_PAGE_BUZZER_ARMED)
                         ? DEBUG_BUZZER_ARMED : DEBUG_BUZZER_NONE;
    s_page = pages[target];
    s_selected = 0;
    s_message[0] = 0;
    s_lottery_animating = false;
    s_lottery_rotation = s_debug_lottery == DEBUG_LOTTERY_SPIN ? 1150 : 0;
    s_find_waiting = false;
    s_find_deadline = 0;
    /* Leave the find status empty so the preview renders the live dual-stack
     * availability line instead of a synthetic placeholder. */
    s_find_status[0] = 0;
    ESP_LOGI(TAG, "debug页面=%d", target);
    render();
    if (s_debug_lottery == DEBUG_LOTTERY_RESULT) {
        s_debug_prize_index = (s_debug_prize_index + 1) % LOTTERY_PRIZE_COUNT;
    }
}
#endif
static void process_event(const app_event_t *event)
{
    if (event->type == APP_EVT_KEY) {
        ESP_LOGI(TAG, "按键=%d event=%d", event->button, event->button_event);
        bool b3_pair_on_release = false;
        if (event->button == BSP_BTN_OK && event->button_event == BSP_BTN_PRESS) {
            s_b3_press_started = event->timestamp_ms;
            s_b3_privacy_consumed = false;
            if (s_page == PAGE_MOLE) s_mole_b3_block_click = false;
        } else if (event->button == BSP_BTN_OK && event->button_event == BSP_BTN_HOLD_5S) {
            privacy_mode_toggle();
            s_b3_privacy_consumed = true;
            if (s_page == PAGE_MOLE) s_mole_b3_block_click = true;
            set_message(privacy_mode_is_active() ? "隐私模式已开启" : "隐私模式已关闭", false);
            sound_service_play(SOUND_DING);
            render();
            return;
        } else if (event->button == BSP_BTN_OK && event->button_event == BSP_BTN_RELEASE) {
            b3_pair_on_release = s_b3_press_started > 0 && !s_b3_privacy_consumed &&
                                 event->timestamp_ms - s_b3_press_started >= 1000;
            s_b3_press_started = 0;
            s_b3_privacy_consumed = false;
        }
        if (s_find_ringing) {
            sound_service_stop_ring();
            find_service_ack(s_find_sender);
            s_find_ringing = false;
            s_find_ring_deadline = 0;
            s_find_sender[0] = 0;
            s_ignore_key_until_release = event->button_event == BSP_BTN_PRESS;
            s_ignore_ring_click = event->button_event == BSP_BTN_PRESS;
            set_find_overlay_visible(false);
            set_message("已告诉" KP_PEER_LABEL "：我在这里", false);
            return;
        }
        if (s_ignore_key_until_release) {
            if (event->button_event == BSP_BTN_LONG) s_ignore_ring_click = false;
            if (event->button_event == BSP_BTN_RELEASE) s_ignore_key_until_release = false;
            return;
        }
        if (s_ignore_ring_click) {
            if (event->button_event == BSP_BTN_CLICK) { s_ignore_ring_click = false; return; }
            if (event->button_event == BSP_BTN_PRESS) s_ignore_ring_click = false;
        }
        if (event->button_event == BSP_BTN_RELEASE && event->button == BSP_BTN_DOWN && ptt_service_is_transmitting()) {
            ptt_service_set_transmitting(false); render(); return;
        }
        if (power_service_key_activity()) { s_suppress_wake_key = true; return; }
        if (s_suppress_wake_key) {
            if (event->button_event == BSP_BTN_CLICK || event->button_event == BSP_BTN_LONG || event->button_event == BSP_BTN_RELEASE) s_suppress_wake_key = false;
            return;
        }
        /* B3 PRESS carries the ISR-adjacent monotonic timestamp. Consume the later CLICK so
         * queue latency and the button library's click recognition cannot affect arbitration. */
        if (s_page == PAGE_BUZZER && event->button == BSP_BTN_OK && event->button_event == BSP_BTN_PRESS) {
            buzzer_game_snapshot_t *buzzer = buzzer_snapshot();
            if (buzzer->state == BUZZER_STATE_ARMED || buzzer->state == BUZZER_STATE_GO) {
                buzzer_game_service_press(event->timestamp_ms);
                s_buzzer_press_consumed = true;
                return;
            }
        }
        if (s_page == PAGE_BUZZER && event->button == BSP_BTN_OK &&
            event->button_event == BSP_BTN_CLICK && s_buzzer_press_consumed) {
            s_buzzer_press_consumed = false;
            return;
        }
        if (event->button_event == BSP_BTN_LONG && event->button == BSP_BTN_DOWN && s_page == PAGE_FIND) {
            if (ptt_service_available()) ptt_service_set_transmitting(true);
            else set_message("对讲不可用\n需配对和70KB内存", true);
            render(); return;
        }
        /* The 1-second B3 long event is deferred until release so a 5-second hold can
         * toggle privacy without also starting pairing. */
        if (event->button_event == BSP_BTN_LONG && event->button == BSP_BTN_OK) return;
        if (s_page == PAGE_MOLE && event->button == BSP_BTN_OK &&
            event->button_event == BSP_BTN_CLICK && s_mole_b3_block_click) {
            s_mole_b3_block_click = false;
            return;
        }
        if (b3_pair_on_release && (s_page == PAGE_HOME || s_page == PAGE_GAMES)) {
            if (game_service_heap_allows_pairing()) { game_service_start_pairing(); go(PAGE_GAMES, 0); }
            else { set_message("内存不足，无法配对", true); render(); }
            return;
        }
        if (event->button == BSP_BTN_UP && event->button_event == BSP_BTN_LONG) {
            app_model_snapshot_t *model = model_snapshot();
            if (model->pending_type == APP_PENDING_NONE) {
                game_service_cancel();
                buzzer_game_service_cancel();
                mole_game_service_cancel();
                go(PAGE_HOME, 0);
            }
            return;
        }
        if (event->button_event == BSP_BTN_CLICK) handle_short_key(event->button);
#if CONFIG_ENABLE_SCREENSHOT
    } else if (event->type == APP_EVT_DEBUG_PAGE) {
        show_debug_page((app_debug_page_t)event->value);
#endif
    } else if (event->type == APP_EVT_FIND_RING) {
        s_ignore_key_until_release = false;
        s_ignore_ring_click = false;
        strlcpy(s_find_sender, event->text, sizeof(s_find_sender));
        s_find_ringing = true; s_find_flash = true;
        s_find_ring_deadline = esp_timer_get_time() / 1000 + 30000;
        s_find_next_flash = esp_timer_get_time() / 1000 + 300;
        ptt_service_set_transmitting(false);
        power_service_wake(); sound_service_play(SOUND_FIND_RING); show_find_ring_overlay();
    } else if (event->type == APP_EVT_FIND_ACK) {
        s_find_waiting = false; s_find_deadline = 0;
        strlcpy(s_find_status, KP_PEER_LABEL "已回应：找到了！", sizeof(s_find_status));
        sound_service_play(SOUND_DING); render();
    } else if (event->type == APP_EVT_MODEL_CHANGED) { if (event->value) ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model()); render(); }
    else if (event->type == APP_EVT_ACTION_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model()); app_model_snapshot_t *model = model_snapshot();
        if (event->ok && model->pending_type == APP_PENDING_LOTTERY) set_message("兑换成功\n正在等待开奖...", false); else set_message(event->text, !event->ok);
        sound_service_play(event->ok ? SOUND_DING : SOUND_DU); render();
    } else if (event->type == APP_EVT_LOTTERY_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        app_model_snapshot_t *model = model_snapshot();
        s_page = PAGE_LOTTERY;
        s_lottery_animating = true;
        s_lottery_rotation = 0;
        s_lottery_reveal_at = esp_timer_get_time() / 1000 + 2700;
        sound_service_play(SOUND_TICK);
        render();
        if (bsp_lvgl_lock(100)) {
            int sector = lottery_sector_for_reward(model->lottery_prize_id);
            ESP_LOGI(TAG, "抽奖动画 prize=%s sector=%d known=%d", model->lottery_prize_id, sector,
                     lottery_asset_index_for_reward(model->lottery_prize_id) >= 0);
            ui_lottery_start_spin(sector);
            bsp_lvgl_unlock();
        }
    } else if (event->type == APP_EVT_GAME_UPDATE) {
#if CONFIG_ENABLE_SCREENSHOT
        /* Keep a serial PAGE preview deterministic if a stale/live radio event arrives. */
        if (s_debug_preview) { render(); return; }
#endif
        game_snapshot_t *game = game_snapshot();
        buzzer_game_snapshot_t *buzzer = buzzer_snapshot();
        mole_game_snapshot_t *mole = mole_snapshot();
        if (mole->phase == MOLE_PHASE_COUNTDOWN && s_page != PAGE_MOLE) {
            power_service_wake();
            sound_service_play(SOUND_DING);
            go(PAGE_MOLE, 0);
        } else if (buzzer->state == BUZZER_STATE_INVITE_RECEIVED) {
            power_service_wake();
            sound_service_play(SOUND_DING);
            go(PAGE_BUZZER, 0);
        } else if (game->state == GAME_STATE_INVITE_RECEIVED) {
            power_service_wake();
            sound_service_play(SOUND_DING);
            /* Entering the page is not enough: the current LVGL tree still belongs
             * to the previous page until it is explicitly rebuilt. */
            go(PAGE_RPS, 0);
        } else if (s_page == PAGE_RPS && game->state == GAME_STATE_IDLE &&
                   (strcmp(game->status, "对方退出了游戏") == 0 || strcmp(game->status, "邀请已超时") == 0))
            go(PAGE_HOME, 0);
        else if (s_page == PAGE_BUZZER && buzzer->state == BUZZER_STATE_IDLE &&
                 strcmp(buzzer->status, "对方退出了游戏") == 0)
            go(PAGE_HOME, 0);
        else render();
    } else if (event->type == APP_EVT_DATA_ERROR || event->type == APP_EVT_ACTION_TIMEOUT) {
        set_message(event->text[0] ? event->text : "请求超时，请重试", true); sound_service_play(SOUND_DU); render();
    } else render();
}
static void ui_task(void *arg)
{
    (void)arg; ui_app_init(); render(); ESP_LOGI(TAG, "UI启动 stack high-water=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    app_event_t event; int64_t next_game_tick = 0;
    for (;;) {
        if (xQueueReceive(app_events_queue(), &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            process_event(&event);
        }
        int64_t now = esp_timer_get_time() / 1000;
        app_model_snapshot_t *model = model_snapshot();
        if (model->pending_type != APP_PENDING_NONE && now >= model->pending_deadline_ms) {
            ESP_LOGW(TAG, "请求超时 key=%s", model->pending_key);
            app_model_finish_pending();
            app_event_t timeout = {.type = APP_EVT_ACTION_TIMEOUT};
            strlcpy(timeout.text, "请求超时，请重试", sizeof(timeout.text));
            process_event(&timeout);
        }
        if (now >= next_game_tick) {
#if CONFIG_ENABLE_SCREENSHOT
            /* Debug PAGE previews are pure UI snapshots: do not advance a live game or
             * originate any retry/timeout ESP-NOW traffic while preview mode is active. */
            if (!s_debug_preview) {
                game_service_tick(now);
                buzzer_game_service_tick(now);
                mole_game_service_tick(now);
            }
#else
            game_service_tick(now);
            buzzer_game_service_tick(now);
            mole_game_service_tick(now);
#endif
            next_game_tick = now + 50;
            if (s_page == PAGE_MOLE && s_mole_page_created) {
                mole_game_snapshot_t *mole = mole_snapshot();
                if (bsp_lvgl_lock(100)) {
                    ui_mole_update(mole);
                    bsp_lvgl_unlock();
                }
            }
        }
        find_service_tick(now);
        if (s_find_waiting && now >= s_find_deadline) {
            s_find_waiting = false; s_find_deadline = 0;
            strlcpy(s_find_status, "30秒未回应\n可再次响铃", sizeof(s_find_status));
            render();
        }
        if (s_find_ringing && now >= s_find_ring_deadline) {
            sound_service_stop_ring();
            s_find_ringing = false;
            s_find_ring_deadline = 0;
            s_find_flash = false;
            s_find_sender[0] = 0;
            s_find_status[0] = 0;
            set_find_overlay_visible(false);
            ESP_LOGI(TAG, "Find 被叫响铃已在30秒后自动停止，不发送ACK");
            render();
        }
        /* Re-tint the existing overlay instead of calling render(): a full rebuild
         * re-rasterises every CJK glyph on the page, which on this chip costs more
         * than the flash interval and starves the idle task into a WDT reset. */
        if (s_find_ringing && now >= s_find_next_flash) {
            s_find_flash = !s_find_flash; s_find_next_flash = now + 500;
            if (s_find_overlay && bsp_lvgl_lock(100)) {
                ui_common_find_overlay_tint(s_find_overlay, s_find_flash);
                bsp_lvgl_unlock();
            }
        }
        if (s_lottery_animating && now >= s_lottery_reveal_at) {
            s_lottery_animating = false;
            sound_service_play(SOUND_FANFARE);
            render();
        }
        if (s_message[0] && now >= s_message_until) { s_message[0] = 0; render(); }
    }
}
esp_err_t ui_app_start(void) { return xTaskCreatePinnedToCore(ui_task, "kp_ui", 8192, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM; }
void ui_app_post_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    app_event_post(&(app_event_t){
        .type = APP_EVT_KEY,
        .button = btn,
        .button_event = event,
        .timestamp_ms = esp_timer_get_time() / 1000,
    }, 0);
}
