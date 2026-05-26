/*
 * SeeedMote v2 - mote_motion_nrf52840.
 *
 * Role: BLE event mote. The LSM6DS3TR-C wake-up/inactivity engine drives
 * INT1; the nRF52840 sleeps in System ON until that GPIO fires. Motion events
 * are emitted as short BTHome v2 bursts, then the mote opens a 30s connectable
 * configuration window. Boot emits one BTHome heartbeat with moving=0.
 *
 * Board: Seeed XIAO nRF52840 Sense (board id: xiao_ble/nrf52840/sense).
 *
 * Wire format:
 *
 *   AD type 0x16 Service Data - 16-bit UUID
 *   [2B] UUID        = 0xFCD2, little-endian on air (D2 FC)
 *   [1B] device_info = BTHome v2, trigger-based, unencrypted (0x44)
 *   [2B] packet id   = object 0x00, uint8 multi-gateway dedup key
 *   [2B] moving      = object 0x22, uint8; 1=motion, 0=boot heartbeat
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <hal/nrf_ficr.h>
#if defined(CONFIG_USB_DEVICE_STACK)
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#endif

LOG_MODULE_REGISTER(mote, LOG_LEVEL_INF);

/* ---- BTHome wire format ------------------------------------------------- */

#define BTHOME_UUID_LSB              0xD2u
#define BTHOME_UUID_MSB              0xFCu
#define BTHOME_DEVINFO_V2_TRIGGER    0x44u

#define BTHOME_OBJ_PACKET_ID         0x00u
#define BTHOME_OBJ_MOVING            0x22u

struct __packed bthome_payload {
    uint8_t uuid_le[2];
    uint8_t device_info;
    uint8_t packet_id_id;
    uint8_t packet_id;
    uint8_t moving_id;
    uint8_t moving;
};
BUILD_ASSERT(sizeof(struct bthome_payload) == 7,
             "BTHome v2 mote payload must be 7 bytes");

static struct bthome_payload payload;
static uint8_t bthome_pid;

/* ---- Timing ------------------------------------------------------------- */

#define EVENT_BURST_COUNT            5
#define EVENT_BURST_INTERVAL_MS      100
#define EVENT_BURST_MS               (EVENT_BURST_COUNT * EVENT_BURST_INTERVAL_MS)
#define BOOT_HEARTBEAT_MS            150
#define CONFIG_WINDOW_MS             30000
#define CONFIG_LED_INTERVAL_MS       2000
#define CONFIG_LED_ON_MS             20

/* ---- LSM6DSL wake-up registers ----------------------------------------- */

#define LSM6DSL_REG_WAKE_UP_SRC      0x1Bu
#define LSM6DSL_REG_CTRL1_XL         0x10u
#define LSM6DSL_REG_CTRL6_C          0x15u
#define LSM6DSL_REG_INT1_CTRL        0x0Du
#define LSM6DSL_REG_TAP_CFG          0x58u
#define LSM6DSL_REG_WAKE_UP_THS      0x5Bu
#define LSM6DSL_REG_WAKE_UP_DUR      0x5Cu
#define LSM6DSL_REG_MD1_CFG          0x5Eu

#define LSM6DSL_WAKE_SRC_WU_IA       BIT(3)
#define LSM6DSL_WAKE_SRC_SLEEP_STATE BIT(4)

/* CTRL1_XL=0x10: accelerometer on at 12.5 Hz, +/-2g. The Zephyr driver
 * leaves ODR runtime-selected by default, so set the wake engine's clock
 * explicitly before programming embedded wake-up detection. */
#define LSM6DSL_CTRL1_XL_12HZ_2G     0x10u
#define LSM6DSL_CTRL6_C_XL_LP        0x10u
#define LSM6DSL_TAP_CFG_WAKE         0xF0u
#define LSM6DSL_WAKE_UP_THS_94MG     0x03u
#define LSM6DSL_WAKE_UP_DUR_80MS     0x21u
#define LSM6DSL_MD1_CFG_WAKE_INACT   0xA0u

/* ---- Devicetree --------------------------------------------------------- */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define IMU_NODE DT_ALIAS(imu)
static const struct device *const imu_dev = DEVICE_DT_GET(IMU_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);
static const struct gpio_dt_spec imu_int =
    GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);

/* ---- BLE advertising state --------------------------------------------- */

