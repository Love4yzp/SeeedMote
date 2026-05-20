#include "http_server.h"
#include "adv_ring.h"
#include "ble_observer.h"
#include "wifi_mgmt.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "http";

static httpd_handle_t    s_httpd              = NULL;
static raw_adv_t         s_http_scratch[RAW_RING_SIZE];
static SemaphoreHandle_t s_http_scratch_mutex;

void http_server_init(void)
{
    s_http_scratch_mutex = xSemaphoreCreateMutex();
    configASSERT(s_http_scratch_mutex);
}

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
    if ((size_t)n >= sizeof(buf)) n = (int)(sizeof(buf) - 1);
    return httpd_resp_send_chunk(req, buf, n);
}

static int format_raw_json(char *buf, size_t len, const raw_adv_t *adv)
{
    int n = snprintf(buf, len,
        "{\"seq\":%" PRIu64
        ",\"ts\":%" PRId64
        ",\"addr\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
        ",\"rssi\":%d,\"len\":%u,\"matched\":%s"
        ",\"moving\":%s,\"vibration\":%s,\"pid\":%u,\"ctr\":%" PRIu32
        ",\"data\":\"",
        adv->seq, adv->ts_ms,
        adv->addr[0], adv->addr[1], adv->addr[2],
        adv->addr[3], adv->addr[4], adv->addr[5],
        adv->rssi, (unsigned)adv->len, adv->matched ? "true" : "false",
        ((adv->parsed.has_moving && adv->parsed.moving) ||
         (adv->parsed.has_motion && adv->parsed.motion)) ? "true" : "false",
        (adv->parsed.has_vibration && adv->parsed.vibration) ? "true" : "false",
        adv->parsed.has_pid ? adv->parsed.pid : 255u,
        adv->parsed.has_ctr ? adv->parsed.ctr : 0u);
    if (n < 0 || (size_t)n >= len) return n;
    n += append_hex(buf + n, len - (size_t)n, adv->data, adv->len);
    if ((size_t)n + 3 <= len) {
        memcpy(buf + n, "\"}", 3);
        n += 2;
    }
    return n;
}

static uint64_t raw_last_seq(httpd_req_t *req)
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

static esp_err_t h_raw_json(httpd_req_t *req)
{
    uint64_t last_seq = raw_last_seq(req);
    int pending_count = 0;
    uint64_t newest_seq = last_seq;

    xSemaphoreTake(s_http_scratch_mutex, portMAX_DELAY);
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    int snap_count = s_raw_count;
    int snap_start = (s_raw_count < RAW_RING_SIZE) ? 0 : s_raw_head;
    uint64_t total_raw = s_total_raw;
    for (int i = 0; i < snap_count; i++) {
        int idx = (snap_start + i) % RAW_RING_SIZE;
        if (s_raw_ring[idx].seq > last_seq && pending_count < RAW_RING_SIZE) {
            s_http_scratch[pending_count++] = s_raw_ring[idx];
            newest_seq = s_raw_ring[idx].seq;
        }
    }
    if (pending_count == 0 && total_raw > last_seq + RAW_RING_SIZE)
        newest_seq = total_raw;
    xSemaphoreGive(s_raw_mutex);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    if (sendf(req, "{\"total\":%" PRIu64 ",\"last\":%" PRIu64 ",\"events\":[",
              total_raw, newest_seq) != ESP_OK) goto fail;

    for (int i = 0; i < pending_count; i++) {
        char json[320];
        int json_len = format_raw_json(json, sizeof(json), &s_http_scratch[i]);
        if (json_len < 0) continue;
        if (i > 0 && send_literal(req, ",") != ESP_OK) goto fail;
        if (httpd_resp_send_chunk(req, json, json_len) != ESP_OK) goto fail;
    }

    if (send_literal(req, "]}") != ESP_OK) goto fail;
    xSemaphoreGive(s_http_scratch_mutex);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;

fail:
    xSemaphoreGive(s_http_scratch_mutex);
    return ESP_FAIL;
}

