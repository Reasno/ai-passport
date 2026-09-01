#pragma once
#include "app_model.h"
#include "lvgl.h"
typedef enum { CONFIRM_TASK, CONFIRM_REDEEM } confirm_kind_t;
lv_obj_t *ui_confirm_build(const app_model_snapshot_t *model, confirm_kind_t kind, const char *name, int points, int selected);
