#include "http_server.h"
#include "adv_ring.h"
#include "ble_observer.h"
#include "wifi_mgmt.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http";

static httpd_handle_t s_httpd = NULL;
static raw_adv_t      s_index_scratch[RAW_RING_SIZE];

void http_server_init(void)
{
}

static esp_err_t send_literal(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sendf(httpd_req_t *req, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= sizeof(buf)) {
        ESP_LOGW(TAG, "sendf truncated: needed %d, have %u", n, (unsigned)sizeof(buf));
        n = (int)(sizeof(buf) - 1);
    }
    return httpd_resp_send_chunk(req, buf, n);
}

static bool query_view_is_raw(httpd_req_t *req)
{
    char query[64];
    char val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return false;
    if (httpd_query_key_value(query, "view", val, sizeof(val)) != ESP_OK)
        return false;
    return strcmp(val, "raw") == 0;
}

/* Parse `?refresh=N` query: N seconds of meta-refresh, 0 = disabled. Default 2. */
static int query_refresh_seconds(httpd_req_t *req)
{
    char query[64];
    char val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return 2;
    if (httpd_query_key_value(query, "refresh", val, sizeof(val)) != ESP_OK)
        return 2;
    int n = atoi(val);
    if (n < 0)  return 0;
    if (n > 60) return 60;
    return n;
}

/* Build `/[?view=raw][&|?refresh=N]` into buf. buf must be >= 32 bytes. */
static void build_url(char *buf, size_t len, bool raw_view, int refresh)
{
    char sep = '?';
    int  n   = snprintf(buf, len, "/");
    if (raw_view && n < (int)len) {
        n += snprintf(buf + n, len - n, "%cview=raw", sep);
        sep = '&';
    }
    if (refresh > 0 && n < (int)len) {
        snprintf(buf + n, len - n, "%crefresh=%d", sep, refresh);
    }
}

