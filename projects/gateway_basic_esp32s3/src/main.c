/*
 * SeeedMote v2 — gateway_basic_esp32s3 hello blink.
 *
 * Role: BLE gateway (always-on, scans for BLE adv, forwards to MQTT).
 * Board: Seeed XIAO ESP32-S3 (board id: seeed_xiao_esp32s3).
 * Scope: hello world — toggle onboard LED + ESP_LOGI hello. No BLE, no WiFi,
 *        no MQTT. Real business code lands here later.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gateway";

/* XIAO ESP32-S3 user LED is on GPIO21 (active low). */
#define LED_GPIO        GPIO_NUM_21
#define BLINK_PERIOD_MS 500

void app_main(void)
{
    ESP_LOGI(TAG, "seeedmote-v2 gateway_basic_esp32s3 hello");

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
        gpio_set_level(LED_GPIO, on ? 0 : 1);  /* active low */
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}
