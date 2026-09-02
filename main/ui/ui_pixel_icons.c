#include "ui_pixel_icons.h"
#include "ui_common.h"
#include "logo_assets.h"

LV_IMAGE_DECLARE(generic_icon_task_s2);
LV_IMAGE_DECLARE(generic_icon_gift_s2);
LV_IMAGE_DECLARE(generic_icon_gift_s3);
LV_IMAGE_DECLARE(generic_icon_game_s2);
LV_IMAGE_DECLARE(generic_icon_radar_s3);
LV_IMAGE_DECLARE(generic_icon_radar_s5);
LV_IMAGE_DECLARE(generic_icon_rps_s3);
LV_IMAGE_DECLARE(generic_icon_rps_s6);
LV_IMAGE_DECLARE(generic_icon_tv_s3);
LV_IMAGE_DECLARE(generic_icon_ticket_s3);
LV_IMAGE_DECLARE(generic_icon_check_s2);
LV_IMAGE_DECLARE(generic_icon_check_s3);
LV_IMAGE_DECLARE(generic_icon_lock_s3);
LV_IMAGE_DECLARE(generic_icon_wifi_s1);
LV_IMAGE_DECLARE(generic_icon_battery_s1);

static const lv_image_dsc_t *icon_source(ui_pixel_icon_t icon, uint8_t scale)
{
    switch (icon) {
    case UI_PIXEL_ICON_TASK: return &generic_icon_task_s2;
    case UI_PIXEL_ICON_GIFT: return scale >= 3 ? &generic_icon_gift_s3 : &generic_icon_gift_s2;
    case UI_PIXEL_ICON_GAME: return &generic_icon_game_s2;
    case UI_PIXEL_ICON_RADAR: return scale >= 5 ? &generic_icon_radar_s5 : &generic_icon_radar_s3;
    case UI_PIXEL_ICON_RPS: return scale >= 6 ? &generic_icon_rps_s6 : &generic_icon_rps_s3;
    case UI_PIXEL_ICON_TV: return &generic_icon_tv_s3;
    case UI_PIXEL_ICON_TICKET: return &generic_icon_ticket_s3;
    case UI_PIXEL_ICON_CHECK: return scale >= 3 ? &generic_icon_check_s3 : &generic_icon_check_s2;
    case UI_PIXEL_ICON_LOCK: return &generic_icon_lock_s3;
    case UI_PIXEL_ICON_WIFI: return &generic_icon_wifi_s1;
    case UI_PIXEL_ICON_BATTERY: return &generic_icon_battery_s1;
    default: return NULL;
    }
}

static const lv_image_dsc_t *full_color_source(ui_pixel_icon_t icon)
{
    switch (icon) {
    case UI_PIXEL_ICON_RADAR: return &logo_05_find_sibling;
    case UI_PIXEL_ICON_RPS: return &logo_06_rock_paper_scissors;
    case UI_PIXEL_ICON_TICKET: return &logo_07_lucky_wheel;
    default: return NULL;
    }
}

lv_obj_t *ui_pixel_icon_create(lv_obj_t *parent, ui_pixel_icon_t icon, int x, int y,
                               uint32_t color, uint8_t scale)
{
    const lv_image_dsc_t *full_color = full_color_source(icon);
    const lv_image_dsc_t *source = full_color ? full_color : icon_source(icon, scale);
    if (!source) return NULL;
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_image_set_antialias(image, false);
    if (!full_color) {
        lv_obj_set_style_image_recolor(image, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    }
    return image;
}

lv_obj_t *ui_home_menu_icon_create(lv_obj_t *parent, ui_pixel_icon_t icon, int x, int y)
{
    const lv_image_dsc_t *source = icon == UI_PIXEL_ICON_TASK ? &logo_02_tasks :
                                   icon == UI_PIXEL_ICON_GIFT ? &logo_03_rewards :
                                   icon == UI_PIXEL_ICON_GAME ? &logo_04_games : NULL;
    if (!source) return NULL;
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_image_set_antialias(image, false);
    return image;
}

lv_obj_t *ui_radar_icon_create(lv_obj_t *parent, int x, int y, uint32_t color, uint8_t scale)
{
    return ui_pixel_icon_create(parent, UI_PIXEL_ICON_RADAR, x, y, color, scale);
}
