#!/usr/bin/env python3
"""联动执行真实花园与电源服务，验证休眠、重启和离线生长结算。"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_WIFI_CONN 0x3007
#define ESP_SLEEP_WAKEUP_ALL 255
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define BSP_BTN_GPIO 0
#define CONFIG_KP_PEER_LABEL "伙伴"
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
typedef void *SemaphoreHandle_t;
typedef unsigned TickType_t;
typedef int nvs_handle_t;
typedef struct stub_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef void *esp_pm_lock_handle_t;
typedef struct { int max_freq_mhz, min_freq_mhz; bool light_sleep_enable; } esp_pm_config_t;
typedef enum { ESP_PM_NO_LIGHT_SLEEP = 2 } esp_pm_lock_type_t;
typedef int gpio_num_t;
typedef enum { GPIO_INTR_LOW_LEVEL = 4 } gpio_int_type_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM } wifi_ps_type_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { APP_EVT_GAME_UPDATE, APP_EVT_STATUS_UPDATE } app_event_type_t;
typedef struct { app_event_type_t type; bool ok; char text[128]; } app_event_t;
const char *esp_err_to_name(esp_err_t);
void test_log(const char *, const char *, ...);
uint32_t esp_random(void);
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
bool app_event_post(const app_event_t *, TickType_t);
esp_err_t esp_pm_configure(const void *);
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t, int, const char *, esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t);
esp_err_t esp_wifi_connect(void);
bool usb_serial_jtag_is_connected(void);
esp_err_t lvgl_port_stop(void);
esp_err_t lvgl_port_resume(void);
esp_err_t esp_sleep_disable_wakeup_source(int);
bool sound_service_prepare_sleep(void);
esp_err_t bsp_battery_sleep(void);
esp_err_t bsp_audio_sleep(void);
esp_err_t bsp_audio_prepare_deep_sleep(void);
esp_err_t bsp_i2c_prepare_deep_sleep(void);
bool bsp_lvgl_lock(int);
void bsp_lvgl_unlock(void);
esp_err_t bsp_display_prepare_deep_sleep(void);
void bsp_display_backlight(uint8_t);
void esp_restart(void);
void esp_deep_sleep_start(void);
void power_test_clear_ram(void);
"""
# 两份业务代码各在自己的编译单元内，测试钩子只模拟 MCU 重启时的 RAM 清空。
POWER_SOURCE = r"""
#include "@POWER_SERVICE@"
void power_test_clear_ram(void) {
    s_timer = NULL; s_lock = NULL; s_last_activity_ms = 0;
    s_level = ACTIVE_BACKLIGHT; s_lvgl_stopped = s_saver = s_usb_connected = false;
    s_awake_lock = NULL; s_awake_lock_held = false;
}
"""
SOURCE = r"""
#include "@GARDEN_SERVICE@"
#include "power_service.h"
#include "app_model.h"
#include "nvs_cache.h"
#include "battery_service.h"
#include "wifi_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#define SOWN_AT (20800LL * 86400)
static int64_t fake_now;
static garden_t persisted;
static bool has_blob;
typedef struct { bool locked; } test_mutex_t;
static test_mutex_t mutexes[4];
static int mutex_count, lock_depth, commits;
static int64_t uptime_ms;
static bool stored_saver, usb_connected, timer_armed, lvgl_stopped;
static int terminal, brightness, pm_held, failure, usb_reads, connect_on_read, saver_writes;
static int timer_token, pm_token, shutdown_calls, radio_sends;
static char shutdown_order[64];
static uint64_t timer_delay_ms;
static void (*timer_callback)(void *);
static jmp_buf sleep_jump;

const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
uint32_t esp_random(void) { return 99; }
int64_t esp_timer_get_time(void) { return uptime_ms * 1000; }
time_t time(time_t *out) { if (out) *out = fake_now; return fake_now; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    assert(mutex_count < 4); return &mutexes[mutex_count++];
}
int xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) {
    (void)t; test_mutex_t *m = h;
    assert(m && !m->locked); m->locked = true; lock_depth++; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t h) {
    test_mutex_t *m = h;
    assert(m && m->locked); m->locked = false; lock_depth--; return pdTRUE;
}
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *h) {
    assert(strcmp(name, "kp_garden") == 0);
    if (mode == NVS_READONLY && !has_blob) return ESP_ERR_NOT_FOUND;
    *h = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *n) {
    assert(h == 1 && strcmp(key, "state_v1") == 0);
    if (!has_blob) return ESP_ERR_NOT_FOUND;
    assert(*n == sizeof(persisted)); memcpy(out, &persisted, *n); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n) {
    assert(h == 1 && strcmp(key, "state_v1") == 0 && n == sizeof(persisted));
    memcpy(&persisted, p, n); has_blob = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1); commits++; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h == 1); }
bool app_event_post(const app_event_t *event, TickType_t wait) {
    (void)event; (void)wait; return true;
}
esp_err_t espnow_service_send(espnow_msg_type_t type, uint16_t session, uint16_t seq,
                             uint8_t choice, int8_t flags, bool broadcast) {
    (void)type; (void)session; (void)seq; (void)choice; (void)flags; (void)broadcast;
    radio_sends++;
    return ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) {
    timer_callback = a->callback; *h = (esp_timer_handle_t)&timer_token; return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) {
    assert(h == (esp_timer_handle_t)&timer_token);
    timer_armed = true; timer_delay_ms = us / 1000; return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
    assert(h == (esp_timer_handle_t)&timer_token);
    bool was = timer_armed; timer_armed = false;
    return was ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t esp_pm_configure(const void *p) {
    assert(((const esp_pm_config_t *)p)->light_sleep_enable); return ESP_OK;
}
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t t, int a, const char *n, esp_pm_lock_handle_t *h) {
    (void)a; (void)n; assert(t == ESP_PM_NO_LIGHT_SLEEP); *h = &pm_token; return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t h) {
    assert(h == &pm_token && pm_held == 0); pm_held++; return ESP_OK;
}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t h) {
    assert(h == &pm_token && pm_held == 1); pm_held--; return ESP_OK;
}
esp_err_t gpio_wakeup_enable(gpio_num_t g, gpio_int_type_t t) {
    assert(g == BSP_BTN_GPIO && t == GPIO_INTR_LOW_LEVEL); return ESP_OK;
}
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t t) { (void)t; return ESP_OK; }
esp_err_t esp_wifi_connect(void) { return ESP_OK; }
bool usb_serial_jtag_is_connected(void) {
    if (++usb_reads == connect_on_read) usb_connected = true;
    return usb_connected;
}
esp_err_t lvgl_port_stop(void) { lvgl_stopped = true; return ESP_OK; }
esp_err_t lvgl_port_resume(void) { lvgl_stopped = false; return ESP_OK; }
void app_model_snapshot(app_model_snapshot_t *out) { memset(out, 0, sizeof(*out)); }
esp_err_t nvs_cache_load_power_saver(bool *enabled) { *enabled = stored_saver; return ESP_OK; }
esp_err_t nvs_cache_save_power_saver(bool enabled) {
    stored_saver = enabled; saver_writes++; return ESP_OK;
}
static void shutdown_step(char c) {
    assert(lock_depth == 0 && shutdown_calls < 63);
    shutdown_order[shutdown_calls++] = c; shutdown_order[shutdown_calls] = 0;
}
esp_err_t esp_sleep_disable_wakeup_source(int sources) {
    assert(sources == ESP_SLEEP_WAKEUP_ALL); shutdown_step('W'); return ESP_OK;
}
esp_err_t battery_service_stop(void) {
    shutdown_step('B'); return failure == 1 ? ESP_ERR_INVALID_STATE : ESP_OK;
}
bool sound_service_prepare_sleep(void) { shutdown_step('A'); return true; }
esp_err_t wifi_service_stop(void) { shutdown_step('N'); return ESP_OK; }
esp_err_t bsp_battery_sleep(void) { shutdown_step('b'); return ESP_OK; }
esp_err_t bsp_audio_sleep(void) { shutdown_step('a'); return ESP_OK; }
esp_err_t bsp_audio_prepare_deep_sleep(void) { shutdown_step('s'); return ESP_OK; }
esp_err_t bsp_i2c_prepare_deep_sleep(void) { shutdown_step('i'); return ESP_OK; }
bool bsp_lvgl_lock(int timeout) {
    if (timeout == 1000) { shutdown_step('L'); return failure != 2; }
    assert(timeout == 500); return true;
}
void bsp_lvgl_unlock(void) {}
esp_err_t bsp_display_prepare_deep_sleep(void) { shutdown_step('d'); return ESP_OK; }
void bsp_display_backlight(uint8_t b) { brightness = b; }
void esp_restart(void) { shutdown_step('R'); terminal = 2; longjmp(sleep_jump, 1); }
void esp_deep_sleep_start(void) { shutdown_step('S'); terminal = 1; longjmp(sleep_jump, 1); }

/* 重启清掉 RAM 状态，保留 NVS；允许独立模拟 RTC 保留或冷启动时间无效。 */
static void reboot(int64_t now) {
    assert(lock_depth == 0);
    power_test_clear_ram(); mutex_count = 0; memset(mutexes, 0, sizeof(mutexes));
    s_lock = NULL; memset(&s_garden, 0, sizeof(s_garden));
    s_clock_lost = false; s_phase = GARDEN_GIFT_IDLE; s_gift_material = 0;
    s_deadline_ms = s_next_tx_ms = 0; s_delivered = false;
    fake_now = now; uptime_ms = 0;
    timer_armed = lvgl_stopped = false;
    terminal = pm_held = failure = shutdown_calls = usb_reads = connect_on_read = radio_sends = 0;
    shutdown_order[0] = 0; timer_callback = NULL;
    assert(garden_service_start() == ESP_OK);
}
static void boot_device(int64_t now) {
    reboot(now); assert(power_service_start() == ESP_OK);
    assert(power_service_saver_enabled() == stored_saver && pm_held == 1 && lock_depth == 0);
}
static void advance_ms(int64_t delta) {
    assert(delta >= 0 && delta % 1000 == 0);
    uptime_ms += delta; if (fake_now) fake_now += delta / 1000;
}
static void poll_power(bool busy) {
    if (!setjmp(sleep_jump)) power_service_poll(busy);
    assert(lock_depth == 0);
}
static void expect_shutdown(void) {
    assert(terminal == 1 && strcmp(shutdown_order, "WBANbasiLdS") == 0);
}
static void clear_shutdown_trace(void) { shutdown_calls = 0; shutdown_order[0] = 0; }
static void fire_power_timer(void) {
    assert(timer_armed && timer_callback);
    timer_armed = false; advance_ms((int64_t)timer_delay_ms); timer_callback(NULL);
    assert(lock_depth == 0);
}
static garden_view_t view(void) {
    garden_view_t v = {0}; garden_service_snapshot(&v); assert(lock_depth == 0); return v;
}
static void saved_plant(int crop, uint8_t draws) {
    garden_init(&persisted, 99); persisted.crop = (uint8_t)crop;
    assert(garden_sow(&persisted, SOWN_AT) == GARDEN_OK);
    persisted.flags = draws; has_blob = true; commits = 0;
}
static void expect_one_day(int crop) {
    /* 按白天水量推导：种子生长 3 小时，剩余水用于幼苗期；无虫草时结果确定。 */
    static const uint32_t progress[] = {720000, 720000, 720000, 432000, 432000, 1008000};
    garden_view_t v = view();
    assert(v.clock_ok && v.day);
    assert(v.garden.planted && v.garden.stage == GARDEN_STAGE_SEEDLING);
    assert(v.garden.progress == progress[crop] && v.garden.water == 0);
    assert(v.garden.settled_at == SOWN_AT + 86400);
    for (int i = 0; i < GARDEN_MATERIALS; i++) assert(v.garden.bag[i] == 1);
    for (int i = 0; i < GARDEN_CROPS; i++) assert(v.garden.album[i] == 0);
}

static void starter_once(void) {
    has_blob = false; commits = 0; reboot(0);
    garden_view_t first = view(); assert(!first.clock_ok && !first.garden.planted);
    persisted.bag[0] = 0; persisted.bag[1] = 7; persisted.album[2] = 1;
    reboot(SOWN_AT);
    garden_view_t v = view();
    assert(v.clock_ok && v.garden.bag[0] == 0 && v.garden.bag[1] == 7);
    assert(v.garden.album[2] == 1 && v.garden.crop == first.garden.crop);
}
static void deep_sleep_day(void) {
    for (int c = 0; c < GARDEN_CROPS; c++) {
        saved_plant(c, 0); reboot(SOWN_AT + 86400); expect_one_day(c);
        garden_view_t before = view(); reboot(SOWN_AT + 86400);
        garden_view_t after = view(); assert(memcmp(&before.garden, &after.garden, sizeof(garden_t)) == 0);
    }
}
static void cold_boot_sync(void) {
    for (int c = 0; c < GARDEN_CROPS; c++) {
        saved_plant(c, 0); garden_t original = persisted; reboot(0);
        for (int i = 0; i < 3; i++) {
            garden_view_t v = view(); assert(!v.clock_ok);
            assert(memcmp(&v.garden, &original, sizeof(garden_t)) == 0);
            assert(memcmp(&persisted, &original, sizeof(garden_t)) == 0 && commits == 0);
        }
        fake_now = SOWN_AT + 86400; expect_one_day(c);
        garden_view_t before = view(), after = view();
        assert(memcmp(&before.garden, &after.garden, sizeof(garden_t)) == 0);
    }
}
static void temporary_clock_loss(void) {
    saved_plant(GARDEN_RADISH, 0); reboot(SOWN_AT);
    fake_now += 3600; garden_view_t before = view();
    assert(before.garden.progress == 360000 && before.garden.water == 604800);
    fake_now = 0; garden_view_t paused = view();
    assert(!paused.clock_ok && memcmp(&before.garden, &paused.garden, sizeof(garden_t)) == 0);
    fake_now = SOWN_AT + 2 * 3600; garden_view_t resumed = view();
    assert(resumed.clock_ok && resumed.garden.progress == 720000 && resumed.garden.water == 453600);
}
static void night_pause(void) {
    saved_plant(GARDEN_RADISH, 0);
    persisted.settled_at = SOWN_AT + 12 * 3600 + 1800; /* 当地 20:30。 */
    reboot(SOWN_AT + 86400 + 1800); /* 次日 08:30，只生长 90 分钟。 */
    garden_view_t v = view();
    assert(v.garden.stage == GARDEN_STAGE_SEED && v.garden.progress == 540000);
    assert(v.garden.water == 529200 && v.day);
}
static void clock_rollback(void) {
    saved_plant(GARDEN_RADISH, 0); reboot(SOWN_AT);
    fake_now = SOWN_AT + 3600; garden_view_t before = view();
    fake_now = SOWN_AT + 3500; garden_view_t jitter = view();
    assert(jitter.clock_ok && memcmp(&before.garden, &jitter.garden, sizeof(garden_t)) == 0);
    fake_now = SOWN_AT + 1000; garden_view_t rollback = view();
    assert(!rollback.clock_ok && memcmp(&before.garden, &rollback.garden, sizeof(garden_t)) == 0);
    fake_now = SOWN_AT + 3600; garden_view_t same = view();
    assert(same.clock_ok && memcmp(&before.garden, &same.garden, sizeof(garden_t)) == 0);
    fake_now += 60; garden_view_t after = view();
    assert(after.garden.progress == before.garden.progress + 6000);
}
static void invalid_actions(void) {
    saved_plant(GARDEN_RADISH, 0); garden_t original = persisted; reboot(0);
    assert(garden_service_water() == GARDEN_ERR_TIME);
    assert(garden_service_use(GARDEN_MAT_FERTILIZER) == GARDEN_ERR_TIME);
    assert(garden_service_sow() == GARDEN_ERR_TIME);
    assert(memcmp(&persisted, &original, sizeof(garden_t)) == 0 && commits == 0);
}
static void dry_refill_after_sync(void) {
    saved_plant(GARDEN_RADISH, 0); reboot(0); fake_now = SOWN_AT + 86400;
    assert(garden_service_water() == GARDEN_OK);
    assert(persisted.stage == GARDEN_STAGE_SEEDLING && persisted.progress == 720000);
    assert(persisted.water == GARDEN_WATER_FULL && commits == 1);
    fake_now += 60; garden_view_t v = view(); assert(v.garden.progress == 726000);
    assert(v.garden.water == GARDEN_WATER_FULL - 2520);
}
static void repeated_restarts(void) {
    saved_plant(GARDEN_RADISH, 0); reboot(0); fake_now = SOWN_AT + 86400;
    garden_view_t first = view(); expect_one_day(GARDEN_RADISH);
    for (int i = 0; i < 3; i++) {
        reboot(0); fake_now = SOWN_AT + 86400; garden_view_t again = view();
        assert(memcmp(&first.garden, &again.garden, sizeof(garden_t)) == 0);
    }
}
static void harvest_once(void) {
    saved_plant(GARDEN_RADISH, 0); reboot(SOWN_AT);
    fake_now += 3 * 3600; garden_view_t v = view(); assert(v.garden.stage == GARDEN_STAGE_SEEDLING);
    assert(garden_service_water() == GARDEN_OK);
    fake_now += 3 * 3600; assert(garden_service_water() == GARDEN_OK);
    fake_now += 3 * 3600; v = view(); assert(v.garden.stage == GARDEN_STAGE_MATURE);
    garden_harvest_t h; assert(garden_service_harvest(&h) == GARDEN_OK);
    assert(h.crop == GARDEN_RADISH && h.first && h.count == 1 && persisted.album[GARDEN_RADISH] == 1);
    garden_t saved = persisted; reboot(fake_now); view();
    assert(garden_service_harvest(&h) == GARDEN_ERR_STATE);
    assert(memcmp(&persisted, &saved, sizeof(garden_t)) == 0);
}
static void event_draws_survive(void) {
    saved_plant(GARDEN_RADISH, GARDEN_FLAG_WEED_DRAWN | GARDEN_FLAG_PEST_DRAWN);
    uint32_t rng = persisted.rng; reboot(SOWN_AT);
    fake_now += 3 * 3600 + 45 * 60;
    garden_view_t v = view();
    assert((v.garden.flags & (GARDEN_FLAG_WEED | GARDEN_FLAG_WEED_SEEN)) ==
           (GARDEN_FLAG_WEED | GARDEN_FLAG_WEED_SEEN));
    assert(v.garden.rng == rng);
    assert(garden_service_use(GARDEN_MAT_HERBICIDE) == GARDEN_OK);
    reboot(0); fake_now = SOWN_AT + 3 * 3600 + 46 * 60; v = view();
    assert(v.garden.progress == 276000 && !(v.garden.flags & GARDEN_FLAG_WEED));
    assert(v.garden.flags & GARDEN_FLAG_WEED_SEEN);
    assert(v.garden.bag[GARDEN_MAT_HERBICIDE] == 0 && v.garden.rng == rng);
}

static void saver_rtc_day(void) {
    stored_saver = true; usb_connected = false;
    for (int c = 0; c < GARDEN_CROPS; c++) {
        saved_plant(c, 0); boot_device(SOWN_AT);
        garden_t at_boot = persisted; assert(!timer_armed);
        advance_ms(29000); poll_power(false); assert(brightness == 80 && !terminal);
        advance_ms(1000); poll_power(false); assert(brightness == 20 && !terminal);
        advance_ms(29000); poll_power(false); assert(!terminal);
        advance_ms(1000); poll_power(false); expect_shutdown();
        assert(memcmp(&persisted, &at_boot, sizeof(persisted)) == 0);
        boot_device(SOWN_AT + 86400); expect_one_day(c);
        garden_t replayed = persisted; view();
        assert(memcmp(&persisted, &replayed, sizeof(persisted)) == 0 && stored_saver);
    }
}
static void saver_cold_sync(void) {
    stored_saver = true; usb_connected = false;
    for (int c = 0; c < GARDEN_CROPS; c++) {
        saved_plant(c, 0); boot_device(SOWN_AT);
        advance_ms(60000); poll_power(false); expect_shutdown();
        garden_t at_sleep = persisted; commits = 0;
        boot_device(0);
        advance_ms(30000); poll_power(false);
        garden_view_t pending = view();
        assert(!pending.clock_ok && memcmp(&pending.garden, &at_sleep, sizeof(persisted)) == 0);
        assert(memcmp(&persisted, &at_sleep, sizeof(persisted)) == 0 && commits == 0);
        fake_now = SOWN_AT + 86400; expect_one_day(c);
        assert(garden_service_water() == GARDEN_OK && commits == 1);
        assert(persisted.stage == GARDEN_STAGE_SEEDLING && persisted.water == GARDEN_WATER_FULL);
        garden_t replayed = persisted; boot_device(fake_now); view();
        assert(memcmp(&persisted, &replayed, sizeof(persisted)) == 0 && stored_saver);
    }
}
static void saver_usb_day(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = true; usb_connected = true;
    boot_device(SOWN_AT);
    advance_ms(60000); poll_power(false);
    assert(!terminal && !shutdown_calls && brightness == 0 && lvgl_stopped && pm_held == 0);
    advance_ms(86400000 - 60000); poll_power(false); expect_one_day(GARDEN_RADISH);
    assert(!terminal && !shutdown_calls && stored_saver);
    usb_connected = false; poll_power(false);
    assert(brightness == 80 && !lvgl_stopped && pm_held == 1);
    advance_ms(30000); poll_power(false); assert(brightness == 20 && !terminal);
    advance_ms(29000); poll_power(false); assert(!terminal);
    advance_ms(1000); poll_power(false); expect_shutdown();
    int64_t wake_at = fake_now; boot_device(0); fake_now = wake_at;
    garden_view_t v = view();
    assert(v.clock_ok && v.garden.stage == GARDEN_STAGE_SEEDLING);
    assert(v.garden.progress == 720000 && v.garden.water == 0);
    assert(v.garden.settled_at == wake_at && stored_saver);
}
static void saver_usb_reconnect(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = true; usb_connected = true;
    boot_device(SOWN_AT); advance_ms(60000); poll_power(false);
    usb_connected = false; poll_power(false); advance_ms(59000);
    usb_connected = true; poll_power(false); advance_ms(1000); poll_power(false);
    assert(!terminal && !shutdown_calls && lvgl_stopped);
    /* 重新拔线后，关机入口再次读取 USB，覆盖策略计算后才插回的窗口。 */
    usb_connected = false; poll_power(false); advance_ms(60000);
    usb_reads = 0; connect_on_read = 2; poll_power(false);
    assert(usb_connected && !terminal && !shutdown_calls);
    poll_power(false); assert(lvgl_stopped);
    usb_connected = false; connect_on_read = 0; poll_power(false);
    advance_ms(60000); poll_power(false); expect_shutdown();
    boot_device(SOWN_AT + 86400); expect_one_day(GARDEN_RADISH);
}
static void saver_key_deadline(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = true; usb_connected = false;
    boot_device(SOWN_AT); advance_ms(59000); poll_power(false);
    assert(power_service_key_activity() && brightness == 80);
    advance_ms(1000); poll_power(false); assert(!terminal && !shutdown_calls);
    advance_ms(58000); power_service_wake(); poll_power(false); assert(!terminal);
    /* 电池省电模式的远程通知只亮屏；实体按键才延后空闲截止时间。 */
    advance_ms(1000); poll_power(false); expect_shutdown();
    boot_device(0); fake_now = SOWN_AT + 86400; expect_one_day(GARDEN_RADISH);
}
static void saver_gift_busy(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = true; usb_connected = false;
    boot_device(SOWN_AT); advance_ms(59000);
    assert(garden_service_gift(GARDEN_MAT_FERTILIZER) == GARDEN_OK);
    assert(garden_service_busy() && persisted.bag[GARDEN_MAT_FERTILIZER] == 0);
    garden_service_tick(uptime_ms); assert(radio_sends == 1);
    advance_ms(1000); garden_service_tick(uptime_ms); poll_power(garden_service_busy());
    assert(!terminal && !shutdown_calls && brightness == 80);
    advance_ms(3000); garden_service_tick(uptime_ms); poll_power(garden_service_busy());
    assert(garden_service_busy() && !terminal);
    advance_ms(1000); garden_service_tick(uptime_ms);
    assert(!garden_service_busy() && persisted.bag[GARDEN_MAT_FERTILIZER] == 1);
    poll_power(garden_service_busy()); assert(!terminal);
    advance_ms(58000); poll_power(false); assert(!terminal);
    advance_ms(1000); poll_power(false); expect_shutdown();
    boot_device(0); fake_now = SOWN_AT + 86400; expect_one_day(GARDEN_RADISH);
    assert(!garden_service_busy() && persisted.pending_mat == 0);
}
static void saver_repeat_cycles(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = false; usb_connected = false;
    boot_device(SOWN_AT);
    assert(power_service_toggle_saver() && stored_saver && !timer_armed && saver_writes == 1);
    for (int day = 1; day <= 3; day++) {
        advance_ms(60000); poll_power(false); expect_shutdown();
        int64_t wake_at = SOWN_AT + day * 86400LL;
        boot_device(day % 2 ? 0 : wake_at); fake_now = wake_at;
        garden_view_t v = view();
        assert(v.clock_ok && v.garden.stage == GARDEN_STAGE_SEEDLING && v.garden.water == 0);
        assert(v.garden.progress == 720000 && v.garden.settled_at == wake_at && stored_saver);
        /* 接收道具会保存当前结算；缺水状态跨多次休眠保持，到账记录也不能重复。 */
        espnow_game_packet_t packet = {.type = ESPNOW_MSG_GARDEN_GIFT,
                                      .session = (uint16_t)day, .choice = GARDEN_MAT_FERTILIZER};
        garden_service_on_packet(NULL, &packet);
        garden_service_on_packet(NULL, &packet);
        assert(persisted.water == 0 && persisted.bag[GARDEN_MAT_FERTILIZER] == 1 + day);
    }
    assert(saver_writes == 1);
    assert(!power_service_toggle_saver() && !stored_saver && saver_writes == 2);
    boot_device(fake_now); assert(!power_service_saver_enabled() && timer_armed);
    fire_power_timer(); fire_power_timer(); poll_power(false);
    assert(lvgl_stopped && !terminal && !shutdown_calls);
}
static void saver_teardown_retry(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = true; usb_connected = false;
    boot_device(SOWN_AT); garden_t saved = persisted;
    failure = 1; advance_ms(60000); poll_power(false);
    assert(!terminal && strcmp(shutdown_order, "WB") == 0);
    assert(memcmp(&persisted, &saved, sizeof(persisted)) == 0);
    failure = 0; clear_shutdown_trace(); advance_ms(1000); poll_power(false); expect_shutdown();
    boot_device(SOWN_AT + 86400); expect_one_day(GARDEN_RADISH);
    failure = 2; advance_ms(60000); poll_power(false);
    assert(terminal == 2 && strcmp(shutdown_order, "WBANbasiLR") == 0);
    boot_device(0); fake_now = SOWN_AT + 86400;
    expect_one_day(GARDEN_RADISH);
}
static void connected_standby_day(void) {
    saved_plant(GARDEN_RADISH, 0); stored_saver = false; usb_connected = false;
    boot_device(SOWN_AT); assert(timer_armed && timer_delay_ms == 30000);
    fire_power_timer(); assert(brightness == 20 && timer_armed);
    fire_power_timer(); assert(brightness == 0 && lvgl_stopped && !timer_armed && pm_held == 0);
    advance_ms(86400000 - 60000); poll_power(false);
    assert(!terminal && !shutdown_calls); expect_one_day(GARDEN_RADISH);
    assert(power_service_key_activity() && !lvgl_stopped && timer_armed && pm_held == 1);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;
static const test_case_t cases[] = {
    {"starter_once", starter_once}, {"deep_sleep_day", deep_sleep_day},
    {"cold_boot_sync", cold_boot_sync}, {"temporary_clock_loss", temporary_clock_loss},
    {"night_pause", night_pause}, {"clock_rollback", clock_rollback},
    {"invalid_actions", invalid_actions}, {"dry_refill_after_sync", dry_refill_after_sync},
    {"repeated_restarts", repeated_restarts}, {"harvest_once", harvest_once},
    {"event_draws_survive", event_draws_survive},
    {"saver_rtc_day", saver_rtc_day}, {"saver_cold_sync", saver_cold_sync},
    {"saver_usb_day", saver_usb_day}, {"saver_usb_reconnect", saver_usb_reconnect},
    {"saver_key_deadline", saver_key_deadline}, {"saver_gift_busy", saver_gift_busy},
    {"saver_repeat_cycles", saver_repeat_cycles}, {"saver_teardown_retry", saver_teardown_retry},
    {"connected_standby_day", connected_standby_day},
};
int main(int argc, char **argv) {
    assert(argc == 2);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(cases[i].name, argv[1]) == 0) {
            cases[i].run(); printf("%s: PASS\n", cases[i].name); return 0;
        }
    }
    return 2;
}
"""
CASES = [
    "starter_once", "deep_sleep_day", "cold_boot_sync", "temporary_clock_loss",
    "night_pause", "clock_rollback", "invalid_actions", "dry_refill_after_sync",
    "repeated_restarts", "harvest_once", "event_draws_survive",
    "saver_rtc_day", "saver_cold_sync", "saver_usb_day", "saver_usb_reconnect",
    "saver_key_deadline", "saver_gift_busy", "saver_repeat_cycles",
    "saver_teardown_retry", "connected_standby_day",
]


