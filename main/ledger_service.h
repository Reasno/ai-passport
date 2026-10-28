#pragma once
#include "ledger_store.h"
#include "esp_err.h"

typedef struct {
    ledger_page_t page;
    bool initialized, loading, error;
} ledger_snapshot_t;
esp_err_t ledger_service_init(void);
void ledger_service_snapshot(ledger_snapshot_t *out);
bool ledger_service_request(uint64_t before_id);
void ledger_service_receive(const char *json, size_t len);
void ledger_service_tick(int64_t now_ms);
