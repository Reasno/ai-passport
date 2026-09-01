#include "mqtt_service.h"
#include "app_events.h"
#include "app_model.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>

#define TOPIC_STATE "kids_points/state/shared"
#define TOPIC_REWARDS "kids_points/rewards"
#define TOPIC_RESULT "kids_points/action/result/+"
#define TOPIC_ACTION_ERROR "kids_points/action/error"
#define TOPIC_LOTTERY_SHARED "kids_points/lottery_result/shared"
#define TOPIC_COMPLETE_V3 "kids_points/action/complete"
#define TOPIC_COMPLETE_LEGACY "kids_points/action/complete_task"
#define TOPIC_REDEEM "kids_points/action/redeem"
#define ACTION_TIMEOUT_MS 5000
#define CONTENT_KEY_LEN 17

typedef enum { PROTOCOL_UNKNOWN, PROTOCOL_LEGACY, PROTOCOL_V3 } protocol_mode_t;
typedef struct {
    char key[CONTENT_KEY_LEN];
    bool received;
    app_task_t value;
} task_item_slot_t;
typedef struct {
    char key[CONTENT_KEY_LEN];
    bool received;
    app_reward_t value;
} reward_item_slot_t;

static const char *TAG = "kp_mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_client_started;
static char s_uri[96], s_tasks_topic[96], s_tasks_items_prefix[112], s_rewards_items_prefix[112];
static char s_lottery_child_topic[96], s_presence_topic[96], s_client_id[64], s_device_id[64];
static char s_find_ring_topic[112], s_find_ack_topic[112];
static char s_rx_topic[128];
static char s_rx_data[APP_JSON_MAX + 1];
static int s_rx_total;
static bool s_rx_drop;
static protocol_mode_t s_tasks_mode = PROTOCOL_UNKNOWN;
static protocol_mode_t s_rewards_mode = PROTOCOL_UNKNOWN;
static task_item_slot_t s_task_slots[APP_MAX_TASKS];
static reward_item_slot_t s_reward_slots[APP_MAX_REWARDS];
static char s_manifest_keys[APP_MAX_TASKS][CONTENT_KEY_LEN];
static app_task_t s_assembled_tasks[APP_MAX_TASKS];
static app_reward_t s_assembled_rewards[APP_MAX_REWARDS];
/* Keep ~2 KB snapshots off the 4 KB MQTT/UI stacks. Separate storage avoids
 * a race between the MQTT event task and UI-triggered publish calls. */
static app_model_snapshot_t s_mqtt_event_model;
static app_model_snapshot_t s_mqtt_publish_model;

