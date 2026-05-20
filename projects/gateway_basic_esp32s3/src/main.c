/*
 * SeeedMote v2 — gateway_basic_esp32s3.
 *
 * Role: BLE gateway. Scans for SeeedMote BLE advertisements, decodes BTHome
 *       v2 Service Data, emits JSON to UART, and serves a live debug page over
 *       HTTP on the local Wi-Fi network.
 *
 *       HTTP endpoint:
 *         GET /        → browser UI with Parsed / Raw views
 *         GET /raw     → short-poll JSON feed for recent advertisements
 *         GET /health  → plain-text status probe
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
 *
 * Module layout:
 *   bthome.h/c       — BTHome v2 wire-format parser (pure, no side effects)
 *   adv_ring.h/c     — thread-safe ring buffer for raw advertisements
 *   ble_observer.h/c — NimBLE scanner, GAP callback, JSON-to-UART emitter
 *   http_server.h/c  — HTTP handlers and embedded debug UI
 *   wifi_mgmt.h/c    — Wi-Fi APSTA init and NVS credential management
 *   main.c           — startup orchestration and LED blink task
 */

#include "adv_ring.h"
#include "ble_observer.h"
#include "http_server.h"
#include "wifi_mgmt.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "gateway";

#define LED_GPIO        GPIO_NUM_21
#define BLINK_PERIOD_MS 500

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

    adv_ring_init();
    http_server_init();

    ESP_ERROR_CHECK(esp_read_mac(gw_mac, ESP_MAC_BT));
    ESP_LOGI(TAG, "gw_id=%02x%02x%02x%02x%02x%02x",
             gw_mac[0], gw_mac[1], gw_mac[2],
             gw_mac[3], gw_mac[4], gw_mac[5]);

    wifi_init();

    xTaskCreate(blink_task, "blink", 4096, NULL, 5, NULL);
}
