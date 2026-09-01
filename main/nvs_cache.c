#include "nvs_cache.h"
#include "app_model.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

#define NS "kp_cache"
#define KEY_BALANCE "balance_i32"
#define KEY_TASKS "tasks_str"
#define KEY_REWARDS "rewards_str"
#define KEY_SYNC "last_sync_u32"
static const char *TAG = "kp_cache";
/* NVS serialization runs in the UI task; avoid a ~2 KB automatic object there. */
static app_model_snapshot_t s_cache_model;

esp_err_t nvs_cache_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t load_string(nvs_handle_t h, const char *key, bool tasks)
{
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, NULL, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK || len == 0 || len > APP_JSON_MAX + 1) return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    char *buf = malloc(len);
    if (!buf) return ESP_ERR_NO_MEM;
    err = nvs_get_str(h, key, buf, &len);
    if (err == ESP_OK) err = tasks ? app_model_parse_tasks(buf, len - 1, NULL) : app_model_parse_rewards(buf, len - 1, NULL);
    free(buf);
    return err;
}

esp_err_t nvs_cache_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) { ESP_LOGI(TAG, "无本地缓存"); return ESP_OK; }
    if (err != ESP_OK) return err;
    int32_t balance = 0;
    if (nvs_get_i32(h, KEY_BALANCE, &balance) == ESP_OK) {
        char json[48]; snprintf(json, sizeof(json), "{\"balance\":%ld}", (long)balance); app_model_parse_balance(json, strlen(json));
    }
    esp_err_t tasks_err = load_string(h, KEY_TASKS, true);
    esp_err_t rewards_err = load_string(h, KEY_REWARDS, false);
    uint32_t sync = 0;
    if (nvs_get_u32(h, KEY_SYNC, &sync) == ESP_OK) app_model_mark_sync_time(sync);
    nvs_close(h);
    ESP_LOGI(TAG, "缓存加载 balance=%ld tasks=%s rewards=%s sync=%lu", (long)balance, esp_err_to_name(tasks_err), esp_err_to_name(rewards_err), (unsigned long)sync);
    return (tasks_err == ESP_OK && rewards_err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

static esp_err_t open_write(nvs_handle_t *h) { return nvs_open(NS, NVS_READWRITE, h); }
esp_err_t nvs_cache_save_balance(int32_t balance) { nvs_handle_t h; esp_err_t e = open_write(&h); if (e == ESP_OK) { e = nvs_set_i32(h, KEY_BALANCE, balance); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); } ESP_LOGI(TAG, "写余额: %s", esp_err_to_name(e)); return e; }
static esp_err_t save_json(const char *key, const char *json) { if (!json) return ESP_ERR_INVALID_ARG; size_t len = strlen(json); if (len > APP_JSON_MAX) { ESP_LOGW(TAG, "%s 超过2KB，拒绝写入", key); return ESP_ERR_INVALID_SIZE; } nvs_handle_t h; esp_err_t e = open_write(&h); if (e == ESP_OK) { e = nvs_set_str(h, key, json); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); } ESP_LOGI(TAG, "写%s(%uB): %s", key, (unsigned)len, esp_err_to_name(e)); return e; }
esp_err_t nvs_cache_save_tasks(const char *json) { return save_json(KEY_TASKS, json); }
esp_err_t nvs_cache_save_rewards(const char *json) { return save_json(KEY_REWARDS, json); }
esp_err_t nvs_cache_save_last_sync(uint32_t value) { nvs_handle_t h; esp_err_t e = open_write(&h); if (e == ESP_OK) { e = nvs_set_u32(h, KEY_SYNC, value); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); } return e; }

esp_err_t nvs_cache_save_model(void)
{
    app_model_snapshot(&s_cache_model);
    const app_model_snapshot_t *model = &s_cache_model;
    esp_err_t result = ESP_OK;
    if (model->balance_valid) result = nvs_cache_save_balance(model->balance);
    cJSON *tasks = cJSON_CreateObject(); cJSON *task_array = cJSON_AddArrayToObject(tasks, "tasks");
    for (size_t i = 0; i < model->task_count; i++) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "task_id", model->tasks[i].id); cJSON_AddStringToObject(o, "display_name", model->tasks[i].name); cJSON_AddNumberToObject(o, "points", model->tasks[i].points); cJSON_AddBoolToObject(o, "completed_today", model->tasks[i].completed_today); cJSON_AddBoolToObject(o, "self_complete", model->tasks[i].self_complete); cJSON_AddItemToArray(task_array, o); }
    char *task_json = cJSON_PrintUnformatted(tasks); if (task_json) { esp_err_t e = nvs_cache_save_tasks(task_json); if (e != ESP_OK) result = e; cJSON_free(task_json); } cJSON_Delete(tasks);
    cJSON *rewards = cJSON_CreateObject(); cJSON *reward_array = cJSON_AddArrayToObject(rewards, "rewards");
    for (size_t i = 0; i < model->reward_count; i++) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "reward_id", model->rewards[i].id); cJSON_AddStringToObject(o, "display_name", model->rewards[i].name); cJSON_AddNumberToObject(o, "price", model->rewards[i].price); cJSON_AddBoolToObject(o, "enabled", model->rewards[i].enabled); cJSON_AddItemToArray(reward_array, o); }
    char *reward_json = cJSON_PrintUnformatted(rewards); if (reward_json) { esp_err_t e = nvs_cache_save_rewards(reward_json); if (e != ESP_OK) result = e; cJSON_free(reward_json); } cJSON_Delete(rewards);
    esp_err_t sync_err = nvs_cache_save_last_sync(model->last_sync); if (sync_err != ESP_OK) result = sync_err;
    return result;
}
