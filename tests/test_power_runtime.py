#!/usr/bin/env python3
"""Execute the power service in connected-standby and power-saving modes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = """
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_CONN 0x3007
#define ESP_SLEEP_WAKEUP_ALL 255
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define BSP_BTN_GPIO 0
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
typedef void *SemaphoreHandle_t;
typedef struct stub_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef void *esp_pm_lock_handle_t;
typedef struct { int max_freq_mhz, min_freq_mhz; bool light_sleep_enable; } esp_pm_config_t;
typedef enum { ESP_PM_NO_LIGHT_SLEEP = 2 } esp_pm_lock_type_t;
typedef int gpio_num_t;
typedef enum { GPIO_INTR_LOW_LEVEL = 4 } gpio_int_type_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM } wifi_ps_type_t;
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
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
esp_err_t battery_service_stop(void);
bool sound_service_prepare_sleep(void);
esp_err_t wifi_service_stop(void);
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
"""
SOURCE = r"""
#include "power_service.h"
#include "app_model.h"
#include "nvs_cache.h"
#include <assert.h>
#include <setjmp.h>
#include <string.h>
static int64_t now;
static char calls[64];
static int used, failure, terminal, brightness, held, ps, reconnects, locks;
static bool lvgl_stopped, timer_armed, stored_saver, saved, online, usb_connected;
static int usb_reads, connect_on_read, standby_entries;
static esp_err_t load_error;
static uint64_t timer_delay_ms;
static void (*timer_callback)(void *);
static int timer_object, mutex_object, pm_object;
static jmp_buf jump;
static void call(char value) { calls[used++] = value; calls[used] = 0; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "injected"; }
int64_t esp_timer_get_time(void) { return now * 1000; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    timer_callback = args->callback; *out = (esp_timer_handle_t)&timer_object; return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t us) {
    (void)timer; timer_armed = true; timer_delay_ms = us / 1000; return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    (void)timer; bool was = timer_armed; timer_armed = false;
    return was ? ESP_OK : ESP_ERR_INVALID_STATE;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_object; }
int xSemaphoreTake(SemaphoreHandle_t lock, uint32_t ticks) {
    (void)ticks; assert(lock == &mutex_object && locks == 0); locks++; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; assert(locks == 1); locks--; return pdTRUE; }
esp_err_t esp_pm_configure(const void *config) {
    const esp_pm_config_t *pm = config; assert(pm->light_sleep_enable); return ESP_OK;
}
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type, int arg, const char *name, esp_pm_lock_handle_t *out) {
    (void)arg; (void)name; assert(type == ESP_PM_NO_LIGHT_SLEEP); *out = &pm_object; return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t lock) { (void)lock; held++; return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t lock) { (void)lock; held--; return ESP_OK; }
esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type) {
    assert(pin == BSP_BTN_GPIO && type == GPIO_INTR_LOW_LEVEL); return ESP_OK;
}
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) { ps = type; return ESP_OK; }
esp_err_t esp_wifi_connect(void) { reconnects++; return ESP_OK; }
bool usb_serial_jtag_is_connected(void) {
    if (++usb_reads == connect_on_read) usb_connected = true;
    return usb_connected;
}
esp_err_t lvgl_port_stop(void) { lvgl_stopped = true; standby_entries++; return ESP_OK; }
esp_err_t lvgl_port_resume(void) { lvgl_stopped = false; return ESP_OK; }
void app_model_snapshot(app_model_snapshot_t *out) { memset(out, 0, sizeof(*out)); out->wifi_online = online; }
esp_err_t nvs_cache_load_power_saver(bool *enabled) { *enabled = load_error ? true : stored_saver; return load_error; }
esp_err_t nvs_cache_save_power_saver(bool enabled) { stored_saver = enabled; saved = true; return ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(int sources) {
    assert(sources == ESP_SLEEP_WAKEUP_ALL); call('W'); return failure == 1;
}
esp_err_t battery_service_stop(void) { call('B'); return failure == 2; }
bool sound_service_prepare_sleep(void) { call('A'); return failure != 3; }
esp_err_t wifi_service_stop(void) { call('N'); return 0; }
esp_err_t bsp_battery_sleep(void) { call('b'); return failure == 4; }
esp_err_t bsp_audio_sleep(void) { call('a'); return failure == 4; }
esp_err_t bsp_audio_prepare_deep_sleep(void) { call('s'); return 0; }
esp_err_t bsp_i2c_prepare_deep_sleep(void) { call('i'); return 0; }
bool bsp_lvgl_lock(int timeout) {
    if (timeout != 1000) { assert(timeout == 500); return true; }
    call('L'); return failure != 5;
}
void bsp_lvgl_unlock(void) {}
esp_err_t bsp_display_prepare_deep_sleep(void) { call('d'); return 0; }
void bsp_display_backlight(uint8_t level) { brightness = level; }
void esp_restart(void) { call('R'); terminal = 2; longjmp(jump, 1); }
void esp_deep_sleep_start(void) { call('S'); terminal = 1; longjmp(jump, 1); }
static void reset(bool saver, int inject) {
    used = 0; calls[0] = 0; failure = inject; terminal = 0; now = 0; held = 0; ps = WIFI_PS_NONE;
    reconnects = 0; lvgl_stopped = false; timer_armed = false; stored_saver = saver; saved = false;
    load_error = ESP_OK; online = false;
    usb_connected = false; usb_reads = 0; connect_on_read = 0; standby_entries = 0;
    assert(power_service_start() == ESP_OK);
    assert(held == 1 && locks == 0);
}
static void fire_timer(void) {
    assert(timer_armed); timer_armed = false; now += (int64_t)timer_delay_ms; timer_callback(NULL);
}
static void sleep_at_deadline(void) {
    now = 60000;
    if (!setjmp(jump)) power_service_poll(false);
}
int main(void) {
    /* Default mode: connected standby, never terminal sleep. */
    reset(false, 0);
    assert(timer_armed && timer_delay_ms == 30000);
    fire_timer(); assert(now == 30000 && brightness == 20 && timer_armed && timer_delay_ms == 30000);
    fire_timer();
    assert(brightness == 0 && lvgl_stopped && ps == WIFI_PS_MIN_MODEM && held == 0 && !timer_armed);
    now = 600000; power_service_poll(false); assert(!used && !terminal);
    assert(power_service_key_activity());
    assert(brightness == 80 && !lvgl_stopped && held == 1 && ps == WIFI_PS_NONE && reconnects == 1);
    assert(timer_armed && timer_delay_ms == 30000);
    now += 20000; power_service_wake(); assert(timer_armed && timer_delay_ms == 30000);
    online = true; assert(!power_service_key_activity()); assert(reconnects == 1);

    /* Enabling power-saving mode persists it and hands idle tracking to the UI worker. */
    assert(power_service_toggle_saver() && saved && stored_saver && !timer_armed);
    now = 0; reset(true, 0); assert(!timer_armed);
    now = 29999; power_service_poll(false); assert(brightness == 80);
    now = 30000; power_service_poll(false); assert(brightness == 20);
    now = 59000; power_service_wake(); assert(brightness == 80 && !timer_armed);
    sleep_at_deadline();
    assert(terminal == 1 && strcmp(calls, "WBANbasiLdS") == 0);

    reset(true, 0);
    now = 30000; power_service_poll(false);
    now = 59000; assert(power_service_key_activity());
    sleep_at_deadline(); assert(!terminal && !used);
    now = 119000;
    if (!setjmp(jump)) power_service_poll(false);
    assert(terminal == 1);

    /* A connected host suppresses terminal sleep, even after a long idle period. */
    reset(true, 0);
    usb_connected = true;
    now = 30000; power_service_poll(false); assert(brightness == 20);
    now = 60000; power_service_poll(false);
    assert(brightness == 0 && lvgl_stopped && !terminal && !used && standby_entries == 1);
    now = 600000; power_service_poll(false);
    assert(!terminal && !used && standby_entries == 1 && stored_saver);

    /* A remote wake and a busy operation still resume the display in USB standby. */
    power_service_wake(); assert(brightness == 80 && !lvgl_stopped);
    now += 60000; power_service_poll(false); assert(lvgl_stopped && brightness == 0);
    power_service_poll(true); assert(brightness == 80 && !lvgl_stopped && !terminal);
    now += 60000; power_service_poll(false); assert(lvgl_stopped && brightness == 0);

    /* Unplugging starts a fresh 60-second deadline without changing the mode. */
    usb_connected = false;
    now = 800000; power_service_poll(false);
    assert(brightness == 80 && !lvgl_stopped && !terminal && !used && stored_saver && !saved);
    now = 829999; power_service_poll(false); assert(brightness == 80);
    now = 830000; power_service_poll(false); assert(brightness == 20);
    now = 859999; power_service_poll(false); assert(!terminal && !used);
    now = 860000;
    if (!setjmp(jump)) power_service_poll(false);
    assert(terminal == 1 && strcmp(calls, "WBANbasiLdS") == 0);

    /* Reconnecting during the countdown cancels its pending deep sleep. */
    reset(true, 0);
    usb_connected = true; now = 60000; power_service_poll(false);
    usb_connected = false; now = 61000; power_service_poll(false);
    usb_connected = true; now = 120000; power_service_poll(false); assert(!terminal);
    now = 121000; power_service_poll(false); assert(lvgl_stopped && !terminal && !used);
    assert(power_service_key_activity() && !lvgl_stopped);

    /* Recheck the live USB signal at the final shutdown entry to catch reconnection
     * after policy evaluation but before the first peripheral is stopped. */
    reset(true, 0);
    now = 60000; usb_reads = 0; connect_on_read = 2;
    power_service_poll(false);
    assert(!terminal && !used);
    power_service_poll(false); assert(lvgl_stopped && brightness == 0);
    assert(power_service_key_activity());

    reset(true, 0);
    now = 60000; power_service_poll(true); assert(!terminal && brightness == 80);
    now = 60001; power_service_poll(false); assert(!terminal);
    now = 120000;
    if (!setjmp(jump)) power_service_poll(false);
    assert(terminal == 1);

    /* Disabling power-saving mode restores connected standby. */
    reset(true, 0);
    now = 30000; power_service_poll(false); assert(brightness == 20);
    assert(!power_service_toggle_saver() && saved && !stored_saver);
    assert(brightness == 80 && timer_armed && timer_delay_ms == 30000);
    now = 600000; power_service_poll(false); assert(!used && !terminal);

    /* An unreadable setting falls back to connected standby. */
    stored_saver = true; load_error = ESP_ERR_INVALID_STATE;
    assert(power_service_start() == ESP_OK && timer_armed);
    now = 600000; power_service_poll(false); assert(!used && !terminal);

    reset(true, 1); sleep_at_deadline(); assert(!terminal && strcmp(calls, "W") == 0);
    reset(true, 2); sleep_at_deadline(); assert(!terminal && strcmp(calls, "WB") == 0);
    reset(true, 3); sleep_at_deadline(); assert(!terminal && strcmp(calls, "WBA") == 0);
    reset(true, 4); sleep_at_deadline(); assert(terminal == 1 && strcmp(calls, "WBANbasiLdS") == 0);
    reset(true, 5); sleep_at_deadline(); assert(terminal == 2 && strcmp(calls, "WBANbasiLR") == 0);
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="passport-power-") as directory:
    stub = Path(directory)
    (stub / "platform.h").write_text(HEADER)
    (stub / "driver").mkdir()
    (stub / "freertos").mkdir()
    for name in ("esp_err", "bsp_audio", "bsp_battery", "bsp_display", "bsp_i2c", "bsp_pins",
                 "battery_service", "sound_service", "wifi_service", "esp_log", "esp_lvgl_port",
                 "esp_pm", "esp_sleep", "esp_system", "esp_timer", "esp_wifi",
                 "driver/gpio", "driver/usb_serial_jtag", "freertos/FreeRTOS", "freertos/semphr"):
        (stub / f"{name}.h").write_text('#include "platform.h"\n')
    (stub / "test.c").write_text(SOURCE)
    subprocess.run([
        *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-DCONFIG_PM_ENABLE=1",
        f"-I{stub}", f"-I{ROOT / 'main'}", "-include", str(stub / "platform.h"),
        str(stub / "test.c"), str(ROOT / "main/power_service.c"),
        str(ROOT / "main/power_policy.c"), "-o", str(stub / "test"),
    ], check=True)
    subprocess.run([str(stub / "test")], check=True)
print("Power runtime: PASS")
