#include "ledger_store.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *valid =
    "{\"version\":1,\"request_id\":\"r1\",\"child_id\":\"brother\",\"ok\":true,"
    "\"items\":["
    "{\"id\":9,\"created_at_ms\":1790000000000,\"points_delta\":3,\"balance_snapshot\":10,\"summary\":\"完成阅读\"},"
    "{\"id\":7,\"created_at_ms\":1789999999999,\"points_delta\":-5,\"balance_snapshot\":7,\"summary\":\"兑换奖品\"}"
    "],\"has_more\":true,\"next_cursor\":7}";

static void rejected(cJSON *root, ledger_parse_result_t expected, uint64_t before)
{
    char *json = cJSON_PrintUnformatted(root);
    ledger_page_t page;
    memset(&page, 0xa5, sizeof(page));
    ledger_page_t unchanged;
    memcpy(&unchanged, &page, sizeof(page));
    assert(ledger_parse(json, strlen(json), "brother", "r1", before, &page) == expected);
    assert(memcmp(&page, &unchanged, sizeof(page)) == 0);
    cJSON_free(json);
    cJSON_Delete(root);
}

int main(int argc, char **argv)
{
    ledger_page_t page;
    if (argc == 2) {
        /* Also accept a real bridge response for cross-language validation. */
        char json[2049];
        FILE *file = fopen(argv[1], "rb");
        assert(file);
        size_t len = fread(json, 1, sizeof(json), file);
        fclose(file);
        assert(ledger_parse(json, len, "brother", "r1", 0, &page) == LEDGER_OK);
        assert(page.count == 2 && page.items[0].id == 9 && page.items[1].id == 7);
        assert(page.has_more && page.next_cursor == 7);
        return 0;
    }
    assert(ledger_parse(valid, strlen(valid), "brother", "r1", 0, &page) == LEDGER_OK);
    assert(page.count == 2 && page.items[0].id == 9 && page.items[1].id == 7);
    assert(page.items[1].points_delta == -5 && page.items[1].balance == 7);
    assert(strcmp(page.items[0].summary, "完成阅读") == 0);
    assert(page.has_more && page.next_cursor == 7 && page.before_id == 0);
    assert(ledger_parse(valid, strlen(valid), "brother", "r1", 10, &page) == LEDGER_OK);
    assert(page.before_id == 10);
    rejected(cJSON_Parse(valid), LEDGER_INVALID, 9);
    rejected(cJSON_Parse(valid), LEDGER_INVALID, 7);

    cJSON *root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "child_id", cJSON_CreateString("sister"));
    rejected(root, LEDGER_IGNORE, 0);
    root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "request_id", cJSON_CreateString("old"));
    rejected(root, LEDGER_IGNORE, 0);
    root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "ok", cJSON_CreateFalse());
    rejected(root, LEDGER_ERROR, 0);
    root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "next_cursor", cJSON_CreateNumber(6));
    rejected(root, LEDGER_INVALID, 0);
    root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "version", cJSON_CreateNumber(2));
    rejected(root, LEDGER_INVALID, 0);

    const char *fields[] = {"id", "created_at_ms", "points_delta", "balance_snapshot"};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        root = cJSON_Parse(valid);
        cJSON *second = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "items"), 1);
        cJSON_ReplaceItemInObject(second, fields[i], cJSON_CreateNumber(1.5));
        rejected(root, LEDGER_INVALID, 0);
    }
    root = cJSON_Parse(valid);
    cJSON *items = cJSON_GetObjectItem(root, "items");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(items, 1), "id", cJSON_CreateNumber(9));
    rejected(root, LEDGER_INVALID, 0);
    root = cJSON_Parse(valid);
    items = cJSON_GetObjectItem(root, "items");
    cJSON_AddItemToArray(items, cJSON_Duplicate(cJSON_GetArrayItem(items, 1), true));
    rejected(root, LEDGER_INVALID, 0);
    root = cJSON_Parse(valid);
    cJSON_ReplaceItemInObject(root, "items", cJSON_CreateArray());
    rejected(root, LEDGER_INVALID, 0);
    const char *empty = "{\"version\":1,\"request_id\":\"r1\",\"child_id\":\"brother\","
                        "\"ok\":true,\"items\":[],\"has_more\":false,\"next_cursor\":null}";
    assert(ledger_parse(empty, strlen(empty), "brother", "r1", 7, &page) == LEDGER_OK);
    assert(page.count == 0 && !page.has_more && page.next_cursor == 0 && page.before_id == 7);
    assert(ledger_parse("bad", 3, "brother", "r1", 0, &page) == LEDGER_INVALID);
    assert(ledger_parse(valid, 0, "brother", "r1", 0, &page) == LEDGER_INVALID);
    assert(ledger_parse(valid, 2049, "brother", "r1", 0, &page) == LEDGER_INVALID);
    puts("Ledger parser: PASS");
    return 0;
}
