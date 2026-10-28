#include "ui_app.h"
#include "app_events.h"
#include "app_model.h"
#include "bsp_display.h"
#include "evidence_service.h"
#include "ledger_service.h"
#include "lottery_assets.h"
#include "mqtt_service.h"
#include "notification_service.h"
#include "nvs_cache.h"
#include "power_service.h"
#include "privacy_mode.h"
#include "sound_service.h"
#include "ui_common.h"
#include "ui_confirm.h"
#include "ui_evidence.h"
#include "ui_home.h"
#include "ui_ledger.h"
#include "ui_lottery.h"
#include "ui_notification.h"
#include "ui_redeem.h"
#include "ui_tasks.h"
#include "ui_text.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>

typedef enum { HOME, TASKS, REDEEM, CONFIRM, EVIDENCE, LOTTERY, LEDGER } page_t;
static page_t s_page, s_return;
static int s_selected, s_points;
static confirm_kind_t s_kind;
static char s_item_id[APP_ID_LEN], s_item_name[APP_NAME_LEN];
static char s_message[APP_MESSAGE_LEN];
static bool s_error, s_then_tasks, s_spinning, s_capture_key, s_notification_visible;
static int64_t s_message_until, s_reveal_at;
static lv_obj_t *s_overlay;
/* Bounded snapshots avoid consuming the worker stack on every render. */
static app_model_snapshot_t s_model;
static evidence_service_snapshot_t s_evidence;
static ledger_snapshot_t s_ledger;
static notification_item_t s_notification;

static void snapshot(void)
{
    app_model_snapshot(&s_model);
    evidence_service_snapshot(&s_evidence);
    ledger_service_snapshot(&s_ledger);
}

static void message(const char *text, bool error)
{
    notification_utf8_copy(s_message, sizeof(s_message), text);
    s_error = error;
    s_then_tasks = false;
    s_message_until = esp_timer_get_time() / 1000 + 2400;
}

static void render(void)
{
    snapshot();
    if (!bsp_lvgl_lock(1000)) return;
    lv_obj_t *screen = NULL;
    switch (s_page) {
    case HOME: screen = ui_home_build(&s_model, s_selected); break;
    case TASKS: screen = ui_tasks_build(&s_model, s_selected); break;
    case REDEEM: screen = ui_redeem_build(&s_model, s_selected); break;
    case CONFIRM: screen = ui_confirm_build(&s_model, s_kind, s_item_name, s_points, s_selected); break;
    case EVIDENCE:
        if (!s_evidence.task_name[0])
            notification_utf8_copy(s_evidence.task_name, sizeof(s_evidence.task_name), s_item_name);
        screen = ui_evidence_build(&s_model, &s_evidence);
        break;
    case LOTTERY: screen = ui_lottery_build(&s_model, 0, s_spinning); break;
    case LEDGER: screen = ui_ledger_build(&s_model, &s_ledger); break;
    }
    if (s_message[0]) ui_common_message(screen, s_message, s_error);
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    bsp_lvgl_unlock();
}

static void go(page_t page, int selected)
{
    s_page = page;
    s_selected = selected;
    if (page == LEDGER) ledger_service_request(0);
    render();
}

static void move(int delta, size_t count)
{
    if (!count) return;
    s_selected = (s_selected + delta + (int)count) % (int)count;
    sound_service_play(SOUND_TICK);
    render();
}

static void confirm(confirm_kind_t kind, page_t back, const char *id, const char *name, int points)
{
    s_kind = kind;
    s_return = back;
    strlcpy(s_item_id, id, sizeof(s_item_id));
    notification_utf8_copy(s_item_name, sizeof(s_item_name), name);
    s_points = points;
    go(CONFIRM, 0);
}

