#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "notification_store.h"

static notification_item_t item(const char *id, const char *text, uint64_t created)
{
    notification_item_t value = {.created_at_ms = created};
    notification_utf8_copy(value.id, sizeof(value.id), id);
    notification_utf8_copy(value.text, sizeof(value.text), text);
    return value;
}

int main(void)
{
    notification_store_t store;
    notification_store_init(&store);
    assert(notification_store_current(&store) == NULL);

    notification_item_t snapshot[10];
    snapshot[0] = item("ntf_new", "最新", 10);
    snapshot[1] = item("ntf_new", "重复项", 9);
    for (size_t i = 2; i < 10; ++i) {
        char id[20];
        snprintf(id, sizeof(id), "ntf_%u", (unsigned)i);
        snapshot[i] = item(id, "通知", 10 - i);
    }
    notification_store_replace(&store, snapshot, 10);
    assert(store.count == NOTIFICATION_MAX_ITEMS);
    assert(strcmp(notification_store_current(&store)->id, "ntf_new") == 0);
    assert(strcmp(store.items[1].id, "ntf_2") == 0);
    assert(strcmp(store.items[7].id, "ntf_8") == 0);

    assert(notification_store_next(&store));
    assert(strcmp(notification_store_current(&store)->id, "ntf_2") == 0);
    assert(notification_store_next(&store));
    assert(strcmp(notification_store_current(&store)->id, "ntf_3") == 0);

    assert(notification_store_mark_read(&store, "ntf_3"));
    assert(store.count == 7);
    assert(strcmp(notification_store_current(&store)->id, "ntf_4") == 0);
    assert(!notification_store_mark_read(&store, "ntf_missing"));

    notification_item_t replacement[] = {
        item("ntf_only", "替换后的快照", 100),
        item("ntf_only", "仍应去重", 99),
    };
    notification_store_replace(&store, replacement, 2);
    assert(store.count == 1);
    assert(store.current == 0);
    assert(strcmp(notification_store_current(&store)->id, "ntf_only") == 0);

    char short_text[5];
    notification_utf8_copy(short_text, sizeof(short_text), "中文A");
    assert(strcmp(short_text, "中") == 0);

    notification_store_replace(&store, NULL, 0);
    assert(store.count == 0);
    assert(notification_store_current(&store) == NULL);
    return 0;
}
