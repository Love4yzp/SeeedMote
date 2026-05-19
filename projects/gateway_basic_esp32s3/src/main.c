/*
 * SeeedMote v2 — gateway_basic_esp32s3.
 *
 * Role: BLE gateway. Scans for SeeedMote BLE advertisements, decodes BTHome
 *       v2 Service Data, emits JSON to UART, and serves a live debug page over
 *       HTTP on the local Wi-Fi network.
 *
 *       HTTP endpoint:
 *         GET /        → browser UI for recent raw advertisement debug output
 *                        and live updates over the same path.
 *
 *       Wi-Fi: APSTA concurrent mode.
 *         STA connects to configured network first; AP "seeedmote-gw-XXYY"
 *         is always available at http://192.168.4.1/ for local debug output.
 *         Credentials live in NVS (namespace "gw_cfg"); compiled-in defaults
 *         are used on first boot or when NVS has no entry.
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
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

/* ── Wi-Fi credentials (NVS-backed) ─────────────────────────────────────── */
/* Compiled-in defaults are used on first boot or when NVS has no entry.    */
#define DEFAULT_WIFI_SSID     "SEEED-COM"
#define DEFAULT_WIFI_PASSWORD "make0314"
#define WIFI_MAX_RETRIES      5
#define NVS_NS                "gw_cfg"
#define NVS_KEY_SSID          "wifi_ssid"
#define NVS_KEY_PASS          "wifi_pass"

static char s_wifi_ssid[33] = DEFAULT_WIFI_SSID;
static char s_wifi_pass[65] = DEFAULT_WIFI_PASSWORD;
static int  s_wifi_retries  = 0;
static bool s_ble_starting  = false;

static void cred_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_wifi_ssid);
    if (nvs_get_str(h, NVS_KEY_SSID, s_wifi_ssid, &len) != ESP_OK)
        strncpy(s_wifi_ssid, DEFAULT_WIFI_SSID, sizeof(s_wifi_ssid) - 1);
    len = sizeof(s_wifi_pass);
    if (nvs_get_str(h, NVS_KEY_PASS, s_wifi_pass, &len) != ESP_OK)
        strncpy(s_wifi_pass, DEFAULT_WIFI_PASSWORD, sizeof(s_wifi_pass) - 1);
    nvs_close(h);
}

/* ── Raw advertisement debug ring ────────────────────────────────────────── */
#define RAW_RING_SIZE 100
#define RAW_DATA_MAX  64

typedef struct {
    uint64_t seq;
    int64_t  ts_ms;
    uint8_t  addr[6];       /* MSB-first (display order) */
    int8_t   rssi;
    uint8_t  len;
    bool     matched;
    uint8_t  data[RAW_DATA_MAX];
} raw_adv_t;

static raw_adv_t         s_raw_ring[RAW_RING_SIZE];
static int               s_raw_head  = 0;
static int               s_raw_count = 0;
static uint64_t          s_total_raw = 0;
static SemaphoreHandle_t s_raw_mutex;

static void raw_ring_push(const uint8_t *addr_le, int8_t rssi,
                          const uint8_t *data, size_t len, bool matched)
{
    raw_adv_t adv = {};
    adv.ts_ms = esp_timer_get_time() / 1000;
    adv.addr[0] = addr_le[5];
    adv.addr[1] = addr_le[4];
    adv.addr[2] = addr_le[3];
    adv.addr[3] = addr_le[2];
    adv.addr[4] = addr_le[1];
    adv.addr[5] = addr_le[0];
    adv.rssi = rssi;
    adv.matched = matched;
    adv.len = (uint8_t)((len > RAW_DATA_MAX) ? RAW_DATA_MAX : len);
    memcpy(adv.data, data, adv.len);

    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    adv.seq = ++s_total_raw;
    s_raw_ring[s_raw_head] = adv;
    s_raw_head = (s_raw_head + 1) % RAW_RING_SIZE;
    if (s_raw_count < RAW_RING_SIZE) s_raw_count++;
    xSemaphoreGive(s_raw_mutex);
}

/* ── Shared state ────────────────────────────────────────────────────────── */
static uint8_t        gw_mac[6];
static char           s_ap_ssid[24]  = {};
static char           s_sta_ip[20]   = "0.0.0.0";
static httpd_handle_t s_httpd        = NULL;

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

