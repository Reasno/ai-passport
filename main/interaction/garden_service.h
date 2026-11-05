#pragma once
#include "espnow_service.h"
#include "garden_rules.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GARDEN_GIFT_IDLE = 0,
    GARDEN_GIFT_SENDING,
    GARDEN_GIFT_SENT,      /* receiver confirmed the credit */
    GARDEN_GIFT_FAILED,    /* never delivered; the item is back in the bag */
    GARDEN_GIFT_PENDING,   /* outcome unknown; the item stays locked until reconciled */
    GARDEN_GIFT_RETURNED,  /* reconciliation found it was never credited; back in the bag */
} garden_gift_phase_t;

typedef struct {
    garden_t garden;
    bool clock_ok;
    bool day;
    uint8_t gift_phase;
    uint8_t gift_material;
} garden_view_t;

esp_err_t garden_service_start(void);
/* 当前时钟有效时补算至现在；时间未就绪时保留原状态和结算时间。 */
void garden_service_snapshot(garden_view_t *out);

garden_result_t garden_service_sow(void);
garden_result_t garden_service_reroll(void);
garden_result_t garden_service_water(void);
garden_result_t garden_service_use(garden_material_t material);
garden_result_t garden_service_harvest(garden_harvest_t *out);

/* Starts delivering one item to the paired sibling. Returns GARDEN_ERR_STATE while a
 * gift is in flight or awaiting confirmation. */
garden_result_t garden_service_gift(garden_material_t material);
/* Clears a finished SENT/FAILED result once the player has seen it. */
void garden_service_gift_seen(void);
bool garden_service_busy(void);
void garden_service_tick(int64_t now_ms);

bool garden_service_owns_packet(uint8_t type);
void garden_service_on_packet(const uint8_t src[6], const espnow_game_packet_t *packet);
/* Called from the Wi-Fi task for every unicast ESP-NOW frame to the paired peer. */
void garden_service_on_send_status(bool delivered);
