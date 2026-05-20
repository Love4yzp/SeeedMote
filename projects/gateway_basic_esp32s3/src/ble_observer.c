#include "ble_observer.h"
#include "adv_ring.h"
#include "bthome.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <inttypes.h>

static const char *TAG = "ble";

uint8_t gw_mac[6];

static bool s_ble_starting = false;

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

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    /* Rate-limited "we hear something" log to verify scan works. */
    static int64_t last_any_log_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_any_log_us > 1000000) {
        const uint8_t *a = event->disc.addr.val;
        ESP_LOGI(TAG, "adv heard: %02x%02x%02x%02x%02x%02x rssi=%d len=%d",
                 a[5], a[4], a[3], a[2], a[1], a[0],
                 event->disc.rssi, event->disc.length_data);
        last_any_log_us = now_us;
    }

    struct bthome_motion_event parsed = {0};
    bool matched = parse_bthome_adv(event->disc.data, event->disc.length_data,
                                    &parsed);
    if (matched)
        emit_event(event->disc.addr.val, event->disc.rssi, &parsed);

    raw_ring_push(event->disc.addr.val, event->disc.rssi,
                  event->disc.data, event->disc.length_data,
                  matched ? &parsed : NULL);
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl              = 0x00A0, /* 100 ms (units of 0.625 ms) */
        .window            = 0x0030, /* 30 ms; leaves airtime for Wi-Fi/AP */
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,      /* no SCAN_REQ; we only want adv data */
        .filter_duplicates = 0,      /* every packet matters (consumer dedups) */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &params, gap_event_cb, NULL);
    if (rc != 0)
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
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

static void ble_start_task(void *param)
{
    const char *reason = (const char *)param;
    ESP_LOGI(TAG, "starting BLE scanner (%s)", reason);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    nimble_port_freertos_init(host_task);

    vTaskDelete(NULL);
}

void schedule_ble_start(const char *reason)
{
    if (s_ble_starting) return;
    s_ble_starting = true;
    xTaskCreate(ble_start_task, "ble_start", 4096, (void *)reason, 5, NULL);
}
