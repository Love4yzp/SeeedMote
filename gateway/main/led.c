#include "led.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define LED_GPIO GPIO_NUM_21

void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(LED_GPIO, 1);
}

void led_set(bool on)
{
    gpio_set_level(LED_GPIO, on ? 0 : 1);
}

static void blink_task(void *arg)
{
    for (int i = 0; i < 12; i++) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(150));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    vTaskDelete(NULL);
}

void led_locate_blink(void)
{
    xTaskCreate(blink_task, "locate", 2048, NULL, 5, NULL);
}
