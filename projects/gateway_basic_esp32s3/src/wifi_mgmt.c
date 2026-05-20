#include "wifi_mgmt.h"
#include "ble_observer.h"
#include "http_server.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "wifi";

#define DEFAULT_WIFI_SSID     "SEEED-COM"
#define DEFAULT_WIFI_PASSWORD "make0314"
#define WIFI_MAX_RETRIES      5
#define NVS_NS                "gw_cfg"
#define NVS_KEY_SSID          "wifi_ssid"
#define NVS_KEY_PASS          "wifi_pass"

char s_sta_ip[20]   = "0.0.0.0";
char s_ap_ssid[24]  = {};
char s_wifi_ssid[33] = DEFAULT_WIFI_SSID;

static char s_wifi_pass[65] = DEFAULT_WIFI_PASSWORD;
static int  s_wifi_retries  = 0;

void cred_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_wifi_ssid);
    if (nvs_get_str(h, NVS_KEY_SSID, s_wifi_ssid, &len) != ESP_OK)
        strncpy(s_wifi_ssid, DEFAULT_WIFI_SSID, sizeof(s_wifi_ssid) - 1);
    len = sizeof(s_wifi_pass);
    if (nvs_get_str(h, NVS_KEY_PASS, s_wifi_pass, &len) != ESP_OK)
        strncpy(s_wifi_pass, DEFAULT_WIFI_PASSWORD, sizeof(s_wifi_pass) - 1);
    nvs_close(h);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        start_httpd();
        ESP_LOGI(TAG, "AP ready: SSID='%s'  live: http://192.168.4.1/",
                 s_ap_ssid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        snprintf(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        if (s_wifi_retries < WIFI_MAX_RETRIES) {
            s_wifi_retries++;
            ESP_LOGW(TAG, "wifi retry %d/%d ...", s_wifi_retries, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "wifi unreachable after %d retries; AP live view remains at http://192.168.4.1/",
                     WIFI_MAX_RETRIES);
            schedule_ble_start("STA unavailable");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retries = 0;
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        start_httpd();
        ESP_LOGI(TAG, "STA connected: ip=%s  health: http://%s/health  live: http://%s/",
                 s_sta_ip, s_sta_ip, s_sta_ip);
        schedule_ble_start("STA connected");
    }
}

void wifi_init(void)
{
    cred_load();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     s_wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, s_wifi_pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.threshold.rssi = -85;

    snprintf(s_ap_ssid, sizeof(s_ap_ssid),
             "seeedmote-gw-%02x%02x", gw_mac[4], gw_mac[5]);
    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    start_httpd();
}
