#pragma once
#include "app_model.h"
#include "lvgl.h"
lv_obj_t *ui_redeem_build(const app_model_snapshot_t *model, int selected);
int ui_redeem_model_index(const app_model_snapshot_t *model, int selected);
