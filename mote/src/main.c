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
#include <errno.h>
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
#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include <hal/nrf_ficr.h>
#if defined(CONFIG_USB_DEVICE_STACK)
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#endif

#include "motion_gate.h"

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
#define CONFIG_LED_INTERVAL_MS       5000
#define CONFIG_LED_ON_MS             30
#define CONNECTED_LED_INTERVAL_MS    2000
#define CONNECTED_LED_ON_MS          80
#define MOTION_COOLDOWN_MS           2000
#define MOTION_MIN_SCORE_MG          90
#define MOTION_SAMPLE_COUNT          4
#define MOTION_SAMPLE_INTERVAL_MS    40

/* ---- LSM6DSL wake-up registers ----------------------------------------- */

#define LSM6DSL_REG_WAKE_UP_SRC      0x1Bu
#define LSM6DSL_REG_CTRL1_XL         0x10u
#define LSM6DSL_REG_CTRL3_C          0x12u
#define LSM6DSL_REG_CTRL6_C          0x15u
#define LSM6DSL_REG_INT1_CTRL        0x0Du
#define LSM6DSL_REG_OUTX_L_XL        0x28u
#define LSM6DSL_REG_TAP_CFG          0x58u
#define LSM6DSL_REG_WAKE_UP_THS      0x5Bu
#define LSM6DSL_REG_WAKE_UP_DUR      0x5Cu
#define LSM6DSL_REG_MD1_CFG          0x5Eu

#define LSM6DSL_WAKE_SRC_WU_IA       BIT(3)
#define LSM6DSL_WAKE_SRC_SLEEP_STATE BIT(4)

/* CTRL1_XL=0x20: accelerometer on at 26 Hz, +/-2g. The Zephyr driver
 * leaves ODR runtime-selected by default, so set the wake engine's clock
 * explicitly before programming embedded wake-up detection. */
#define LSM6DSL_CTRL1_XL_26HZ_2G     0x20u
#define LSM6DSL_CTRL3_C_BDU_IF_INC   (BIT(6) | BIT(2))
#define LSM6DSL_CTRL6_C_XL_LP        0x10u
#define LSM6DSL_TAP_CFG_WAKE         0xF0u
/* Runtime-tunable via cfg_svc (Web BT). Defaults are used until settings load.
 * THS units: ±2g / 64 = 31.25 mg per LSB. 0x03 ≈ 94 mg.
 * DUR layout: bits[6:5]=WAKE_DUR (in 1/ODR units), bits[3:0]=SLEEP_DUR.
 * 0x21 = WAKE_DUR=1 (38 ms @ 26 Hz) + SLEEP_DUR=1 (~20 s before INACT). */
#define LSM6DSL_WAKE_UP_THS_DEFAULT  0x03u
#define LSM6DSL_WAKE_UP_DUR_DEFAULT  0x21u

static uint8_t imu_wake_ths = LSM6DSL_WAKE_UP_THS_DEFAULT;
static uint8_t imu_wake_dur = LSM6DSL_WAKE_UP_DUR_DEFAULT;
#define CFG_SETTINGS_TREE            "mote/imu"
#define CFG_SETTINGS_THS             CFG_SETTINGS_TREE "/ths"
#define CFG_SETTINGS_DUR             CFG_SETTINGS_TREE "/dur"
#if defined(CONFIG_SETTINGS)
static bool cfg_settings_ready;
#endif
/* LSM6DSL/LSM6DS3TR-C MD1_CFG: bit7=INT1_INACT_STATE, bit5=INT1_WU. */
#define LSM6DSL_MD1_CFG_INT1_INACT_STATE BIT(7)
#define LSM6DSL_MD1_CFG_INT1_WU          BIT(5)
#define LSM6DSL_MD1_CFG_WAKE_INACT       (LSM6DSL_MD1_CFG_INT1_INACT_STATE | \
                                           LSM6DSL_MD1_CFG_INT1_WU)
BUILD_ASSERT(LSM6DSL_MD1_CFG_WAKE_INACT == 0xA0u,
             "MD1_CFG must route inactivity + wake-up to INT1");

/* ---- Devicetree --------------------------------------------------------- */

#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

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
static char bt_name[sizeof("SEEED-FFFFFF")];
static K_MUTEX_DEFINE(adv_mutex);

