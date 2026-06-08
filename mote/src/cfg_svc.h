/*
 * SeeedMote v2 — Web BT config GATT service.
 *
 * Exposes IMU wake-up tuning (THS, DUR) and a reboot command to a Web BT
 * client during the 30 s connectable config window. See `tools/web-bt/`.
 *
 * Backing storage is Zephyr settings/NVS: writes update the live IMU
 * registers and persist across reboot. Missing settings fall back to
 * LSM6DSL_WAKE_UP_THS_DEFAULT / _DUR_DEFAULT in main.c.
 */

#pragma once

#include <stdint.h>

/* main.c provides the runtime accessors backing the GATT characteristics. */
uint8_t imu_get_wake_ths(void);
uint8_t imu_get_wake_dur(void);
int imu_set_wake_ths(uint8_t value);
int imu_set_wake_dur(uint8_t value);
