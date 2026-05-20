#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BTHOME_UUID_LSB           0xD2u
#define BTHOME_UUID_MSB           0xFCu
#define BTHOME_VERSION_V2         0x02u
#define BTHOME_ENCRYPTED_FLAG     0x01u

#define BTHOME_OBJ_PACKET_ID      0x00u
#define BTHOME_OBJ_BATTERY        0x01u
#define BTHOME_OBJ_COUNT_U8       0x09u
#define BTHOME_OBJ_MOTION         0x21u
#define BTHOME_OBJ_MOVING         0x22u
#define BTHOME_OBJ_VIBRATION      0x2Cu
#define BTHOME_OBJ_COUNT_U16      0x3Du
#define BTHOME_OBJ_COUNT_U32      0x3Eu
#define BTHOME_OBJ_DEVICE_TYPE_ID 0xF0u
#define BTHOME_OBJ_FW_VERSION_U32 0xF1u
#define BTHOME_OBJ_FW_VERSION_U24 0xF2u

#define BLE_AD_TYPE_SERVICE_DATA16 0x16

struct bthome_motion_event {
    bool     has_motion;
    bool     motion;
    bool     has_moving;
    bool     moving;
    bool     has_vibration;
    bool     vibration;
    bool     has_pid;
    uint8_t  pid;
    bool     has_ctr;
    uint32_t ctr;
};

/* Parse BTHome v2 AD structures from a raw advertisement payload.
 * Returns true if at least one motion/vibration/count field was decoded.
 * Pure function — no logging, no side effects. */
bool parse_bthome_adv(const uint8_t *data, size_t len,
                      struct bthome_motion_event *out_event);