static void post_error(const char *message)
{
    app_event_t event = {.type = APP_EVT_DATA_ERROR};
    strlcpy(event.text, message, sizeof(event.text));
    app_event_post(&event, 0);
}
static bool topic_is(const char *topic) { return strcmp(s_rx_topic, topic) == 0; }
static const char *json_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}
static bool json_bool(cJSON *root, const char *name, bool *present)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    *present = cJSON_IsBool(item);
    return cJSON_IsTrue(item);
}
static const char *mode_name(protocol_mode_t mode)
{
    return mode == PROTOCOL_V3 ? "v3-manifest" : mode == PROTOCOL_LEGACY ? "legacy-array" : "unknown";
}
static bool valid_content_key(const char *key)
{
    if (!key || strlen(key) != 16) return false;
    for (size_t i = 0; i < 16; i++) {
        char c = key[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
static void make_item_topic(char *out, size_t size, const char *prefix, const char *key)
{
    snprintf(out, size, "%s%s", prefix, key);
}
static bool key_in_list(const char *key, char keys[][CONTENT_KEY_LEN], size_t count)
{
    for (size_t i = 0; i < count; i++) if (strcmp(key, keys[i]) == 0) return true;
    return false;
}
static void sha256_short(const char *input, char out[CONTENT_KEY_LEN])
{
    unsigned char digest[32];
    mbedtls_sha256((const unsigned char *)input, strlen(input), digest, 0);
    for (size_t i = 0; i < 8; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
    out[16] = 0;
}
static bool pending_matches_result(const char *key, app_pending_type_t *type, char *item_id, size_t item_size)
{
    if (!key) return false;
    app_model_snapshot(&s_mqtt_event_model);
    if (app_model_pending_matches(key, type, item_id, item_size)) return true;
    if (s_mqtt_event_model.pending_type == APP_PENDING_NONE || !s_mqtt_event_model.pending_key[0]) return false;
    char correlation[CONTENT_KEY_LEN];
    sha256_short(s_mqtt_event_model.pending_key, correlation);
    if (strcmp(key, correlation) != 0) return false;
    if (type) *type = s_mqtt_event_model.pending_type;
    if (item_id && item_size) strlcpy(item_id, s_mqtt_event_model.pending_item_id, item_size);
    return true;
}

static void model_changed(bool save)
{
    uint32_t sync = (uint32_t)(esp_timer_get_time() / 1000000);
    app_model_mark_sync_time(sync);
    app_event_post(&(app_event_t){.type = APP_EVT_MODEL_CHANGED, .value = save}, 0);
}

static void assemble_tasks_if_complete(void)
{
    size_t count = 0;
    for (size_t i = 0; i < APP_MAX_TASKS && s_task_slots[i].key[0]; i++) {
        if (!s_task_slots[i].received) return;
        s_assembled_tasks[count++] = s_task_slots[i].value;
    }
    app_model_replace_tasks(s_assembled_tasks, count);
    ESP_LOGI(TAG, "tasks assembled=%u protocol=%s", (unsigned)count, mode_name(s_tasks_mode));
    model_changed(true);
}
static void assemble_rewards_if_complete(void)
{
    size_t count = 0;
    for (size_t i = 0; i < APP_MAX_REWARDS && s_reward_slots[i].key[0]; i++) {
        if (!s_reward_slots[i].received) return;
        s_assembled_rewards[count++] = s_reward_slots[i].value;
    }
    app_model_replace_rewards(s_assembled_rewards, count);
    ESP_LOGI(TAG, "rewards assembled=%u protocol=%s", (unsigned)count, mode_name(s_rewards_mode));
    model_changed(true);
}

static bool parse_manifest(cJSON *root, bool tasks)
{
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    if (!cJSON_IsNumber(version) || version->valueint != 3 || !cJSON_IsObject(active)) return false;
    memset(s_manifest_keys, 0, sizeof(s_manifest_keys));
    size_t count = 0;
    size_t active_count = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, active) {
        active_count++;
        if (count >= APP_MAX_TASKS) continue;
        if (cJSON_IsString(entry) && valid_content_key(entry->valuestring) && !key_in_list(entry->valuestring, s_manifest_keys, count)) {
            strlcpy(s_manifest_keys[count++], entry->valuestring, CONTENT_KEY_LEN);
        }
    }
    const char *prefix = tasks ? s_tasks_items_prefix : s_rewards_items_prefix;
    char topic[128];
    if (tasks) {
        for (size_t i = 0; i < APP_MAX_TASKS && s_task_slots[i].key[0]; i++) {
            if (!key_in_list(s_task_slots[i].key, s_manifest_keys, count)) {
                make_item_topic(topic, sizeof(topic), prefix, s_task_slots[i].key);
                esp_mqtt_client_unsubscribe(s_client, topic);
            }
        }
        memset(s_task_slots, 0, sizeof(s_task_slots));
        for (size_t i = 0; i < count; i++) strlcpy(s_task_slots[i].key, s_manifest_keys[i], CONTENT_KEY_LEN);
        s_tasks_mode = PROTOCOL_V3;
    } else {
        for (size_t i = 0; i < APP_MAX_REWARDS && s_reward_slots[i].key[0]; i++) {
            if (!key_in_list(s_reward_slots[i].key, s_manifest_keys, count)) {
                make_item_topic(topic, sizeof(topic), prefix, s_reward_slots[i].key);
                esp_mqtt_client_unsubscribe(s_client, topic);
            }
        }
        memset(s_reward_slots, 0, sizeof(s_reward_slots));
        for (size_t i = 0; i < count; i++) strlcpy(s_reward_slots[i].key, s_manifest_keys[i], CONTENT_KEY_LEN);
        s_rewards_mode = PROTOCOL_V3;
    }
    for (size_t i = 0; i < count; i++) {
        make_item_topic(topic, sizeof(topic), prefix, s_manifest_keys[i]);
        esp_mqtt_client_subscribe(s_client, topic, 1);
    }
    if (active_count > APP_MAX_TASKS) ESP_LOGW(TAG, "%s manifest active=%u，设备最多加载%u项", tasks ? "tasks" : "rewards", (unsigned)active_count, APP_MAX_TASKS);
    ESP_LOGI(TAG, "%s manifest count=%u selected=%u protocol=%s", tasks ? "tasks" : "rewards", (unsigned)active_count, (unsigned)count, mode_name(PROTOCOL_V3));
    if (tasks) assemble_tasks_if_complete(); else assemble_rewards_if_complete();
    return true;
}

static esp_err_t parse_task_item(const char *key, cJSON *root)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "task");
    if (!cJSON_IsObject(item)) return ESP_ERR_INVALID_ARG;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    cJSON *points = cJSON_GetObjectItemCaseSensitive(item, "p");
    if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsNumber(points)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < APP_MAX_TASKS && s_task_slots[i].key[0]; i++) {
        if (strcmp(key, s_task_slots[i].key) != 0) continue;
        app_task_t *task = &s_task_slots[i].value;
        memset(task, 0, sizeof(*task));
        strlcpy(task->id, id->valuestring, sizeof(task->id));
        strlcpy(task->name, name->valuestring, sizeof(task->name));
        task->points = points->valueint;
        task->completed_today = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "done"));
        task->self_complete = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "self_complete"));
        s_task_slots[i].received = true;
        ESP_LOGI(TAG, "task item key=%s id=%s", key, task->id);
        assemble_tasks_if_complete();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}
