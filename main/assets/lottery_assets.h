#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#define LOTTERY_PRIZE_COUNT 7
#define LOTTERY_PRIZE_UNKNOWN (-1)

const lv_image_dsc_t *lottery_asset_for_reward(const char *reward_id);
/* Returns 0..6 for a known prize, LOTTERY_PRIZE_UNKNOWN otherwise. */
int lottery_asset_index_for_reward(const char *reward_id);
/* Always returns a valid wheel sector 0..6, even for an unknown prize id. */
int lottery_sector_for_reward(const char *reward_id);
const char *lottery_result_text(const char *reward_id, const char *fallback_label);
/* True when the server already credited the prize (points prizes). */
bool lottery_prize_is_auto_credited(const char *reward_id, int32_t points_delta);
/* Result page sub-line: auto-credited prizes never ask for a parent. */
const char *lottery_result_hint(const char *reward_id, int32_t points_delta);
/* Canonical server prize id for sector 0..6, used by the debug preview. */
const char *lottery_prize_id_at(int index);