enum adv_state {
    ADV_IDLE = 0,
    ADV_EVENT_BURST,
    ADV_CONFIG_WINDOW,
    ADV_CONNECTED,
};

static enum adv_state adv_state = ADV_IDLE;
static bool motion_active;
static char bt_name[sizeof("SEEED-FFFFFF")];

static const uint8_t adv_flags[] = {
    BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR,
};

static struct bt_data bthome_adv_data[] = {
    BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
    BT_DATA(BT_DATA_NAME_COMPLETE, bt_name, 0),
    BT_DATA(BT_DATA_SVC_DATA16, &payload, sizeof(payload)),
};

static struct bt_data config_adv_data[] = {
    BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
    BT_DATA(BT_DATA_NAME_COMPLETE, bt_name, 0),
};

static const struct bt_le_adv_param event_adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY, 0x00a0, 0x00a0, NULL);

static const struct bt_le_adv_param config_adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY |
                         BT_LE_ADV_OPT_CONNECTABLE,
                         0x0140, 0x0140, NULL);

/* ---- Work items --------------------------------------------------------- */

static void burst_done_handler(struct k_work *work);
static void config_timeout_handler(struct k_work *work);
static void config_led_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(burst_done_work, burst_done_handler);
static K_WORK_DELAYABLE_DEFINE(config_timeout_work, config_timeout_handler);
static K_WORK_DELAYABLE_DEFINE(config_led_work, config_led_handler);

/* ---- LED ---------------------------------------------------------------- */

static void led_off_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    gpio_pin_set_dt(&led, 0);
}
K_TIMER_DEFINE(led_off_timer, led_off_handler, NULL);

static void led_pulse(k_timeout_t on_time)
{
    gpio_pin_set_dt(&led, 1);
    k_timer_start(&led_off_timer, on_time, K_NO_WAIT);
}

static void led_pulse_event(void)
{
    led_pulse(K_MSEC(50));
}

static void led_config_tick(void)
{
    led_pulse(K_MSEC(CONFIG_LED_ON_MS));
}

/* One-time boot indicator: 3 short blinks before BLE/IMU init. After this,
 * LED patterns are: event=single 50ms pulse, config window=20ms every 2s,
 * connected=solid on. */
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

/* ---- BLE helpers -------------------------------------------------------- */

static void refresh_adv_name_len(void)
{
    size_t len = strlen(bt_name);

    bthome_adv_data[1].data_len = len;
    config_adv_data[1].data_len = len;
}

static void build_ble_name(void)
{
    uint32_t addr0 = NRF_FICR->DEVICEADDR[0];

    /* BLE addresses are displayed MSB first. DEVICEADDR[0]'s low three
     * bytes are therefore the trailing six hex chars of the displayed MAC. */
    uint8_t b0 = (uint8_t)(addr0 & 0xffu);
    uint8_t b1 = (uint8_t)((addr0 >> 8) & 0xffu);
    uint8_t b2 = (uint8_t)((addr0 >> 16) & 0xffu);

    (void)snprintf(bt_name, sizeof(bt_name), "SEEED-%02X%02X%02X",
                   b2, b1, b0);
    refresh_adv_name_len();
}

static void fill_bthome_payload(uint8_t moving)
{
    bthome_pid++;

    payload.uuid_le[0]   = BTHOME_UUID_LSB;
    payload.uuid_le[1]   = BTHOME_UUID_MSB;
    payload.device_info  = BTHOME_DEVINFO_V2_TRIGGER;
    payload.packet_id_id = BTHOME_OBJ_PACKET_ID;
    payload.packet_id    = bthome_pid;
    payload.moving_id    = BTHOME_OBJ_MOVING;
    payload.moving       = moving ? 1u : 0u;
}

static void cancel_config_led(void)
{
    (void)k_work_cancel_delayable(&config_led_work);
    gpio_pin_set_dt(&led, 0);
}

static void stop_advertising(void)
{
    int rc = bt_le_adv_stop();

    if (rc && rc != -EALREADY && rc != -EINVAL) {
        LOG_WRN("bt_le_adv_stop failed: %d", rc);
    }
}

