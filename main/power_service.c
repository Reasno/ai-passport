#include "power_service.h"
#include "power_policy.h"
#include "battery_service.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "sound_service.h"
#include "wifi_service.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "kp_power";
/* All functions execute on the UI worker except initialization before use. */
static int64_t s_last_activity_ms;
static uint8_t s_level = 80;
static bool s_started;

static void level(uint8_t value)
{
    if (s_level == value) return;
    s_level = value;
    bsp_display_backlight(value);
}

static void warn(const char *step, esp_err_t err)
{
    if (err != ESP_OK) ESP_LOGW(TAG, "%s: %s", step, esp_err_to_name(err));
}

static void deep_sleep(void)
{
    /* No ADC, timer, radio or USB wake source. The separate hardware power
     * button restores power; it is not an MCU GPIO exposed by this board. */
    if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) != ESP_OK) return;
    if (battery_service_stop() != ESP_OK) {
        ESP_LOGW(TAG, "Battery worker still active; retry sleep later");
        return;
    }
    /* Finish any current tone/PCM access before sleeping the codec. Holding
     * this lock blocks all later writers until terminal sleep/restart. */
    if (!sound_service_prepare_sleep()) return;
    warn("network stop", wifi_service_stop());
    warn("CW2017 suspend", bsp_battery_sleep());
    warn("ES8311 suspend", bsp_audio_sleep());
    warn("I2S release", bsp_audio_prepare_deep_sleep());
    warn("I2C release", bsp_i2c_prepare_deep_sleep());
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Cannot stop display; restarting released peripherals");
        esp_restart();
    }
    warn("LCD suspend", bsp_display_prepare_deep_sleep());
    ESP_LOGI(TAG, "Deep sleep: all software wake sources disabled; use hardware power button");
    esp_deep_sleep_start();
    esp_restart();
}

esp_err_t power_service_start(void)
{
    s_last_activity_ms = esp_timer_get_time() / 1000;
    s_started = true;
    bsp_display_backlight(80);
    return ESP_OK;
}

void power_service_wake(void)
{
    /* An incoming message may brighten an awake screen, but cannot extend
     * the idle deadline or configure a source to wake a sleeping device. */
    level(80);
}

bool power_service_key_activity(void)
{
    bool consumed = s_level < 80;
    s_last_activity_ms = esp_timer_get_time() / 1000;
    level(80);
    return consumed;
}

void power_service_poll(bool busy)
{
    if (!s_started) return;
    int64_t now = esp_timer_get_time() / 1000;
    if (busy) s_last_activity_ms = now;
    power_state_t state = power_policy_state(now - s_last_activity_ms, busy);
    level(state == POWER_ACTIVE ? 80 : 20);
    if (state == POWER_DEEP_SLEEP) deep_sleep();
}
