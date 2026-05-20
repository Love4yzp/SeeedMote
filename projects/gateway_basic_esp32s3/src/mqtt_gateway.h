#pragma once

#include "bthome.h"

#include <stddef.h>
#include <stdint.h>

void mqtt_gateway_init(void);
void mqtt_gateway_start(void);

void mqtt_gateway_publish_motion_event(const uint8_t *mote_addr_le, int8_t rssi,
                                       const uint8_t *adv_data, size_t adv_len,
                                       const struct bthome_motion_event *event);
void mqtt_gateway_publish_status(const char *reason);
