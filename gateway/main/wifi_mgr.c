#include "wifi_mgr.h"
#include "led.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#define TAG "wifi_mgr"
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t wifi_events;
static char ip_str[16];
static TimerHandle_t reconnect_timer;
static wifi_mgr_on_connect_cb_t on_connect_cb;

static void reconnect_cb(TimerHandle_t t)
{
    ESP_LOGI(TAG, "STA reconnect attempt");
    esp_wifi_connect();
}

static void ensure_apsta(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_LOGI(TAG, "Restored APSTA mode");
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            xTimerStart(reconnect_timer, 0);
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = data;
            xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
            ip_str[0] = '\0';
            ESP_LOGW(TAG, "STA disconnected reason=%d, retry in 5s", d->reason);
            xTimerStart(reconnect_timer, 0);
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP client connected");
            led_locate_blink();
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA connected, IP: %s", ip_str);
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
        ensure_apsta();
        if (on_connect_cb) {
            on_connect_cb();
            on_connect_cb = NULL;
        }
    }
}

void wifi_mgr_init(const char *ap_name, const char *ssid, const char *pass)
{
    wifi_events = xEventGroupCreate();
    ip_str[0] = '\0';
    reconnect_timer = xTimerCreate("wifi_rc", pdMS_TO_TICKS(2000),
                                   pdFALSE, NULL, reconnect_cb);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);

    // Always APSTA so the config AP never disappears
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, ap_name, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(ap_name);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    bool has_sta = ssid && ssid[0];
    if (has_sta) {
        wifi_config_t sta_cfg = {};
        strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
        if (pass && pass[0])
            strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi APSTA started, AP: %s, STA: %s",
             ap_name, has_sta ? ssid : "none");
}

void wifi_mgr_set_on_connect(wifi_mgr_on_connect_cb_t cb)
{
    on_connect_cb = cb;
}

void wifi_mgr_connect(const char *ssid, const char *pass)
{
    esp_wifi_disconnect();

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (pass && pass[0])
        strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);

    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();
}

bool wifi_mgr_is_connected(void)
{
    return (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) != 0;
}

const char *wifi_mgr_get_ip(void)
{
    return ip_str;
}

cJSON *wifi_mgr_scan(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_wifi_scan_start(&scan_cfg, true);

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 20) count = 20;

    wifi_ap_record_t *records = calloc(count, sizeof(wifi_ap_record_t));
    if (!records) return cJSON_CreateArray();

    esp_wifi_scan_get_ap_records(&count, records);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        if (records[i].ssid[0] == '\0') continue;

        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp((char *)records[i].ssid, (char *)records[j].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(entry, "rssi", records[i].rssi);
        const char *auth = (records[i].authmode == WIFI_AUTH_OPEN) ? "open" : "secured";
        cJSON_AddStringToObject(entry, "auth", auth);
        cJSON_AddItemToArray(arr, entry);
    }

    free(records);
    return arr;
}
