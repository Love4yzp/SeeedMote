#pragma once

#include <stdbool.h>
#include <stdint.h>

void mqtt_mgr_start(const char *broker, uint16_t port, const char *username, const char *password);
void mqtt_mgr_stop(void);
void mqtt_mgr_reconfigure(const char *broker, uint16_t port, const char *username, const char *password);
bool mqtt_mgr_is_connected(void);
void mqtt_mgr_publish_event(const char *mac, uint8_t packet_id, int rssi);
void mqtt_mgr_publish_seen(const char *mac, int rssi);
void mqtt_mgr_publish_status(void);
