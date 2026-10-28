#include "battery_service.h"
#include "app_events.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "kp_battery";
static SemaphoreHandle_t s_lock;
static battery_snapshot_t s_state;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_stopped;

static void battery_task(void *arg)
{
    (void)arg;
    esp_err_t init = bsp_battery_init();
    if (init == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "CW2017 SOC initialization timed out after 5s; status bar will show --%%");
    } else if (init != ESP_OK) {
        ESP_LOGW(TAG, "CW2017 unavailable: %s; status bar will show --%%", esp_err_to_name(init));
    }
    for (;;) {
        int soc = init == ESP_OK ? bsp_battery_soc() : -1;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool changed = s_state.valid != (soc >= 0) || (soc >= 0 && s_state.percent != soc);
        s_state.valid = soc >= 0;
        if (soc >= 0) s_state.percent = soc;
        xSemaphoreGive(s_lock);
        if (changed) app_event_post(&(app_event_t){.type = APP_EVT_STATUS_UPDATE}, 0);
        if (soc < 0 && init == ESP_OK) ESP_LOGW(TAG, "CW2017 SOC read failed; keeping UI safe");
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000))) break;
    }
    xSemaphoreGive(s_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t battery_service_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_stopped = xSemaphoreCreateBinary();
    if (!s_lock || !s_stopped) return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(battery_task, "kp_battery", 2048, NULL, 2, &s_task, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t battery_service_stop(void)
{
    if (!s_task) return ESP_OK;
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(6000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    vTaskDelete(s_task);
    s_task = NULL;
    return ESP_OK;
}

void battery_service_snapshot(battery_snapshot_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}
