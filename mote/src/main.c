/*
 * SeeedMote v2 — mote_motion_nrf52840.
 *
 * Role: BLE event mote. Uses the LSM6DS3TR-C accelerometer data-ready
 *       trigger to sample movement, classifies events (MOVING / PICK_UP),
 *       and broadcasts each as a BTHome v2 Service Data advertisement.
 *       STILL is the implicit default; no frame is emitted, consumers infer
 *       it from the absence of further events. No reverse channel is
 *       implemented in this version; configuration and OTA are kept out
 *       of the default air format.
 *
 * Board: Seeed XIAO nRF52840 Sense (board id: xiao_ble).
 *
 * Event-driven model (in lockstep with contracts/airframe.yaml):
 *   - event_counter (BTHome 0x3E) advances exactly once per business
 *     event. A burst retransmission of the same event reuses the same
 *     event_counter so consumer-side (mote_mac, ctr) dedup collapses
 *     the burst to one interaction.
 *   - packet_id (BTHome 0x00) advances on every adv update so BTHome
 *     receivers can drop in-air duplicates at the link layer.
 *   - No periodic heartbeat. MOVING -> STILL transition emits no frame.
 *
 * Wire format (must stay in lockstep with contracts/airframe.yaml and the
 * gateway parser):
 *
 *   AD type 0x16 Service Data - 16-bit UUID
 *   [2B] UUID               = 0xFCD2, little-endian on air (D2 FC)
 *   [1B] device_info        = BTHome v2, trigger-based, unencrypted (0x44)
 *   [2B] packet id          = object 0x00, uint8 BLE-link dedup id
 *   [2B] moving             = object 0x22, uint8
 *   [2B] vibration          = object 0x2C, uint8 (PICK_UP pulse)
 *   [5B] count              = object 0x3E, uint32 business event counter
 *
 * Detection happens here on the mote (physical-event semantics). Business
 * interpretation (which SKU, conversion, UI) lives on the consumer side.
 * Threshold parameters live in the local config struct in this version; keep
 * their names stable so a future configuration channel can wire them up
 * without renaming.
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
#if defined(CONFIG_USB_DEVICE_STACK)
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#endif

LOG_MODULE_REGISTER(mote, LOG_LEVEL_INF);

/* ---- Wire format constants ---------------------------------------------- */

#define BTHOME_UUID_LSB       0xD2u
#define BTHOME_UUID_MSB       0xFCu
#define BTHOME_DEVINFO_V2_TRIGGER 0x44u

#define BTHOME_OBJ_PACKET_ID  0x00u
#define BTHOME_OBJ_MOVING     0x22u
#define BTHOME_OBJ_VIBRATION  0x2Cu
#define BTHOME_OBJ_COUNT_U32  0x3Eu

#define EV_MOVING  0x01u
#define EV_PICKUP  0x02u

/* ---- Detection parameters ------------------------------------------------ */

struct mote_config {
    int32_t motion_threshold_mg;/* per-sample |delta| trigger              */
    int32_t pickup_peak_mg;    /* abs L1-norm accel magnitude → PICK_UP     */
    int32_t t_idle_ms;         /* MOVING → STILL idle window                */
    int32_t sample_hz;         /* accel data-ready frequency                */
    int     pickup_burst_count;/* burst retransmissions per event           */
    int     pickup_burst_ms;   /* interval between burst frames             */
};

/* Defaults. Tune pickup_peak_mg in lab — light items (rings, light shoes)
 * likely need ~300-500. The upstream Zephyr LSM6DSL driver exposes
 * data-ready interrupts, not the chip's wake-up/motion interrupt, so motion
 * is classified from successive accel samples in this firmware.
 *
 * sample_hz=12 maps to the LSM6DSL's 12.5 Hz ODR (lowest non-zero rate in
 * the driver's ODR table). At this rate data-ready fires every ~80 ms,
 * which is well inside t_idle_ms=2000 and keeps the CPU in System ON sleep
 * the rest of the time. Hardware wake-up interrupt (~3 µA quiescent) is
 * Stage 2 — needs direct register access since the driver doesn't expose it.
 * TODO: load from NVS/settings when a downlink config channel exists (Step 5/6). */
