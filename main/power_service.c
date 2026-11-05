#include "power_service.h"
#include "power_policy.h"
#include "app_model.h"
#include "battery_service.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "nvs_cache.h"
#include "sound_service.h"
#include "wifi_service.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#define ACTIVE_BACKLIGHT 80
#define DIM_BACKLIGHT 20

static const char *TAG = "kp_power";
/* Connected standby is driven by s_timer. Power-saving mode is driven by the
 * UI worker through power_service_poll(), the only path that may deep sleep. */
static esp_timer_handle_t s_timer;
static SemaphoreHandle_t s_lock;
static int64_t s_last_activity_ms;
static uint8_t s_level = ACTIVE_BACKLIGHT;
static bool s_lvgl_stopped;
static bool s_saver;
static bool s_usb_connected;
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_awake_lock;
static bool s_awake_lock_held;
#endif

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

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

static void stop_timer_locked(void)
{
    if (!s_timer) return;
    esp_err_t err = esp_timer_stop(s_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "停止省电计时器失败: %s", esp_err_to_name(err));
    }
}

static void arm_timer_locked(uint64_t delay_ms)
{
    if (!s_timer) return;
    stop_timer_locked();
    esp_err_t err = esp_timer_start_once(s_timer, delay_ms * 1000);
    if (err != ESP_OK) ESP_LOGW(TAG, "启动省电计时器失败: %s", esp_err_to_name(err));
}

static void restore_locked(const char *reason)
{
#ifdef CONFIG_PM_ENABLE
    if (s_awake_lock && !s_awake_lock_held) {
        esp_err_t err = esp_pm_lock_acquire(s_awake_lock);
        if (err == ESP_OK) s_awake_lock_held = true;
        else ESP_LOGW(TAG, "恢复运行态 PM 锁失败: %s", esp_err_to_name(err));
    }
#endif
    if (s_lvgl_stopped && bsp_lvgl_lock(500)) {
        esp_err_t err = lvgl_port_resume();
        if (err != ESP_OK) ESP_LOGW(TAG, "恢复 LVGL 失败: %s", esp_err_to_name(err));
        else s_lvgl_stopped = false;
        bsp_lvgl_unlock();
    }
    level(ACTIVE_BACKLIGHT);
    s_last_activity_ms = now_ms();
    if (s_saver) stop_timer_locked();
    else arm_timer_locked(POWER_DIM_MS);

    app_model_snapshot_t model;
    app_model_snapshot(&model);
    if (!model.wifi_online) {
        esp_err_t wifi_pm = esp_wifi_set_ps(WIFI_PS_NONE);
        if (wifi_pm != ESP_OK) ESP_LOGW(TAG, "恢复离线 Wi-Fi 射频失败: %s", esp_err_to_name(wifi_pm));
    }
    ESP_LOGI(TAG, "%s恢复：%s，LVGL运行，背光=%u%%", reason,
             s_saver ? "省电模式" : "Wi-Fi/ESP-NOW 保持", ACTIVE_BACKLIGHT);
}

