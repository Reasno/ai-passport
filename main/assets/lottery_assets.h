#pragma once
#include "lvgl.h"

LV_IMAGE_DECLARE(lottery_mcd);
LV_IMAGE_DECLARE(lottery_cash20);
LV_IMAGE_DECLARE(lottery_cash10);
LV_IMAGE_DECLARE(lottery_cash2);
LV_IMAGE_DECLARE(lottery_points10);
LV_IMAGE_DECLARE(lottery_points5);
LV_IMAGE_DECLARE(lottery_points2);
LV_IMAGE_DECLARE(lottery_wheel);

const lv_image_dsc_t *lottery_asset_for_reward(const char *reward_id);
int lottery_asset_index_for_reward(const char *reward_id);
const char *lottery_result_text(const char *reward_id, const char *fallback_label);
