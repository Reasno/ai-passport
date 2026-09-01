#pragma once
#include "app_model.h"
#include "game_service.h"
#include "lvgl.h"
#include <stdbool.h>

lv_obj_t *ui_games_build(const app_model_snapshot_t *model, const game_snapshot_t *game, int selected);
lv_obj_t *ui_find_build(const app_model_snapshot_t *model, const game_snapshot_t *game,
                        const char *find_status, bool waiting);
lv_obj_t *ui_rps_build(const app_model_snapshot_t *model, const game_snapshot_t *game);
