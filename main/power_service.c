#include "power_service.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "kp_power";
static esp_timer_handle_t s_timer;
static int64_t s_last_activity_ms;
static uint8_t s_level = 80;

static void timer_cb(void *arg)
{
    (void)arg;
    int64_t idle = (esp_timer_get_time() / 1000) - s_last_activity_ms;
    uint8_t target = idle >= 60000 ? 0 : (idle >= 30000 ? 20 : 80);
    if (target != s_level) { s_level = target; bsp_display_backlight(target); ESP_LOGI(TAG, "空闲%lldms 背光=%u%%", (long long)idle, target); }
}

esp_err_t power_service_start(void)
{
    esp_err_t err;
    s_last_activity_ms = esp_timer_get_time() / 1000;
    bsp_display_backlight(80);
#ifdef CONFIG_PM_ENABLE
    esp_pm_config_t pm = {.max_freq_mhz = 160, .min_freq_mhz = 80, .light_sleep_enable = false};
    err = esp_pm_configure(&pm);
    if (err != ESP_OK) ESP_LOGW(TAG, "PM配置失败: %s", esp_err_to_name(err));
#else
    ESP_LOGI(TAG, "CONFIG_PM_ENABLE未开启，仅启用背光节能");
#endif
    esp_timer_create_args_t args = {.callback = timer_cb, .name = "kp_backlight"};
    err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK) err = esp_timer_start_periodic(s_timer, 1000000);
    return err;
}

void power_service_wake(void)
{
    s_last_activity_ms = esp_timer_get_time() / 1000;
    if (s_level < 80) {
        s_level = 80;
        bsp_display_backlight(80);
        ESP_LOGI(TAG, "远程提醒唤醒背光");
    }
}

bool power_service_key_activity(void)
{
    bool consume = s_level < 80;
    power_service_wake();
    if (consume) ESP_LOGI(TAG, "按键唤醒背光，消费本次操作");
    return consume;
}
