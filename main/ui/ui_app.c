#include "ui_app.h"
#include "app_events.h"
#include "app_model.h"
#include "mqtt_service.h"
#include "nvs_cache.h"
#include "power_service.h"
#include "sound_service.h"
#include "ui_common.h"
#include "ui_confirm.h"
#include "ui_home.h"
#include "ui_lottery.h"
#include "ui_redeem.h"
#include "ui_tasks.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

typedef enum { PAGE_HOME, PAGE_TASKS, PAGE_CONFIRM, PAGE_REDEEM, PAGE_LOTTERY } page_t;
static const char *TAG = "kp_ui";
static page_t s_page = PAGE_HOME, s_confirm_return = PAGE_HOME;
static int s_selected;
static confirm_kind_t s_confirm_kind;
static char s_confirm_id[APP_ID_LEN], s_confirm_name[APP_NAME_LEN];
static int s_confirm_points;
static char s_message[128];
static bool s_message_error;
static int64_t s_message_until;
static int s_lottery_highlight;
static bool s_suppress_wake_key;
/* UI task owns this reusable snapshot; keeping the ~2 KB model off its stack is critical. */
static app_model_snapshot_t s_ui_model;

static app_model_snapshot_t *model_snapshot(void)
{
    app_model_snapshot(&s_ui_model);
    return &s_ui_model;
}

