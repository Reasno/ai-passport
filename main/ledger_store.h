#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEDGER_PAGE_SIZE 2
#define LEDGER_TEXT_LEN 224
#define LEDGER_REQUEST_LEN 40
typedef struct {
    uint64_t id, created_at_ms;
    int32_t points_delta, balance;
    char summary[LEDGER_TEXT_LEN];
} ledger_entry_t;
typedef struct {
    ledger_entry_t items[LEDGER_PAGE_SIZE];
    size_t count;
    uint64_t before_id, next_cursor;
    bool has_more;
} ledger_page_t;
typedef enum { LEDGER_IGNORE, LEDGER_INVALID, LEDGER_ERROR, LEDGER_OK } ledger_parse_result_t;
/* Pure parser: never mutates out unless the entire matching page is valid. */
ledger_parse_result_t ledger_parse(const char *json, size_t len,
    const char *child, const char *request, uint64_t before_id, ledger_page_t *out);