static esp_err_t parse_reward_item(const char *key, cJSON *root)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "reward");
    if (!cJSON_IsObject(item)) return ESP_ERR_INVALID_ARG;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    cJSON *price = cJSON_GetObjectItemCaseSensitive(item, "price");
    if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsNumber(price)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < APP_MAX_REWARDS && s_reward_slots[i].key[0]; i++) {
        if (strcmp(key, s_reward_slots[i].key) != 0) continue;
        app_reward_t *reward = &s_reward_slots[i].value;
        memset(reward, 0, sizeof(*reward));
        strlcpy(reward->id, id->valuestring, sizeof(reward->id));
        strlcpy(reward->name, name->valuestring, sizeof(reward->name));
        reward->price = price->valueint;
        reward->enabled = true;
        s_reward_slots[i].received = true;
        ESP_LOGI(TAG, "reward item key=%s id=%s type=%s", key, reward->id, json_string(item, "type") ? json_string(item, "type") : "(none)");
        assemble_rewards_if_complete();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static void handle_result(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) { post_error("结果数据格式错误，已保留旧数据"); return; }
    const char *key = json_string(root, "correlation_id");
    if (!key) key = json_string(root, "request_id");
    if (!key) key = json_string(root, "idempotency_key");
    app_pending_type_t pending;
    char item_id[APP_ID_LEN];
    if (!pending_matches_result(key, &pending, item_id, sizeof(item_id))) {
        ESP_LOGW(TAG, "忽略非当前结果 key=%s", key ? key : "(null)"); cJSON_Delete(root); return;
    }
    bool ok_present = false;
    bool ok = json_bool(root, "ok", &ok_present);
    if (!ok_present) {
        const char *status = json_string(root, "status");
        ok = status && (strcmp(status, "ok") == 0 || strcmp(status, "success") == 0);
    }
    cJSON *balance = cJSON_GetObjectItemCaseSensitive(root, "balance_after");
    if (!cJSON_IsNumber(balance)) balance = cJSON_GetObjectItemCaseSensitive(root, "balance");
    if (cJSON_IsNumber(balance)) app_model_apply_action_balance(balance->valueint, true);
    const char *message = json_string(root, "message");
    if (!message) message = json_string(root, "error");
    if (!message) message = json_string(root, "error_code");
    app_event_t event = {.type = APP_EVT_ACTION_RESULT, .ok = ok, .value = pending};
    strlcpy(event.text, message ? message : (ok ? "操作成功" : "操作失败，请重试"), sizeof(event.text));
    bool wait_lottery = ok && pending == APP_PENDING_REDEEM && strcmp(item_id, "lottery_ticket") == 0;
    if (wait_lottery) app_model_begin_pending(APP_PENDING_LOTTERY, s_mqtt_event_model.pending_key, item_id, esp_timer_get_time() / 1000 + ACTION_TIMEOUT_MS);
    else app_model_finish_pending();
    cJSON *already = cJSON_GetObjectItemCaseSensitive(root, "already_processed");
    ESP_LOGI(TAG, "action result key=%s ok=%d already_processed=%d lottery_wait=%d", key, ok, cJSON_IsTrue(already), wait_lottery);
    app_event_post(&event, 0);
    cJSON_Delete(root);
}

