file(GLOB GENERIC_ICON_SRCS "assets/icons/*.c")
file(GLOB LOGO_ARGB8888_SRCS "assets/logo_argb8888/*.c")

idf_component_register(
    SRCS "main.c"
         "app_events.c"
         "app_model.c"
         "nvs_cache.c"
         "wifi_service.c"
         "time_service.c"
         "mqtt_service.c"
         "power_service.c"
         "battery_service.c"
         "privacy_mode.c"
         "screenshot_service.c"
         "interaction/espnow_service.c"
         "interaction/find_service.c"
         "interaction/game_service.c"
         "interaction/buzzer_game_service.c"
         "interaction/mole_game_service.c"
         "interaction/ptt_service.c"
         "ui/ui_app.c"
         "ui/ui_common.c"
         "ui/ui_text.c"
         "ui/ui_avatar.c"
         "ui/ui_home.c"
         "ui/ui_tasks.c"
         "ui/ui_confirm.c"
         "ui/ui_redeem.c"
         "ui/ui_lottery.c"
         "ui/ui_games.c"
         "ui/ui_pixel_icons.c"
         "assets/avatar_brother.c"
         "assets/avatar_sister.c"
         "assets/lottery_assets.c"
         ${GENERIC_ICON_SRCS}
         ${LOGO_ARGB8888_SRCS}
         "sound/sound_service.c"
         "fonts/lv_font_noto_sc_14.c"
         "fonts/lv_font_noto_sc_18.c"
         "fonts/lv_font_noto_sc_24.c"
    EMBED_FILES "assets/rps_official/rock_54x72.rgb565"
                "assets/rps_official/scissors_54x72.rgb565"
                "assets/rps_official/paper_54x72.rgb565"
                "assets/mole/mole_54x72.rgb565"
    INCLUDE_DIRS "." "ui" "sound" "fonts" "assets" "interaction"
    REQUIRES bsp esp_driver_gpio esp_driver_usb_serial_jtag esp_event esp_hw_support esp_netif esp_pm esp_timer esp_wifi json lwip mqtt nvs_flash vfs
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