static void set_message(const char *text, bool error)
{
    strlcpy(s_message, text ? text : "", sizeof(s_message)); s_message_error = error; s_message_until = esp_timer_get_time() / 1000 + 2200;
}
static void render(void)
{
    app_model_snapshot_t *model = model_snapshot();
    if (!bsp_lvgl_lock(1000)) return;
    lv_obj_t *screen = NULL;
    if (s_page == PAGE_HOME) screen = ui_home_build(model, s_selected);
    else if (s_page == PAGE_TASKS) screen = ui_tasks_build(model, s_selected);
    else if (s_page == PAGE_REDEEM) screen = ui_redeem_build(model, s_selected);
    else if (s_page == PAGE_CONFIRM) screen = ui_confirm_build(model, s_confirm_kind, s_confirm_name, s_confirm_points, s_selected);
    else screen = ui_lottery_build(model, s_lottery_highlight);
    if (s_message[0] && esp_timer_get_time() / 1000 < s_message_until) ui_common_message(screen, s_message, s_message_error);
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    bsp_lvgl_unlock();
}
static void go(page_t page, int selected)
{
    s_page = page; s_selected = selected; ESP_LOGI(TAG, "页面=%d 选中=%d", page, selected); render();
}
static void key_move(int delta, int count)
{
    if (count <= 0) return;
    s_selected = (s_selected + delta + count) % count;
    sound_service_play(SOUND_TICK);
    render();
}
static void begin_confirm(confirm_kind_t kind, page_t back, const char *id, const char *name, int points)
{
    s_confirm_kind = kind; s_confirm_return = back; strlcpy(s_confirm_id, id, sizeof(s_confirm_id)); strlcpy(s_confirm_name, name, sizeof(s_confirm_name)); s_confirm_points = points; go(PAGE_CONFIRM, 0);
}
static void handle_short_key(bsp_btn_t key)
{
    app_model_snapshot_t *model = model_snapshot();
    if (model->pending_type != APP_PENDING_NONE && s_page != PAGE_LOTTERY) return;
    int delta = key == BSP_BTN_UP ? -1 : 1;
    if (s_page == PAGE_HOME) {
        if (key != BSP_BTN_OK) key_move(delta, 3);
        else if (s_selected == 0) go(PAGE_TASKS, 0);
        else if (s_selected == 1) go(PAGE_REDEEM, 0);
        else { mqtt_service_resubscribe(); set_message(model->mqtt_online ? "正在同步最新数据" : "当前离线，显示缓存数据", !model->mqtt_online); render(); }
    } else if (s_page == PAGE_TASKS) {
        if (key != BSP_BTN_OK) key_move(delta, model->task_count);
        else if (model->task_count == 0) { set_message("今天还没有任务", false); render(); }
        else { const app_task_t *t = &model->tasks[s_selected]; if (!model->mqtt_online) set_message("当前离线，请联网后再试", true); else if (t->completed_today) set_message("这个任务已经完成啦", false); else if (!t->self_complete) set_message("这个任务需要爸爸妈妈确认", false); else { begin_confirm(CONFIRM_TASK, PAGE_TASKS, t->id, t->name, t->points); return; } render(); }
    } else if (s_page == PAGE_REDEEM) {
        if (key != BSP_BTN_OK) key_move(delta, 2);
        else { int idx = ui_redeem_model_index(model, s_selected); if (!model->mqtt_online) set_message("当前离线，请连接家里的 Wi-Fi 再试", true); else if (idx < 0 || !model->rewards[idx].enabled) set_message("奖品暂未开放", true); else if (!model->balance_valid || model->balance < model->rewards[idx].price) set_message("积分不够，继续努力哦", true); else { begin_confirm(CONFIRM_REDEEM, PAGE_REDEEM, model->rewards[idx].id, model->rewards[idx].name, model->rewards[idx].price); return; } render(); }
    } else if (s_page == PAGE_CONFIRM) {
        if (key != BSP_BTN_OK) key_move(delta, 2);
        else if (s_selected == 0) go(s_confirm_return, 0);
        else { bool sent = s_confirm_kind == CONFIRM_TASK ? mqtt_service_publish_complete_task(s_confirm_id) : mqtt_service_publish_redeem(s_confirm_id); if (sent) { set_message("正在处理中，请稍候...", false); sound_service_play(SOUND_TICK); go(s_confirm_return, 0); } else { set_message("无法提交，请检查网络或稍后重试", true); sound_service_play(SOUND_DU); render(); } }
    } else if (s_page == PAGE_LOTTERY && model->lottery_ready && key == BSP_BTN_OK) go(PAGE_REDEEM, 1);
}
static void process_event(const app_event_t *event)
{
    if (event->type == APP_EVT_KEY) {
        ESP_LOGI(TAG, "按键=%d event=%d", event->button, event->button_event);
        if (power_service_key_activity()) { s_suppress_wake_key = true; return; }
        if (s_suppress_wake_key) { if (event->button_event == BSP_BTN_CLICK || event->button_event == BSP_BTN_LONG) s_suppress_wake_key = false; return; }
        if (event->button == BSP_BTN_UP && event->button_event == BSP_BTN_LONG) { app_model_snapshot_t *model = model_snapshot(); if (model->pending_type == APP_PENDING_NONE) go(PAGE_HOME, 0); return; }
        if (event->button_event == BSP_BTN_CLICK) handle_short_key(event->button);
    } else if (event->type == APP_EVT_MODEL_CHANGED) {
        if (event->value) ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        render();
    } else if (event->type == APP_EVT_ACTION_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        app_model_snapshot_t *model = model_snapshot();
        set_message(event->text, !event->ok);
        if (event->ok) sound_service_play(SOUND_DING); else sound_service_play(SOUND_DU);
        if (event->ok && model->pending_type == APP_PENDING_LOTTERY) go(PAGE_LOTTERY, 0); else render();
    } else if (event->type == APP_EVT_LOTTERY_RESULT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_save_model());
        s_page = PAGE_LOTTERY; sound_service_play(SOUND_FANFARE); render();
    } else if (event->type == APP_EVT_DATA_ERROR || event->type == APP_EVT_ACTION_TIMEOUT) {
        set_message(event->text[0] ? event->text : "请求超时，请重试", true); sound_service_play(SOUND_DU); render();
    } else render();
}
static void ui_task(void *arg)
{
    (void)arg; render(); ESP_LOGI(TAG, "UI启动 stack high-water=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL)); app_event_t event; int64_t next_anim = 0;
    for (;;) {
        if (xQueueReceive(app_events_queue(), &event, pdMS_TO_TICKS(100)) == pdTRUE) process_event(&event);
        int64_t now = esp_timer_get_time() / 1000;
        app_model_snapshot_t *model = model_snapshot();
        if (model->pending_type != APP_PENDING_NONE && now >= model->pending_deadline_ms) { ESP_LOGW(TAG, "请求超时 key=%s", model->pending_key); app_model_finish_pending(); app_event_t timeout = {.type = APP_EVT_ACTION_TIMEOUT}; strlcpy(timeout.text, "请求超时，请重试", sizeof(timeout.text)); process_event(&timeout); }
        if (s_page == PAGE_LOTTERY && !model->lottery_ready && now >= next_anim) { static const int route[8] = {0,1,2,5,8,7,6,3}; static int route_pos; s_lottery_highlight = route[route_pos++ % 8]; next_anim = now + 120; sound_service_play(SOUND_TICK); if (bsp_lvgl_lock(100)) { ui_lottery_set_highlight(s_lottery_highlight); bsp_lvgl_unlock(); } }
        if (s_message[0] && now >= s_message_until) { s_message[0] = 0; render(); }
    }
}
esp_err_t ui_app_start(void)
{
    if (xTaskCreatePinnedToCore(ui_task, "kp_ui", 4096, NULL, 3, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void ui_app_post_key(bsp_btn_t btn, bsp_btn_ev_t event) { app_event_post(&(app_event_t){.type = APP_EVT_KEY, .button = btn, .button_event = event}, 0); }
