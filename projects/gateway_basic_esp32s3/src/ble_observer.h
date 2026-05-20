#pragma once

#include <stdint.h>

/* Gateway BT MAC address — set in app_main before wifi_init(). */
extern uint8_t gw_mac[6];

/* Start NimBLE scanner in a one-shot task. Idempotent: ignored if already
 * starting. reason is a short string logged for diagnostics. */
void schedule_ble_start(const char *reason);
