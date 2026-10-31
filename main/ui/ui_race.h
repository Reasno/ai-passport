#pragma once
#include "app_model.h"
#include "race_game_service.h"
#include "lvgl.h"

lv_obj_t *ui_race_build(const app_model_snapshot_t *model, const race_game_snapshot_t *game);
/* Updates the existing race page in place; the track canvas is only invalidated when it changes. */
void ui_race_update(const race_game_snapshot_t *game);
void ui_race_forget(void);
