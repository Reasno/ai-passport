#include "lottery_assets.h"
#include "logo_assets.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "lottery_assets";

/* Wheel sector order must stay in sync with tools/process_logo_assets.py build_wheel(). */
static const lv_image_dsc_t *const REWARD_IMAGES[LOTTERY_PRIZE_COUNT] = {
    &logo_08_mcdonalds_burger, &logo_09_cash_20, &logo_10_cash_10, &logo_11_cash_2,
    &logo_12_points_10, &logo_13_points_5, &logo_14_points_2,
};

static const char *const RESULT_TEXTS[LOTTERY_PRIZE_COUNT] = {
    "恭喜获得麦当劳！",
    "恭喜获得20元！",
    "恭喜获得10元！",
    "恭喜获得2元！",
    "恭喜获得10积分！",
    "恭喜获得5积分！",
    "恭喜获得2积分！",
};

/* Points prizes are credited by the server inside the redeem transaction, so the child
 * must never be told to ask a parent for them. Cash/goods still need a parent. */
static const bool AUTO_CREDITED[LOTTERY_PRIZE_COUNT] = {
    false, false, false, false, true, true, true,
};

/* Canonical server ids, index-aligned with the wheel sectors. */
static const char *const CANONICAL_IDS[LOTTERY_PRIZE_COUNT] = {
    "prize_mcd", "prize_cash_20", "prize_cash_10", "prize_cash_2",
    "prize_points_10", "prize_points_5", "prize_points_2",
};

/* The server (kids_points storage.py SEED_REWARDS) sends the prize_* ids; the short
 * forms stay listed so older payloads and debug previews still resolve correctly. */
typedef struct {
    const char *reward_id;
    int index;
} reward_alias_t;

static const reward_alias_t REWARD_ALIASES[] = {
    {"prize_mcd", 0},       {"mcd", 0},       {"mcdonalds", 0},
    {"prize_cash_20", 1},   {"cash_20", 1},   {"cash20", 1},
    {"prize_cash_10", 2},   {"cash_10", 2},   {"cash10", 2},
    {"prize_cash_2", 3},    {"cash_2", 3},    {"cash2", 3},
    {"prize_points_10", 4}, {"points_10", 4}, {"points10", 4},
    {"prize_points_5", 5},  {"points_5", 5},  {"points5", 5},
    {"prize_points_2", 6},  {"points_2", 6},  {"points2", 6},
};

int lottery_asset_index_for_reward(const char *reward_id)
{
    if (!reward_id || !reward_id[0]) return LOTTERY_PRIZE_UNKNOWN;
    for (size_t i = 0; i < sizeof(REWARD_ALIASES) / sizeof(REWARD_ALIASES[0]); i++) {
        if (strcmp(reward_id, REWARD_ALIASES[i].reward_id) == 0) return REWARD_ALIASES[i].index;
    }
    ESP_LOGW(TAG, "未知奖品 reward_id=%s，使用通用展示", reward_id);
    return LOTTERY_PRIZE_UNKNOWN;
}

int lottery_sector_for_reward(const char *reward_id)
{
    int index = lottery_asset_index_for_reward(reward_id);
    if (index >= 0) return index;
    /* An unknown id must not always park the pointer on sector 0 (the burger). */
    uint32_t hash = 2166136261u;
    for (const char *p = reward_id ? reward_id : ""; *p; p++) {
        hash = (hash ^ (uint8_t)*p) * 16777619u;
    }
    return (int)(hash % LOTTERY_PRIZE_COUNT);
}

const lv_image_dsc_t *lottery_asset_for_reward(const char *reward_id)
{
    int index = lottery_asset_index_for_reward(reward_id);
    if (index < 0) return &logo_07_lucky_wheel;
    return REWARD_IMAGES[index];
}

const char *lottery_result_text(const char *reward_id, const char *fallback_label)
{
    int index = lottery_asset_index_for_reward(reward_id);
    if (index >= 0) return RESULT_TEXTS[index];
    return fallback_label && fallback_label[0] ? fallback_label : "恭喜获得奖励！";
}

bool lottery_prize_is_auto_credited(const char *reward_id, int32_t points_delta)
{
    int index = lottery_asset_index_for_reward(reward_id);
    if (index >= 0) return AUTO_CREDITED[index];
    return points_delta != 0;
}

const char *lottery_result_hint(const char *reward_id, int32_t points_delta)
{
    return lottery_prize_is_auto_credited(reward_id, points_delta) ? "积分已自动入账"
                                                                  : "请找爸爸妈妈兑换";
}

const char *lottery_prize_id_at(int index)
{
    if (index < 0 || index >= LOTTERY_PRIZE_COUNT) return CANONICAL_IDS[0];
    return CANONICAL_IDS[index];
}