static esp_err_t h_health(httpd_req_t *req)
{
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    uint64_t total_raw      = s_total_raw;
    uint64_t total_scanned  = s_total_scanned;
    xSemaphoreGive(s_raw_mutex);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    esp_err_t err = sendf(req,
        "ok\n"
        "sta_ip=%s\n"
        "ap_ip=192.168.4.1\n"
        "ap_ssid=%s\n"
        "wifi_ssid=%s\n"
        "uptime_ms=%" PRId64 "\n"
        "scanned_total=%" PRIu64 "\n"
        "mote_total=%" PRIu64 "\n",
        s_sta_ip,
        s_ap_ssid,
        s_wifi_ssid,
        (int64_t)(esp_timer_get_time() / 1000),
        total_scanned,
        total_raw);
    if (err != ESP_OK) return err;

    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t h_index(httpd_req_t *req)
{
    bool raw_view = query_view_is_raw(req);
    int  refresh  = query_refresh_seconds(req);

    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    int snap_count          = s_raw_count;
    int snap_head           = s_raw_head;
    uint64_t total_raw      = s_total_raw;
    uint64_t total_scanned  = s_total_scanned;
    memcpy(s_index_scratch, s_raw_ring, RAW_RING_SIZE * sizeof(raw_adv_t));
    xSemaphoreGive(s_raw_mutex);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    if (send_literal(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>") != ESP_OK) goto fail;

    if (refresh > 0) {
        if (sendf(req, "<meta http-equiv='refresh' content='%d'>", refresh) != ESP_OK) goto fail;
    }

    if (send_literal(req,
        "<title>SeeedMote Gateway</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:16px;background:#f7f8fa;color:#17202a}"
        "header{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}"
        "h1{font-size:20px;margin:0}.muted{color:#667085}"
        ".tabs{display:inline-flex;border:1px solid #cfd7e3;border-radius:6px;overflow:hidden}"
        ".tabs a{padding:5px 10px;font-size:13px;text-decoration:none;color:#334155;background:#fff}"
        ".tabs a.active{background:#334155;color:#fff}"
        ".tabs a+a{border-left:1px solid #cfd7e3}"
        ".meta{display:flex;gap:12px;flex-wrap:wrap;margin:0 0 12px;font-size:13px}"
        "table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #d8dee8;table-layout:fixed}"
        "th,td{text-align:left;padding:7px 8px;border-bottom:1px solid #edf0f4;font-size:13px;vertical-align:top}"
        "th{background:#eef2f6;color:#334155}code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere}"
        ".ok{color:#047857;font-weight:600}.no{color:#64748b}"
        ".raw{display:none}body.raw-mode .parsed{display:none}body.raw-mode .raw{display:table-cell}"
        "</style></head>") != ESP_OK) goto fail;

    /* Build the six nav URLs. View links preserve refresh; refresh links preserve view. */
    char u_parsed[32], u_raw[32], u_off[32], u_1s[32], u_2s[32], u_5s[32];
    build_url(u_parsed, sizeof(u_parsed), false,    refresh);
    build_url(u_raw,    sizeof(u_raw),    true,     refresh);
    build_url(u_off,    sizeof(u_off),    raw_view, 0);
    build_url(u_1s,     sizeof(u_1s),     raw_view, 1);
    build_url(u_2s,     sizeof(u_2s),     raw_view, 2);
    build_url(u_5s,     sizeof(u_5s),     raw_view, 5);

    if (sendf(req,
        "<body%s><header><h1>SeeedMote Gateway</h1>"
        "<nav class='tabs'>"
        "<a class='%s' href='%s'>Parsed</a>"
        "<a class='%s' href='%s'>Raw</a>"
        "</nav>"
        "<nav class='tabs'>"
        "<a class='%s' href='%s'>Off</a>"
        "<a class='%s' href='%s'>1s</a>"
        "<a class='%s' href='%s'>2s</a>"
        "<a class='%s' href='%s'>5s</a>"
        "</nav>"
        "<span class='muted'>%" PRIu64 " mote / %" PRIu64 " scanned / %d shown</span>"
        "</header>",
        raw_view ? " class='raw-mode'" : "",
        raw_view ? ""       : "active", u_parsed,
        raw_view ? "active" : "",       u_raw,
        refresh == 0 ? "active" : "",   u_off,
        refresh == 1 ? "active" : "",   u_1s,
        refresh == 2 ? "active" : "",   u_2s,
        refresh == 5 ? "active" : "",   u_5s,
        total_raw, total_scanned, snap_count) != ESP_OK) goto fail;

    if (sendf(req,
        "<div class='meta'>"
        "<span>GW <code>%02x%02x%02x%02x%02x%02x</code></span>"
        "<span>STA <code>%s</code></span>"
        "<span>AP <code>192.168.4.1</code> <code>%s</code></span>"
        "<span>Up %" PRId64 " ms</span>"
        "</div>",
        gw_mac[0], gw_mac[1], gw_mac[2],
        gw_mac[3], gw_mac[4], gw_mac[5],
        s_sta_ip, s_ap_ssid,
        (int64_t)(esp_timer_get_time() / 1000)) != ESP_OK) goto fail;

    if (send_literal(req,
        "<table><thead><tr>"
        "<th style='width:64px'>Seq</th><th style='width:86px'>Time</th>"
        "<th style='width:150px'>Address</th><th style='width:58px'>RSSI</th>"
        "<th class='parsed' style='width:76px'>Moving</th>"
        "<th class='parsed' style='width:86px'>Vibration</th>"
        "<th class='parsed' style='width:58px'>PID</th>"
        "<th class='parsed' style='width:80px'>Count</th>"
        "<th class='raw' style='width:58px'>Len</th><th class='raw'>Data hex</th>"
        "</tr></thead><tbody>") != ESP_OK) goto fail;

    if (snap_count == 0) {
        if (send_literal(req, "<tr><td colspan='10' class='muted'>No advertisements yet</td></tr>") != ESP_OK) goto fail;
    }

    for (int i = 0; i < snap_count; i++) {
        int idx = (snap_head - 1 - i + RAW_RING_SIZE) % RAW_RING_SIZE;
        const raw_adv_t *adv = &s_index_scratch[idx];
        char hexbuf[RAW_DATA_MAX * 2 + 1];
        bool moving    = (adv->parsed.has_moving && adv->parsed.moving) ||
                         (adv->parsed.has_motion && adv->parsed.motion);
        bool vibration = adv->parsed.has_vibration && adv->parsed.vibration;
        append_hex(hexbuf, sizeof(hexbuf), adv->data, adv->len);
        if (sendf(req,
            "<tr><td>%" PRIu64 "</td><td>%" PRId64 "</td>"
            "<td><code>%02x:%02x:%02x:%02x:%02x:%02x</code></td>"
            "<td>%d</td><td class='parsed %s'>%s</td><td class='parsed %s'>%s</td>"
            "<td class='parsed'>%u</td><td class='parsed'>%" PRIu32 "</td>"
            "<td class='raw'>%u</td><td class='raw'><code>%s</code></td></tr>",
            adv->seq, adv->ts_ms,
            adv->addr[0], adv->addr[1], adv->addr[2],
            adv->addr[3], adv->addr[4], adv->addr[5],
            adv->rssi,
            moving    ? "ok" : "no", adv->matched ? (moving    ? "yes" : "no") : "-",
            vibration ? "ok" : "no", adv->matched ? (vibration ? "yes" : "no") : "-",
            adv->parsed.has_pid ? adv->parsed.pid : 255u,
            adv->parsed.has_ctr ? adv->parsed.ctr : 0u,
            (unsigned)adv->len,
            hexbuf) != ESP_OK) goto fail;
    }

    if (send_literal(req, "</tbody></table></body></html>") != ESP_OK) goto fail;
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;

fail:
    return ESP_FAIL;
}

static esp_err_t register_uri_checked(const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(s_httpd, uri);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "register %s failed: %s", uri->uri, esp_err_to_name(err));
    return err;
}

static void stop_httpd_after_start_error(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

void start_httpd(void)
{
    if (s_httpd) return;

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    cfg.stack_size       = 12288;
    cfg.max_open_sockets = 7;
    cfg.task_priority    = tskIDLE_PRIORITY + 7;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        s_httpd = NULL;
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }

    static const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = h_index,
    };
    if (register_uri_checked(&root) != ESP_OK) {
        stop_httpd_after_start_error();
        return;
    }

    static const httpd_uri_t health = {
        .uri = "/health", .method = HTTP_GET, .handler = h_health,
    };
    if (register_uri_checked(&health) != ESP_OK) {
        stop_httpd_after_start_error();
        return;
    }

    ESP_LOGI(TAG, "http server started (port 80, prio %d)", cfg.task_priority);
}