static esp_err_t h_health(httpd_req_t *req)
{
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    uint64_t total_raw = s_total_raw;
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
        "raw_total=%" PRIu64 "\n",
        s_sta_ip,
        s_ap_ssid,
        s_wifi_ssid,
        (int64_t)(esp_timer_get_time() / 1000),
        total_raw);
    if (err != ESP_OK) return err;

    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t h_index(httpd_req_t *req)
{
    xSemaphoreTake(s_http_scratch_mutex, portMAX_DELAY);
    xSemaphoreTake(s_raw_mutex, portMAX_DELAY);
    int snap_count  = s_raw_count;
    int snap_head   = s_raw_head;
    uint64_t total_raw = s_total_raw;
    memcpy(s_http_scratch, s_raw_ring, RAW_RING_SIZE * sizeof(raw_adv_t));
    xSemaphoreGive(s_raw_mutex);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    if (send_literal(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SeeedMote Gateway</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:16px;background:#f7f8fa;color:#17202a}"
        "header{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}"
        "h1{font-size:20px;margin:0}.muted{color:#667085}.pill,button{border:1px solid #cfd7e3;background:#fff;border-radius:6px;padding:5px 9px;font-size:13px}"
        "button{cursor:pointer}.meta{display:flex;gap:12px;flex-wrap:wrap;margin:0 0 12px;font-size:13px}"
        "table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #d8dee8;table-layout:fixed}"
        "th,td{text-align:left;padding:7px 8px;border-bottom:1px solid #edf0f4;font-size:13px;vertical-align:top}"
        "th{background:#eef2f6;color:#334155}code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere}"
        ".ok{color:#047857;font-weight:600}.no{color:#64748b}.raw{display:none}body.raw-mode .parsed{display:none}body.raw-mode .raw{display:table-cell}"
        "</style></head><body><header><h1>SeeedMote Gateway</h1>"
        "<button id='mode' type='button'>Raw</button><span id='state' class='pill'>loading</span>") != ESP_OK) goto fail;

    if (sendf(req, "<span id='count' class='muted'>%" PRIu64 " total / %d shown</span></header>",
              total_raw, snap_count) != ESP_OK) goto fail;

    if (sendf(req,
        "<div class='meta'>"
        "<span>GW <code>%02x%02x%02x%02x%02x%02x</code></span>"
        "<span>STA <code>%s</code></span>"
        "<span>AP <code>192.168.4.1</code> <code>%s</code></span>"
        "<span>Up %" PRId64 " ms</span>"
        "</div>"
        "<table><thead><tr>"
        "<th style='width:64px'>Seq</th><th style='width:86px'>Time</th>"
        "<th style='width:150px'>Address</th><th style='width:58px'>RSSI</th>"
        "<th class='parsed' style='width:76px'>Moving</th>"
        "<th class='parsed' style='width:86px'>Vibration</th>"
        "<th class='parsed' style='width:58px'>PID</th>"
        "<th class='parsed' style='width:80px'>Count</th>"
        "<th class='raw' style='width:58px'>Len</th><th class='raw'>Data hex</th>"
        "</tr></thead><tbody id='rows'>",
        gw_mac[0], gw_mac[1], gw_mac[2],
        gw_mac[3], gw_mac[4], gw_mac[5],
        s_sta_ip, s_ap_ssid,
        (int64_t)(esp_timer_get_time() / 1000)) != ESP_OK) goto fail;

    if (snap_count == 0) {
        if (send_literal(req, "<tr id='empty'><td colspan='8' class='muted'>No advertisements yet</td></tr>") != ESP_OK) goto fail;
    }

    for (int i = 0; i < snap_count; i++) {
        int idx = (snap_head - 1 - i + RAW_RING_SIZE) % RAW_RING_SIZE;
        const raw_adv_t *adv = &s_http_scratch[idx];
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

    if (sendf(req,
        "</tbody></table><script>"
        "const rows=document.getElementById('rows'),count=document.getElementById('count'),state=document.getElementById('state'),mode=document.getElementById('mode');"
        "let lastSeq=%" PRIu64 ";"
        "mode.onclick=()=>{document.body.classList.toggle('raw-mode');mode.textContent=document.body.classList.contains('raw-mode')?'Parsed':'Raw'};"
        "function yn(e,k){return e.matched?(e[k]?'yes':'no'):'-'}"
        "function row(e){return `<td>${e.seq}</td><td>${e.ts}</td><td><code>${e.addr}</code></td><td>${e.rssi}</td><td class='parsed ${e.moving?'ok':'no'}'>${yn(e,'moving')}</td><td class='parsed ${e.vibration?'ok':'no'}'>${yn(e,'vibration')}</td><td class='parsed'>${e.pid}</td><td class='parsed'>${e.ctr}</td><td class='raw'>${e.len}</td><td class='raw'><code>${e.data}</code></td>`}"
        "function add(e){if(e.seq<=lastSeq)return;lastSeq=e.seq;const empty=document.getElementById('empty');if(empty)empty.remove();const tr=document.createElement('tr');tr.innerHTML=row(e);rows.prepend(tr);while(rows.children.length>100)rows.lastElementChild.remove();count.textContent=`${lastSeq} total / ${rows.children.length} shown`;}"
        "let busy=false;"
        "async function poll(){if(busy)return;busy=true;const c=new AbortController();const t=setTimeout(()=>c.abort(),2500);try{const r=await fetch('/raw?last='+lastSeq,{cache:'no-store',signal:c.signal});if(!r.ok)throw new Error(r.status);const p=await r.json();p.events.forEach(add);if(p.events.length===0&&p.last>lastSeq)lastSeq=p.last;count.textContent=`${p.total} total / ${rows.children.length} shown`;state.textContent='live';}catch(e){state.textContent='retrying';}finally{clearTimeout(t);busy=false;}}"
        "poll();setInterval(poll,500);"
        "</script></body></html>",
        total_raw) != ESP_OK) goto fail;
    xSemaphoreGive(s_http_scratch_mutex);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;

fail:
    xSemaphoreGive(s_http_scratch_mutex);
    return ESP_FAIL;
}

void start_httpd(void)
{
    if (s_httpd) return;

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    cfg.stack_size       = 8192;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    static const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = h_index,
    };
    httpd_register_uri_handler(s_httpd, &root);

    static const httpd_uri_t raw = {
        .uri = "/raw", .method = HTTP_GET, .handler = h_raw_json,
    };
    httpd_register_uri_handler(s_httpd, &raw);

    static const httpd_uri_t health = {
        .uri = "/health", .method = HTTP_GET, .handler = h_health,
    };
    httpd_register_uri_handler(s_httpd, &health);

    ESP_LOGI(TAG, "http server started (port 80)");
}
