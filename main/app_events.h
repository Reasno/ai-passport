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
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    bsp_btn_t button;
    bsp_btn_ev_t button_event;
    bool ok;
    int32_t value;
    char text[128];
} app_event_t;

void app_events_init(void);
QueueHandle_t app_events_queue(void);
bool app_event_post(const app_event_t *event, TickType_t wait);
