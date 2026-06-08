#include "mqtt_mgr.h"
#include "gw_id.h"
#include "led.h"
#include <string.h>
#include <stdio.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include <cJSON.h>

#define TAG "mqtt_mgr"
#define CMD_TOPIC "seeedmote/gateway/cmd"

static esp_mqtt_client_handle_t client;
static bool connected;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        connected = true;
        esp_mqtt_client_subscribe(client, CMD_TOPIC, 1);
        mqtt_mgr_publish_status();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        connected = false;
        break;

    case MQTT_EVENT_DATA: {
        if (event->topic_len == 0 || event->data_len == 0) break;
        if (event->topic_len != (int)strlen(CMD_TOPIC)) break;
        if (memcmp(event->topic, CMD_TOPIC, event->topic_len) != 0) break;

        char *data = malloc(event->data_len + 1);
        if (!data) break;
        memcpy(data, event->data, event->data_len);
        data[event->data_len] = '\0';

        cJSON *j = cJSON_Parse(data);
        free(data);
        if (!j) break;

        const char *gw = cJSON_GetStringValue(cJSON_GetObjectItem(j, "gw"));
        const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(j, "cmd"));

        if (gw && cmd && strcmp(gw, gw_id_get()) == 0) {
            if (strcmp(cmd, "locate") == 0) {
                ESP_LOGI(TAG, "locate command received");
                led_locate_blink();
            }
        }
        cJSON_Delete(j);
        break;
    }
    default:
        break;
    }
}

void mqtt_mgr_start(const char *broker, uint16_t port,
                     const char *username, const char *password)
{
    if (!broker || !broker[0]) return;

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", broker, port);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = username,
        .credentials.authentication.password =
            (password && password[0]) ? password : NULL,
    };

    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "MQTT client started -> %s", uri);
}

void mqtt_mgr_stop(void)
{
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
        connected = false;
    }
}

void mqtt_mgr_reconfigure(const char *broker, uint16_t port,
                           const char *username, const char *password)
{
    mqtt_mgr_stop();
    mqtt_mgr_start(broker, port, username, password);
}

bool mqtt_mgr_is_connected(void)
{
    return connected;
}

void mqtt_mgr_publish_status(void)
{
    if (!client || !connected) return;

    const char *gw = gw_id_get();
    char topic[80];
    snprintf(topic, sizeof(topic), "seeedmote/gateway/%s/status", gw);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"gw\":\"%s\",\"version\":\"%s\"}", gw, GATEWAY_FW_VERSION);

    esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
}

void mqtt_mgr_publish_event(const char *mac, uint8_t packet_id, int rssi)
{
    if (!client || !connected) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "seeedmote/mote/%s/event", mac);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"packet_id\":%u,\"rssi\":%d,\"gw\":\"%s\"}",
             packet_id, rssi, gw_id_get());

    esp_mqtt_client_publish(client, topic, payload, 0, 0, 0);
}

void mqtt_mgr_publish_seen(const char *mac, int rssi)
{
    if (!client || !connected) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "seeedmote/mote/%s/seen", mac);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"rssi\":%d,\"gw\":\"%s\",\"reason\":\"boot\"}",
             rssi, gw_id_get());

    esp_mqtt_client_publish(client, topic, payload, 0, 0, 0);
}