static int start_bthome_burst(uint8_t moving, int32_t duration_ms)
{
    fill_bthome_payload(moving);
    cancel_config_led();
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);

    stop_advertising();

    int rc = bt_le_adv_start(&event_adv_param,
                             bthome_adv_data, ARRAY_SIZE(bthome_adv_data),
                             NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start burst failed: %d", rc);
        adv_state = ADV_IDLE;
        return rc;
    }

    adv_state = ADV_EVENT_BURST;
    k_work_schedule(&burst_done_work, K_MSEC(duration_ms));
    LOG_INF("BTHome burst started moving=%u packet_id=%u",
            payload.moving, payload.packet_id);
    return 0;
}

static int enter_config_window(void)
{
    cancel_config_led();
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);

    stop_advertising();

    int rc = bt_le_adv_start(&config_adv_param,
                             config_adv_data, ARRAY_SIZE(config_adv_data),
                             NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start config window failed: %d", rc);
        adv_state = ADV_IDLE;
        return rc;
    }

    adv_state = ADV_CONFIG_WINDOW;
    led_config_tick();
    k_work_schedule(&config_led_work, K_MSEC(CONFIG_LED_INTERVAL_MS));
    k_work_schedule(&config_timeout_work, K_MSEC(CONFIG_WINDOW_MS));
    LOG_INF("config window opened for %d ms", CONFIG_WINDOW_MS);
    return 0;
}

static void enter_idle(void)
{
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);
    cancel_config_led();
    stop_advertising();
    adv_state = ADV_IDLE;
    motion_active = false;
    LOG_INF("advertising idle");
}

static void burst_done_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    (void)enter_config_window();
}

static void config_timeout_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    enter_idle();
}

static void config_led_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (adv_state != ADV_CONFIG_WINDOW) {
        return;
    }

    led_config_tick();
    k_work_schedule(&config_led_work, K_MSEC(CONFIG_LED_INTERVAL_MS));
}

static void bt_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_WRN("BLE connect failed: %u", err);
        return;
    }

    (void)conn;
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);
    cancel_config_led();
    adv_state = ADV_CONNECTED;
    gpio_pin_set_dt(&led, 1);
    LOG_INF("BLE connected");
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE disconnected (reason=%u)", reason);
    enter_idle();
}

static struct bt_conn_cb conn_callbacks = {
    .connected = bt_connected,
    .disconnected = bt_disconnected,
};

/* ---- IMU wake-up INT1 --------------------------------------------------- */

static K_SEM_DEFINE(wake_sem, 0, 1);
static struct gpio_callback imu_int_cb;

static int imu_write_reg(uint8_t reg, uint8_t value)
{
    int rc = i2c_reg_write_byte_dt(&imu_i2c, reg, value);

    if (rc) {
        LOG_ERR("imu write reg 0x%02x failed: %d", reg, rc);
    }
    return rc;
}

static int imu_read_reg(uint8_t reg, uint8_t *value)
{
    int rc = i2c_reg_read_byte_dt(&imu_i2c, reg, value);

    if (rc) {
        LOG_ERR("imu read reg 0x%02x failed: %d", reg, rc);
    }
    return rc;
}

static void imu_int_handler(const struct device *dev,
                            struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&wake_sem);
}

static int setup_imu_wake_int1(void)
{
    uint8_t src;
    int rc;

    if (!device_is_ready(imu_dev)) {
        LOG_ERR("imu device %s not ready", imu_dev->name);
        return -ENODEV;
    }
    if (!device_is_ready(imu_i2c.bus)) {
        LOG_ERR("imu i2c bus not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&imu_int)) {
        LOG_ERR("imu INT1 gpio not ready");
        return -ENODEV;
    }

    rc = gpio_pin_configure_dt(&imu_int, GPIO_INPUT);
    if (rc) {
        LOG_ERR("imu INT1 gpio configure failed: %d", rc);
        return rc;
    }

    /* Make sure Zephyr's data-ready routing is not left on INT1. */
    rc = imu_write_reg(LSM6DSL_REG_INT1_CTRL, 0x00);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_CTRL1_XL, LSM6DSL_CTRL1_XL_12HZ_2G);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_CTRL6_C, LSM6DSL_CTRL6_C_XL_LP);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_TAP_CFG, LSM6DSL_TAP_CFG_WAKE);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_THS, LSM6DSL_WAKE_UP_THS_94MG);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_DUR, LSM6DSL_WAKE_UP_DUR_80MS);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_MD1_CFG, LSM6DSL_MD1_CFG_WAKE_INACT);
    if (rc) {
        return rc;
    }

    (void)imu_read_reg(LSM6DSL_REG_WAKE_UP_SRC, &src);

    gpio_init_callback(&imu_int_cb, imu_int_handler, BIT(imu_int.pin));
    rc = gpio_add_callback(imu_int.port, &imu_int_cb);
    if (rc) {
        LOG_ERR("imu INT1 callback add failed: %d", rc);
        return rc;
    }

    rc = gpio_pin_interrupt_configure_dt(&imu_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (rc) {
        LOG_ERR("imu INT1 interrupt configure failed: %d", rc);
        return rc;
    }

    LOG_INF("imu INT1 wake-up engine configured");
    return 0;
}

