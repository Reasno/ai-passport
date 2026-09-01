#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_MAX_TASKS 8
#define APP_MAX_REWARDS 8
#define APP_ID_LEN 40
#define APP_NAME_LEN 64
#define APP_MESSAGE_LEN 128
#define APP_JSON_MAX 2048

typedef struct {
    char id[APP_ID_LEN];
    char name[APP_NAME_LEN];
    int32_t points;
    bool completed_today;
    bool self_complete;
} app_task_t;

typedef struct {
    char id[APP_ID_LEN];
    char name[APP_NAME_LEN];
    int32_t price;
    bool enabled;
} app_reward_t;

typedef enum { APP_PENDING_NONE, APP_PENDING_TASK, APP_PENDING_REDEEM, APP_PENDING_LOTTERY } app_pending_type_t;

typedef struct {
    int32_t balance;
    bool balance_valid;
    app_task_t tasks[APP_MAX_TASKS];
    size_t task_count;
    app_reward_t rewards[APP_MAX_REWARDS];
    size_t reward_count;
    bool wifi_online;
    bool mqtt_online;
    bool synced_balance;
    bool synced_tasks;
    bool synced_rewards;
    uint32_t last_sync;
    app_pending_type_t pending_type;
    char pending_key[64];
    char pending_item_id[APP_ID_LEN];
    int64_t pending_deadline_ms;
    bool lottery_ready;
    char lottery_prize_id[APP_ID_LEN];
    char lottery_label[APP_NAME_LEN];
    char lottery_message[APP_MESSAGE_LEN];
    int32_t lottery_points_delta;
} app_model_snapshot_t;

void app_model_init(void);
void app_model_snapshot(app_model_snapshot_t *out);
esp_err_t app_model_parse_balance(const char *json, size_t len);
esp_err_t app_model_parse_tasks(const char *json, size_t len, size_t *count);
esp_err_t app_model_parse_rewards(const char *json, size_t len, size_t *count);
esp_err_t app_model_replace_tasks(const app_task_t *tasks, size_t count);
esp_err_t app_model_replace_rewards(const app_reward_t *rewards, size_t count);
esp_err_t app_model_parse_lottery(const char *json, size_t len);
void app_model_set_connectivity(bool wifi, bool mqtt);
void app_model_mark_sync_time(uint32_t value);
void app_model_reset_online_sync(void);
void app_model_begin_pending(app_pending_type_t type, const char *key, const char *item_id, int64_t deadline_ms);
void app_model_finish_pending(void);
bool app_model_pending_matches(const char *key, app_pending_type_t *type, char *item_id, size_t item_id_size);
void app_model_apply_action_balance(int32_t balance, bool valid);
bool app_model_is_synced(void);
