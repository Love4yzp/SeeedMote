/*
 * SeeedMote v2 — gateway_basic_esp32s3.
 *
 * Role: BLE gateway (always-on). Scans for SeeedMote BLE advertisements,
 *       decodes BTHome v2 Service Data, and emits one structured JSON line
 *       per packet to the UART console. First version is uplink-only; no
 *       Wi-Fi, no MQTT, no GATT central.
 *
 * Board: Seeed XIAO ESP32-S3 (board id: seeed_xiao_esp32s3).
 *
 * Wire format (mote → gateway, BTHome v2 Service Data). Must stay in
 * lockstep with contracts/airframe.yaml and the mote implementation.
 *
 *   AD type 0x16 Service Data - 16-bit UUID
 *   [2B] UUID               = 0xFCD2, little-endian on air (D2 FC)
 *   [1B] device_info        = BTHome v2, trigger-based, unencrypted (0x44)
 *   [2B] packet id          = object 0x00, uint8 duplicate filter
 *   [2B] moving             = object 0x22, uint8
 *   [2B] vibration          = object 0x2C, uint8 (PICK_UP pulse)
 *   [5B] count              = object 0x3E, uint32 event counter
 *
 * No deduplication is done here. Consumers can use (mote_mac, ctr) for
 * repeated advertisements, with the usual reboot caveat unless counters are
 * made persistent in a later product build.
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

#define BTHOME_UUID_LSB       0xD2u
#define BTHOME_UUID_MSB       0xFCu
#define BTHOME_VERSION_V2     0x02u
#define BTHOME_ENCRYPTED_FLAG 0x01u

#define BTHOME_OBJ_PACKET_ID  0x00u
#define BTHOME_OBJ_BATTERY    0x01u
#define BTHOME_OBJ_COUNT_U8   0x09u
#define BTHOME_OBJ_MOTION     0x21u
#define BTHOME_OBJ_MOVING     0x22u
#define BTHOME_OBJ_VIBRATION  0x2Cu
#define BTHOME_OBJ_COUNT_U16  0x3Du
#define BTHOME_OBJ_COUNT_U32  0x3Eu
#define BTHOME_OBJ_DEVICE_TYPE_ID 0xF0u
#define BTHOME_OBJ_FW_VERSION_U32 0xF1u
#define BTHOME_OBJ_FW_VERSION_U24 0xF2u

#define BLE_AD_TYPE_SERVICE_DATA16 0x16

static uint8_t gw_mac[6];

struct bthome_motion_event {
    bool has_motion;
    bool motion;
    bool has_moving;
    bool moving;
    bool has_vibration;
    bool vibration;
    bool has_pid;
    uint8_t pid;
    bool has_ctr;
    uint32_t ctr;
};

static void emit_event(const uint8_t *mote_addr_le, int8_t rssi,
                       const struct bthome_motion_event *event)
{
    int64_t ts_ms = esp_timer_get_time() / 1000;

    /* BLE addresses are stored little-endian; print MSB->LSB as compact
     * 12-hex identifiers so gateway and mote ids share one JSON format. */
    ESP_LOGI(TAG,
        "{\"ts\":%" PRId64
        ",\"gw_id\":\"%02x%02x%02x%02x%02x%02x\""
        ",\"mote_mac\":\"%02x%02x%02x%02x%02x%02x\""
        ",\"rssi\":%d"
        ",\"moving\":%s"
        ",\"vibration\":%s"
        ",\"pid\":%u"
        ",\"ctr\":%" PRIu32 "}",
        ts_ms,
        gw_mac[0], gw_mac[1], gw_mac[2], gw_mac[3], gw_mac[4], gw_mac[5],
        mote_addr_le[5], mote_addr_le[4], mote_addr_le[3],
        mote_addr_le[2], mote_addr_le[1], mote_addr_le[0],
        rssi,
        ((event->has_moving && event->moving) ||
         (event->has_motion && event->motion)) ? "true" : "false",
        (event->has_vibration && event->vibration) ? "true" : "false",
        event->has_pid ? event->pid : 255u,
        event->has_ctr ? event->ctr : 0u);
}