static void state_machine_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        uint8_t src = 0;

        k_sem_take(&wake_sem, K_FOREVER);

        if (imu_read_reg(LSM6DSL_REG_WAKE_UP_SRC, &src)) {
            continue;
        }

        bool is_wake = (src & LSM6DSL_WAKE_SRC_WU_IA) != 0;
        bool is_inact = (src & LSM6DSL_WAKE_SRC_SLEEP_STATE) != 0;

        if (is_wake && !motion_active) {
            motion_active = true;
            LOG_INF("IMU WAKE_UP src=0x%02x -> event packet_id=%u",
                    src, (uint8_t)(bthome_pid + 1));
            if (adv_state == ADV_CONNECTED) {
                LOG_INF("connected; suppressing event advertising");
            } else {
                led_pulse_event();
                (void)start_bthome_burst(1, EVENT_BURST_MS);
            }
        }

        if (is_inact && motion_active) {
            motion_active = false;
            LOG_INF("IMU INACTIVITY src=0x%02x", src);
        }
    }
}

K_THREAD_DEFINE(state_thread_id, 2048, state_machine_task, NULL, NULL, NULL,
                K_PRIO_PREEMPT(5), 0, K_TICKS_FOREVER);

/* ---- 1200-baud touch DFU trigger (debug builds only) ------------------- */

#if defined(CONFIG_USB_DEVICE_STACK)
/* The Adafruit nRF52 bootloader checks GPREGRET on reset: writing 0x57
 * before sys_reboot puts the chip into UF2 mode (mounts as XIAO-SENSE). */
#define DFU_MAGIC_UF2_RESET 0x57u

static const struct device *const cdc_dev =
    DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

static void touch_poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(touch_poll_work, touch_poll_handler);
static uint32_t last_baud;

static void touch_poll_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!device_is_ready(cdc_dev)) {
        k_work_schedule(&touch_poll_work, K_MSEC(200));
        return;
    }

    uint32_t baud = 0;
    (void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_BAUD_RATE, &baud);

    if (baud == 1200 && last_baud != 1200) {
        LOG_INF("1200-baud touch detected; rebooting to UF2 bootloader");
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
    int rc;
    bool imu_ready = false;

    build_ble_name();
    led_init_and_boot_blink();

#if defined(CONFIG_USB_DEVICE_STACK)
    rc = usb_enable(NULL);
    if (rc && rc != -EALREADY) {
        /* No console yet, but keep going; RTT may still work. */
    }
    k_msleep(500);
    LOG_INF("seeedmote-v2 starting (name=%s usb_rc=%d)", bt_name, rc);
    k_work_schedule(&touch_poll_work, K_MSEC(100));
#else
    LOG_INF("seeedmote-v2 starting (name=%s RTT-only build)", bt_name);
#endif

    rc = bt_enable(NULL);
    if (rc) {
        LOG_ERR("bt_enable failed: %d", rc);
        return rc;
    }

    rc = bt_set_name(bt_name);
    if (rc) {
        LOG_WRN("bt_set_name(%s) failed: %d", bt_name, rc);
    }
    bt_conn_cb_register(&conn_callbacks);

    rc = setup_imu_wake_int1();
    if (rc) {
        LOG_WRN("imu wake setup failed (%d); boot/config adv still enabled", rc);
    } else {
        imu_ready = true;
    }

    rc = start_bthome_burst(0, BOOT_HEARTBEAT_MS);
    if (rc) {
        return rc;
    }

    if (imu_ready) {
        k_thread_start(state_thread_id);
    }

    return 0;
}
