#pragma once
#include "app_model.h"
#include "interaction/evidence_service.h"
#include "lvgl.h"

lv_obj_t *ui_evidence_build(const app_model_snapshot_t *model, const evidence_service_snapshot_t *evidence);
