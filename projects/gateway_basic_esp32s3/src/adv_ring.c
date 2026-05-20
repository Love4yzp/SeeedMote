#include "adv_ring.h"

#include "esp_timer.h"

#include <string.h>

raw_adv_t         s_raw_ring[RAW_RING_SIZE];
int               s_raw_head      = 0;
int               s_raw_count     = 0;
uint64_t          s_total_raw     = 0;
uint64_t          s_total_scanned = 0;
SemaphoreHandle_t s_raw_mutex;

void adv_ring_init(void)
{
    s_raw_mutex = xSemaphoreCreateMutex();
    configASSERT(s_raw_mutex);
}

void adv_ring_count_scan(void)
{
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    s_total_scanned++;
    xSemaphoreGive(s_raw_mutex);
}

void raw_ring_push(const uint8_t *addr_le, int8_t rssi,
                   const uint8_t *data, size_t len,
                   const struct bthome_motion_event *parsed)
{
    raw_adv_t adv = {};
    adv.ts_ms  = esp_timer_get_time() / 1000;
    adv.addr[0] = addr_le[5];
    adv.addr[1] = addr_le[4];
    adv.addr[2] = addr_le[3];
    adv.addr[3] = addr_le[2];
    adv.addr[4] = addr_le[1];
    adv.addr[5] = addr_le[0];
    adv.rssi    = rssi;
    adv.matched = parsed != NULL;
    if (parsed) adv.parsed = *parsed;
    adv.len = (uint8_t)((len > RAW_DATA_MAX) ? RAW_DATA_MAX : len);
    memcpy(adv.data, data, adv.len);

    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    adv.seq = ++s_total_raw;
    s_raw_ring[s_raw_head] = adv;
    s_raw_head = (s_raw_head + 1) % RAW_RING_SIZE;
    if (s_raw_count < RAW_RING_SIZE) s_raw_count++;
    xSemaphoreGive(s_raw_mutex);
}

int append_hex(char *buf, size_t len, const uint8_t *data, size_t data_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t off = 0;
    for (size_t i = 0; i < data_len && off + 2 < len; i++) {
        buf[off++] = hex[data[i] >> 4];
        buf[off++] = hex[data[i] & 0x0f];
    }
    if (off < len) buf[off] = '\0';
    return (int)off;
}