static uint16_t bthome_get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t bthome_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0]        |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool parse_bthome_objects(const uint8_t *p, size_t len,
                                 struct bthome_motion_event *event)
{
    const uint8_t *end = p + len;

    while (p < end) {
        uint8_t object_id = p[0];
        size_t remaining = (size_t)(end - p);

        switch (object_id) {
        case BTHOME_OBJ_PACKET_ID:
            if (remaining < 2) {
                return false;
            }
            event->has_pid = true;
            event->pid = p[1];
            p += 2;
            break;

        case BTHOME_OBJ_BATTERY:
            if (remaining < 2) {
                return false;
            }
            p += 2;
            break;

        case BTHOME_OBJ_COUNT_U8:
            if (remaining < 2) {
                return false;
            }
            event->has_ctr = true;
            event->ctr = p[1];
            p += 2;
            break;

        case BTHOME_OBJ_MOTION:
            if (remaining < 2) {
                return false;
            }
            event->has_motion = true;
            event->motion = p[1] != 0;
            p += 2;
            break;

        case BTHOME_OBJ_MOVING:
            if (remaining < 2) {
                return false;
            }
            event->has_moving = true;
            event->moving = p[1] != 0;
            p += 2;
            break;

        case BTHOME_OBJ_VIBRATION:
            if (remaining < 2) {
                return false;
            }
            event->has_vibration = true;
            event->vibration = p[1] != 0;
            p += 2;
            break;

        case BTHOME_OBJ_COUNT_U16:
            if (remaining < 3) {
                return false;
            }
            event->has_ctr = true;
            event->ctr = bthome_get_le16(&p[1]);
            p += 3;
            break;

        case BTHOME_OBJ_COUNT_U32:
            if (remaining < 5) {
                return false;
            }
            event->has_ctr = true;
            event->ctr = bthome_get_le32(&p[1]);
            p += 5;
            break;

        case BTHOME_OBJ_DEVICE_TYPE_ID:
            if (remaining < 3) {
                return false;
            }
            p += 3;
            break;

        case BTHOME_OBJ_FW_VERSION_U32:
            if (remaining < 5) {
                return false;
            }
            p += 5;
            break;

        case BTHOME_OBJ_FW_VERSION_U24:
            if (remaining < 4) {
                return false;
            }
            p += 4;
            break;

        default:
            /* BTHome receivers stop at unknown object ids. Emit what we
             * already understood instead of guessing the remaining layout. */
            return event->has_motion || event->has_moving ||
                   event->has_vibration || event->has_ctr;
        }
    }

    return event->has_motion || event->has_moving ||
           event->has_vibration || event->has_ctr;
}

static void parse_bthome_adv(const uint8_t *data, size_t len,
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
        if (ad_type == BLE_AD_TYPE_SERVICE_DATA16) {
            const uint8_t *svc = p + 2;
            size_t svc_len = ad_len - 1;
            if (svc_len >= 3 &&
                svc[0] == BTHOME_UUID_LSB &&
                svc[1] == BTHOME_UUID_MSB) {
                uint8_t device_info = svc[2];
                uint8_t version = (device_info >> 5) & 0x07;
                if ((device_info & BTHOME_ENCRYPTED_FLAG) != 0 ||
                    version != BTHOME_VERSION_V2) {
                    return;
                }

                struct bthome_motion_event event = {0};
                if (parse_bthome_objects(svc + 3, svc_len - 3, &event)) {
                    emit_event(mote_addr_le, rssi, &event);
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
            ESP_LOGI(TAG, "adv heard: %02x%02x%02x%02x%02x%02x rssi=%d len=%d",
                     a[5], a[4], a[3], a[2], a[1], a[0],
                     event->disc.rssi, event->disc.length_data);
            last_any_log_us = now_us;
        }
        parse_bthome_adv(event->disc.data, event->disc.length_data,
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
    ESP_LOGI(TAG, "scanning for BTHome v2 service UUID 0xFCD2");
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
