/*
 * SeeedMote v2 — mote_motion_nrf52840 (bring-up step 2: BT adv + blink).
 *
 * Implements step 2 of the recovery plan: blink keeps proving the
 * scheduler is alive, and we broadcast a fixed-size SeeedMote frame so
 * gateway_basic_esp32s3 can validate the airframe end-to-end.
 *
 * Recovery plan (re-add in order, verify LED keeps blinking each step):
 *   1. blink_task only                                    [done]
 *   2. + bt_enable + BT_LE_ADV with static payload        [this file]
 *   3. + IMU sample (no trigger), magnitude threshold     [next]
 *   4. + IMU trigger / state machine                      [next]
 *   5. + USB CDC console                                  [last]
 *
 * Board: Seeed XIAO nRF52840 Sense (board id: xiao_ble/nrf52840/sense).
 *
 * Wire format: see contracts/airframe.yaml — 11 bytes, little-endian.
 * Any change to the layout MUST go through that file.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(mote, LOG_LEVEL_INF);

/* --- LED ----------------------------------------------------------------- */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static void blink_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("led0 not ready");
        return;
    }
    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
        LOG_ERR("gpio_pin_configure_dt failed");
        return;
    }
    LOG_INF("blink_task running");
    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }
}

K_THREAD_DEFINE(blink_thread_id, 1024, blink_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(7), 0, 0);

/* --- BLE advertising ---------------------------------------------------- */

/* Wire format mirrors contracts/airframe.yaml v1. Total length: 11 bytes. */
#define MFG_LEN          11
#define COMPANY_ID       0xFFFFu /* SIG "test" CID — bring-up only */
#define PROTO_V1         0x01
#define EV_STILL         0x00
#define EV_MOVING        0x01
#define EV_PICKUP        0x02

/* 100 ms advertising interval in 0.625-ms units (debug-mode default;
 * see docs/build.md for the rationale). */
#define ADV_INTERVAL_625US ((100 * 1000) / 625)

static uint8_t  mfg[MFG_LEN];
static uint16_t boot_uuid;
static uint32_t event_ctr;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg, sizeof(mfg)),
};

static const struct bt_le_adv_param adv_param = {
    .id           = BT_ID_DEFAULT,
    .options      = BT_LE_ADV_OPT_USE_IDENTITY, /* stable MAC for debugging */
    .interval_min = ADV_INTERVAL_625US,
    .interval_max = ADV_INTERVAL_625US,
    .peer         = NULL,
};

static void fill_mfg(uint8_t event)
{
    sys_put_le16(COMPANY_ID, &mfg[0]);
    mfg[2] = PROTO_V1;
    mfg[3] = event;
    sys_put_le16(boot_uuid, &mfg[4]);
    sys_put_le32(event_ctr, &mfg[6]);
    mfg[10] = 0x00; /* reserved */
}

/* Cycle event_type every second so a fresh gateway run sees variety
 * across STILL/MOVING/PICK_UP within ~3 s. Counter increments every
 * second; the gateway dedupes on (mote_mac, boot, ctr). */
static void adv_pump_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    static const uint8_t cycle[] = { EV_STILL, EV_MOVING, EV_PICKUP };
    size_t idx = 0;

    while (1) {
        k_msleep(1000);
        event_ctr++;
        fill_mfg(cycle[idx]);
        idx = (idx + 1) % ARRAY_SIZE(cycle);

        int rc = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
        if (rc) {
            LOG_WRN("adv update failed: %d", rc);
        }
    }
}

K_THREAD_DEFINE(adv_pump_id, 1024, adv_pump_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(7), 0, 0);

static void start_adv(void)
{
    boot_uuid = (uint16_t)sys_rand32_get();
    event_ctr = 0;
    fill_mfg(EV_STILL);

    int rc = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start failed: %d", rc);
        return;
    }
    LOG_INF("adv started: boot=0x%04x interval=100ms cid=0x%04x proto=v%u",
            boot_uuid, COMPANY_ID, PROTO_V1);
}

int main(void)
{
    LOG_INF("seeedmote-v2 mote_motion_nrf52840 (bring-up step 2)");

    int rc = bt_enable(NULL);
    if (rc) {
        LOG_ERR("bt_enable failed: %d", rc);
        return 0;
    }
    start_adv();
    return 0;
}