static void short_key(bsp_btn_t key)
{
    snapshot();
    if (s_model.pending_type != APP_PENDING_NONE) return;
    int delta = key == BSP_BTN_UP ? -1 : 1;
    if (s_page == HOME) {
        if (key != BSP_BTN_OK) move(delta, 3);
        else go(s_selected == 0 ? TASKS : s_selected == 1 ? REDEEM : LEDGER, 0);
    } else if (s_page == LEDGER) {
        if (key == BSP_BTN_UP) ledger_service_request(0);
        else if (key == BSP_BTN_DOWN && s_ledger.page.has_more)
            ledger_service_request(s_ledger.page.next_cursor);
        else if (key == BSP_BTN_OK) ledger_service_request(s_ledger.page.before_id);
        render();
    } else if (s_page == TASKS) {
        if (key != BSP_BTN_OK) { move(delta, s_model.task_count); return; }
        if (!s_model.task_count || s_selected >= s_model.task_count) return;
        const app_task_t *task = &s_model.tasks[s_selected];
        if (!s_model.mqtt_online) message("当前离线，请联网再试", true);
        else if (task->completed_today) message("这个任务已经完成啦", false);
        else if (task->evidence_required) {
            strlcpy(s_item_id, task->id, sizeof(s_item_id));
            notification_utf8_copy(s_item_name, sizeof(s_item_name), task->name);
            evidence_service_finish();
            go(EVIDENCE, 0);
            return;
        } else if (!task->self_complete) message("这个任务不在这里完成", false);
        else { confirm(CONFIRM_TASK, TASKS, task->id, task->name, task->points); return; }
        render();
    } else if (s_page == REDEEM) {
        if (key != BSP_BTN_OK) { move(delta, 2); return; }
        int idx = ui_redeem_model_index(&s_model, s_selected);
        if (!s_model.mqtt_online) message("当前离线，请联网再试", true);
        else if (idx < 0 || !s_model.rewards[idx].enabled) message("奖品暂未开放", true);
        else if (!s_model.balance_valid || s_model.balance < s_model.rewards[idx].price)
            message("积分不够，继续努力哦", true);
        else {
            confirm(CONFIRM_REDEEM, REDEEM, s_model.rewards[idx].id,
                    s_model.rewards[idx].name, s_model.rewards[idx].price);
            return;
        }
        render();
    } else if (s_page == CONFIRM) {
        if (key != BSP_BTN_OK) move(delta, 2);
        else if (!s_selected) go(s_return, 0);
        else {
            bool sent = s_kind == CONFIRM_TASK ? mqtt_service_publish_complete_task(s_item_id)
                                              : mqtt_service_publish_redeem(s_item_id);
            message(sent ? "正在处理中，请稍候..." : "无法提交，请联网重试", !sent);
            if (sent) go(s_return, 0); else render();
        }
    } else if (s_page == EVIDENCE && key == BSP_BTN_UP && !s_evidence.active) go(TASKS, 0);
    else if (s_page == LOTTERY && !s_spinning && key == BSP_BTN_OK) go(REDEEM, 1);
}

static void notifications(bool show)
{
    size_t position, count;
    bool available = show && notification_service_current(&s_notification, &position, &count);
    if (!s_overlay || !bsp_lvgl_lock(200)) return;
    s_notification_visible = available;
    if (available) ui_notification_show(s_overlay, &s_notification, position, count);
    else ui_notification_hide(s_overlay);
    bsp_lvgl_unlock();
}

static void key(const app_event_t *event)
{
    /* Wake/idle bookkeeping precedes overlays so reading a notification is activity. */
    bool dimmed = power_service_key_activity();
    if (event->button_event == BSP_BTN_PRESS) s_capture_key = dimmed;
    if (s_capture_key) return; /* Consume the whole press/release/click sequence. */
    if (s_notification_visible) {
        if (event->button_event != BSP_BTN_PRESS) return;
        if (event->button == BSP_BTN_UP) {
            if (mqtt_service_publish_notification_read(s_notification.id))
                notification_service_mark_read(s_notification.id);
            notifications(false);
        } else if (event->button == BSP_BTN_DOWN) {
            notification_service_next();
            notifications(true);
        } else notifications(false);
        s_capture_key = true;
        return;
    }
    if (event->button == BSP_BTN_DOWN && event->button_event == BSP_BTN_RELEASE && s_page == EVIDENCE) {
        evidence_service_stop();
        return;
    }
    if (event->button == BSP_BTN_DOWN && event->button_event == BSP_BTN_LONG && s_page == EVIDENCE) {
        if (!evidence_service_begin(s_item_id, s_item_name)) {
            message("当前无法开始录音", true);
            render();
        }
        return;
    }
    if (event->button == BSP_BTN_OK && event->button_event == BSP_BTN_HOLD_5S && s_page == HOME) {
        privacy_mode_toggle();
        message(privacy_mode_is_active() ? "隐私模式已开启" : "隐私模式已关闭", false);
        render();
        s_capture_key = true;
        return;
    }
    if (event->button == BSP_BTN_UP && event->button_event == BSP_BTN_LONG) {
        snapshot();
        if (!s_evidence.active && s_model.pending_type == APP_PENDING_NONE) go(HOME, 0);
        return;
    }
    if (event->button_event == BSP_BTN_CLICK) short_key(event->button);
}