static void handle_lottery(void)
{
    cJSON *root = cJSON_ParseWithLength(s_rx_data, s_rx_total);
    if (!root) { post_error("抽奖结果格式错误"); return; }
    const char *actor = json_string(root, "actor_child_id");
    if (!actor) actor = json_string(root, "child_id");
    bool child_topic = topic_is(s_lottery_child_topic);
    if (!child_topic && actor && strcmp(actor, CONFIG_ACTOR_CHILD_ID) != 0) {
        ESP_LOGI(TAG, "忽略其他孩子的抽奖结果"); cJSON_Delete(root); return;
    }
    const char *key = json_string(root, "correlation_id");
    if (!key) key = json_string(root, "request_id");
    if (!key) key = json_string(root, "idempotency_key");
    app_model_snapshot(&s_mqtt_event_model);
    if (s_mqtt_event_model.pending_type != APP_PENDING_LOTTERY) {
        ESP_LOGW(TAG, "忽略未由抽奖券兑换触发的抽奖结果"); cJSON_Delete(root); return;
    }
    app_pending_type_t type;
    if ((key && !pending_matches_result(key, &type, NULL, 0)) || (!key && child_topic)) {
        ESP_LOGW(TAG, "忽略非当前抽奖结果 key=%s", key ? key : "(missing)"); cJSON_Delete(root); return;
    }
    cJSON_Delete(root);
    esp_err_t err = app_model_parse_lottery(s_rx_data, s_rx_total);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "lottery result protocol=%s key=%s", child_topic ? "v3-child" : "legacy-shared", key ? key : "(none)");
        app_event_post(&(app_event_t){.type = APP_EVT_LOTTERY_RESULT, .ok = true}, 0);
    } else {
        ESP_LOGW(TAG, "抽奖结果解析失败: %s", esp_err_to_name(err));
        post_error("抽奖结果格式错误");
    }
}

