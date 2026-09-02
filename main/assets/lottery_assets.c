#include "lottery_assets.h"
#include "logo_assets.h"
#include <string.h>

static const char *const REWARD_IDS[] = {
    "mcd", "cash20", "cash10", "cash2", "points10", "points5", "points2",
};

static const lv_image_dsc_t *const REWARD_IMAGES[] = {
    &logo_08_mcdonalds_burger, &logo_09_cash_20, &logo_10_cash_10, &logo_11_cash_2,
    &logo_12_points_10, &logo_13_points_5, &logo_14_points_2,
};

static const char *const RESULT_TEXTS[] = {
    "恭喜获得麦当劳！",
    "恭喜获得20元！",
    "恭喜获得10元！",
    "恭喜获得2元！",
    "恭喜获得10积分！",
    "恭喜获得5积分！",
    "恭喜获得2积分！",
};

int lottery_asset_index_for_reward(const char *reward_id)
{
    if (!reward_id) return 0;
    for (int i = 0; i < 7; i++) {
        if (strcmp(reward_id, REWARD_IDS[i]) == 0) return i;
    }
    return 0;
}

const lv_image_dsc_t *lottery_asset_for_reward(const char *reward_id)
{
    return REWARD_IMAGES[lottery_asset_index_for_reward(reward_id)];
}

const char *lottery_result_text(const char *reward_id, const char *fallback_label)
{
    if (reward_id) {
        for (int i = 0; i < 7; i++) {
            if (strcmp(reward_id, REWARD_IDS[i]) == 0) return RESULT_TEXTS[i];
        }
    }
    return fallback_label && fallback_label[0] ? fallback_label : "恭喜获得奖励！";
}
