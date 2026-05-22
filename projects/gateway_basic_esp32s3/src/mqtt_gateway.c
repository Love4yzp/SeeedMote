#include "mqtt_gateway.h"

#include "ble_observer.h"
#include "wifi_mgmt.h"

#include "cJSON.h"
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

/* Topic namespace per contracts/mqtt-uplink.yaml + mqtt-downlink.yaml. */
#ifndef SEEEDMOTE_MQTT_TOPIC_PREFIX
#define SEEEDMOTE_MQTT_TOPIC_PREFIX "mote/v1"
#endif

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static bool s_started;

static char s_gw_id[13];
static char s_client_id[32];
static char s_topic_event[80];
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

/* JSON-only command parsing per contracts/mqtt-downlink.yaml.
 * Bare-string payloads are explicitly rejected. */
static void handle_command(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "cmd payload is not valid JSON: %.*s", len, data);
        mqtt_gateway_publish_status("cmd_unknown");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd) || !cmd->valuestring) {
        ESP_LOGW(TAG, "cmd field missing or not a string");
        cJSON_Delete(root);
        mqtt_gateway_publish_status("cmd_unknown");
        return;
    }

    const char *c = cmd->valuestring;
    if (strcmp(c, "ping") == 0 || strcmp(c, "status") == 0) {
        mqtt_gateway_publish_status("cmd");
    } else if (strcmp(c, "start_ble") == 0) {
        schedule_ble_start("MQTT command");
        mqtt_gateway_publish_status("cmd_start_ble");
    } else {
        ESP_LOGW(TAG, "unknown cmd value: %s", c);
        mqtt_gateway_publish_status("cmd_unknown");
    }

    cJSON_Delete(root);
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

    ESP_LOGI(TAG, "client_id=%s event=%s status=%s cmd=%s all_cmd=%s",
             s_client_id, s_topic_event, s_topic_status, s_topic_cmd, s_topic_cmd_all);
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
    /* Event-driven status: published only on state transitions. No periodic
     * telemetry counters; per contracts/mqtt-uplink.yaml the status payload
     * carries only online/reason/gw_id/ip/wifi_ssid. */
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"reason\":\"%s\",\"gw_id\":\"%s\","
             "\"ip\":\"%s\",\"wifi_ssid\":\"%s\"}",
             reason ? reason : "status", s_gw_id, s_sta_ip, s_wifi_ssid);
    publish(s_topic_status, payload, 1, true);
}

void mqtt_gateway_publish_motion_event(const uint8_t *mote_addr_le, int8_t rssi,
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
}