static void handle_payload(void)
{
    if (topic_is(s_find_ring_topic) || topic_is(s_find_ack_topic)) {
        cJSON *root = cJSON_ParseWithLength(s_rx_data, s_rx_total);
        if (!root) { ESP_LOGW(TAG, "Find payload invalid JSON"); return; }
        const char *sender = json_string(root, "sender");
        if (!sender) sender = json_string(root, "device_id");
        if (sender && strcmp(sender, CONFIG_KIDS_DEVICE_ID) != 0) {
            app_event_t event = {.type = topic_is(s_find_ring_topic) ? APP_EVT_FIND_RING : APP_EVT_FIND_ACK};
            strlcpy(event.text, sender, sizeof(event.text));
            app_event_post(&event, 0);
            ESP_LOGI(TAG, "Find %s from=%s", topic_is(s_find_ring_topic) ? "ring" : "ack", sender);
        }
        cJSON_Delete(root);
        return;
    }
    if (strncmp(s_rx_topic, "kids_points/action/result/", 27) == 0 || topic_is(TOPIC_ACTION_ERROR)) {
        handle_result(s_rx_data, s_rx_total); return;
    }
    if (topic_is(TOPIC_LOTTERY_SHARED) || topic_is(s_lottery_child_topic)) { handle_lottery(); return; }

    cJSON *root = cJSON_ParseWithLength(s_rx_data, s_rx_total);
    if (!root) { post_error("收到的数据格式错误，已保留旧数据"); return; }
    if (topic_is(s_tasks_topic) || topic_is(TOPIC_REWARDS)) {
        bool tasks = topic_is(s_tasks_topic);
        if (parse_manifest(root, tasks)) { cJSON_Delete(root); return; }
        cJSON_Delete(root);
        size_t parsed = 0;
        esp_err_t err = tasks ? app_model_parse_tasks(s_rx_data, s_rx_total, &parsed) : app_model_parse_rewards(s_rx_data, s_rx_total, &parsed);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "root解析失败 topic=%s err=%s", s_rx_topic, esp_err_to_name(err));
            post_error("收到的数据格式错误，已保留旧数据"); return;
        }
        if (tasks) s_tasks_mode = PROTOCOL_LEGACY; else s_rewards_mode = PROTOCOL_LEGACY;
        ESP_LOGI(TAG, "%s legacy snapshot count=%u protocol=%s", tasks ? "tasks" : "rewards", (unsigned)parsed, mode_name(PROTOCOL_LEGACY));
        model_changed(true);
        return;
    }
    if (topic_is(TOPIC_STATE)) {
        cJSON_Delete(root);
        esp_err_t err = app_model_parse_balance(s_rx_data, s_rx_total);
        if (err == ESP_OK) { ESP_LOGI(TAG, "state snapshot parsed"); model_changed(true); }
        else { ESP_LOGW(TAG, "state解析失败: %s", esp_err_to_name(err)); post_error("余额数据格式错误，已保留旧数据"); }
        return;
    }
    const char *key = NULL;
    bool task_item = strncmp(s_rx_topic, s_tasks_items_prefix, strlen(s_tasks_items_prefix)) == 0;
    bool reward_item = strncmp(s_rx_topic, s_rewards_items_prefix, strlen(s_rewards_items_prefix)) == 0;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (task_item) { key = s_rx_topic + strlen(s_tasks_items_prefix); err = parse_task_item(key, root); }
    else if (reward_item) { key = s_rx_topic + strlen(s_rewards_items_prefix); err = parse_reward_item(key, root); }
    cJSON_Delete(root);
    if ((task_item || reward_item) && err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "item解析失败 key=%s err=%s", key ? key : "", esp_err_to_name(err));
        post_error("任务或奖励数据格式错误，已保留旧数据");
    }
}

static void subscribe_known_items(void)
{
    char topic[128];
    for (size_t i = 0; i < APP_MAX_TASKS && s_task_slots[i].key[0]; i++) {
        make_item_topic(topic, sizeof(topic), s_tasks_items_prefix, s_task_slots[i].key);
        esp_mqtt_client_subscribe(s_client, topic, 1);
    }
    for (size_t i = 0; i < APP_MAX_REWARDS && s_reward_slots[i].key[0]; i++) {
        make_item_topic(topic, sizeof(topic), s_rewards_items_prefix, s_reward_slots[i].key);
        esp_mqtt_client_subscribe(s_client, topic, 1);
    }
}
static void subscribe_all_roots(esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_subscribe(client, TOPIC_STATE, 1);
    esp_mqtt_client_subscribe(client, s_tasks_topic, 1);
    esp_mqtt_client_subscribe(client, TOPIC_REWARDS, 1);
    esp_mqtt_client_subscribe(client, TOPIC_RESULT, 1);
    esp_mqtt_client_subscribe(client, TOPIC_ACTION_ERROR, 1);
    esp_mqtt_client_subscribe(client, TOPIC_LOTTERY_SHARED, 1);
    esp_mqtt_client_subscribe(client, s_lottery_child_topic, 1);
    esp_mqtt_client_subscribe(client, s_find_ring_topic, 1);
    esp_mqtt_client_subscribe(client, s_find_ack_topic, 1);
    subscribe_known_items();
}

