#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t nvs_cache_init(void);
esp_err_t nvs_cache_load(void);
esp_err_t nvs_cache_save_balance(int32_t balance);
esp_err_t nvs_cache_save_tasks(const char *json);
esp_err_t nvs_cache_save_rewards(const char *json);
esp_err_t nvs_cache_save_last_sync(uint32_t value);
esp_err_t nvs_cache_load_game_stats(uint32_t *rps_wins, uint32_t *rps_losses,
                                    uint32_t *buzzer_wins, uint32_t *buzzer_losses);
esp_err_t nvs_cache_save_rps_stats(uint32_t wins, uint32_t losses);
esp_err_t nvs_cache_save_buzzer_stats(uint32_t wins, uint32_t losses);
esp_err_t nvs_cache_load_mole_stats(uint32_t *wins, uint32_t *losses);
esp_err_t nvs_cache_save_mole_stats(uint32_t wins, uint32_t losses);
esp_err_t nvs_cache_save_model(void);
