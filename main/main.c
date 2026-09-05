#include "app_events.h"
#include "app_model.h"
#include "nvs_cache.h"
#include "battery_service.h"
#include "buzzer_game_service.h"
#include "espnow_service.h"
#include "find_service.h"
#include "game_service.h"
#include "mole_game_service.h"
#include "ptt_service.h"
#include "power_service.h"
#include "screenshot_service.h"
#include "sound_service.h"
#include "ui_app.h"
#include "wifi_service.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "kids_points";
static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user) { (void)user; ui_app_post_key(btn, event); }
static void heap_log_task(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(8000)); ESP_LOGI(TAG, "启动稳定后 free heap=%lu min=%lu", (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size()); vTaskDelete(NULL); }

void app_main(void)
{
    ESP_LOGI(TAG, "kids_points v1.0.0 actor=%s build=%s %s", CONFIG_ACTOR_CHILD_ID, __DATE__, __TIME__);
    app_model_init(); app_events_init();
    ESP_ERROR_CHECK(nvs_cache_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_cache_load());
    app_model_reset_online_sync();
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) { ESP_LOGE(TAG, "LVGL初始化失败"); return; }
    ESP_ERROR_CHECK(power_service_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(battery_service_start());
    ESP_ERROR_CHECK(ui_app_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(screenshot_service_start());
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));
    esp_err_t audio = sound_service_start();
    if (audio != ESP_OK) ESP_LOGW(TAG, "音效不可用: %s", esp_err_to_name(audio));
    esp_err_t wifi = wifi_service_start();
    /* Find de-duplication must exist before either transport can deliver a ring. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(find_service_start());
    if (wifi != ESP_OK) ESP_LOGW(TAG, "WiFi后台启动失败: %s，继续离线模式", esp_err_to_name(wifi));
    else {
        esp_err_t now = espnow_service_start();
        if (now == ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(game_service_start());
            ESP_ERROR_CHECK_WITHOUT_ABORT(buzzer_game_service_start());
            ESP_ERROR_CHECK_WITHOUT_ABORT(mole_game_service_start());
            if (audio == ESP_OK) ESP_ERROR_CHECK_WITHOUT_ABORT(ptt_service_start());
            else ESP_LOGW(TAG, "PTT灰化: 音频RX不可用（codec初始化失败）");
        } else ESP_LOGW(TAG, "ESP-NOW互动不可用: %s", esp_err_to_name(now));
    }
    xTaskCreatePinnedToCore(heap_log_task, "kp_heap", 2048, NULL, 1, NULL, 0);
}
