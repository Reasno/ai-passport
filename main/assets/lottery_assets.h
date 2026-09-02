#pragma once
#include "lvgl.h"

const lv_image_dsc_t *lottery_asset_for_reward(const char *reward_id);
int lottery_asset_index_for_reward(const char *reward_id);
const char *lottery_result_text(const char *reward_id, const char *fallback_label);
