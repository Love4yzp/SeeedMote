#include "mqtt_gateway.h"

#include "adv_ring.h"
#include "ble_observer.h"
#include "wifi_mgmt.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef SEEEDMOTE_MQTT_BROKER_URI
#define SEEEDMOTE_MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
#endif

#ifndef SEEEDMOTE_MQTT_TOPIC_PREFIX
#define SEEEDMOTE_MQTT_TOPIC_PREFIX "mote"
#endif

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static bool s_started;

static char s_gw_id[13];
static char s_client_id[32];
static char s_topic_event[80];
static char s_topic_raw[80];
static char s_topic_status[80];
static char s_topic_cmd[80];
static char s_topic_cmd_all[80];

static int publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!s_client || !s_connected) return -1;
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed topic=%s", topic);
    }
    return msg_id;
}

static bool payload_has(const char *data, int len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || len < (int)needle_len) return false;
    for (int i = 0; i <= len - (int)needle_len; i++) {
        if (memcmp(data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static void handle_command(const char *data, int len)
{
    if (payload_has(data, len, "ping") ||
        payload_has(data, len, "status") ||
        payload_has(data, len, "\"cmd\":\"status\"")) {
        mqtt_gateway_publish_status("cmd");
        return;
    }

    if (payload_has(data, len, "start_ble") ||
        payload_has(data, len, "\"cmd\":\"start_ble\"")) {
        schedule_ble_start("MQTT command");
        mqtt_gateway_publish_status("cmd_start_ble");
        return;
    }

    ESP_LOGW(TAG, "unknown command: %.*s", len, data);
    mqtt_gateway_publish_status("cmd_unknown");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected broker=%s", SEEEDMOTE_MQTT_BROKER_URI);
        esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
        esp_mqtt_client_subscribe(s_client, s_topic_cmd_all, 1);
        mqtt_gateway_publish_status("connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "rx topic=%.*s payload=%.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        handle_command(event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "transport error");
        break;
    default:
        break;
    }
}

void mqtt_gateway_init(void)
{
    snprintf(s_gw_id, sizeof(s_gw_id), "%02x%02x%02x%02x%02x%02x",
             gw_mac[0], gw_mac[1], gw_mac[2], gw_mac[3], gw_mac[4], gw_mac[5]);
    snprintf(s_client_id, sizeof(s_client_id), "seeedmote-gw-%s", s_gw_id + 8);
    snprintf(s_topic_event, sizeof(s_topic_event), "%s/%s/event",
             SEEEDMOTE_MQTT_TOPIC_PREFIX, s_gw_id);
    snprintf(s_topic_raw, sizeof(s_topic_raw), "%s/%s/raw",
             SEEEDMOTE_MQTT_TOPIC_PREFIX, s_gw_id);
    snprintf(s_topic_status, sizeof(s_topic_status), "%s/%s/status",
             SEEEDMOTE_MQTT_TOPIC_PREFIX, s_gw_id);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "%s/%s/cmd",
             SEEEDMOTE_MQTT_TOPIC_PREFIX, s_gw_id);
    snprintf(s_topic_cmd_all, sizeof(s_topic_cmd_all), "%s/all/cmd",
             SEEEDMOTE_MQTT_TOPIC_PREFIX);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = SEEEDMOTE_MQTT_BROKER_URI,
        .credentials.client_id = s_client_id,
        .session.last_will.topic = s_topic_status,
        .session.last_will.msg = "{\"online\":false}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    s_client = esp_mqtt_client_init(&cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_LOGI(TAG, "client_id=%s event=%s raw=%s cmd=%s all_cmd=%s",
             s_client_id, s_topic_event, s_topic_raw, s_topic_cmd, s_topic_cmd_all);
}

void mqtt_gateway_start(void)
{
    if (!s_client) mqtt_gateway_init();
    if (s_started) return;
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    s_started = true;
}

void mqtt_gateway_publish_status(const char *reason)
{
    uint64_t total_raw;
    uint64_t total_scanned;

    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    total_raw = s_total_raw;
    total_scanned = s_total_scanned;
    xSemaphoreGive(s_raw_mutex);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"reason\":\"%s\",\"gw_id\":\"%s\","
             "\"ip\":\"%s\",\"wifi_ssid\":\"%s\",\"total_scanned\":%" PRIu64
             ",\"total_matched\":%" PRIu64 "}",
             reason ? reason : "status", s_gw_id, s_sta_ip, s_wifi_ssid,
             total_scanned, total_raw);
    publish(s_topic_status, payload, 1, true);
}

void mqtt_gateway_publish_motion_event(const uint8_t *mote_addr_le, int8_t rssi,
                                       const uint8_t *adv_data, size_t adv_len,
                                       const struct bthome_motion_event *event)
{
    if (!event) return;

    int64_t ts_ms = esp_timer_get_time() / 1000;
    bool moving = (event->has_moving && event->moving) ||
                  (event->has_motion && event->motion);
    bool vibration = event->has_vibration && event->vibration;

    char mote_id[13];
    snprintf(mote_id, sizeof(mote_id), "%02x%02x%02x%02x%02x%02x",
             mote_addr_le[5], mote_addr_le[4], mote_addr_le[3],
             mote_addr_le[2], mote_addr_le[1], mote_addr_le[0]);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"ts\":%" PRId64 ",\"gw_id\":\"%s\",\"mote_mac\":\"%s\","
             "\"rssi\":%d,\"moving\":%s,\"vibration\":%s,"
             "\"pid\":%u,\"ctr\":%" PRIu32 "}",
             ts_ms, s_gw_id, mote_id, rssi,
             moving ? "true" : "false",
             vibration ? "true" : "false",
             event->has_pid ? event->pid : 255u,
             event->has_ctr ? event->ctr : 0u);
    publish(s_topic_event, payload, 1, false);

    char hex[(RAW_DATA_MAX * 2) + 1];
    size_t capped_len = adv_len > RAW_DATA_MAX ? RAW_DATA_MAX : adv_len;
    append_hex(hex, sizeof(hex), adv_data, capped_len);

    char raw_payload[256];
    snprintf(raw_payload, sizeof(raw_payload),
             "{\"ts\":%" PRId64 ",\"gw_id\":\"%s\",\"mote_mac\":\"%s\","
             "\"rssi\":%d,\"len\":%u,\"data\":\"%s\"}",
             ts_ms, s_gw_id, mote_id, rssi, (unsigned)capped_len, hex);
    publish(s_topic_raw, raw_payload, 0, false);
}
