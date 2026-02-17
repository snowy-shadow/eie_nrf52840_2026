/**
 * @file main.c
 */

extern "C"
{
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include "BTN.h"
#include "LED.h"
}

#include <array>

// ===== Custom UUIDs (match client UUID!) =====
#define SERVICE_UUID_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x123456789abc)
#define CHAR_UUID_VAL    BT_UUID_128_ENCODE(0xabcdef01, 0x1234, 0x5678, 0x1234, 0x123456789abc)

static bt_uuid_128 service_uuid = BT_UUID_INIT_128(SERVICE_UUID_VAL);
static bt_uuid_128 char_uuid    = BT_UUID_INIT_128(CHAR_UUID_VAL);

// ===== Characteristic Value =====
static uint8_t notify_value = 0;

// ===== Read Callback =====
static ssize_t read_func(bt_conn* conn, const bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset)
{
    const uint8_t* value = reinterpret_cast<const uint8_t*>(attr->user_data);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(notify_value));
}

// ===== CCC (Client Config) Changed Callback =====
static void ccc_changed(const bt_gatt_attr* attr, uint16_t value)
{
    if (value == BT_GATT_CCC_NOTIFY)
    {
        printk("Notifications enabled\n");
    }
    else
    {
        printk("Notifications disabled\n");
    }
}

// ===== GATT Service Definition =====
BT_GATT_SERVICE_DEFINE(custom_svc,
                       BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_func,
                                              nullptr,
                                              &notify_value),
                       BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

// ===== Connection Callbacks =====
static void connected(bt_conn* conn, uint8_t err)
{
    if (err)
    {
        printk("Connection failed (err %u)\n", err);
    }
    else
    {
        printk("Connected\n");
    }
}

static void disconnected(bt_conn* conn, uint8_t reason) { printk("Disconnected (reason %u)\n", reason); }

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

#define BT_UUID_CUSTOM_SERVICE_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),

    BT_DATA(BT_DATA_NAME_SHORTENED, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),

    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE_VAL),
};

/* MAIN ----------------------------------------------------------------------------------------- */

int main(void)
{
    if (0 > BTN_init())
    {
        return 0;
    }
    if (0 > LED_init())
    {
        return 0;
    }

    int err = bt_enable(NULL);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }
    else
    {
        printk("Bluetooth initialized\n");
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), nullptr, 0);
    if (err)
    {
        printk("Advertising failed (err %d)\n", err);
        return 0;
    }
    printk("Advertising started\n");

    while (1)
    {
        printk("hi mom\n");
        k_msleep(100);
    }
    return 0;
    while (1)
    {
        k_msleep(20);

        // notify_value++;
        //
        // bt_gatt_notify(nullptr, &custom_svc.attrs[1], &notify_value, sizeof(notify_value));
        //
        printk("Notification sent: %u\n", notify_value);
    }

    // To disconnect: bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);

    return 0;
}
