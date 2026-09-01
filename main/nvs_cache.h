#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t nvs_cache_init(void);
esp_err_t nvs_cache_load(void);
esp_err_t nvs_cache_save_balance(int32_t balance);
esp_err_t nvs_cache_save_tasks(const char *json);
esp_err_t nvs_cache_save_rewards(const char *json);
esp_err_t nvs_cache_save_last_sync(uint32_t value);
esp_err_t nvs_cache_save_model(void);
