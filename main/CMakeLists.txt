file(GLOB GENERIC_ICON_SRCS "assets/icons/*.c")
file(GLOB LOGO_ARGB8888_SRCS "assets/logo_argb8888/*.c")

if(CONFIG_KIDS_ENABLE_GAMES)
    set(APP_UI_SRCS "ui/ui_app.c" "ui/ui_games.c")
    set(APP_POWER_SRC "power_connected_service.c")
    set(GAME_SRCS "interaction/espnow_service.c" "interaction/find_service.c"
        "interaction/game_service.c" "interaction/buzzer_game_service.c"
        "interaction/mole_game_service.c" "interaction/ptt_service.c")
    set(GAME_ASSETS "assets/rps_official/rock_54x72.rgb565"
        "assets/rps_official/scissors_54x72.rgb565"
        "assets/rps_official/paper_54x72.rgb565" "assets/mole/mole_54x72.rgb565a8")
else()
    set(APP_UI_SRCS "ui/ui_points_app.c" "ui/ui_ledger.c")
    set(APP_POWER_SRC "power_service.c")
    list(FILTER GENERIC_ICON_SRCS EXCLUDE REGEX "generic_icon_(game|radar|rps)_")
    list(FILTER LOGO_ARGB8888_SRCS EXCLUDE REGEX "logo_0[456]_")
endif()

idf_component_register(
    SRCS "main.c"
         "app_events.c"
         "app_model.c"
         "nvs_cache.c"
         "wifi_service.c"
         "time_service.c"
         "mqtt_service.c"
         "notification_store.c"
         "notification_service.c"
         ${APP_POWER_SRC}
         "power_policy.c"
         "ledger_store.c"
         "ledger_service.c"
         "battery_service.c"
         "privacy_mode.c"
         "screenshot_service.c"
         ${GAME_SRCS}
         "interaction/evidence_service.c"
         ${APP_UI_SRCS}
         "ui/ui_notification.c"
         "ui/ui_common.c"
         "ui/ui_text.c"
         "ui/ui_avatar.c"
         "ui/ui_home.c"
         "ui/ui_tasks.c"
         "ui/ui_confirm.c"
         "ui/ui_evidence.c"
         "ui/ui_redeem.c"
         "ui/ui_lottery.c"
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
    EMBED_FILES ${GAME_ASSETS}
    INCLUDE_DIRS "." "ui" "sound" "fonts" "assets" "interaction"
    REQUIRES bsp esp_driver_gpio esp_driver_usb_serial_jtag esp_event esp_hw_support esp_netif esp_pm esp_timer esp_wifi json lwip mqtt nvs_flash vfs
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
