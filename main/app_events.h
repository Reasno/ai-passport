#pragma once
#include "bsp_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_EVT_KEY,
    APP_EVT_MODEL_CHANGED,
    APP_EVT_CONNECTIVITY,
    APP_EVT_ACTION_RESULT,
    APP_EVT_LOTTERY_RESULT,
    APP_EVT_DATA_ERROR,
    APP_EVT_ACTION_TIMEOUT,
    APP_EVT_GAME_UPDATE,
    APP_EVT_FIND_RING,
    APP_EVT_FIND_ACK,
    APP_EVT_STATUS_UPDATE,
#if CONFIG_ENABLE_SCREENSHOT
    APP_EVT_DEBUG_PAGE,
#endif
} app_event_type_t;

#if CONFIG_ENABLE_SCREENSHOT
typedef enum {
    APP_DEBUG_PAGE_HOME,
    APP_DEBUG_PAGE_TASKS,
    APP_DEBUG_PAGE_REDEEM,
    APP_DEBUG_PAGE_LOTTERY,
    APP_DEBUG_PAGE_GAMES,
    APP_DEBUG_PAGE_FIND,
    APP_DEBUG_PAGE_RPS,
    APP_DEBUG_PAGE_BUZZER,
    APP_DEBUG_PAGE_BUZZER_ARMED,
    APP_DEBUG_PAGE_BUZZER_GO,
    APP_DEBUG_PAGE_BUZZER_RESULT,
    APP_DEBUG_PAGE_LOTTERY_SPIN,
    APP_DEBUG_PAGE_LOTTERY_RESULT,
    APP_DEBUG_PAGE_COUNT,
} app_debug_page_t;
#endif

typedef struct {
    app_event_type_t type;
    bsp_btn_t button;
    bsp_btn_ev_t button_event;
    bool ok;
    int32_t value;
    int64_t timestamp_ms;
    char text[128];
} app_event_t;

void app_events_init(void);
QueueHandle_t app_events_queue(void);
bool app_event_post(const app_event_t *event, TickType_t wait);
