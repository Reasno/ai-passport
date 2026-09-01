#include "app_model.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

static app_model_snapshot_t s_model;
static SemaphoreHandle_t s_mutex;

static void copy_string(char *dst, size_t size, const cJSON *item)
{
    if (size == 0) return;
    dst[0] = 0;
    if (cJSON_IsString(item) && item->valuestring) strlcpy(dst, item->valuestring, size);
}
static void lock(void) { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mutex); }

void app_model_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    memset(&s_model, 0, sizeof(s_model));
}

void app_model_snapshot(app_model_snapshot_t *out)
{
    if (!out || !s_mutex) return;
    lock(); *out = s_model; unlock();
}

esp_err_t app_model_parse_balance(const char *json, size_t len)
{
    if (!json || !len || len > APP_JSON_MAX) return ESP_ERR_INVALID_SIZE;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "balance");
    if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItemCaseSensitive(root, "balance_after");
    if (!cJSON_IsNumber(item)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    lock(); s_model.balance = item->valueint; s_model.balance_valid = true; s_model.synced_balance = true; unlock();
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t app_model_replace_tasks(const app_task_t *tasks, size_t count)
{
    if ((!tasks && count) || count > APP_MAX_TASKS) return ESP_ERR_INVALID_ARG;
    lock();
    memset(s_model.tasks, 0, sizeof(s_model.tasks));
    if (count) memcpy(s_model.tasks, tasks, count * sizeof(*tasks));
    s_model.task_count = count;
    s_model.synced_tasks = true;
    unlock();
    return ESP_OK;
}

esp_err_t app_model_replace_rewards(const app_reward_t *rewards, size_t count)
{
    if ((!rewards && count) || count > APP_MAX_REWARDS) return ESP_ERR_INVALID_ARG;
    lock();
    memset(s_model.rewards, 0, sizeof(s_model.rewards));
    if (count) memcpy(s_model.rewards, rewards, count * sizeof(*rewards));
    s_model.reward_count = count;
    s_model.synced_rewards = true;
    unlock();
    return ESP_OK;
}

esp_err_t app_model_parse_tasks(const char *json, size_t len, size_t *parsed_count)
{
    if (!json || !len || len > APP_JSON_MAX) return ESP_ERR_INVALID_SIZE;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON *array = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "tasks");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    app_task_t temp[APP_MAX_TASKS] = {0};
    size_t count = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, array) {
        if (count >= APP_MAX_TASKS) break;
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "task_id");
        if (!cJSON_IsString(id)) id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "display_name");
        if (!cJSON_IsString(name)) name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        cJSON *points = cJSON_GetObjectItemCaseSensitive(entry, "points");
        if (!cJSON_IsNumber(points)) points = cJSON_GetObjectItemCaseSensitive(entry, "p");
        if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsNumber(points)) continue;
        copy_string(temp[count].id, sizeof(temp[count].id), id);
        copy_string(temp[count].name, sizeof(temp[count].name), name);
        temp[count].points = points->valueint;
        cJSON *done = cJSON_GetObjectItemCaseSensitive(entry, "completed_today");
        if (!done) done = cJSON_GetObjectItemCaseSensitive(entry, "done");
        temp[count].completed_today = cJSON_IsTrue(done);
        temp[count].self_complete = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "self_complete"));
        count++;
    }
    app_model_replace_tasks(temp, count);
    if (parsed_count) *parsed_count = count;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t app_model_parse_rewards(const char *json, size_t len, size_t *parsed_count)
{
    if (!json || !len || len > APP_JSON_MAX) return ESP_ERR_INVALID_SIZE;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON *array = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "rewards");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    app_reward_t temp[APP_MAX_REWARDS] = {0};
    size_t count = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, array) {
        if (count >= APP_MAX_REWARDS) break;
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "reward_id");
        if (!cJSON_IsString(id)) id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "display_name");
        if (!cJSON_IsString(name)) name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        cJSON *price = cJSON_GetObjectItemCaseSensitive(entry, "price");
        if (!cJSON_IsNumber(price)) price = cJSON_GetObjectItemCaseSensitive(entry, "points");
        if (!cJSON_IsString(id) || !cJSON_IsNumber(price)) continue;
        copy_string(temp[count].id, sizeof(temp[count].id), id);
        if (cJSON_IsString(name)) copy_string(temp[count].name, sizeof(temp[count].name), name);
        else strlcpy(temp[count].name, id->valuestring, sizeof(temp[count].name));
        temp[count].price = price->valueint;
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(entry, "enabled");
        temp[count].enabled = enabled == NULL || cJSON_IsTrue(enabled);
        count++;
    }
    app_model_replace_rewards(temp, count);
    if (parsed_count) *parsed_count = count;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t app_model_parse_lottery(const char *json, size_t len)
{
    if (!json || !len || len > APP_JSON_MAX) return ESP_ERR_INVALID_SIZE;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    cJSON *prize_id = cJSON_GetObjectItemCaseSensitive(root, "prize_reward_id");
    if (!cJSON_IsString(prize_id)) prize_id = cJSON_GetObjectItemCaseSensitive(root, "reward_id");
    if (!cJSON_IsString(prize_id)) prize_id = cJSON_GetObjectItemCaseSensitive(root, "prize_id");
    if (cJSON_IsFalse(ok) || !cJSON_IsString(prize_id)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    lock();
    copy_string(s_model.lottery_prize_id, sizeof(s_model.lottery_prize_id), prize_id);
    cJSON *label = cJSON_GetObjectItemCaseSensitive(root, "display_name");
    if (!cJSON_IsString(label)) label = cJSON_GetObjectItemCaseSensitive(root, "prize_label");
    copy_string(s_model.lottery_label, sizeof(s_model.lottery_label), label);
    cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsString(message)) message = cJSON_GetObjectItemCaseSensitive(root, "error");
    copy_string(s_model.lottery_message, sizeof(s_model.lottery_message), message);
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "points_awarded");
    if (!cJSON_IsNumber(delta)) delta = cJSON_GetObjectItemCaseSensitive(root, "points_delta");
    s_model.lottery_points_delta = cJSON_IsNumber(delta) ? delta->valueint : 0;
    cJSON *balance = cJSON_GetObjectItemCaseSensitive(root, "balance_after");
    if (!cJSON_IsNumber(balance)) balance = cJSON_GetObjectItemCaseSensitive(root, "balance");
    if (cJSON_IsNumber(balance)) { s_model.balance = balance->valueint; s_model.balance_valid = true; }
    s_model.lottery_ready = true;
    s_model.pending_type = APP_PENDING_NONE;
    s_model.pending_key[0] = 0;
    unlock();
    cJSON_Delete(root);
    return ESP_OK;
}

