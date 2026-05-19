/*
 * SeeedMote v2 — mote_motion_nrf52840 hello blink.
 *
 * Role: BLE node (battery, sleeps most of the time, broadcasts events).
 * Board: Seeed XIAO nRF52840 Sense (board id: xiao_ble).
 * Scope: hello world — blink onboard LED, print boot log. No BLE, no sensor,
 *        no System OFF. Real business code lands here later.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mote, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define BLINK_INTERVAL_MS 500

int main(void)
{
    LOG_INF("seeedmote-v2 mote_motion_nrf52840 hello");

    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("led0 not ready");
        return -1;
    }

    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("gpio_pin_configure_dt failed: %d", ret);
        return ret;
    }

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(BLINK_INTERVAL_MS);
    }

    return 0;
}
