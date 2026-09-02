#pragma once
#include "lvgl.h"

typedef enum {
    UI_PIXEL_ICON_TASK,
    UI_PIXEL_ICON_GIFT,
    UI_PIXEL_ICON_GAME,
    UI_PIXEL_ICON_RADAR,
    UI_PIXEL_ICON_RPS,
    UI_PIXEL_ICON_BUZZER,
    UI_PIXEL_ICON_TV,
    UI_PIXEL_ICON_TICKET,
    UI_PIXEL_ICON_CHECK,
    UI_PIXEL_ICON_LOCK,
    UI_PIXEL_ICON_WIFI,
    UI_PIXEL_ICON_BATTERY,
} ui_pixel_icon_t;

lv_obj_t *ui_pixel_icon_create(lv_obj_t *parent, ui_pixel_icon_t icon, int x, int y,
                               uint32_t color, uint8_t scale);

/** Create a full-color 32x32 Home icon generated from a 16x16 pixel-art tile. */
lv_obj_t *ui_home_menu_icon_create(lv_obj_t *parent, ui_pixel_icon_t icon, int x, int y);

/** Draw a hard-edged radar/crosshair from opaque LVGL rectangles (no image alpha). */
lv_obj_t *ui_radar_icon_create(lv_obj_t *parent, int x, int y, uint32_t color, uint8_t scale);

/** Draw a compact three-red-light buzzer icon for game list cards. */
lv_obj_t *ui_buzzer_icon_create(lv_obj_t *parent, int x, int y, bool enabled, uint8_t scale);
