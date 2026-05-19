/*
 * SeeedMote v2 — mote_motion_nrf52840.
 *
 * Role: BLE event mote. Samples the LSM6DS3TR-C IMU, classifies motion
 *       events (STILL / MOVING / PICK_UP), and broadcasts each event as
 *       a BTHome v2 Service Data advertisement. No reverse channel is
 *       implemented in this version; configuration and OTA are kept out of
 *       the default air format.
 *
 * Board: Seeed XIAO nRF52840 Sense (board id: xiao_ble).
 *
 * Wire format (must stay in lockstep with contracts/airframe.yaml and the
 * gateway parser):
 *
 *   AD type 0x16 Service Data - 16-bit UUID
 *   [2B] UUID               = 0xFCD2, little-endian on air (D2 FC)
 *   [1B] device_info        = BTHome v2, trigger-based, unencrypted (0x44)
 *   [2B] packet id          = object 0x00, uint8 duplicate filter
 *   [2B] moving             = object 0x22, uint8
 *   [2B] vibration          = object 0x2C, uint8 (PICK_UP pulse)
 *   [5B] count              = object 0x3E, uint32 event counter
 *
 * Detection happens here on the mote (physical-event semantics). Business
 * interpretation (which SKU, conversion, UI) lives on the consumer side.
 * Threshold parameters are #defines in this version; keep their names stable
 * so a future configuration channel can wire them up without renaming.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(mote, LOG_LEVEL_INF);

/* ---- Wire format constants ---------------------------------------------- */

#define BTHOME_UUID_LSB       0xD2u
#define BTHOME_UUID_MSB       0xFCu
#define BTHOME_DEVINFO_V2_TRIGGER 0x44u

#define BTHOME_OBJ_PACKET_ID  0x00u
#define BTHOME_OBJ_MOVING     0x22u
#define BTHOME_OBJ_VIBRATION  0x2Cu
#define BTHOME_OBJ_COUNT_U32  0x3Eu

#define EV_STILL   0x00u
#define EV_MOVING  0x01u
#define EV_PICKUP  0x02u

/* ---- Detection parameters (future config field names) ------------------- */

/* First-pass defaults; tune in lab. Light items (rings, light shoes) likely
 * need PICKUP_PEAK_MG dropped to ~300-500. See plan risk note. */
#define MOTION_THRESHOLD_MG    80     /* per-sample |delta| trigger        */
#define PICKUP_PEAK_MG         1500   /* abs accel magnitude → PICK_UP     */
#define T_IDLE_MS              2000   /* MOVING → STILL after idle window  */
#define ADV_HEARTBEAT_MS       200    /* MOVING heartbeat interval         */
#define PICKUP_BURST_COUNT     5      /* PICK_UP burst transmissions       */
#define PICKUP_BURST_MS        100    /* PICK_UP burst interval            */

#define SAMPLE_INTERVAL_MS     50

/* ---- Devicetree ---------------------------------------------------------- */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The IMU is exposed via app.overlay as `aliases { imu = &lsm6dsl; };` so
 * this main.c stays board-neutral. */
#define IMU_NODE DT_ALIAS(imu)
static const struct device *const imu_dev = DEVICE_DT_GET(IMU_NODE);

/* ---- State -------------------------------------------------------------- */

enum mote_state {
    STATE_STILL = 0,
    STATE_MOVING,
};

static enum mote_state current_state = STATE_STILL;

static uint32_t event_counter;

struct __packed bthome_payload {
    uint8_t  uuid_le[2];
    uint8_t  device_info;
    uint8_t  packet_id_id;
    uint8_t  packet_id;
    uint8_t  moving_id;
    uint8_t  moving;
    uint8_t  vibration_id;
    uint8_t  vibration;
    uint8_t  count_id;
    uint32_t event_counter;  /* LE */
};
BUILD_ASSERT(sizeof(struct bthome_payload) == 14,
             "BTHome motion payload must be 14 bytes");

static struct bthome_payload payload;

static struct bt_data adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_SVC_DATA16,
            (uint8_t *)&payload, sizeof(payload)),
};

/* ---- Adv ---------------------------------------------------------------- */

static int update_payload_and_adv(uint8_t event_type)
{
    uint32_t next_counter = ++event_counter;

    payload.uuid_le[0]    = BTHOME_UUID_LSB;
    payload.uuid_le[1]    = BTHOME_UUID_MSB;
    payload.device_info   = BTHOME_DEVINFO_V2_TRIGGER;
    payload.packet_id_id  = BTHOME_OBJ_PACKET_ID;
    payload.packet_id     = (uint8_t)next_counter;
    payload.moving_id     = BTHOME_OBJ_MOVING;
    payload.moving        = (event_type == EV_STILL) ? 0u : 1u;
    payload.vibration_id  = BTHOME_OBJ_VIBRATION;
    payload.vibration     = (event_type == EV_PICKUP) ? 1u : 0u;
    payload.count_id      = BTHOME_OBJ_COUNT_U32;
    payload.event_counter = sys_cpu_to_le32(next_counter);
    return bt_le_adv_update_data(adv_data, ARRAY_SIZE(adv_data),
                                 NULL, 0);
}

/* ---- IMU sampling ------------------------------------------------------- */

/* L1-norm magnitude in mg. Accurate enough for threshold detection
 * (within ~13% of L2 norm) and avoids pulling in math / sqrt. */