def main():
    with tempfile.TemporaryDirectory(prefix="passport-garden-service-") as directory:
        stub = Path(directory)
        (stub / "freertos").mkdir()
        (stub / "driver").mkdir()
        (stub / "platform.h").write_text(HEADER)
        for name in ("app_events", "sdkconfig", "esp_err", "esp_log", "esp_random",
                     "esp_timer", "nvs", "freertos/FreeRTOS", "freertos/semphr",
                     "bsp_audio", "bsp_battery", "bsp_display", "bsp_i2c", "bsp_pins",
                     "sound_service", "esp_lvgl_port", "esp_pm", "esp_sleep",
                     "esp_system", "esp_wifi", "driver/gpio", "driver/usb_serial_jtag"):
            (stub / f"{name}.h").write_text('#include "platform.h"\n')
        source = SOURCE.replace("@GARDEN_SERVICE@", str(ROOT / "main/interaction/garden_service.c"))
        (stub / "test.c").write_text(source)
        power_source = POWER_SOURCE.replace("@POWER_SERVICE@", str(ROOT / "main/power_service.c"))
        (stub / "power.c").write_text(power_source)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DCONFIG_PM_ENABLE=1", f"-I{stub}", f"-I{ROOT / 'main/interaction'}", f"-I{ROOT / 'main'}",
            str(stub / "test.c"), str(ROOT / "main/interaction/garden_rules.c"),
            str(stub / "power.c"), str(ROOT / "main/power_policy.c"),
            "-o", str(stub / "test"),
        ], check=True)
        failed = []
        for case in CASES:
            result = subprocess.run([str(stub / "test"), case], capture_output=True, text=True)
            print(result.stdout, end="")
            if result.returncode:
                failed.append(case)
                print(f"{case}: FAIL\n{result.stderr.strip()}")
        if failed:
            raise SystemExit(f"Garden service runtime: FAIL ({len(failed)}/{len(CASES)}): {', '.join(failed)}")
    print(f"Garden service runtime: PASS ({len(CASES)}/{len(CASES)})")


if __name__ == "__main__":
    main()
