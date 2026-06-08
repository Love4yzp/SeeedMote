#include "gw_id.h"
#include "nvs_config.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "mqtt_mgr.h"
#include "ble_scanner.h"
#include "led.h"
#include "cli.h"
#include <esp_log.h>
#include <esp_timer.h>

#define TAG "main"

static struct gw_config saved_cfg;

static void status_timer_cb(void *arg)
{
    mqtt_mgr_publish_status();
}

static void on_wifi_connected(void)
{
    ESP_LOGI(TAG, "WiFi up — starting BLE scanner and MQTT");

    ble_scanner_init();

    if (saved_cfg.has_mqtt) {
        mqtt_mgr_start(saved_cfg.mqtt_broker, saved_cfg.mqtt_port,
                       gw_id_get(), saved_cfg.mqtt_pass);
    }
}

void app_main(void)
{
    led_init();
    nvs_config_init();
    gw_id_init();

    ESP_LOGI(TAG, "SeeedMote Gateway %s [%s]", gw_id_get(), GATEWAY_FW_VERSION);

    nvs_config_load(&saved_cfg);

    wifi_mgr_init(gw_id_get(),
                  saved_cfg.has_wifi ? saved_cfg.wifi_ssid : NULL,
                  saved_cfg.has_wifi ? saved_cfg.wifi_pass : NULL);

    web_server_init();

    if (saved_cfg.has_wifi) {
        wifi_mgr_set_on_connect(on_wifi_connected);
    } else {
        ble_scanner_init();
    }

    const esp_timer_create_args_t timer_args = {
        .callback = status_timer_cb,
        .name = "status",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, 60ULL * 1000000);

    cli_init();

    ESP_LOGI(TAG, "Gateway ready — type 'help' in serial console");
}