static int append_hex(char *buf, size_t len, const uint8_t *data, size_t data_len)
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

static int format_raw_json(char *buf, size_t len, const raw_adv_t *adv)
{
    int n = snprintf(buf, len,
        "{\"seq\":%" PRIu64
        ",\"ts\":%" PRId64
        ",\"addr\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
        ",\"rssi\":%d,\"len\":%u,\"matched\":%s,\"data\":\"",
        adv->seq, adv->ts_ms,
        adv->addr[0], adv->addr[1], adv->addr[2],
        adv->addr[3], adv->addr[4], adv->addr[5],
        adv->rssi, (unsigned)adv->len, adv->matched ? "true" : "false");
    if (n < 0 || (size_t)n >= len) return n;
    n += append_hex(buf + n, len - (size_t)n, adv->data, adv->len);
    if ((size_t)n + 3 <= len) {
        memcpy(buf + n, "\"}", 3);
        n += 2;
    }
    return n;
}

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

static bool parse_bthome_adv(const uint8_t *data, size_t len,
                             const uint8_t *mote_addr_le, int8_t rssi)
{
    const uint8_t *p = data;
    const uint8_t *end = data + len;
    bool matched = false;

    while (p < end) {
        uint8_t ad_len = p[0];
        if (ad_len == 0 || p + 1 + ad_len > end) {
            return matched;
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
                    return matched;
                }

                struct bthome_motion_event event = {0};
                if (parse_bthome_objects(svc + 3, svc_len - 3, &event)) {
                    emit_event(mote_addr_le, rssi, &event);
                    matched = true;
                }
            }
        }
        p += 1 + ad_len;
    }
    return matched;
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
        bool matched = parse_bthome_adv(event->disc.data, event->disc.length_data,
                                        event->disc.addr.val, event->disc.rssi);
        raw_ring_push(event->disc.addr.val, event->disc.rssi,
                      event->disc.data, event->disc.length_data, matched);
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

static void schedule_ble_start(const char *reason)
{
    if (s_ble_starting) return;
    s_ble_starting = true;
    xTaskCreate(ble_start_task, "ble_start", 4096, (void *)reason, 5, NULL);
}

/* ── HTTP handlers ───────────────────────────────────────────────────────── */

static esp_err_t send_literal(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sendf(httpd_req_t *req, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
    return httpd_resp_send_chunk(req, buf, n);
}

static esp_err_t send_html_escaped(httpd_req_t *req, const char *s)
{
    while (*s) {
        const char *esc = NULL;
        switch (*s) {
        case '&': esc = "&amp;"; break;
        case '<': esc = "&lt;"; break;
        case '>': esc = "&gt;"; break;
        case '"': esc = "&quot;"; break;
        case '\'': esc = "&#39;"; break;
        default: break;
        }
        esp_err_t err = esc ? send_literal(req, esc) : httpd_resp_send_chunk(req, s, 1);
        if (err != ESP_OK) return err;
        s++;
    }
    return ESP_OK;
}

static bool is_stream_request(httpd_req_t *req)
{
    char query[32];
    char stream[4];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, "stream", stream, sizeof(stream)) != ESP_OK) return false;
    return strcmp(stream, "1") == 0;
}

static uint64_t stream_last_seq(httpd_req_t *req)
{
    char query[64];
    char last[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "last", last, sizeof(last)) == ESP_OK) {
        return strtoull(last, NULL, 10);
    }

    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    uint64_t total = s_total_raw;
    xSemaphoreGive(s_raw_mutex);
    return total;
}

