#pragma once
#include "app_model.h"
#include "buzzer_game_service.h"
#include "game_service.h"
#include "mole_game_service.h"
#include "lvgl.h"
#include <stdbool.h>

lv_obj_t *ui_games_build(const app_model_snapshot_t *model, const game_snapshot_t *game, int selected);
lv_obj_t *ui_find_build(const app_model_snapshot_t *model, const game_snapshot_t *game,
                        const char *find_status, bool waiting);
lv_obj_t *ui_rps_build(const app_model_snapshot_t *model, const game_snapshot_t *game);
lv_obj_t *ui_buzzer_build(const app_model_snapshot_t *model, const buzzer_game_snapshot_t *game);
lv_obj_t *ui_mole_build(const app_model_snapshot_t *model, const mole_game_snapshot_t *game);
void ui_mole_update(const mole_game_snapshot_t *game);
void ui_mole_forget(void);