static void process(const app_event_t *event)
{
    if (event->type == APP_EVT_KEY) { key(event); return; }
    if (event->type == APP_EVT_LEDGER_CHANGED) { if (s_page == LEDGER) render(); return; }
    if (event->type == APP_EVT_NOTIFICATION_SNAPSHOT) {
        if (s_page == EVIDENCE) evidence_service_stop();
        notifications(true);
        if (s_notification_visible) { power_service_wake(); sound_service_play(SOUND_DING); }
        return;
    }
    if (event->type == APP_EVT_MODEL_CHANGED) {
        if (event->value) ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        snapshot();
        if (s_page == TASKS && s_selected >= s_model.task_count) s_selected = 0;
    } else if (event->type == APP_EVT_ACTION_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        if (event->value == APP_PENDING_EVIDENCE) {
            evidence_service_finish();
            message(event->ok ? "证据提交成功" : "审核失败", !event->ok);
            s_then_tasks = event->ok;
        } else message(event->text, !event->ok);
        sound_service_play(event->ok ? SOUND_DING : SOUND_DU);
    } else if (event->type == APP_EVT_LOTTERY_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        s_message[0] = 0;
        s_page = LOTTERY;
        s_spinning = true;
        s_reveal_at = esp_timer_get_time() / 1000 + 2700;
        render();
        if (bsp_lvgl_lock(200)) {
            ui_lottery_start_spin(lottery_sector_for_reward(s_model.lottery_prize_id));
            bsp_lvgl_unlock();
        }
        return;
    } else if (event->type == APP_EVT_STATUS_UPDATE) {
        if (!event->text[0]) return; /* Battery status is drawn by the status timer. */
        message(event->text, !event->ok);
    } else if (event->type == APP_EVT_DATA_ERROR || event->type == APP_EVT_ACTION_TIMEOUT) {
        /* Do not reset a recording worker's state while it still owns PCM. */
        if (s_evidence.waiting_review) evidence_service_finish();
        message(event->text[0] ? event->text : "请求超时，请重试", true);
        sound_service_play(SOUND_DU);
#if CONFIG_ENABLE_SCREENSHOT
    } else if (event->type == APP_EVT_DEBUG_PAGE) {
        switch (event->value) {
        case APP_DEBUG_PAGE_HOME: go(HOME, 0); break;
        case APP_DEBUG_PAGE_TASKS: go(TASKS, 0); break;
        case APP_DEBUG_PAGE_REDEEM: go(REDEEM, 0); break;
        case APP_DEBUG_PAGE_LOTTERY: go(LOTTERY, 0); break;
        default: go(LEDGER, 0); break;
        }
        return;
#endif
    }
    render();
}

static void ui_task(void *arg)
{
    (void)arg;
    if (bsp_lvgl_lock(1000)) {
        s_overlay = ui_notification_create(lv_layer_top());
        bsp_lvgl_unlock();
    }
    render();
    uint32_t last_second = UINT32_MAX;
    for (;;) {
        app_event_t event;
        if (xQueueReceive(app_events_queue(), &event, pdMS_TO_TICKS(100))) process(&event);
        int64_t now = esp_timer_get_time() / 1000;
        snapshot();
        ledger_service_tick(now);
        if (s_model.pending_type != APP_PENDING_NONE && now >= s_model.pending_deadline_ms) {
            app_model_finish_pending();
            process(&(app_event_t){.type = APP_EVT_ACTION_TIMEOUT});
        }
        if (s_page == EVIDENCE && s_evidence.recording && s_evidence.elapsed_ms / 1000 != last_second) {
            last_second = s_evidence.elapsed_ms / 1000;
            render();
        }
        if (s_spinning && now >= s_reveal_at) {
            s_spinning = false;
            sound_service_play(SOUND_FANFARE);
            render();
        }
        if (s_message[0] && now >= s_message_until) {
            s_message[0] = 0;
            if (s_then_tasks) { s_then_tasks = false; go(TASKS, 0); }
            else render();
        }
        power_service_poll(s_evidence.active || s_model.pending_type != APP_PENDING_NONE ||
                           s_spinning || s_ledger.loading);
    }
}
esp_err_t ui_app_start(void)
{
    return xTaskCreatePinnedToCore(ui_task, "kp_ui", 8192, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void ui_app_post_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    app_event_post(&(app_event_t){.type = APP_EVT_KEY, .button = btn, .button_event = event,
        .timestamp_ms = esp_timer_get_time() / 1000}, 0);
}
