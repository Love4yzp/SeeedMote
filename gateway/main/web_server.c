#include "web_server.h"
#include "wifi_mgr.h"
#include "nvs_config.h"
#include "mqtt_mgr.h"
#include "gw_id.h"
#include "led.h"
#include <string.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#define TAG "web_srv"

extern const char web_ui_html_start[] asm("_binary_web_ui_html_start");
extern const char web_ui_html_end[] asm("_binary_web_ui_html_end");

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, web_ui_html_start,
                    web_ui_html_end - web_ui_html_start);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    struct gw_config cfg;
    nvs_config_load(&cfg);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "gw_id", gw_id_get());
    cJSON_AddStringToObject(j, "wifi_ssid", cfg.wifi_ssid);
    cJSON_AddStringToObject(j, "wifi_pass", cfg.wifi_pass);
    cJSON_AddBoolToObject(j, "wifi_connected", wifi_mgr_is_connected());
    cJSON_AddStringToObject(j, "ip", wifi_mgr_get_ip());
    cJSON_AddStringToObject(j, "mqtt_broker", cfg.mqtt_broker);
    cJSON_AddNumberToObject(j, "mqtt_port", cfg.mqtt_port);
    cJSON_AddStringToObject(j, "mqtt_pass", cfg.mqtt_pass);
    cJSON_AddBoolToObject(j, "mqtt_connected", mqtt_mgr_is_connected());
    cJSON_AddStringToObject(j, "version", GATEWAY_FW_VERSION);

    char *str = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t handle_scan(httpd_req_t *req)
{
    cJSON *arr = wifi_mgr_scan();
    char *str = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(arr);
    return ESP_OK;
}

static char *read_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 512) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) { free(buf); return NULL; }
    buf[ret] = '\0';
    return buf;
}

static esp_err_t handle_wifi_post(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(j, "password"));
    if (!ssid || !ssid[0]) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    nvs_config_save_wifi(ssid, pass);
    wifi_mgr_connect(ssid, pass);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t handle_mqtt_post(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *broker = cJSON_GetStringValue(cJSON_GetObjectItem(j, "broker"));
    cJSON *port_item = cJSON_GetObjectItem(j, "port");
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(j, "password"));

    if (!broker || !broker[0]) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Broker required");
        return ESP_FAIL;
    }

    uint16_t port = 1883;
    if (port_item && cJSON_IsNumber(port_item))
        port = (uint16_t)port_item->valuedouble;

    nvs_config_save_mqtt(broker, port, pass);
    mqtt_mgr_reconfigure(broker, port, gw_id_get(), pass);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t handle_locate(httpd_req_t *req)
{
    led_locate_blink();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handle_restart(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handle_captive(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Captive portal DNS: answer every query with 192.168.4.1 */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    while (1) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &client_len);
        if (n < 12) continue;

        uint8_t resp[528];
        if (n + 16 > (int)sizeof(resp)) continue;
        memcpy(resp, buf, n);

        resp[2] = 0x81; resp[3] = 0x80;
        resp[6] = 0x00; resp[7] = 0x01;

        int pos = n;
        resp[pos++] = 0xC0; resp[pos++] = 0x0C;
        resp[pos++] = 0x00; resp[pos++] = 0x01;
        resp[pos++] = 0x00; resp[pos++] = 0x01;
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 0x3C;
        resp[pos++] = 0x00; resp[pos++] = 0x04;
        resp[pos++] = 192;  resp[pos++] = 168;
        resp[pos++] = 4;    resp[pos++] = 1;

        sendto(sock, resp, pos, 0,
               (struct sockaddr *)&client, client_len);
    }
}

void web_server_init(void)
{
    xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    httpd_handle_t server;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t routes[] = {
        { "/",                    HTTP_GET,  handle_root,      NULL },
        { "/api/status",          HTTP_GET,  handle_status,    NULL },
        { "/api/scan",            HTTP_GET,  handle_scan,      NULL },
        { "/api/wifi",            HTTP_POST, handle_wifi_post, NULL },
        { "/api/mqtt",            HTTP_POST, handle_mqtt_post, NULL },
        { "/api/locate",          HTTP_POST, handle_locate,    NULL },
        { "/api/restart",         HTTP_POST, handle_restart,   NULL },
        { "/generate_204",        HTTP_GET,  handle_captive,   NULL },
        { "/generate204",         HTTP_GET,  handle_captive,   NULL },
        { "/hotspot-detect.html", HTTP_GET,  handle_captive,   NULL },
        { "/favicon.ico",         HTTP_GET,  handle_captive,   NULL },
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on :80");
}
