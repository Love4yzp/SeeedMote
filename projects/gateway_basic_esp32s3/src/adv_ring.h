#pragma once

#include "bthome.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stddef.h>
#include <stdint.h>

#define RAW_RING_SIZE 100
#define RAW_DATA_MAX   64

typedef struct {
    uint64_t seq;
    int64_t  ts_ms;
    uint8_t  addr[6];       /* MSB-first (display order) */
    int8_t   rssi;
    uint8_t  len;
    bool     matched;
    struct bthome_motion_event parsed;
    uint8_t  data[RAW_DATA_MAX];
} raw_adv_t;

extern raw_adv_t         s_raw_ring[RAW_RING_SIZE];
extern int               s_raw_head;
extern int               s_raw_count;
extern uint64_t          s_total_raw;      /* mote-only (BTHome matched) */
extern uint64_t          s_total_scanned;  /* all BLE advertisements seen */
extern SemaphoreHandle_t s_raw_mutex;

void adv_ring_init(void);
void adv_ring_count_scan(void); /* bump s_total_scanned for every BLE adv */

void raw_ring_push(const uint8_t *addr_le, int8_t rssi,
                   const uint8_t *data, size_t len,
                   const struct bthome_motion_event *parsed);

int append_hex(char *buf, size_t len, const uint8_t *data, size_t data_len);
