#include "ledger_service.h"
#include "app_events.h"
#include "mqtt_service.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static ledger_snapshot_t s_state;
static char s_request[LEDGER_REQUEST_LEN];
static uint64_t s_before_id;
static int64_t s_deadline;
/* Only the serialized MQTT receive callback uses this candidate. */
static ledger_page_t s_candidate;
static void changed(void) { app_event_post(&(app_event_t){.type = APP_EVT_LEDGER_CHANGED}, 0); }

esp_err_t ledger_service_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    return s_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}
void ledger_service_snapshot(ledger_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}
bool ledger_service_request(uint64_t before_id)
{
    char request[LEDGER_REQUEST_LEN];
    snprintf(request, sizeof(request), "ledger_%08lx_%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state.loading) { xSemaphoreGive(s_mutex); return false; }
    strcpy(s_request, request);
    s_before_id = before_id;
    s_state.loading = true;
    s_state.error = false;
    s_deadline = esp_timer_get_time() / 1000 + 5000;
    xSemaphoreGive(s_mutex);
    bool sent = mqtt_service_publish_ledger(request, before_id);
    if (!sent) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_state.loading = false;
        s_state.error = true;
        xSemaphoreGive(s_mutex);
    }
    changed();
    return sent;
}
void ledger_service_receive(const char *json, size_t len)
{
    char request[LEDGER_REQUEST_LEN];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool loading = s_state.loading;
    uint64_t before_id = s_before_id;
    strcpy(request, s_request);
    xSemaphoreGive(s_mutex);
    if (!loading) return;
    ledger_parse_result_t result = ledger_parse(json, len, CONFIG_ACTOR_CHILD_ID,
                                               request, before_id, &s_candidate);
    if (result == LEDGER_IGNORE) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state.loading && strcmp(request, s_request) == 0) {
        s_state.loading = false;
        s_state.error = result != LEDGER_OK;
        if (result == LEDGER_OK) {
            s_state.page = s_candidate;
            s_state.initialized = true;
        }
    }
    xSemaphoreGive(s_mutex);
    changed();
}
void ledger_service_tick(int64_t now_ms)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool expired = s_state.loading && now_ms >= s_deadline;
    if (expired) { s_state.loading = false; s_state.error = true; }
    xSemaphoreGive(s_mutex);
    if (expired) changed();
}
