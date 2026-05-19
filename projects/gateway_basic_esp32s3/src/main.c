/*
 * SeeedMote v2 — gateway_basic_esp32s3.
 *
 * Role: BLE gateway (always-on). Scans for SeeedMote BLE advertisements,
 *       decodes the manufacturer-specific payload, and emits one structured
 *       JSON line per packet to the UART console. First version is uplink-
 *       only; no Wi-Fi, no MQTT, no GATT central. v2 will flip NimBLE to
 *       OBSERVER+CENTRAL to proxy downlink Config Service writes.
 *
 * Board: Seeed XIAO ESP32-S3 (board id: seeed_xiao_esp32s3).
 *
 * Wire format: contracts/airframe.yaml v1 (11 bytes, little-endian). The
 * field offsets and enum codes below mirror that file — any change there
 * MUST land here in the same review, and vice versa.
 *
 * No deduplication is done here — that is the consumer's responsibility,
 * keyed on (mote_mac, boot, ctr).
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "gateway";

/* XIAO ESP32-S3 user LED is on GPIO21 (active low). */
#define LED_GPIO        GPIO_NUM_21
#define BLINK_PERIOD_MS 500

#define SEEEDMOTE_COMPANY_ID  0xFFFFu
#define SEEEDMOTE_PROTO_V1    0x01
#define MFG_PAYLOAD_LEN       11

#define EV_STILL    0x00
#define EV_MOVING   0x01
#define EV_PICKUP   0x02

#define BLE_AD_TYPE_MFG_DATA  0xFF

static uint8_t gw_mac[6];

static const char *event_name(uint8_t ev)
{
    switch (ev) {
    case EV_STILL:  return "STILL";
    case EV_MOVING: return "MOVING";
    case EV_PICKUP: return "PICK_UP";
    default:        return "UNK";
    }
}

static void emit_event(const uint8_t *mote_addr_le, int8_t rssi,
                       uint8_t ev, uint16_t boot, uint32_t ctr)
{
    int64_t ts_ms = esp_timer_get_time() / 1000;

    /* BLE addresses are stored little-endian; print MSB->LSB so the
     * output matches the colon-separated form seen in BLE tooling. */
    ESP_LOGI(TAG,
        "{\"ts\":%" PRId64
        ",\"gw_id\":\"%02x%02x%02x%02x%02x%02x\""
        ",\"mote_mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
        ",\"rssi\":%d"
        ",\"ev\":\"%s\""
        ",\"boot\":%u"
        ",\"ctr\":%" PRIu32 "}",
        ts_ms,
        gw_mac[0], gw_mac[1], gw_mac[2], gw_mac[3], gw_mac[4], gw_mac[5],
        mote_addr_le[5], mote_addr_le[4], mote_addr_le[3],
        mote_addr_le[2], mote_addr_le[1], mote_addr_le[0],
        rssi, event_name(ev), (unsigned)boot, ctr);
}

static void parse_seeedmote_adv(const uint8_t *data, size_t len,
                                const uint8_t *mote_addr_le, int8_t rssi)
{
    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end) {
        uint8_t ad_len = p[0];
        if (ad_len == 0 || p + 1 + ad_len > end) {
            return;
        }
        uint8_t ad_type = p[1];
        if (ad_type == BLE_AD_TYPE_MFG_DATA) {
            const uint8_t *mfg = p + 2;
            size_t mfg_len = ad_len - 1;
            /* contracts/airframe.yaml v1 fixes payload at exactly 11 bytes
             * with a zero reserved byte. Reject anything else so a future
             * proto_version bump (or a foreign device that happens to use
             * 0xFFFF) cannot smuggle frames past us. */
            if (mfg_len == MFG_PAYLOAD_LEN && mfg[10] == 0x00) {
                uint16_t cid = (uint16_t)mfg[0] | ((uint16_t)mfg[1] << 8);
                if (cid == SEEEDMOTE_COMPANY_ID && mfg[2] == SEEEDMOTE_PROTO_V1) {
                    uint8_t  ev   = mfg[3];
                    uint16_t boot = (uint16_t)mfg[4] | ((uint16_t)mfg[5] << 8);
                    uint32_t ctr  = (uint32_t)mfg[6]        |
                                    ((uint32_t)mfg[7] << 8)  |
                                    ((uint32_t)mfg[8] << 16) |
                                    ((uint32_t)mfg[9] << 24);
                    emit_event(mote_addr_le, rssi, ev, boot, ctr);
                }
            }
        }
        p += 1 + ad_len;
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        /* DEBUG: rate-limited "we hear something" log to verify scan works. */
        static int64_t last_any_log_us = 0;
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_any_log_us > 1000000) { /* once per second */
            const uint8_t *a = event->disc.addr.val;
            ESP_LOGI(TAG, "adv heard: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d len=%d",
                     a[5], a[4], a[3], a[2], a[1], a[0],
                     event->disc.rssi, event->disc.length_data);
            last_any_log_us = now_us;
        }
        parse_seeedmote_adv(event->disc.data, event->disc.length_data,
                            event->disc.addr.val, event->disc.rssi);
    }
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl              = 0x0020, /* 20 ms (units of 0.625 ms) */
        .window            = 0x0020, /* 20 ms — 100% duty for first version */
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,      /* no SCAN_REQ; we only want adv data */
        .filter_duplicates = 0,      /* every packet matters (consumer dedups) */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }
    start_scan();
    ESP_LOGI(TAG, "scanning for seeedmote adv (mfg_id=0x%04x)",
             SEEEDMOTE_COMPANY_ID);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "ble host reset, reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void blink_task(void *arg)
{
    (void)arg;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    bool on = false;
    while (1) {
        on = !on;
        gpio_set_level(LED_GPIO, on ? 0 : 1); /* active low */
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "seeedmote-v2 gateway_basic_esp32s3 starting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_read_mac(gw_mac, ESP_MAC_BT));
    ESP_LOGI(TAG, "gw_id=%02x%02x%02x%02x%02x%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    nimble_port_freertos_init(host_task);

    xTaskCreate(blink_task, "blink", 4096, NULL, 5, NULL);
}
