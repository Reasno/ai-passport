#!/usr/bin/env python3
"""Execute the terminal power service with injected peripheral failures."""
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
#define ESP_SLEEP_WAKEUP_ALL 255
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);
esp_err_t esp_sleep_disable_wakeup_source(int);
esp_err_t battery_service_stop(void);
bool sound_service_prepare_sleep(void);
esp_err_t wifi_service_stop(void);
esp_err_t bsp_battery_sleep(void);
esp_err_t bsp_audio_sleep(void);
esp_err_t bsp_audio_prepare_deep_sleep(void);
esp_err_t bsp_i2c_prepare_deep_sleep(void);
bool bsp_lvgl_lock(int);
esp_err_t bsp_display_prepare_deep_sleep(void);
void bsp_display_backlight(uint8_t);
void esp_restart(void);
void esp_deep_sleep_start(void);
"""
SOURCE = r"""
#include "power_service.h"
#include <assert.h>
#include <setjmp.h>
#include <string.h>
static int64_t now;
static char calls[64];
static int used, failure, terminal, brightness;
static jmp_buf jump;
static void call(char value) { calls[used++] = value; calls[used] = 0; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "injected"; }
int64_t esp_timer_get_time(void) { return now * 1000; }
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
bool bsp_lvgl_lock(int timeout) { assert(timeout == 1000); call('L'); return failure != 5; }
esp_err_t bsp_display_prepare_deep_sleep(void) { call('d'); return 0; }
void bsp_display_backlight(uint8_t level) { brightness = level; }
void esp_restart(void) { call('R'); terminal = 2; longjmp(jump, 1); }
void esp_deep_sleep_start(void) { call('S'); terminal = 1; longjmp(jump, 1); }
static void reset(int inject) {
    used = 0; calls[0] = 0; failure = inject; terminal = 0; now = 0;
    assert(power_service_start() == ESP_OK);
}
static void sleep_at_deadline(void) {
    now = 60000;
    if (!setjmp(jump)) power_service_poll(false);
}
int main(void) {
    reset(0);
    now = 29999; power_service_poll(false); assert(brightness == 80);
    now = 30000; power_service_poll(false); assert(brightness == 20);
    now = 59000; power_service_wake(); assert(brightness == 80);
    sleep_at_deadline();
    assert(terminal == 1 && strcmp(calls, "WBANbasiLdS") == 0);

    reset(0);
    now = 30000; power_service_poll(false);
    now = 59000; assert(power_service_key_activity());
    sleep_at_deadline(); assert(!terminal && !used);
    now = 119000;
    if (!setjmp(jump)) power_service_poll(false);
    assert(terminal == 1);

    reset(0);
    now = 60000; power_service_poll(true); assert(!terminal && brightness == 80);
    now = 60001; power_service_poll(false); assert(!terminal);
    now = 120000;
    if (!setjmp(jump)) power_service_poll(false);
    assert(terminal == 1);

    reset(1); sleep_at_deadline(); assert(!terminal && strcmp(calls, "W") == 0);
    reset(2); sleep_at_deadline(); assert(!terminal && strcmp(calls, "WB") == 0);
    reset(3); sleep_at_deadline(); assert(!terminal && strcmp(calls, "WBA") == 0);
    reset(4); sleep_at_deadline(); assert(terminal == 1 && strcmp(calls, "WBANbasiLdS") == 0);
    reset(5); sleep_at_deadline(); assert(terminal == 2 && strcmp(calls, "WBANbasiLR") == 0);
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="passport-power-") as directory:
    stub = Path(directory)
    (stub / "platform.h").write_text(HEADER)
    for name in ("esp_err", "bsp_audio", "bsp_battery", "bsp_display", "bsp_i2c",
                 "sound_service", "esp_log", "esp_sleep", "esp_system", "esp_timer"):
        (stub / f"{name}.h").write_text('#include "platform.h"\n')
    (stub / "test.c").write_text(SOURCE)
    subprocess.run([
        *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-variable",
        f"-I{stub}", f"-I{ROOT / 'main'}", "-include", str(stub / "platform.h"),
        str(stub / "test.c"), str(ROOT / "main/power_service.c"),
        str(ROOT / "main/power_policy.c"), "-o", str(stub / "test"),
    ], check=True)
    subprocess.run([str(stub / "test")], check=True)
print("Power runtime: PASS")
