#include "espnow_service.h"
#include "game_service.h"
#include "ptt_service.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <string.h>

#define ESPNOW_MAGIC 0x4B50
#define ESPNOW_VERSION 1
#define PAIR_NS "kp_pair"
#define PAIR_KEY "peer_mac"

typedef struct { uint8_t src[6]; espnow_game_packet_t packet; int8_t rssi; } rx_event_t;
static const char *TAG = "kp_espnow";
static const uint8_t BROADCAST[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
static QueueHandle_t s_queue;
static uint8_t s_peer[6];
static bool s_ready, s_has_peer;
static uint16_t s_sequence;

static uint16_t packet_crc(const espnow_game_packet_t *packet)
{
    espnow_game_packet_t copy = *packet; copy.checksum = 0;
    return esp_crc16_le(0xffff, (const uint8_t *)&copy, sizeof(copy));
}
static bool add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer = {0}; memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) ESP_LOGW(TAG, "add peer failed: %s", esp_err_to_name(err));
    return err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST;
}
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !info->src_addr || !data) return;
    if (ptt_service_is_packet(data, len)) {
        ptt_service_on_packet(info->src_addr, data, len);
        return;
    }
    if (!s_queue || len != sizeof(espnow_game_packet_t)) return;
    rx_event_t event = {0}; memcpy(event.src, info->src_addr, 6); memcpy(&event.packet, data, sizeof(event.packet));
    event.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    xQueueSend(s_queue, &event, 0); /* Wi-Fi callback stays non-blocking. */
}
static void rx_task(void *arg)
{
    (void)arg; rx_event_t event;
    for (;;) if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
        espnow_game_packet_t *p = &event.packet;
        if (p->magic != ESPNOW_MAGIC || p->version != ESPNOW_VERSION || p->checksum != packet_crc(p)) continue;
#ifdef CONFIG_KIDS_ACTOR_SISTER
        if (p->actor == 2) continue;
#else
        if (p->actor == 1) continue;
#endif
        uint8_t own[6]; esp_read_mac(own, ESP_MAC_WIFI_STA);
        if (memcmp(own, event.src, 6) == 0) continue;
        game_service_on_packet(event.src, p, event.rssi);
    }
}
static void load_peer(void)
{
    nvs_handle_t h; if (nvs_open(PAIR_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_peer);
    if (nvs_get_blob(h, PAIR_KEY, s_peer, &len) == ESP_OK && len == 6 && add_peer(s_peer)) s_has_peer = true;
    nvs_close(h);
}
bool espnow_service_get_peer(uint8_t mac[6]) { if (!s_has_peer || !mac) return false; memcpy(mac, s_peer, 6); return true; }
bool espnow_service_ready(void) { return s_ready; }
bool espnow_service_has_peer(void) { return s_has_peer; }
esp_err_t espnow_service_clear_peer(void)
{
    if (s_has_peer) esp_now_del_peer(s_peer);
    memset(s_peer, 0, sizeof(s_peer));
    s_has_peer = false;
    nvs_handle_t h; esp_err_t err = nvs_open(PAIR_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) { err = nvs_erase_key(h, PAIR_KEY); if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK; if (err == ESP_OK) err = nvs_commit(h); nvs_close(h); }
    return err;
}
/* Called only by the game worker after actor/self validation. */
esp_err_t espnow_service_store_peer(const uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    if (s_has_peer && memcmp(s_peer, mac, 6) != 0) esp_now_del_peer(s_peer);
    if (!add_peer(mac)) return ESP_FAIL;
    memcpy(s_peer, mac, 6); s_has_peer = true;
    nvs_handle_t h; esp_err_t err = nvs_open(PAIR_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) { err = nvs_set_blob(h, PAIR_KEY, mac, 6); if (err == ESP_OK) err = nvs_commit(h); nvs_close(h); }
    ESP_LOGI(TAG, "paired peer=" MACSTR " nvs=%s", MAC2STR(mac), esp_err_to_name(err));
    return err;
}
esp_err_t espnow_service_send(espnow_msg_type_t type, uint16_t session, uint16_t sequence,
                              uint8_t choice, int8_t value, bool broadcast)
{
    if (!s_ready || (!broadcast && !s_has_peer)) return ESP_ERR_INVALID_STATE;
    espnow_game_packet_t p = {.magic=ESPNOW_MAGIC,.version=ESPNOW_VERSION,.type=type,
#ifdef CONFIG_KIDS_ACTOR_SISTER
        .actor=2,
#else
        .actor=1,
#endif
        .choice=choice,.session=session,.sequence=sequence ? sequence : ++s_sequence,.value=value};
    p.checksum = packet_crc(&p);
    const uint8_t *target = broadcast ? BROADCAST : s_peer;
    esp_err_t err = esp_now_send(target, (const uint8_t *)&p, sizeof(p));
    if (err != ESP_OK) ESP_LOGW(TAG, "send type=%u failed: %s", type, esp_err_to_name(err));
    return err;
}
esp_err_t espnow_service_send_raw_to_peer(const void *data, size_t len)
{
    if (!s_ready || !s_has_peer || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) return ESP_ERR_INVALID_STATE;
    return esp_now_send(s_peer, data, len);
}
esp_err_t espnow_service_start(void)
{
    if (s_ready) return ESP_OK;
    s_queue = xQueueCreate(8, sizeof(rx_event_t)); if (!s_queue) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_now_init(); if (err != ESP_OK) return err;
    if ((err = esp_now_register_recv_cb(recv_cb)) != ESP_OK) return err;
    if (!add_peer(BROADCAST)) return ESP_FAIL;
    load_peer();
    if (xTaskCreatePinnedToCore(rx_task, "kp_espnow", 3072, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    s_ready = true; ESP_LOGI(TAG, "ready packet=%uB paired=%d", (unsigned)sizeof(espnow_game_packet_t), s_has_peer);
    return ESP_OK;
}
