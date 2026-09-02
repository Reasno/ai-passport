#include "time_service.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "kp_time";
static bool s_started;
static bool s_synced;

static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    time_t now = 0;
    time(&now);
    struct tm local;
    localtime_r(&now, &local);
    ESP_LOGI(TAG, "SNTP synced: %04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

void time_service_start(void)
{
    if (s_started) return;
    s_started = true;

    /* Asia/Shanghai (UTC+8). */
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP init");
}

bool time_service_is_synced(void)
{
    if (s_synced) return true;
    /* Fallback if callback got dropped: check lwIP status. */
    sntp_sync_status_t status = esp_sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) s_synced = true;
    return s_synced;
}
