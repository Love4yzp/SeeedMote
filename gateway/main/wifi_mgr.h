#pragma once

#include <stdbool.h>
#include <cJSON.h>

typedef void (*wifi_mgr_on_connect_cb_t)(void);

void wifi_mgr_init(const char *ap_name, const char *ssid, const char *pass);
void wifi_mgr_set_on_connect(wifi_mgr_on_connect_cb_t cb);
void wifi_mgr_connect(const char *ssid, const char *pass);
bool wifi_mgr_is_connected(void);
const char *wifi_mgr_get_ip(void);
cJSON *wifi_mgr_scan(void);
