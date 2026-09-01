#include "app_events.h"
#include "esp_log.h"

static const char *TAG = "kp_events";
static QueueHandle_t s_queue;

void app_events_init(void)
{
    if (!s_queue) s_queue = xQueueCreate(16, sizeof(app_event_t));
    if (!s_queue) ESP_LOGE(TAG, "事件队列创建失败");
}

QueueHandle_t app_events_queue(void) { return s_queue; }

bool app_event_post(const app_event_t *event, TickType_t wait)
{
    if (!s_queue || !event) return false;
    if (xQueueSend(s_queue, event, wait) != pdTRUE) {
        ESP_LOGW(TAG, "事件队列已满 type=%d", event->type);
        return false;
    }
    return true;
}
