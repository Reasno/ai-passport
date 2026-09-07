#pragma once
#include "app_model.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool available;
    bool active;
    bool recording;
    bool uploading;
    bool waiting_review;
    uint32_t elapsed_ms;
    uint32_t max_duration_ms;
    char task_id[APP_ID_LEN];
    char task_name[APP_NAME_LEN];
} evidence_service_snapshot_t;

esp_err_t evidence_service_start(void);
bool evidence_service_available(void);
bool evidence_service_begin(const char *task_id, const char *task_name);
void evidence_service_stop(void);
void evidence_service_finish(void);
void evidence_service_snapshot(evidence_service_snapshot_t *out);