static void standby_locked(int64_t idle)
{
    level(0);
    if (!s_lvgl_stopped && bsp_lvgl_lock(500)) {
        esp_err_t err = lvgl_port_stop();
        if (err != ESP_OK) ESP_LOGW(TAG, "暂停 LVGL 失败: %s", esp_err_to_name(err));
        else s_lvgl_stopped = true;
        bsp_lvgl_unlock();
    }
#ifdef CONFIG_PM_ENABLE
    /* wifi_service intentionally parks a disconnected radio in PS_NONE between
     * reconnect attempts. Override that only when the UI goes idle so the
     * connectionless receive window can participate in automatic Light Sleep. */
    esp_err_t wifi_pm = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (wifi_pm != ESP_OK) ESP_LOGW(TAG, "启用 Wi-Fi modem sleep 失败: %s", esp_err_to_name(wifi_pm));
    if (s_awake_lock && s_awake_lock_held) {
        esp_err_t err = esp_pm_lock_release(s_awake_lock);
        if (err == ESP_OK) s_awake_lock_held = false;
        else ESP_LOGW(TAG, "释放运行态 PM 锁失败: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "空闲%lldms，允许自动 Light Sleep（GPIO/ESP-NOW 可唤醒）", (long long)idle);
#else
    ESP_LOGI(TAG, "空闲%lldms，背光关闭；CONFIG_PM_ENABLE 未开启，无法 Light Sleep", (long long)idle);
#endif
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;

    if (!s_saver) {
        int64_t idle = now_ms() - s_last_activity_ms;
        power_state_t state = power_policy_state(idle, false, false, false);
        if (state == POWER_ACTIVE) {
            arm_timer_locked(POWER_DIM_MS - idle);
        } else if (state == POWER_DIM) {
            if (s_level != DIM_BACKLIGHT) {
                level(DIM_BACKLIGHT);
                ESP_LOGI(TAG, "空闲%lldms 背光=%u%%", (long long)idle, DIM_BACKLIGHT);
            }
            arm_timer_locked(POWER_SLEEP_MS - idle);
        } else {
            standby_locked(idle);
        }
    }

    xSemaphoreGive(s_lock);
}

static void deep_sleep(void)
{
    /* Check the live connection again before any terminal peripheral teardown. */
    if (usb_serial_jtag_is_connected()) return;
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
    esp_err_t err;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    err = nvs_cache_load_power_saver(&s_saver);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "读取省电模式失败，按关闭处理: %s", esp_err_to_name(err));
        s_saver = false;
    }
    s_last_activity_ms = now_ms();
    s_usb_connected = usb_serial_jtag_is_connected();
    s_level = ACTIVE_BACKLIGHT;
    bsp_display_backlight(ACTIVE_BACKLIGHT);

#ifdef CONFIG_PM_ENABLE
    esp_pm_config_t pm = {.max_freq_mhz = 160, .min_freq_mhz = 80, .light_sleep_enable = true};
    err = esp_pm_configure(&pm);
    if (err != ESP_OK) return err;

    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "passport_awake", &s_awake_lock);
    if (err != ESP_OK) return err;
    err = esp_pm_lock_acquire(s_awake_lock);
    if (err != ESP_OK) return err;
    s_awake_lock_held = true;

    err = gpio_wakeup_enable((gpio_num_t)BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) return err;
    err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) return err;
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE 未开启，仅启用背光节能");
#endif

    esp_timer_create_args_t args = {.callback = timer_cb, .name = "kp_power"};
    err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK && !s_saver) err = esp_timer_start_once(s_timer, POWER_DIM_MS * 1000ULL);
    if (err == ESP_OK) ESP_LOGI(TAG, "省电模式=%s", s_saver ? "开启" : "关闭");
    return err;
}

void power_service_wake(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    /* Battery-powered saver mode does not extend the idle deadline for incoming
     * messages. USB standby uses the resumable display/connection wake path. */
    if (s_saver && !usb_serial_jtag_is_connected()) level(ACTIVE_BACKLIGHT);
    else restore_locked("远程事件");
    xSemaphoreGive(s_lock);
}

bool power_service_key_activity(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return false;
    bool consume = s_level < ACTIVE_BACKLIGHT || s_lvgl_stopped;
    restore_locked("GPIO按键");
    xSemaphoreGive(s_lock);
    if (consume) {
        app_model_snapshot_t model;
        app_model_snapshot(&model);
        if (!model.wifi_online) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "GPIO 唤醒后 Wi-Fi 重连失败: %s", esp_err_to_name(err));
            }
        }
        ESP_LOGI(TAG, "按键仅用于唤醒，本次操作已消费");
    }
    return consume;
}

bool power_service_toggle_saver(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return s_saver;
    s_saver = !s_saver;
    restore_locked(s_saver ? "开启省电模式" : "关闭省电模式");
    bool enabled = s_saver;
    xSemaphoreGive(s_lock);
    esp_err_t err = nvs_cache_save_power_saver(enabled);
    if (err != ESP_OK) ESP_LOGW(TAG, "保存省电模式失败: %s", esp_err_to_name(err));
    return enabled;
}

bool power_service_saver_enabled(void)
{
    /* A single aligned bool written only by the UI worker; readers need no lock. */
    return s_saver;
}

void power_service_poll(bool busy)
{
    /* s_saver is written only by the UI worker, which is also the only caller. */
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    bool usb_connected = usb_serial_jtag_is_connected();
    if (s_usb_connected != usb_connected) {
        if (s_saver && !usb_connected) restore_locked("USB disconnected");
        ESP_LOGI(TAG, "USB host %s; power saver=%d", usb_connected ? "connected" : "disconnected", s_saver);
        s_usb_connected = usb_connected;
    }
    if (!s_saver) {
        xSemaphoreGive(s_lock);
        return;
    }
    int64_t now = now_ms();
    if (busy) s_last_activity_ms = now;
    int64_t idle = now - s_last_activity_ms;
    power_state_t state = power_policy_state(idle, busy, true, usb_connected);
    if (state == POWER_STANDBY) {
        /* Only enter once; keep the UI worker, radio and USB connection monitor live. */
        if (s_level != 0 || !s_lvgl_stopped) standby_locked(idle);
    } else {
        if (s_lvgl_stopped) restore_locked("Active operation");
        level(state == POWER_ACTIVE ? ACTIVE_BACKLIGHT : DIM_BACKLIGHT);
    }
    xSemaphoreGive(s_lock);
    if (state == POWER_DEEP_SLEEP) deep_sleep();
}
