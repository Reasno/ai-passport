#pragma once
#include "app_model.h"
#include "bsp_button.h"
#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    char message[96];
    bool error;
    /* sound_service_t value to play, or -1 for none. */
    int sound;
    /* Leave the garden for the device pairing entry. */
    bool open_pairing;
} ui_garden_feedback_t;

/* Opens the garden on its home page with the first item focused. */
void ui_garden_enter(void);
lv_obj_t *ui_garden_build(const app_model_snapshot_t *model);
/* Short press. Returns true when the garden should close. */
bool ui_garden_key(bsp_btn_t key, ui_garden_feedback_t *feedback);
/* Long press on B3: back one level. Returns true when leaving the garden home. */
bool ui_garden_back(ui_garden_feedback_t *feedback);
/* Mature tomato artwork for the games list. It shares the garden's single sprite
 * buffer, which is safe because only one screen exists at a time. */
const lv_image_dsc_t *ui_garden_menu_icon(void);
/* True on pages whose content changes with the clock. */
bool ui_garden_live_page(void);
