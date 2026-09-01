#include "ui_avatar.h"
#include "ui_common.h"

#if CONFIG_KIDS_USE_OFFICIAL_AVATAR
LV_IMAGE_DECLARE(avatar_brother);
LV_IMAGE_DECLARE(avatar_sister);
#else
static lv_obj_t *part(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}
#endif

lv_obj_t *ui_avatar_create(lv_obj_t *parent, int x, int y)
{
#if CONFIG_KIDS_USE_OFFICIAL_AVATAR
    lv_obj_t *image = lv_image_create(parent);
#ifdef CONFIG_KIDS_THEME_SISTER
    lv_image_set_src(image, &avatar_sister);
#else
    lv_image_set_src(image, &avatar_brother);
#endif
    lv_obj_set_pos(image, x, y);
    return image;
#else
    /* Small all-vector fallback for builds that deliberately exclude artwork. */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_pos(root, x + 24, y + 54);
    lv_obj_set_size(root, 48, 48);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    part(root, 11, 8, 26, 26, 0xF2C49B, 12);
    part(root, 10, 5, 28, 11, 0x161A22, 5);
#ifdef CONFIG_KIDS_THEME_SISTER
    part(root, 4, 7, 10, 17, 0x161A22, 5);
    part(root, 35, 7, 10, 17, 0x161A22, 5);
#endif
    part(root, 17, 18, 3, 3, 0x161A22, 2);
    part(root, 28, 18, 3, 3, 0x161A22, 2);
    part(root, 20, 25, 9, 2, 0x9A4A42, 1);
    part(root, 9, 33, 30, 15, KP_THEME, 5);
    return root;
#endif
}
