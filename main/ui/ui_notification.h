#pragma once

#include "lvgl.h"
#include "notification_store.h"
#include <stddef.h>

lv_obj_t *ui_notification_create(lv_obj_t *parent);
void ui_notification_show(lv_obj_t *overlay, const notification_item_t *item,
                          size_t position, size_t count);
void ui_notification_hide(lv_obj_t *overlay);
