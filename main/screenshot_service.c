#include "screenshot_service.h"
#include "mqtt_service.h"

#if CONFIG_ENABLE_SCREENSHOT

#include "app_events.h"

#include "bsp_display.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SHOT_W 240
#define SHOT_H 320
#define SHOT_PIXELS (SHOT_W * SHOT_H)

#define SHOT_RECORD_MAGIC "KPRC"

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t x1, y1, x2, y2;
    uint32_t len;
} shot_record_t;

_Static_assert(sizeof(shot_record_t) == 16, "screenshot record header must be 16 bytes");

static bool s_capturing;
static uint32_t s_crc;
static uint32_t s_pixels;
static esp_log_level_t s_previous_log_level;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc;
}

static bool write_all(const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len) {
        ssize_t written = write(STDOUT_FILENO, p, len);
        if (written <= 0) return false;
        p += written;
        len -= (size_t)written;
    }
    return true;
}

static void flush_capture_cb(lv_event_t *event)
{
    if (!s_capturing) return;
    lv_display_t *display = lv_event_get_target(event);
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *draw = lv_display_get_buf_active(display);
    if (!area || !draw || !draw->data) return;

    uint32_t pixels = (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);
    shot_record_t record = {
        .magic = SHOT_RECORD_MAGIC,
        .x1 = (uint16_t)area->x1, .y1 = (uint16_t)area->y1,
        .x2 = (uint16_t)area->x2, .y2 = (uint16_t)area->y2,
        .len = pixels * 2U,
    };
    uint32_t record_crc = 0xFFFFFFFFU;
    record_crc = crc32_update(record_crc, (const uint8_t *)&record, sizeof(record));
    record_crc = crc32_update(record_crc, draw->data, record.len) ^ 0xFFFFFFFFU;

    bool ok = write_all(&record, sizeof(record)) &&
              write_all(draw->data, record.len) &&
              write_all(&record_crc, sizeof(record_crc));
    if (ok) {
        s_crc = crc32_update(s_crc, (const uint8_t *)&record, sizeof(record));
        s_crc = crc32_update(s_crc, draw->data, record.len);
        s_pixels += pixels;
    }

    if (!ok || lv_display_flush_is_last(display)) {
        char end[48];
        uint32_t result = s_crc ^ 0xFFFFFFFFU;
        int n = snprintf(end, sizeof(end), "KPSS_END %08lx %lu\n",
                         (unsigned long)result, (unsigned long)s_pixels);
        write_all(end, (size_t)n);
        fsync(STDOUT_FILENO);
        s_capturing = false;
        usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
        esp_log_level_set("*", s_previous_log_level);
    }
}

static bool post_page_command(const char *name)
{
    static const char *const names[APP_DEBUG_PAGE_COUNT] = {
        "HOME", "TASKS", "REDEEM", "LOTTERY", "GAMES", "FIND", "RPS",
        "LOTTERY_SPIN", "LOTTERY_RESULT",
    };
    for (int i = 0; i < APP_DEBUG_PAGE_COUNT; i++) {
        if (strcmp(name, names[i]) == 0) {
            app_event_t event = {.type = APP_EVT_DEBUG_PAGE, .value = i};
            if (!app_event_post(&event, pdMS_TO_TICKS(50))) {
                static const char error[] = "KP_PAGE_ERR QUEUE_FULL\n";
                write_all(error, sizeof(error) - 1);
                return false;
            }
            char response[32];
            int length = snprintf(response, sizeof(response), "KP_PAGE_OK %s\n", name);
            write_all(response, (size_t)length);
            fsync(STDOUT_FILENO);
            return true;
        }
    }
    static const char error[] = "KP_PAGE_ERR UNKNOWN_PAGE\n";
    write_all(error, sizeof(error) - 1);
    fsync(STDOUT_FILENO);
    return false;
}

static void screenshot_task(void *arg)
{
    lv_display_t *display = arg;
    char command[32];
    for (;;) {
        if (!fgets(command, sizeof(command), stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        command[strcspn(command, "\r\n")] = '\0';
        if (strncmp(command, "REDEEM ", 7) == 0) {
            bool sent = mqtt_service_publish_redeem(command + 7);
            char response[64];
            int length = snprintf(response, sizeof(response), sent ? "KP_REDEEM_OK %s\n" : "KP_REDEEM_ERR %s\n", command + 7);
            write_all(response, (size_t)length);
            fsync(STDOUT_FILENO);
            continue;
        }
        if (strncmp(command, "PAGE ", 5) == 0) {
            post_page_command(command + 5);
            continue;
        }
        if (strcmp(command, "SCREENSHOT") != 0 || s_capturing) continue;

        if (!bsp_lvgl_lock(1000)) continue;
        s_previous_log_level = esp_log_level_get("*");
        esp_log_level_set("*", ESP_LOG_NONE);
        usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
        s_crc = 0xFFFFFFFFU;
        s_pixels = 0;
        s_capturing = true;
        static const char header[] = "KPSS2 240 320 RGB565LE KPRC\n";
        write_all(header, sizeof(header) - 1);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        bsp_lvgl_unlock();
    }
}

esp_err_t screenshot_service_start(void)
{
    lv_display_t *display = lv_display_get_default();
    if (!display) return ESP_ERR_INVALID_STATE;
    lv_display_add_event_cb(display, flush_capture_cb, LV_EVENT_FLUSH_START, NULL);
    /* lv_refr_now() renders synchronously on this task; match the UI render stack budget. */
    if (xTaskCreatePinnedToCore(screenshot_task, "kp_shot", 6144, display, 1, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else

esp_err_t screenshot_service_start(void) { return ESP_OK; }

#endif