static esp_err_t h_stream(httpd_req_t *req)
{
    uint64_t last_seq = stream_last_seq(req);
    raw_adv_t *pending = malloc(RAW_RING_SIZE * sizeof(raw_adv_t));
    if (!pending) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    if (httpd_resp_send_chunk(req, ": connected\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        free(pending);
        return ESP_FAIL;
    }

    while (true) {
        int pending_count = 0;
        uint64_t newest_seq = last_seq;

        xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
        int snap_count = s_raw_count;
        int snap_start = (s_raw_count < RAW_RING_SIZE) ? 0 : s_raw_head;
        for (int i = 0; i < snap_count; i++) {
            int idx = (snap_start + i) % RAW_RING_SIZE;
            if (s_raw_ring[idx].seq > last_seq && pending_count < RAW_RING_SIZE) {
                pending[pending_count++] = s_raw_ring[idx];
                newest_seq = s_raw_ring[idx].seq;
            }
        }
        if (pending_count == 0 && s_total_raw > last_seq + RAW_RING_SIZE) {
            newest_seq = s_total_raw;
        }
        xSemaphoreGive(s_raw_mutex);

        for (int i = 0; i < pending_count; i++) {
            char json[320];
            char chunk[352];
            int json_len = format_raw_json(json, sizeof(json), &pending[i]);
            if (json_len < 0) continue;
            int chunk_len = snprintf(chunk, sizeof(chunk), "data: %.*s\n\n", json_len, json);
            if (httpd_resp_send_chunk(req, chunk, chunk_len) != ESP_OK) {
                free(pending);
                return ESP_FAIL;
            }
        }
        last_seq = newest_seq;

        if (pending_count == 0) {
            if (httpd_resp_send_chunk(req, ": keepalive\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                free(pending);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t h_index(httpd_req_t *req)
{
    if (is_stream_request(req)) return h_stream(req);

    raw_adv_t *snap = malloc(RAW_RING_SIZE * sizeof(raw_adv_t));
    if (!snap) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int snap_count;
    int snap_head;
    uint64_t total_raw;
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    snap_count = s_raw_count;
    snap_head = s_raw_head;
    total_raw = s_total_raw;
    memcpy(snap, s_raw_ring, RAW_RING_SIZE * sizeof(raw_adv_t));
    xSemaphoreGive(s_raw_mutex);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (send_literal(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SeeedMote Gateway</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:20px;background:#f7f8fa;color:#1f2933}"
        "header{display:flex;gap:16px;align-items:baseline;flex-wrap:wrap;margin-bottom:12px}"
        "h2{margin:0;font-size:22px}.muted{color:#637083}.pill{background:#e8edf3;border-radius:999px;padding:4px 10px;font-size:13px}"
        ".status{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin:0 0 16px}"
        ".stat{background:white;border:1px solid #d8dee8;padding:8px 10px}.label{font-size:12px;color:#637083}.value{font-size:14px;font-weight:600;overflow-wrap:anywhere}"
        "table{width:100%;border-collapse:collapse;background:white;border:1px solid #d8dee8;table-layout:fixed}"
        "th,td{text-align:left;padding:8px 10px;border-bottom:1px solid #edf0f4;font-size:13px;vertical-align:top}"
        "th{background:#eef2f6;color:#334155}code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere}"
        ".ok{color:#047857;font-weight:600}.no{color:#64748b}"
        "</style></head><body><header><h2>Raw BLE Debug</h2>"
        "<span id='state' class='pill'>connecting</span>") != ESP_OK) goto fail;

    if (sendf(req, "<span id='count' class='muted'>%" PRIu64 " total / %d shown</span></header>",
              total_raw, snap_count) != ESP_OK) goto fail;

    if (sendf(req,
        "<section class='status'>"
        "<div class='stat'><div class='label'>GW ID</div><div class='value'>%02x%02x%02x%02x%02x%02x</div></div>"
        "<div class='stat'><div class='label'>STA IP</div><div class='value'>%s</div></div>"
        "<div class='stat'><div class='label'>AP</div><div class='value'>192.168.4.1 / %s</div></div>"
        "<div class='stat'><div class='label'>Uptime</div><div class='value'>%" PRId64 " ms</div></div>"
        "<div class='stat'><div class='label'>Wi-Fi</div><div class='value'>",
        gw_mac[0], gw_mac[1], gw_mac[2],
        gw_mac[3], gw_mac[4], gw_mac[5],
        s_sta_ip, s_ap_ssid,
        (int64_t)(esp_timer_get_time() / 1000)) != ESP_OK) goto fail;

    if (send_html_escaped(req, s_wifi_ssid) != ESP_OK) goto fail;

    if (send_literal(req,
        "</div></div></section>"
        "<table><thead><tr><th style='width:64px'>Seq</th><th style='width:86px'>Time ms</th>"
        "<th style='width:150px'>Address</th><th style='width:58px'>RSSI</th>"
        "<th style='width:58px'>Len</th><th style='width:82px'>BTHome</th><th>Data hex</th>"
        "</tr></thead><tbody id='rows'>") != ESP_OK) goto fail;

    if (snap_count == 0) {
        if (send_literal(req, "<tr id='empty'><td colspan='7' class='muted'>No advertisements yet</td></tr>") != ESP_OK) goto fail;
    }

    for (int i = 0; i < snap_count; i++) {
        int idx = (snap_head - 1 - i + RAW_RING_SIZE) % RAW_RING_SIZE;
        char hexbuf[RAW_DATA_MAX * 2 + 1];
        append_hex(hexbuf, sizeof(hexbuf), snap[idx].data, snap[idx].len);
        if (sendf(req,
            "<tr><td>%" PRIu64 "</td><td>%" PRId64 "</td>"
            "<td><code>%02x:%02x:%02x:%02x:%02x:%02x</code></td>"
            "<td>%d</td><td>%u</td><td class='%s'>%s</td><td><code>%s</code></td></tr>",
            snap[idx].seq, snap[idx].ts_ms,
            snap[idx].addr[0], snap[idx].addr[1], snap[idx].addr[2],
            snap[idx].addr[3], snap[idx].addr[4], snap[idx].addr[5],
            snap[idx].rssi, (unsigned)snap[idx].len,
            snap[idx].matched ? "ok" : "no",
            snap[idx].matched ? "yes" : "no",
            hexbuf) != ESP_OK) goto fail;
    }

    if (sendf(req,
        "</tbody></table><script>"
        "const rows=document.getElementById('rows'),count=document.getElementById('count'),state=document.getElementById('state');"
        "let lastSeq=%" PRIu64 ";"
        "function row(e){return `<td>${e.seq}</td><td>${e.ts}</td><td><code>${e.addr}</code></td><td>${e.rssi}</td><td>${e.len}</td><td class='${e.matched?'ok':'no'}'>${e.matched?'yes':'no'}</td><td><code>${e.data}</code></td>`}"
        "function add(e){if(e.seq<=lastSeq)return;lastSeq=e.seq;const empty=document.getElementById('empty');if(empty)empty.remove();const tr=document.createElement('tr');tr.innerHTML=row(e);rows.prepend(tr);while(rows.children.length>100)rows.lastElementChild.remove();count.textContent=`${lastSeq} total / ${rows.children.length} shown`;}"
        "const es=new EventSource('/?stream=1&last='+lastSeq);"
        "es.onopen=()=>state.textContent='live';"
        "es.onerror=()=>state.textContent='reconnecting';"
        "es.onmessage=m=>add(JSON.parse(m.data));"
        "</script></body></html>",
        total_raw) != ESP_OK) goto fail;
    free(snap);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;

fail:
    free(snap);
    return ESP_FAIL;
}

static void start_httpd(void)
{
    if (s_httpd) return;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port    = 80;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = h_index,
    };
    httpd_register_uri_handler(s_httpd, &root);

    ESP_LOGI(TAG, "http server started (port 80)");
}

/* ── Wi-Fi ───────────────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        start_httpd();
        ESP_LOGI(TAG, "AP ready: SSID='%s'  live: http://192.168.4.1/",
                 s_ap_ssid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        snprintf(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        if (s_wifi_retries < WIFI_MAX_RETRIES) {
            s_wifi_retries++;
            ESP_LOGW(TAG, "wifi retry %d/%d ...", s_wifi_retries, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "wifi unreachable after %d retries; AP live view remains at http://192.168.4.1/",
                     WIFI_MAX_RETRIES);
            schedule_ble_start("STA unavailable");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retries = 0;
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "STA connected: ip=%s  live: http://%s/",
                 s_sta_ip, s_sta_ip);
        schedule_ble_start("STA connected");
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     s_wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, s_wifi_pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.threshold.rssi = -85;

    snprintf(s_ap_ssid, sizeof(s_ap_ssid),
             "seeedmote-gw-%02x%02x", gw_mac[4], gw_mac[5]);
    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
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

    s_raw_mutex = xSemaphoreCreateMutex();
    configASSERT(s_raw_mutex);

    cred_load();  /* must run before wifi_init() which reads s_wifi_ssid/pass */

    ESP_ERROR_CHECK(esp_read_mac(gw_mac, ESP_MAC_BT));
    ESP_LOGI(TAG, "gw_id=%02x%02x%02x%02x%02x%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);

    wifi_init();

    xTaskCreate(blink_task, "blink", 4096, NULL, 5, NULL);
}
