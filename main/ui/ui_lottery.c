#include "ui_lottery.h"
#include "lottery_assets.h"
#include "logo_assets.h"
#include "ui_common.h"
#include <stdio.h>

#define LOTTERY_WHEEL_SIZE 152
#define LOTTERY_WHEEL_TOP 44
#define LOTTERY_SPIN_MS 2600

static lv_obj_t *s_wheel;

static void spin_exec(void *obj, int32_t value)
{
    lv_image_set_rotation((lv_obj_t *)obj, value);
}

lv_obj_t *ui_lottery_build(const app_model_snapshot_t *model, int rotation, bool animating)
{
    lv_obj_t *screen = ui_common_screen("幸运开奖", model);
    s_wheel = NULL;

    if (model->lottery_ready && !animating) {
        lv_obj_t *image = lv_image_create(screen);
        lv_image_set_src(image, lottery_asset_for_reward(model->lottery_prize_id));
        lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 36);
        lv_image_set_antialias(image, false);

        const char *result = lottery_result_text(model->lottery_prize_id, model->lottery_label);
        lv_obj_t *title = ui_common_label(screen, result, 8, 96, 224, LV_TEXT_ALIGN_CENTER, true);
        lv_obj_set_style_text_color(title, lv_color_hex(KP_YELLOW), 0);

        ui_common_label(screen,
                        lottery_result_hint(model->lottery_prize_id, model->lottery_points_delta),
                        10, 142, 220, LV_TEXT_ALIGN_CENTER, false);
        ui_common_footer(screen, "B1 B2选择  B3确认  长按B1主页", false);
        return screen;
    }

    s_wheel = lv_image_create(screen);
    lv_image_set_src(s_wheel, &logo_lottery_wheel);
    lv_image_set_pivot(s_wheel, LOTTERY_WHEEL_SIZE / 2, LOTTERY_WHEEL_SIZE / 2);
    lv_image_set_rotation(s_wheel, rotation);
    lv_image_set_antialias(s_wheel, false);
    lv_obj_align(s_wheel, LV_ALIGN_TOP_MID, 0, LOTTERY_WHEEL_TOP);

    lv_obj_t *pointer = lv_label_create(screen);
    lv_label_set_text(pointer, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(pointer, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(pointer, lv_color_hex(KP_RED), 0);
    lv_obj_align(pointer, LV_ALIGN_TOP_MID, 0, LOTTERY_WHEEL_TOP - 18);

    /* Status band starts below the wheel so no text can sit on the artwork. */
    ui_common_label(screen, animating ? "正在转动..." : "等待开奖结果...", 10,
                    LOTTERY_WHEEL_TOP + LOTTERY_WHEEL_SIZE + 14, 220, LV_TEXT_ALIGN_CENTER, false);
    ui_common_footer(screen, "B1 B2选择  B3确认  长按B1主页", false);
    return screen;
}

void ui_lottery_start_spin(int target_index)
{
    if (!s_wheel) return;
    if (target_index < 0 || target_index >= LOTTERY_PRIZE_COUNT) target_index = 0;

    /* Sector i sits i*(360/7) degrees clockwise from the top pointer, so the wheel has to
     * come back counter-clockwise by the same amount after seven full turns. */
    int32_t offset = (target_index * 3600 + LOTTERY_PRIZE_COUNT / 2) / LOTTERY_PRIZE_COUNT;
    int32_t final_rotation = 7 * 3600 - offset;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_wheel);
    lv_anim_set_values(&anim, 0, final_rotation);
    lv_anim_set_duration(&anim, LOTTERY_SPIN_MS);
    lv_anim_set_exec_cb(&anim, spin_exec);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}
