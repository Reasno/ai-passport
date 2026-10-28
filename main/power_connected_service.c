#include "power_service.h"
#include "app_model.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#define DIM_AFTER_MS 30000
#define LIGHT_SLEEP_AFTER_MS 60000
#define ACTIVE_BACKLIGHT 80
#define DIM_BACKLIGHT 20

static const char *TAG = "kp_power";
static esp_timer_handle_t s_timer;
static SemaphoreHandle_t s_lock;
static int64_t s_last_activity_ms;
static uint8_t s_level = ACTIVE_BACKLIGHT;
static bool s_lvgl_stopped;
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_awake_lock;
static bool s_awake_lock_held;
#endif

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void arm_timer_locked(uint64_t delay_ms)
{
    if (!s_timer) return;
    esp_err_t err = esp_timer_stop(s_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "停止省电计时器失败: %s", esp_err_to_name(err));
    }
    err = esp_timer_start_once(s_timer, delay_ms * 1000);
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
    if (s_level != ACTIVE_BACKLIGHT) {
        s_level = ACTIVE_BACKLIGHT;
        bsp_display_backlight(ACTIVE_BACKLIGHT);
    }
    s_last_activity_ms = now_ms();
    arm_timer_locked(DIM_AFTER_MS);

    app_model_snapshot_t model;
    app_model_snapshot(&model);
    if (!model.wifi_online) {
        esp_err_t wifi_pm = esp_wifi_set_ps(WIFI_PS_NONE);
        if (wifi_pm != ESP_OK) ESP_LOGW(TAG, "恢复离线 Wi-Fi 射频失败: %s", esp_err_to_name(wifi_pm));
    }
    ESP_LOGI(TAG, "%s恢复：Wi-Fi/ESP-NOW 保持，LVGL运行，背光=%u%%", reason, ACTIVE_BACKLIGHT);
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;

    int64_t idle = now_ms() - s_last_activity_ms;
    if (idle < DIM_AFTER_MS) {
        arm_timer_locked(DIM_AFTER_MS - idle);
    } else if (idle < LIGHT_SLEEP_AFTER_MS) {
        if (s_level != DIM_BACKLIGHT) {
            s_level = DIM_BACKLIGHT;
            bsp_display_backlight(DIM_BACKLIGHT);
            ESP_LOGI(TAG, "空闲%lldms 背光=%u%%", (long long)idle, DIM_BACKLIGHT);
        }
        arm_timer_locked(LIGHT_SLEEP_AFTER_MS - idle);
    } else {
        s_level = 0;
        bsp_display_backlight(0);
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

    xSemaphoreGive(s_lock);
}

esp_err_t power_service_start(void)
{
    esp_err_t err;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    s_last_activity_ms = now_ms();
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
    if (err == ESP_OK) err = esp_timer_start_once(s_timer, DIM_AFTER_MS * 1000ULL);
    return err;
}

void power_service_wake(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    restore_locked("远程事件");
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
