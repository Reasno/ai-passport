#include "notification_store.h"

#include <string.h>

static size_t utf8_sequence_length(const unsigned char *text)
{
    if (text[0] < 0x80) return 1;
    if ((text[0] & 0xE0) == 0xC0 && text[1] && (text[1] & 0xC0) == 0x80) return 2;
    if ((text[0] & 0xF0) == 0xE0 && text[1] && text[2] &&
        (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80) return 3;
    if ((text[0] & 0xF8) == 0xF0 && text[1] && text[2] && text[3] &&
        (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80 &&
        (text[3] & 0xC0) == 0x80) return 4;
    return 1;
}

void notification_utf8_copy(char *output, size_t output_size, const char *input)
{
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!input) return;

    const unsigned char *read = (const unsigned char *)input;
    size_t used = 0;
    while (*read) {
        size_t bytes = utf8_sequence_length(read);
        if (used + bytes >= output_size) break;
        memcpy(output + used, read, bytes);
        used += bytes;
        read += bytes;
    }
    output[used] = '\0';
}

void notification_store_init(notification_store_t *store)
{
    if (store) memset(store, 0, sizeof(*store));
}

static bool contains_id(const notification_store_t *store, const char *id)
{
    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].id, id) == 0) return true;
    }
    return false;
}

void notification_store_replace(notification_store_t *store,
                                const notification_item_t *items, size_t count)
{
    if (!store) return;
    notification_store_init(store);
    if (!items) return;

    for (size_t i = 0; i < count && store->count < NOTIFICATION_MAX_ITEMS; ++i) {
        if (!items[i].id[0] || contains_id(store, items[i].id)) continue;
        notification_item_t *out = &store->items[store->count++];
        notification_utf8_copy(out->id, sizeof(out->id), items[i].id);
        notification_utf8_copy(out->text, sizeof(out->text), items[i].text);
        out->created_at_ms = items[i].created_at_ms;
    }
}

const notification_item_t *notification_store_current(const notification_store_t *store)
{
    if (!store || store->count == 0 || store->current >= store->count) return NULL;
    return &store->items[store->current];
}

bool notification_store_next(notification_store_t *store)
{
    if (!store || store->count == 0) return false;
    store->current = (store->current + 1) % store->count;
    return true;
}

bool notification_store_mark_read(notification_store_t *store, const char *id)
{
    if (!store || !id) return false;
    size_t found = store->count;
    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].id, id) == 0) {
            found = i;
            break;
        }
    }
    if (found == store->count) return false;

    if (found + 1 < store->count) {
        memmove(&store->items[found], &store->items[found + 1],
                (store->count - found - 1) * sizeof(store->items[0]));
    }
    memset(&store->items[store->count - 1], 0, sizeof(store->items[0]));
    store->count--;
    if (store->count == 0 || store->current >= store->count) store->current = 0;
    return true;
}