static struct motion_gate motion_gate;
static const struct motion_gate_config motion_gate_cfg = {
    .min_score_mg = MOTION_MIN_SCORE_MG,
    .cooldown_ms = MOTION_COOLDOWN_MS,
};

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
static void connected_led_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(burst_done_work, burst_done_handler);
static K_WORK_DELAYABLE_DEFINE(config_timeout_work, config_timeout_handler);
static K_WORK_DELAYABLE_DEFINE(config_led_work, config_led_handler);
static K_WORK_DELAYABLE_DEFINE(connected_led_work, connected_led_handler);

/* ---- LED ---------------------------------------------------------------- */

enum led_color {
    LED_COLOR_OFF = 0,
    LED_COLOR_RED = BIT(0),
    LED_COLOR_GREEN = BIT(1),
    LED_COLOR_BLUE = BIT(2),
    LED_COLOR_YELLOW = LED_COLOR_RED | LED_COLOR_GREEN,
    LED_COLOR_CYAN = LED_COLOR_GREEN | LED_COLOR_BLUE,
    LED_COLOR_WHITE = LED_COLOR_RED | LED_COLOR_GREEN | LED_COLOR_BLUE,
};

static bool leds_ready;

static void led_set_color(enum led_color color)
{
    if (!leds_ready) {
        return;
    }

    gpio_pin_set_dt(&led_red, (color & LED_COLOR_RED) != 0);
    gpio_pin_set_dt(&led_green, (color & LED_COLOR_GREEN) != 0);
    gpio_pin_set_dt(&led_blue, (color & LED_COLOR_BLUE) != 0);
}

static void led_off_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    led_set_color(LED_COLOR_OFF);
}
K_TIMER_DEFINE(led_off_timer, led_off_handler, NULL);

static void led_pulse(enum led_color color, k_timeout_t on_time)
{
    led_set_color(color);
    k_timer_start(&led_off_timer, on_time, K_NO_WAIT);
}

static void led_pulse_event(void)
{
    led_pulse(LED_COLOR_GREEN, K_MSEC(80));
}

#if defined(CONFIG_USB_DEVICE_STACK)
static void led_pulse_imu_irq(void)
{
    led_pulse(LED_COLOR_YELLOW, K_MSEC(25));
}
#endif

static void led_config_tick(void)
{
    led_pulse(LED_COLOR_BLUE, K_MSEC(CONFIG_LED_ON_MS));
}

static void led_connected_tick(void)
{
    led_pulse(LED_COLOR_CYAN, K_MSEC(CONNECTED_LED_ON_MS));
}

/* One-time boot indicator before BLE/IMU init. After this, release LED
 * patterns are user-level only: event=green pulse, config window=blue sparse
 * tick, connected=cyan sparse tick. Debug builds also show IMU IRQ yellow. */
static void led_init_and_boot_blink(void)
{
    if (!gpio_is_ready_dt(&led_red) ||
        !gpio_is_ready_dt(&led_green) ||
        !gpio_is_ready_dt(&led_blue)) {
        LOG_ERR("RGB LEDs not ready");
        return;
    }
    if (gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0 ||
        gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0 ||
        gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE) < 0) {
        LOG_ERR("RGB LED gpio configure failed");
        return;
    }
    leds_ready = true;

    led_set_color(LED_COLOR_WHITE);
    k_msleep(80);
    led_set_color(LED_COLOR_OFF);
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
    led_set_color(LED_COLOR_OFF);
}

static void cancel_connected_led(void)
{
    (void)k_work_cancel_delayable(&connected_led_work);
    led_set_color(LED_COLOR_OFF);
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
    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_state == ADV_CONNECTED) {
        k_mutex_unlock(&adv_mutex);
        LOG_INF("connected; suppressing BTHome burst moving=%u", moving);
        return -EALREADY;
    }

    cancel_config_led();
    cancel_connected_led();
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);
    fill_bthome_payload(moving);
    if (moving) {
        led_pulse_event();
    }

    stop_advertising();

    int rc = bt_le_adv_start(&event_adv_param,
                             bthome_adv_data, ARRAY_SIZE(bthome_adv_data),
                             NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start burst failed: %d", rc);
        adv_state = ADV_IDLE;
        k_mutex_unlock(&adv_mutex);
        return rc;
    }

    adv_state = ADV_EVENT_BURST;
    k_work_schedule(&burst_done_work, K_MSEC(duration_ms));
    LOG_INF("BTHome burst started moving=%u packet_id=%u",
            payload.moving, payload.packet_id);
    k_mutex_unlock(&adv_mutex);
    return 0;
}