void app_model_set_connectivity(bool wifi, bool mqtt) { lock(); s_model.wifi_online = wifi; s_model.mqtt_online = mqtt; unlock(); }
void app_model_mark_sync_time(uint32_t value) { lock(); s_model.last_sync = value; unlock(); }
void app_model_reset_online_sync(void) { lock(); s_model.synced_balance = false; s_model.synced_tasks = false; s_model.synced_rewards = false; unlock(); }
void app_model_begin_pending(app_pending_type_t type, const char *key, const char *item_id, int64_t deadline_ms)
{
    lock(); s_model.pending_type = type; strlcpy(s_model.pending_key, key ? key : "", sizeof(s_model.pending_key)); strlcpy(s_model.pending_item_id, item_id ? item_id : "", sizeof(s_model.pending_item_id)); s_model.pending_deadline_ms = deadline_ms; s_model.lottery_ready = false; unlock();
}
void app_model_finish_pending(void) { lock(); s_model.pending_type = APP_PENDING_NONE; s_model.pending_key[0] = 0; s_model.pending_item_id[0] = 0; s_model.pending_deadline_ms = 0; unlock(); }
bool app_model_pending_matches(const char *key, app_pending_type_t *type, char *item_id, size_t item_id_size)
{
    bool match; lock(); match = key && s_model.pending_type != APP_PENDING_NONE && strcmp(key, s_model.pending_key) == 0; if (match) { if (type) *type = s_model.pending_type; if (item_id && item_id_size) strlcpy(item_id, s_model.pending_item_id, item_id_size); } unlock(); return match;
}
void app_model_apply_action_balance(int32_t balance, bool valid) { if (!valid) return; lock(); s_model.balance = balance; s_model.balance_valid = true; unlock(); }
bool app_model_is_synced(void) { bool value; lock(); value = s_model.synced_balance && s_model.synced_tasks && s_model.synced_rewards; unlock(); return value; }