static void mqtt_event(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args; (void)base;
    esp_mqtt_event_handle_t event = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        app_model_set_connectivity(true, true);
        subscribe_all_roots(event->client);
        esp_mqtt_client_publish(event->client, s_presence_topic, "1", 0, 1, true);
        ESP_LOGI(TAG, "MQTT连接，已订阅root/result/lottery及已知items");
        app_event_post(&(app_event_t){.type = APP_EVT_CONNECTIVITY}, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        app_model_snapshot(&s_mqtt_event_model); app_model_set_connectivity(s_mqtt_event_model.wifi_online, false);
        ESP_LOGW(TAG, "MQTT断开，等待自动重连");
        app_event_post(&(app_event_t){.type = APP_EVT_CONNECTIVITY}, 0);
        break;
    case MQTT_EVENT_DATA:
        if (event->current_data_offset == 0) {
            s_rx_drop = event->total_data_len <= 0 || event->total_data_len > APP_JSON_MAX || event->topic_len <= 0 || event->topic_len >= (int)sizeof(s_rx_topic);
            s_rx_total = event->total_data_len;
            if (!s_rx_drop) { memcpy(s_rx_topic, event->topic, event->topic_len); s_rx_topic[event->topic_len] = 0; }
            else post_error("MQTT数据超过2KB，已忽略");
        }
        if (!s_rx_drop && event->current_data_offset + event->data_len <= APP_JSON_MAX) {
            memcpy(s_rx_data + event->current_data_offset, event->data, event->data_len);
            if (event->current_data_offset + event->data_len == s_rx_total) { s_rx_data[s_rx_total] = 0; handle_payload(); }
        }
        break;
    case MQTT_EVENT_ERROR: ESP_LOGW(TAG, "MQTT传输错误"); break;
    default: break;
    }
}

esp_err_t mqtt_service_start(void)
{
    if (s_started) return ESP_OK;
    snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);
    snprintf(s_tasks_topic, sizeof(s_tasks_topic), "kids_points/tasks/%s", CONFIG_ACTOR_CHILD_ID);
    snprintf(s_tasks_items_prefix, sizeof(s_tasks_items_prefix), "%s/items/", s_tasks_topic);
    snprintf(s_rewards_items_prefix, sizeof(s_rewards_items_prefix), "%s/items/", TOPIC_REWARDS);
    snprintf(s_lottery_child_topic, sizeof(s_lottery_child_topic), "kids_points/lottery_result/%s", CONFIG_ACTOR_CHILD_ID);
    strlcpy(s_device_id, CONFIG_KIDS_DEVICE_ID, sizeof(s_device_id));
    snprintf(s_find_ring_topic, sizeof(s_find_ring_topic), "kids_points/game/find/%s/ring", CONFIG_KIDS_DEVICE_ID);
    snprintf(s_find_ack_topic, sizeof(s_find_ack_topic), "kids_points/game/find/%s/ack", CONFIG_KIDS_DEVICE_ID);
    snprintf(s_client_id, sizeof(s_client_id), "ai_passport_%s_001", CONFIG_ACTOR_CHILD_ID);
    snprintf(s_presence_topic, sizeof(s_presence_topic), "kids_points/device/%s/online", s_device_id);
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.client_id = s_client_id,
        .session.last_will = {.topic = s_presence_topic, .msg = "0", .qos = 1, .retain = 1},
        .session.keepalive = 30,
        .network.timeout_ms = 5000,
        .network.reconnect_timeout_ms = 2000,
        .task.priority = 5,
        .task.stack_size = 4096,
        .buffer.size = 2048,
        .buffer.out_size = 1024,
        .outbox.limit = 4096,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_ERR_NO_MEM;
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    s_started = true;
    return ESP_OK;
}
void mqtt_service_on_ip_ready(void) { if (mqtt_service_start() == ESP_OK && !s_client_started) { esp_err_t err = esp_mqtt_client_start(s_client); if (err == ESP_OK) s_client_started = true; else ESP_LOGE(TAG, "MQTT启动失败: %s", esp_err_to_name(err)); } }
void mqtt_service_resubscribe(void) { if (s_client) subscribe_all_roots(s_client); }

