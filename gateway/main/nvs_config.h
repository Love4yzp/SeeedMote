#pragma once

#include <stdint.h>
#include <stdbool.h>

struct gw_config {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_broker[65];
    uint16_t mqtt_port;
    char mqtt_pass[65];
    bool has_wifi;
    bool has_mqtt;
};

void nvs_config_init(void);
void nvs_config_load(struct gw_config *cfg);
void nvs_config_save_wifi(const char *ssid, const char *pass);
void nvs_config_save_mqtt(const char *broker, uint16_t port, const char *pass);
