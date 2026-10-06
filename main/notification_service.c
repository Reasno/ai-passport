#include "notification_service.h"

#include "app_events.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static notification_store_t s_store;
/* MQTT dispatch is serial. Keeping the parse candidate static avoids adding
 * roughly 1.5 KB to its 4 KB task stack. */
static notification_item_t s_parse_items[NOTIFICATION_MAX_ITEMS];

static bool valid_child_id(const char *child_id)
{
    return child_id &&
           (strcmp(child_id, "brother") == 0 || strcmp(child_id, "sister") == 0) &&
           strcmp(child_id, CONFIG_ACTOR_CHILD_ID) == 0;
}

static bool valid_id(const char *id)
{
    return id && strncmp(id, "ntf_", 4) == 0 && id[4] != '\0' &&
           strlen(id) < NOTIFICATION_ID_LEN;
}

esp_err_t notification_service_init(void)
{
    if (s_mutex) return ESP_OK;
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (!s_mutex) return ESP_ERR_NO_MEM;
    notification_store_init(&s_store);
    return ESP_OK;
}

static bool parsed_contains_id(const char *id, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(s_parse_items[i].id, id) == 0) return true;
    }
    return false;
}

esp_err_t notification_service_apply_json(const char *json, size_t len)
{
    if (!s_mutex || !json || len == 0) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ESP_ERR_INVALID_ARG;

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *child = cJSON_GetObjectItemCaseSensitive(root, "child_id");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    cJSON *updated = cJSON_GetObjectItemCaseSensitive(root, "updated_at_ms");
    bool valid = cJSON_IsNumber(version) && version->valuedouble == 1.0 &&
                 cJSON_IsString(child) && valid_child_id(child->valuestring) &&
                 cJSON_IsArray(items) && cJSON_IsNumber(updated) && updated->valuedouble >= 0;
    size_t count = 0;
    memset(s_parse_items, 0, sizeof(s_parse_items));

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, items) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "text");
        cJSON *created = cJSON_GetObjectItemCaseSensitive(entry, "created_at_ms");
        if (!cJSON_IsObject(entry) || !cJSON_IsString(id) || !valid_id(id->valuestring) ||
            !cJSON_IsString(text) || !cJSON_IsNumber(created) || created->valuedouble < 0) {
            valid = false;
            break;
        }
        if (count < NOTIFICATION_MAX_ITEMS && !parsed_contains_id(id->valuestring, count)) {
            notification_utf8_copy(s_parse_items[count].id, sizeof(s_parse_items[count].id), id->valuestring);
            notification_utf8_copy(s_parse_items[count].text, sizeof(s_parse_items[count].text), text->valuestring);
            s_parse_items[count].created_at_ms = (uint64_t)created->valuedouble;
            count++;
        }
    }
    cJSON_Delete(root);
    if (!valid) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    notification_store_replace(&s_store, s_parse_items, count);
    xSemaphoreGive(s_mutex);
    app_event_post(&(app_event_t){.type = APP_EVT_NOTIFICATION_SNAPSHOT}, 0);
    return ESP_OK;
}

bool notification_service_current(notification_item_t *item, size_t *position, size_t *count)
{
    if (!s_mutex || !item || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const notification_item_t *current = notification_store_current(&s_store);
    bool found = current != NULL;
    if (found) *item = *current;
    if (position) *position = found ? s_store.current : 0;
    if (count) *count = s_store.count;
    xSemaphoreGive(s_mutex);
    return found;
}

bool notification_service_next(void)
{
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool moved = notification_store_next(&s_store);
    xSemaphoreGive(s_mutex);
    return moved;
}

bool notification_service_mark_read(const char *notification_id)
{
    if (!s_mutex || !notification_id || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool removed = notification_store_mark_read(&s_store, notification_id);
    xSemaphoreGive(s_mutex);
    return removed;
}
