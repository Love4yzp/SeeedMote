#include "nvs_config.h"
#include <string.h>
#include <nvs_flash.h>
#include <esp_log.h>

#define TAG "nvs_config"
#define NVS_NS "gw_cfg"

void nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void nvs_config_load(struct gw_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mqtt_port = 1883;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len;

    len = sizeof(cfg->wifi_ssid);
    if (nvs_get_str(h, "wifi_ssid", cfg->wifi_ssid, &len) == ESP_OK && len > 1) {
        len = sizeof(cfg->wifi_pass);
        nvs_get_str(h, "wifi_pass", cfg->wifi_pass, &len);
        cfg->has_wifi = true;
    }

    len = sizeof(cfg->mqtt_broker);
    if (nvs_get_str(h, "mqtt_broker", cfg->mqtt_broker, &len) == ESP_OK && len > 1) {
        len = sizeof(cfg->mqtt_pass);
        nvs_get_str(h, "mqtt_pass", cfg->mqtt_pass, &len);
        uint16_t port = 0;
        if (nvs_get_u16(h, "mqtt_port", &port) == ESP_OK && port > 0)
            cfg->mqtt_port = port;
        cfg->has_mqtt = true;
    }

    nvs_close(h);
}

void nvs_config_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifi_ssid", ssid);
    nvs_set_str(h, "wifi_pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "WiFi config saved");
}

void nvs_config_save_mqtt(const char *broker, uint16_t port, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "mqtt_broker", broker);
    nvs_set_u16(h, "mqtt_port", port);
    nvs_set_str(h, "mqtt_pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "MQTT config saved");
}