static int enter_config_window(void)
{
    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_state == ADV_CONNECTED) {
        k_mutex_unlock(&adv_mutex);
        return 0;
    }

    cancel_config_led();
    cancel_connected_led();
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);

    stop_advertising();

    int rc = bt_le_adv_start(&config_adv_param,
                             config_adv_data, ARRAY_SIZE(config_adv_data),
                             NULL, 0);
    if (rc) {
        LOG_ERR("bt_le_adv_start config window failed: %d", rc);
        adv_state = ADV_IDLE;
        k_mutex_unlock(&adv_mutex);
        return rc;
    }

    adv_state = ADV_CONFIG_WINDOW;
    led_config_tick();
    k_work_schedule(&config_led_work, K_MSEC(CONFIG_LED_INTERVAL_MS));
    k_work_schedule(&config_timeout_work, K_MSEC(CONFIG_WINDOW_MS));
    LOG_INF("config window opened for %d ms", CONFIG_WINDOW_MS);
    k_mutex_unlock(&adv_mutex);
    return 0;
}

static void enter_idle(void)
{
    k_mutex_lock(&adv_mutex, K_FOREVER);
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);
    cancel_config_led();
    cancel_connected_led();
    stop_advertising();
    adv_state = ADV_IDLE;
    LOG_INF("advertising idle");
    k_mutex_unlock(&adv_mutex);
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

    k_mutex_lock(&adv_mutex, K_FOREVER);
    if (adv_state != ADV_CONFIG_WINDOW) {
        k_mutex_unlock(&adv_mutex);
        return;
    }

    led_config_tick();
    k_work_schedule(&config_led_work, K_MSEC(CONFIG_LED_INTERVAL_MS));
    k_mutex_unlock(&adv_mutex);
}

static void connected_led_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    k_mutex_lock(&adv_mutex, K_FOREVER);
    if (adv_state != ADV_CONNECTED) {
        k_mutex_unlock(&adv_mutex);
        return;
    }

    led_connected_tick();
    k_work_schedule(&connected_led_work, K_MSEC(CONNECTED_LED_INTERVAL_MS));
    k_mutex_unlock(&adv_mutex);
}

static void bt_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_WRN("BLE connect failed: %u", err);
        return;
    }

    (void)conn;
    k_mutex_lock(&adv_mutex, K_FOREVER);
    k_timer_stop(&led_off_timer);
    (void)k_work_cancel_delayable(&config_timeout_work);
    (void)k_work_cancel_delayable(&burst_done_work);
    cancel_config_led();
    adv_state = ADV_CONNECTED;
    led_connected_tick();
    k_work_schedule(&connected_led_work, K_MSEC(CONNECTED_LED_INTERVAL_MS));
    LOG_INF("BLE connected");
    k_mutex_unlock(&adv_mutex);
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

/* ---- Runtime IMU config (exposed to cfg_svc GATT writes) ---------------- */

uint8_t imu_get_wake_ths(void) { return imu_wake_ths; }
uint8_t imu_get_wake_dur(void) { return imu_wake_dur; }

#if defined(CONFIG_SETTINGS)
static int persist_u8_setting(const char *key, uint8_t value)
{
    if (!cfg_settings_ready) {
        return 0;
    }

    int rc = settings_save_one(key, &value, sizeof(value));

    if (rc) {
        LOG_WRN("settings_save_one(%s) failed: %d", key, rc);
    }
    return rc;
}

