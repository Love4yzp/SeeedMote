#include "ble_scanner.h"
#include "mqtt_mgr.h"
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

#define TAG "ble_scan"
#define BTHOME_UUID 0xFCD2
#define BTHOME_DEVINFO_V2_TRIGGER 0x44

#define MAX_MOTES 32

static struct {
    uint8_t mac[6];
    uint8_t pid;
    bool used;
} dedup_table[MAX_MOTES];

static int find_or_add_dedup(const uint8_t *mac)
{
    int empty = -1;
    for (int i = 0; i < MAX_MOTES; i++) {
        if (dedup_table[i].used && memcmp(dedup_table[i].mac, mac, 6) == 0)
            return i;
        if (!dedup_table[i].used && empty < 0)
            empty = i;
    }
    if (empty >= 0) {
        memcpy(dedup_table[empty].mac, mac, 6);
        dedup_table[empty].pid = 0xFF;
        dedup_table[empty].used = true;
    }
    return empty;
}

static void parse_bthome(const uint8_t *addr, int8_t rssi,
                         const uint8_t *data, uint8_t data_len)
{
    if (data_len < 1 || data[0] != BTHOME_DEVINFO_V2_TRIGGER) return;

    bool have_pid = false, have_moving = false, parse_err = false;
    uint8_t packet_id = 0, moving = 0;
    size_t i = 1;

    while (i < data_len) {
        uint8_t obj = data[i];
        size_t need;
        switch (obj) {
        case 0x00: need = 1; break;
        case 0x22: need = 1; break;
        case 0x2C: need = 1; break;
        case 0x3E: need = 4; break;
        default:   parse_err = true; need = 0; break;
        }
        if (parse_err) break;
        if (i + 1 + need > data_len) { parse_err = true; break; }
        if (obj == 0x00) { packet_id = data[i + 1]; have_pid = true; }
        else if (obj == 0x22) { moving = data[i + 1]; have_moving = true; }
        i += 1 + need;
    }

    if (parse_err || !have_pid || !have_moving) return;

    int idx = find_or_add_dedup(addr);
    if (idx < 0) return;
    if (dedup_table[idx].pid == packet_id) return;
    dedup_table[idx].pid = packet_id;

    // NimBLE stores addresses little-endian; print MSB-first to match ESPHome
    char mac_key[13];
    snprintf(mac_key, sizeof(mac_key), "%02x%02x%02x%02x%02x%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    if (moving == 1) {
        ESP_LOGI(TAG, "event mac=%s pid=%u rssi=%d", mac_key, packet_id, rssi);
        mqtt_mgr_publish_event(mac_key, packet_id, rssi);
    } else {
        ESP_LOGI(TAG, "seen mac=%s pid=%u rssi=%d", mac_key, packet_id, rssi);
        mqtt_mgr_publish_seen(mac_key, rssi);
    }
}

static void start_scan(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_gap_disc_desc *disc = &event->disc;

        // Walk AD structures for Service Data UUID 0xFCD2
        const uint8_t *ad = disc->data;
        uint8_t ad_len = disc->length_data;
        size_t pos = 0;

        while (pos < ad_len) {
            uint8_t len = ad[pos];
            if (len == 0 || pos + 1 + len > ad_len) break;
            uint8_t type = ad[pos + 1];

            // AD type 0x16 = Service Data - 16-bit UUID
            if (type == 0x16 && len >= 3) {
                uint16_t uuid = ad[pos + 2] | ((uint16_t)ad[pos + 3] << 8);
                if (uuid == BTHOME_UUID) {
                    parse_bthome(disc->addr.val, disc->rssi,
                                 &ad[pos + 4], len - 3);
                }
            }
            pos += 1 + len;
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "scan completed, restarting");
        start_scan();
        break;
    }
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .passive = 1,
        .itvl = 0x00A0,    // 100ms
        .window = 0x0090,   // 90ms
        .filter_duplicates = 0,
        .limited = 0,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "BLE synced, starting passive scan");
    start_scan();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_scanner_init(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE scanner initialized");
}
