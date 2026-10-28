#include "ledger_store.h"
#include "notification_store.h"
#include "cJSON.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static bool number(cJSON *root, const char *key, double min, double max, double *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < min || item->valuedouble > max ||
        floor(item->valuedouble) != item->valuedouble) return false;
    *out = item->valuedouble;
    return true;
}

static bool matches(cJSON *root, const char *key, const char *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && strcmp(item->valuestring, value) == 0;
}

ledger_parse_result_t ledger_parse(const char *json, size_t len,
    const char *child, const char *request, uint64_t before_id, ledger_page_t *out)
{
    if (!json || !child || !request || !out || len == 0 || len > 2048) return LEDGER_INVALID;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return LEDGER_INVALID;
    ledger_parse_result_t result = LEDGER_INVALID;
    if (!matches(root, "request_id", request) || !matches(root, "child_id", child)) {
        result = LEDGER_IGNORE;
        goto done;
    }
    if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok"))) {
        result = LEDGER_ERROR;
        goto done;
    }
    double version;
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    cJSON *more = cJSON_GetObjectItemCaseSensitive(root, "has_more");
    if (!number(root, "version", 1, 1, &version) ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")) ||
        !cJSON_IsArray(items) || !cJSON_IsBool(more) ||
        cJSON_GetArraySize(items) > LEDGER_PAGE_SIZE) goto done;
    ledger_page_t page = {.before_id = before_id, .has_more = cJSON_IsTrue(more)};
    uint64_t previous = before_id ? before_id : UINT64_MAX;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        double id, ts, delta, balance;
        cJSON *summary = cJSON_GetObjectItemCaseSensitive(item, "summary");
        if (!number(item, "id", 1, 9007199254740991.0, &id) ||
            !number(item, "created_at_ms", 0, 9007199254740991.0, &ts) ||
            !number(item, "points_delta", INT32_MIN, INT32_MAX, &delta) ||
            !number(item, "balance_snapshot", INT32_MIN, INT32_MAX, &balance) ||
            !cJSON_IsString(summary) || (uint64_t)id >= previous) goto done;
        ledger_entry_t *entry = &page.items[page.count++];
        entry->id = (uint64_t)id;
        entry->created_at_ms = (uint64_t)ts;
        entry->points_delta = (int32_t)delta;
        entry->balance = (int32_t)balance;
        notification_utf8_copy(entry->summary, sizeof(entry->summary), summary->valuestring);
        previous = entry->id;
    }
    if (page.has_more) {
        double next;
        if (!page.count || !number(root, "next_cursor", 1, 9007199254740991.0, &next) ||
            (uint64_t)next != previous) goto done;
        page.next_cursor = (uint64_t)next;
    }
    *out = page;
    result = LEDGER_OK;
done:
    cJSON_Delete(root);
    return result;
}
