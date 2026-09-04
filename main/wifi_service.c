#include "wifi_service.h"
#include "app_events.h"
#include "app_model.h"
#include "mqtt_service.h"
#include "time_service.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <string.h>

#define RETRY_FAST_MS 2000

static const char *TAG = "kp_wifi";
static bool s_started;
static esp_timer_handle_t s_retry_timer;
static int s_failures;

/* While no AP is reachable the STA keeps rescanning every channel, which drags ESP-NOW
 * along with it and breaks the offline "find sibling" ring. Between reconnect attempts we
 * pin the radio to a fixed channel and drop power save, so ESP-NOW has a stable window. */
static void park_radio_for_espnow(void)
{
    esp_err_t chan = esp_wifi_set_channel(CONFIG_KP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "offline mode: ESP-NOW channel pinned to %d (%s), power save off (%s)",
             CONFIG_KP_ESPNOW_CHANNEL, esp_err_to_name(chan), esp_err_to_name(ps));
}

static void retry_connect(void *arg)
{
    (void)arg;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) ESP_LOGW(TAG, "reconnect call failed: %s", esp_err_to_name(err));
}

static void schedule_retry(void)
{
    if (!s_retry_timer) { retry_connect(NULL); return; }
    bool fast = s_failures <= CONFIG_KP_WIFI_RETRY_FAST_TRIES;
    int64_t delay_ms = fast ? RETRY_FAST_MS : CONFIG_KP_WIFI_RETRY_SLOW_MS;
    if (!fast) park_radio_for_espnow();
    esp_timer_stop(s_retry_timer);
    esp_err_t err = esp_timer_start_once(s_retry_timer, delay_ms * 1000);
    ESP_LOGI(TAG, "disconnect #%d, %s retry in %lldms (%s)", s_failures, fast ? "fast" : "backoff",
             (long long)delay_ms, esp_err_to_name(err));
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        s_failures++;
        ESP_LOGW(TAG, "WiFi断开 reason=%d", event ? event->reason : -1);
        app_model_set_connectivity(false, false);
        app_event_post(&(app_event_t){.type = APP_EVT_CONNECTIVITY}, 0);
        schedule_retry();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        s_failures = 0;
        if (s_retry_timer) esp_timer_stop(s_retry_timer);
        /* Keep the radio awake on the home AP so asynchronous ESP-NOW find and game
         * packets are received reliably even while the display backlight is off. */
        esp_wifi_set_ps(WIFI_PS_NONE);
        ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        app_model_set_connectivity(true, false);
        app_event_post(&(app_event_t){.type = APP_EVT_CONNECTIVITY}, 0);
        time_service_start();
        mqtt_service_on_ip_ready();
    }
}

esp_err_t wifi_service_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    const esp_timer_create_args_t retry_args = {.callback = retry_connect, .name = "kp_wifi_retry"};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_create(&retry_args, &s_retry_timer));
    wifi_config_t wifi = {0};
    strlcpy((char *)wifi.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi.sta.password));
    wifi.sta.threshold.authmode = strlen(CONFIG_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    err = esp_wifi_start();
    if (err == ESP_OK) { s_started = true; ESP_LOGI(TAG, "后台连接 WiFi SSID=%s", CONFIG_WIFI_SSID); }
    return err;
}
bool wifi_service_is_started(void) { return s_started; }
