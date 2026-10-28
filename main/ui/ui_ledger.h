#pragma once
#include "app_model.h"
#include "ledger_service.h"
#include "lvgl.h"
lv_obj_t *ui_ledger_build(const app_model_snapshot_t *model, const ledger_snapshot_t *ledger);