static int32_t read_magnitude_mg(void)
{
    struct sensor_value accel[3];

    if (sensor_sample_fetch(imu_dev) < 0) {
        return 0;
    }
    if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel) < 0) {
        return 0;
    }

    /* sensor_value_to_milli() returns value × 1000 (m/s² × 1000).
     * Convert to mg by /9.80665, approximated as ×102/1000. */
    int64_t ax = sensor_value_to_milli(&accel[0]);
    int64_t ay = sensor_value_to_milli(&accel[1]);
    int64_t az = sensor_value_to_milli(&accel[2]);
    int64_t mag_mm = llabs(ax) + llabs(ay) + llabs(az);
    return (int32_t)((mag_mm * 102) / 1000);
}

/* ---- State machine thread ---------------------------------------------- */

static void state_machine_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    int32_t prev_mg = 1000;
    int64_t last_motion_time = 0;
    int     burst_left = 0;
    int64_t next_burst_time = 0;
    int64_t next_heartbeat_time = 0;

    /* Settle and seed prev_mg with first reading. */
    k_msleep(100);
    int32_t seed = read_magnitude_mg();
    if (seed > 0) {
        prev_mg = seed;
    }

    while (1) {
        int32_t mg_now = read_magnitude_mg();
        int32_t delta  = (mg_now > prev_mg) ? (mg_now - prev_mg)
                                             : (prev_mg - mg_now);
        int64_t now    = k_uptime_get();
        bool    motion = delta > MOTION_THRESHOLD_MG;
        prev_mg = mg_now;

        if (motion) {
            last_motion_time = now;
            if (current_state == STATE_STILL) {
                current_state = STATE_MOVING;
                LOG_INF("STILL -> MOVING (mg=%d delta=%d)", mg_now, delta);

                /* Hard accel spike → PICK_UP, burst the gateway. */
                if (mg_now > PICKUP_PEAK_MG || delta > PICKUP_PEAK_MG) {
                    burst_left      = PICKUP_BURST_COUNT;
                    next_burst_time = now;
                    LOG_INF("PICK_UP burst (peak=%d)", mg_now);
                } else {
                    update_payload_and_adv(EV_MOVING);
                }
                next_heartbeat_time = now + ADV_HEARTBEAT_MS;
            }
        }

        if (burst_left > 0 && now >= next_burst_time) {
            update_payload_and_adv(EV_PICKUP);
            burst_left--;
            next_burst_time = now + PICKUP_BURST_MS;
        }

        if (current_state == STATE_MOVING && burst_left == 0 &&
            now >= next_heartbeat_time) {
            update_payload_and_adv(EV_MOVING);
            next_heartbeat_time = now + ADV_HEARTBEAT_MS;
        }

        if (current_state == STATE_MOVING &&
            (now - last_motion_time) >= T_IDLE_MS) {
            current_state = STATE_STILL;
            LOG_INF("MOVING -> STILL (idle %lldms)",
                    (long long)(now - last_motion_time));
            update_payload_and_adv(EV_STILL);
            burst_left = 0;
        }

        k_msleep(SAMPLE_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(state_thread_id, 2048, state_machine_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(5), 0, 0);

/* ---- Liveness LED ------------------------------------------------------- */

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
    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }
}

K_THREAD_DEFINE(blink_thread_id, 512, blink_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(7), 0, 0);

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    /* Bring USB CDC up so console log is visible without an SWD probe.
     * Failure is non-fatal: a non-debug build still works without USB. */
    int usb_rc = usb_enable(NULL);
    if (usb_rc && usb_rc != -EALREADY) {
        /* No console yet, but keep going — RTT may still work. */
    }
    /* Wait a short moment so the host enumerates CDC before we splash. */
    k_msleep(500);

    LOG_INF("seeedmote-v2 mote_motion_nrf52840 starting (usb_rc=%d)", usb_rc);

    event_counter = 0;

    /* Bring BLE up first so adv is visible even if the IMU is dead —
     * easier to debug from the gateway side than a silent mote. */
    int rc = bt_enable(NULL);
    if (rc) {
        LOG_ERR("bt_enable failed: %d", rc);
        return rc;
    }

    if (!device_is_ready(imu_dev)) {
        LOG_WRN("imu device %s not ready — broadcasting STILL only",
                imu_dev->name);
    } else {
        LOG_INF("imu device: %s", imu_dev->name);
    }

    /* Pre-seed payload so the first advertisement is well-formed. */
    payload.uuid_le[0]    = BTHOME_UUID_LSB;
    payload.uuid_le[1]    = BTHOME_UUID_MSB;
    payload.device_info   = BTHOME_DEVINFO_V2_TRIGGER;
    payload.packet_id_id  = BTHOME_OBJ_PACKET_ID;
    payload.packet_id     = 0;
    payload.moving_id     = BTHOME_OBJ_MOVING;
    payload.moving        = 0;
    payload.vibration_id  = BTHOME_OBJ_VIBRATION;
    payload.vibration     = 0;
    payload.count_id      = BTHOME_OBJ_COUNT_U32;
    payload.event_counter = sys_cpu_to_le32(0);

    rc = bt_le_adv_start(BT_LE_ADV_NCONN,
                         adv_data, ARRAY_SIZE(adv_data),
                         NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start failed: %d", rc);
        return rc;
    }
    LOG_INF("advertising started (BTHome v2 service UUID 0xFCD2)");

    return 0;
}