static bool publish_action(const char *item_field, const char *item_id, app_pending_type_t type)
{
    app_model_snapshot(&s_mqtt_publish_model);
    if (!s_client || !s_mqtt_publish_model.mqtt_online || s_mqtt_publish_model.pending_type != APP_PENDING_NONE) return false;
    char key[64]; snprintf(key, sizeof(key), "req_%lld", (long long)(esp_timer_get_time() / 1000));
    bool legacy = type == APP_PENDING_TASK && s_tasks_mode == PROTOCOL_LEGACY;
    const char *topic = type == APP_PENDING_TASK ? (legacy ? TOPIC_COMPLETE_LEGACY : TOPIC_COMPLETE_V3) : TOPIC_REDEEM;
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, item_field, item_id);
    cJSON_AddStringToObject(root, "child_id", CONFIG_ACTOR_CHILD_ID);
    cJSON_AddStringToObject(root, "actor_child_id", CONFIG_ACTOR_CHILD_ID);
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddStringToObject(root, "account_id", "shared");
    cJSON_AddStringToObject(root, "idempotency_key", key);
    cJSON_AddStringToObject(root, "request_id", key);
    cJSON_AddStringToObject(root, "source", "ai_passport");
    char *payload = cJSON_PrintUnformatted(root);
    int msg_id = payload ? esp_mqtt_client_publish(s_client, topic, payload, 0, 1, false) : -1;
    cJSON_free(payload); cJSON_Delete(root);
    if (msg_id < 0) return false;
    app_model_begin_pending(type, key, item_id, esp_timer_get_time() / 1000 + ACTION_TIMEOUT_MS);
    ESP_LOGI(TAG, "publish protocol=%s topic=%s key=%s qos=1 msg_id=%d", legacy ? "legacy" : "v3", topic, key, msg_id);
    return true;
}
bool mqtt_service_publish_complete_task(const char *task_id) { return publish_action("task_id", task_id, APP_PENDING_TASK); }
bool mqtt_service_publish_redeem(const char *reward_id) { return publish_action("reward_id", reward_id, APP_PENDING_REDEEM); }

static bool publish_find(const char *target, const char *suffix)
{
    app_model_snapshot(&s_mqtt_publish_model);
    if (!s_client || !s_mqtt_publish_model.mqtt_online || !target || !target[0]) return false;
    char topic[128];
    snprintf(topic, sizeof(topic), "kids_points/game/find/%s/%s", target, suffix);
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "sender", CONFIG_KIDS_DEVICE_ID);
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)(esp_timer_get_time() / 1000));
    char *payload = cJSON_PrintUnformatted(root);
    int msg_id = payload ? esp_mqtt_client_publish(s_client, topic, payload, 0, 1, false) : -1;
    cJSON_free(payload);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Find publish topic=%s msg_id=%d", topic, msg_id);
    return msg_id >= 0;
}
bool mqtt_service_publish_find_ring(void) { return publish_find(CONFIG_KIDS_PEER_DEVICE_ID, "ring"); }
bool mqtt_service_publish_find_ack(const char *target_device_id) { return publish_find(target_device_id, "ack"); }
