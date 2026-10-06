#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTIFICATION_MAX_ITEMS 8
#define NOTIFICATION_ID_LEN 48
#define NOTIFICATION_TEXT_LEN 128

typedef struct {
    char id[NOTIFICATION_ID_LEN];
    char text[NOTIFICATION_TEXT_LEN];
    uint64_t created_at_ms;
} notification_item_t;

typedef struct {
    notification_item_t items[NOTIFICATION_MAX_ITEMS];
    size_t count;
    size_t current;
} notification_store_t;

void notification_store_init(notification_store_t *store);
void notification_store_replace(notification_store_t *store,
                                const notification_item_t *items, size_t count);
const notification_item_t *notification_store_current(const notification_store_t *store);
bool notification_store_next(notification_store_t *store);
bool notification_store_mark_read(notification_store_t *store, const char *id);
void notification_utf8_copy(char *output, size_t output_size, const char *input);
