/*
 * SeeedMote v2 — Web BT config GATT service implementation.
 *
 *   Primary service     a8b00001-3e8e-4b8f-9a1c-9b1f5e88aa00
 *     THS char (r/w u8) a8b00002-3e8e-4b8f-9a1c-9b1f5e88aa00
 *     DUR char (r/w u8) a8b00003-3e8e-4b8f-9a1c-9b1f5e88aa00
 *     CMD char (w u8)   a8b00004-3e8e-4b8f-9a1c-9b1f5e88aa00
 *
 * THS / DUR are LSM6DSL register payloads (see datasheet 0x5B/0x5C);
 * CMD currently accepts 0x01 = cold reboot. Reboot is deferred 200 ms so
 * the GATT write response goes out before NVIC reset.
 *
 * Handlers run in the BT RX thread. THS/DUR writes update the live IMU
 * registers and synchronously persist via Zephyr settings/NVS in main.c.
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "cfg_svc.h"

LOG_MODULE_REGISTER(cfg_svc, LOG_LEVEL_INF);

#define BT_UUID_CFG_SVC_VAL \
    BT_UUID_128_ENCODE(0xa8b00001, 0x3e8e, 0x4b8f, 0x9a1c, 0x9b1f5e88aa00)
#define BT_UUID_CFG_THS_VAL \
    BT_UUID_128_ENCODE(0xa8b00002, 0x3e8e, 0x4b8f, 0x9a1c, 0x9b1f5e88aa00)
#define BT_UUID_CFG_DUR_VAL \
    BT_UUID_128_ENCODE(0xa8b00003, 0x3e8e, 0x4b8f, 0x9a1c, 0x9b1f5e88aa00)
#define BT_UUID_CFG_CMD_VAL \
    BT_UUID_128_ENCODE(0xa8b00004, 0x3e8e, 0x4b8f, 0x9a1c, 0x9b1f5e88aa00)

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(BT_UUID_CFG_SVC_VAL);
static struct bt_uuid_128 ths_uuid = BT_UUID_INIT_128(BT_UUID_CFG_THS_VAL);
static struct bt_uuid_128 dur_uuid = BT_UUID_INIT_128(BT_UUID_CFG_DUR_VAL);
static struct bt_uuid_128 cmd_uuid = BT_UUID_INIT_128(BT_UUID_CFG_CMD_VAL);

#define CMD_REBOOT 0x01u

static void deferred_reboot(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_INF("cfg cmd: rebooting now");
    sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, deferred_reboot);

static ssize_t read_ths(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);
    uint8_t v = imu_get_wake_ths();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static ssize_t read_dur(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);
    uint8_t v = imu_get_wake_dur();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static ssize_t write_ths(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(flags);
    if (offset != 0 || len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t v = ((const uint8_t *)buf)[0];
    if (imu_set_wake_ths(v)) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    LOG_INF("THS <- 0x%02x", v);
    return len;
}

static ssize_t write_dur(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(flags);
    if (offset != 0 || len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t v = ((const uint8_t *)buf)[0];
    if (imu_set_wake_dur(v)) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    LOG_INF("DUR <- 0x%02x", v);
    return len;
}

static ssize_t write_cmd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(flags);
    if (offset != 0 || len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t cmd = ((const uint8_t *)buf)[0];
    if (cmd == CMD_REBOOT) {
        k_work_schedule(&reboot_work, K_MSEC(200));
        return len;
    }
    return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
}

/* clang-format off */
BT_GATT_SERVICE_DEFINE(cfg_svc,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),
    BT_GATT_CHARACTERISTIC(&ths_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_ths, write_ths, NULL),
    BT_GATT_CHARACTERISTIC(&dur_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_dur, write_dur, NULL),
    BT_GATT_CHARACTERISTIC(&cmd_uuid.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, write_cmd, NULL),
);
/* clang-format on */