static struct mote_config cfg = {
    .motion_threshold_mg = 80,
    .pickup_peak_mg    = 1500,
    .t_idle_ms         = 2000,
    .sample_hz          = 12,
    .pickup_burst_count = 5,
    .pickup_burst_ms   = 100,
};

/* ---- Devicetree ---------------------------------------------------------- */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The LED is event-driven: a one-shot 100 ms pulse per emitted business
 * event (not per burst retransmit). Boot is 3 × 80 ms quick blinks so the
 * patterns are easy to tell apart: 3-fast = booted, 1-medium = motion.
 * gpio_dt_spec handles ACTIVE_LOW inversion so set(1) = lit, set(0) = dark
 * on either polarity. */
static void led_off_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    gpio_pin_set_dt(&led, 0);
}
K_TIMER_DEFINE(led_off_timer, led_off_handler, NULL);

static void led_pulse_event(void)
{
    gpio_pin_set_dt(&led, 1);
    k_timer_start(&led_off_timer, K_MSEC(100), K_NO_WAIT);
}

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

/* event_counter advances exactly once per business event (a discrete
 * pickup or a STILL -> MOVING transition). Burst retransmissions of the
 * same event reuse the same event_counter so consumer-side
 * (mote_mac, ctr) dedup collapses them to one interaction.
 *
 * bthome_pid advances on every BLE advertisement update so BTHome
 * receivers can drop in-air duplicates at the link layer. The two
 * counters intentionally drift apart. */
static uint32_t event_counter;
static uint8_t  bthome_pid;

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

static struct bt_data idle_adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_NAME_COMPLETE, "SEEED", 5),
};

static struct bt_data event_adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_NAME_COMPLETE, "SEEED", 5),
    BT_DATA(BT_DATA_SVC_DATA16,
            (uint8_t *)&payload, sizeof(payload)),
};

/* ---- Adv ---------------------------------------------------------------- */

/* Rebuild the BLE payload with the current event_counter and a fresh
 * BTHome packet_id, then push it to the controller. Used by both
 * emit_new_event() and republish_event(). */
static int rebuild_and_push_adv(uint8_t event_type)
{
    bthome_pid++;

    payload.uuid_le[0]    = BTHOME_UUID_LSB;
    payload.uuid_le[1]    = BTHOME_UUID_MSB;
    payload.device_info   = BTHOME_DEVINFO_V2_TRIGGER;
    payload.packet_id_id  = BTHOME_OBJ_PACKET_ID;
    payload.packet_id     = bthome_pid;
    payload.moving_id     = BTHOME_OBJ_MOVING;
    payload.moving        = 1u;  /* any emitted event implies moving=1 */
    payload.vibration_id  = BTHOME_OBJ_VIBRATION;
    payload.vibration     = (event_type == EV_PICKUP) ? 1u : 0u;
    payload.count_id      = BTHOME_OBJ_COUNT_U32;
    payload.event_counter = sys_cpu_to_le32(event_counter);
    return bt_le_adv_update_data(event_adv_data, ARRAY_SIZE(event_adv_data),
                                 NULL, 0);
}

/* New business event: bump event_counter, then push the frame. The LED
 * pulse fires here (not in republish_event) so burst retransmits don't
 * leave the LED on for the whole burst window — single 100 ms blink per
 * business event. */
static int emit_new_event(uint8_t event_type)
{
    event_counter++;
    led_pulse_event();
    return rebuild_and_push_adv(event_type);
}

/* Retransmit the most recent business event for BLE-link reliability.
 * event_counter is unchanged so consumer-side dedup collapses the burst
 * to a single interaction. Only BTHome packet_id advances. */
static int republish_event(uint8_t event_type)
{
    return rebuild_and_push_adv(event_type);
}

/* ---- IMU data-ready trigger + motion classification ---------------------- */

static K_SEM_DEFINE(data_ready_sem, 0, 1);

static void data_ready_trigger_handler(const struct device *dev,
                                       const struct sensor_trigger *trig)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(trig);
    k_sem_give(&data_ready_sem);
}

static int setup_data_ready_trigger(void)
{
    static const struct sensor_trigger trig = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ACCEL_XYZ,
    };
    return sensor_trigger_set(imu_dev, &trig, data_ready_trigger_handler);
}

static int setup_imu(void)
{
    struct sensor_value odr = {
        .val1 = cfg.sample_hz,
        .val2 = 0,
    };
    int rc = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
                             SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
    if (rc) {
        return rc;
    }

    return setup_data_ready_trigger();
}

