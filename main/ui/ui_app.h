#pragma once
#include "esp_err.h"
#include "bsp_button.h"
esp_err_t ui_app_start(void);
void ui_app_post_key(bsp_btn_t btn, bsp_btn_ev_t event);
