#pragma once
#include "app_model.h"
#include "lvgl.h"

lv_obj_t *ui_lottery_build(const app_model_snapshot_t *model, int rotation, bool animating);
void ui_lottery_start_spin(int target_index);