/* L1-norm magnitude in mg. Accurate enough for threshold detection
 * (within ~13% of L2 norm) and avoids pulling in math / sqrt.
 * sensor_value_to_milli() returns m/s² × 1000. Convert to mg via ×102/1000
 * (approximation of /9.80665 × 1000). */
static int32_t read_magnitude_mg(void)
{
    struct sensor_value accel[3];

    if (sensor_sample_fetch(imu_dev) < 0 ||
        sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel) < 0) {
        return 0;
    }

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

    int32_t prev_mg          = 1000;
    int64_t last_motion_time = 0;
    int     burst_left       = 0;
    uint8_t burst_event_type = EV_MOVING;
    int64_t next_burst_time  = 0;

    k_msleep(100);
    int32_t seed = read_magnitude_mg();
    if (seed > 0) {
        prev_mg = seed;
    }

    /* Block on the data-ready trigger. When a burst is in flight we also
     * need to wake every pickup_burst_ms to push the next retransmit; with
     * no burst pending we wait K_FOREVER so the idle thread can park the
     * CPU in WFI (System ON sleep) until the IMU pings us again. The
     * idle-window check below is still exercised at ODR rate while
     * STATE_MOVING (~12.5 Hz), well inside t_idle_ms=2000. */
    while (1) {
        k_timeout_t wait = (burst_left > 0) ? K_MSEC(cfg.pickup_burst_ms)
                                            : K_FOREVER;
        bool got_sample = (k_sem_take(&data_ready_sem, wait) == 0);
        int64_t now = k_uptime_get();

        if (got_sample) {
            int32_t mg_now = read_magnitude_mg();
            int32_t delta = (mg_now > prev_mg) ? (mg_now - prev_mg)
                                               : (prev_mg - mg_now);
            bool motion = delta > cfg.motion_threshold_mg;
            prev_mg = mg_now;

            if (motion) {
                last_motion_time = now;
            }

            if (motion && current_state == STATE_STILL) {
                current_state    = STATE_MOVING;
                bool pickup      = (mg_now > cfg.pickup_peak_mg) ||
                                   (delta > cfg.pickup_peak_mg);
                burst_event_type = pickup ? EV_PICKUP : EV_MOVING;
                LOG_INF("STILL -> %s (mg=%d delta=%d ctr=%u)",
                        pickup ? "PICK_UP" : "MOVING",
                        mg_now, delta, event_counter + 1);
                emit_new_event(burst_event_type);
                /* Burst retransmits the same ctr to harden the BLE
                 * single-frame loss probability. PID still advances per
                 * adv so BTHome-level dedup keeps working. */
                burst_left      = cfg.pickup_burst_count - 1;
                next_burst_time = now + cfg.pickup_burst_ms;
            }
            /* STATE_MOVING + motion: last_motion_time updated above; idle window
             * is reset without emitting a new event. */
        }

        if (burst_left > 0 && now >= next_burst_time) {
            republish_event(burst_event_type);
            burst_left--;
            next_burst_time = now + cfg.pickup_burst_ms;
        }

        if (current_state == STATE_MOVING &&
            (now - last_motion_time) >= cfg.t_idle_ms) {
            current_state = STATE_STILL;
            LOG_INF("MOVING -> STILL (idle) — no frame emitted");
            burst_left = 0;
        }
    }
}

/* Do not auto-start at boot. main() starts the thread only after BLE has
 * been enabled, the first advertisement is live, and the IMU is verified
 * ready. Otherwise the first events race bt_le_adv_update_data() and are
 * lost on every cold boot. */
K_THREAD_DEFINE(state_thread_id, 2048, state_machine_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(5), 0, K_TICKS_FOREVER);

/* ---- Liveness LED ------------------------------------------------------- */

/* One-time boot indicator: 3 short blinks before BLE/IMU init so a user can
 * confirm the MCU is alive even when nothing else is hooked up. After this
 * the LED is silent except for led_pulse_50ms() on each emitted event. */
static void led_init_and_boot_blink(void)
{
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("led0 not ready");
        return;
    }
    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
        LOG_ERR("gpio_pin_configure_dt failed");
        return;
    }
    for (int i = 0; i < 3; i++) {
        gpio_pin_set_dt(&led, 1);
        k_msleep(80);
        gpio_pin_set_dt(&led, 0);
        k_msleep(120);
    }
}