static int cfg_settings_set(const char *key, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    uint8_t value;
    ssize_t read_len;

    if (len != sizeof(value)) {
        return -EINVAL;
    }

    read_len = read_cb(cb_arg, &value, sizeof(value));
    if (read_len < 0) {
        return (int)read_len;
    }
    if (read_len != sizeof(value)) {
        return -EINVAL;
    }

    if (strcmp(key, "ths") == 0) {
        if (value > 63u) {
            return -ERANGE;
        }
        imu_wake_ths = value;
        LOG_INF("loaded THS=0x%02x from settings", value);
        return 0;
    }
    if (strcmp(key, "dur") == 0) {
        imu_wake_dur = value;
        LOG_INF("loaded DUR=0x%02x from settings", value);
        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(cfg_imu, CFG_SETTINGS_TREE, NULL,
                               cfg_settings_set, NULL, NULL);

static void cfg_settings_init(void)
{
    int rc = settings_subsys_init();

    if (rc) {
        LOG_WRN("settings_subsys_init failed: %d", rc);
        return;
    }

    cfg_settings_ready = true;
    rc = settings_load_subtree(CFG_SETTINGS_TREE);
    if (rc) {
        LOG_WRN("settings_load_subtree(%s) failed: %d", CFG_SETTINGS_TREE, rc);
    }
}
#else
static int persist_u8_setting(const char *key, uint8_t value)
{
    ARG_UNUSED(key);
    ARG_UNUSED(value);
    return 0;
}

static void cfg_settings_init(void) {}
#endif

int imu_set_wake_ths(uint8_t value)
{
    if (value > 63u) {
        return -ERANGE;
    }

    int rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_THS, value);

    if (rc == 0) {
        imu_wake_ths = value;
        rc = persist_u8_setting(CFG_SETTINGS_THS, value);
    }
    return rc;
}

int imu_set_wake_dur(uint8_t value)
{
    int rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_DUR, value);

    if (rc == 0) {
        imu_wake_dur = value;
        rc = persist_u8_setting(CFG_SETTINGS_DUR, value);
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

static int imu_read_accel_sample(struct motion_sample *sample)
{
    uint8_t raw[6];
    int rc;

    rc = i2c_burst_read_dt(&imu_i2c, LSM6DSL_REG_OUTX_L_XL,
                           raw, sizeof(raw));
    if (rc) {
        LOG_ERR("imu accel burst read failed: %d", rc);
        return rc;
    }

    sample->x = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
    sample->y = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
    sample->z = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);
    return 0;
}

static int imu_capture_motion_samples(struct motion_sample *samples,
                                      size_t count)
{
    for (size_t i = 0; i < count; i++) {
        int rc = imu_read_accel_sample(&samples[i]);

        if (rc) {
            return rc;
        }
        if (i + 1 < count) {
            k_msleep(MOTION_SAMPLE_INTERVAL_MS);
        }
    }

    return 0;
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
    rc = imu_write_reg(LSM6DSL_REG_CTRL3_C, LSM6DSL_CTRL3_C_BDU_IF_INC);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_CTRL1_XL, LSM6DSL_CTRL1_XL_26HZ_2G);
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
    rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_THS, imu_wake_ths);
    if (rc) {
        return rc;
    }
    rc = imu_write_reg(LSM6DSL_REG_WAKE_UP_DUR, imu_wake_dur);
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
        int32_t score_mg = 0;
        enum motion_gate_decision decision;
        struct motion_sample samples[MOTION_SAMPLE_COUNT];

        k_sem_take(&wake_sem, K_FOREVER);
#if defined(CONFIG_USB_DEVICE_STACK)
        led_pulse_imu_irq();
#endif

        if (imu_read_reg(LSM6DSL_REG_WAKE_UP_SRC, &src)) {
            continue;
        }

        bool is_wake = (src & LSM6DSL_WAKE_SRC_WU_IA) != 0;
        bool is_inact = (src & LSM6DSL_WAKE_SRC_SLEEP_STATE) != 0;

        if (is_wake) {
            if (imu_capture_motion_samples(samples, ARRAY_SIZE(samples)) == 0) {
                score_mg = motion_score_mg(samples, ARRAY_SIZE(samples));
            } else {
                LOG_WRN("IMU WAKE_UP src=0x%02x; using hardware trigger only",
                        src);
            }
        }

        decision = motion_gate_update(&motion_gate, &motion_gate_cfg,
                                      is_wake, is_inact, score_mg,
                                      k_uptime_get());

        if (decision == MOTION_GATE_EMIT) {
            LOG_INF("IMU motion src=0x%02x score=%dmg -> event packet_id=%u",
                    src, score_mg, (uint8_t)(bthome_pid + 1));
            (void)start_bthome_burst(1, EVENT_BURST_MS);
        } else if (decision == MOTION_GATE_INACTIVITY) {
            LOG_INF("IMU inactivity src=0x%02x", src);
        } else if (is_wake) {
            LOG_DBG("IMU motion suppressed src=0x%02x score=%dmg", src,
                    score_mg);
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

    cfg_settings_init();

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