/* ---- 1200-baud touch DFU trigger (debug builds only) ------------------- */

#if defined(CONFIG_USB_DEVICE_STACK)
/* The Adafruit nRF52 bootloader checks GPREGRET on reset: writing 0x57
 * before sys_reboot puts the chip into UF2 mode (mounts as XIAO-SENSE).
 * Polls every 50 ms for a baud-rate transition into 1200 — the
 * Arduino / CircuitPython "1200bps touch" convention. Tracking the
 * transition (rather than instantaneous DTR state) avoids missing the
 * touch when the host's open/close window is shorter than the poll
 * period. The whole block is gated by CONFIG_USB_DEVICE_STACK so the
 * release build doesn't pay for it. */
#define DFU_MAGIC_UF2_RESET 0x57u

static const struct device *const cdc_dev =
    DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

static void touch_poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(touch_poll_work, touch_poll_handler);
static uint32_t last_baud;

static void touch_poll_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    /* Skip the poll until USB CDC has come up — uart_line_ctrl_get on an
     * unready device would call into NULL driver vtable. Re-arm the timer
     * at a slower cadence; once the host attaches we fall through to the
     * normal 50 ms poll. */
    if (!device_is_ready(cdc_dev)) {
        k_work_schedule(&touch_poll_work, K_MSEC(200));
        return;
    }

    uint32_t baud = 0;
    (void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_BAUD_RATE, &baud);

    if (baud == 1200 && last_baud != 1200) {
        LOG_INF("1200-baud touch detected — rebooting to UF2 bootloader");
        nrf_power_gpregret_set(NRF_POWER, 0, DFU_MAGIC_UF2_RESET);
        sys_reboot(SYS_REBOOT_COLD);
    }
    last_baud = baud;

    k_work_schedule(&touch_poll_work, K_MSEC(50));
}
#endif /* CONFIG_USB_DEVICE_STACK */

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    led_init_and_boot_blink();

#if defined(CONFIG_USB_DEVICE_STACK)
    /* Debug-only path: bring USB CDC up so console log is visible without
     * an SWD probe. Failure is non-fatal — RTT is still attached. The
     * release build skips this block entirely (no ~3 mA USB quiescent). */
    int usb_rc = usb_enable(NULL);
    if (usb_rc && usb_rc != -EALREADY) {
        /* No console yet, but keep going — RTT may still work. */
    }
    /* Wait a short moment so the host enumerates CDC before we splash. */
    k_msleep(500);
    LOG_INF("seeedmote-v2 mote_motion_nrf52840 starting (usb_rc=%d)", usb_rc);
    /* Start polling for the 1200-baud touch so `make flash DEBUG=1` can
     * drop us into the UF2 bootloader without a physical RESET tap. */
    k_work_schedule(&touch_poll_work, K_MSEC(100));
#else
    LOG_INF("seeedmote-v2 mote_motion_nrf52840 starting (RTT-only build)");
#endif

    event_counter = 0;

    /* Bring BLE up first so adv is visible even if the IMU is dead —
     * easier to debug from the gateway side than a silent mote. */
    int rc = bt_enable(NULL);
    if (rc) {
        LOG_ERR("bt_enable failed: %d", rc);
        return rc;
    }

    bool imu_ready = device_is_ready(imu_dev);
    if (!imu_ready) {
        LOG_WRN("imu device %s not ready — state machine will not start",
                imu_dev->name);
    } else {
        LOG_INF("imu device: %s", imu_dev->name);
        rc = setup_imu();
        if (rc) {
            LOG_WRN("imu data-ready setup failed (%d) — state machine will not start",
                    rc);
            imu_ready = false;
        }
    }

    rc = bt_le_adv_start(BT_LE_ADV_NCONN,
                         idle_adv_data, ARRAY_SIZE(idle_adv_data),
                         NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start failed: %d", rc);
        return rc;
    }
    LOG_INF("advertising started; BTHome event payloads publish on motion");

    /* All init succeeded — release the state machine thread. Skipping the
     * start when the IMU is dead keeps advertising visible without producing
     * synthetic BTHome motion frames. */
    if (imu_ready) {
        k_thread_start(state_thread_id);
    }

    return 0;
}
