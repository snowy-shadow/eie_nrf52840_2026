#include "BLE.h"
#include "AudioService/Audio.h"

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

#define TRY_OR(expr, action)                            \
    do                                                  \
    {                                                   \
        if (auto Err = (expr); Err)                     \
        {                                               \
            printk("%s failed (err %d)\n", #expr, Err); \
            action;                                     \
        }                                               \
    } while (0)

namespace ble
{
static const bt_data AdvertData[] {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_SHORTENED, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const bt_data ScanResp[] {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// Define advertising parameters (connectable + use device name)
constexpr bt_le_adv_param AdvParam = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN,        // connectable
                                                          BT_GAP_ADV_FAST_INT_MIN_2, // min interval
                                                          BT_GAP_ADV_FAST_INT_MAX_2, // max interval
                                                          nullptr                    // no peer address filtering
);

static bt_conn* ActiveConn = nullptr;
static k_work_delayable RpcWork;

static void OnMtuExchangeResult(struct bt_conn* Conn, uint8_t Err, struct bt_gatt_exchange_params* Params)
{
    if (Err)
    {
        printk("MTU exchange or parameter updates failed (err %u)\n", Err);
    }
}

static void OnUpdateParams(k_work* Work)
{
    if (!ActiveConn)
    {
        return;
    }

    // Request 2M PHY for high throughput
    static const bt_conn_le_phy_param PhyParams = {
        .options     = BT_CONN_LE_PHY_OPT_NONE,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    TRY_OR(bt_conn_le_phy_update(ActiveConn, &PhyParams), printk("PHY update failed\n"));

    // Request maximum data length for the controller
    constexpr bt_conn_le_data_len_param DataLenParams = {
        .tx_max_len  = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };

    TRY_OR(bt_conn_le_data_len_update(ActiveConn, &DataLenParams), printk("Data length update failed\n"));

    // Initiate MTU exchange for the GATT layer
    static bt_gatt_exchange_params ExchangeParams;
    ExchangeParams.func = OnMtuExchangeResult;

    TRY_OR(bt_gatt_exchange_mtu(ActiveConn, &ExchangeParams), printk("MTU exchange failed\n"));
}

static void OnConnected(struct bt_conn* Conn, uint8_t Err)
{
    if (Err)
    {
        printk("Connection failed (err %u)\n", Err);
        return;
    }

    AudioService::GetInstance().OnConnected(Conn);

    if (ActiveConn)
    {
        bt_conn_unref(ActiveConn);
    }
    ActiveConn = bt_conn_ref(Conn);

    // Delay parameter updates to ensure peripheral stability on connection
    constexpr auto UpdateDelay = K_MSEC(500);
    k_work_reschedule(&RpcWork, UpdateDelay);

    printk("Connected\n");
}

static void OnDisconnected(bt_conn* Conn, uint8_t Reason)
{
    if (ActiveConn == Conn)
    {
        k_work_cancel_delayable(&RpcWork);
        bt_conn_unref(ActiveConn);
        ActiveConn = nullptr;
    }
    printk("Disconnected (reason %u)\n", Reason);
}

static void OnLeParamUpdated(struct bt_conn* Conn, uint16_t Interval, uint16_t Latency, uint16_t Timeout)
{
    printk("LE Params Updated: Interval %u, Latency %u, Timeout %u\n", Interval, Latency, Timeout);
}

static void OnPhyUpdated(struct bt_conn* Conn, struct bt_conn_le_phy_info* Param)
{
    printk("PHY Updated: TX %u, RX %u\n", Param->tx_phy, Param->rx_phy);
}

static void OnMtuUpdated(struct bt_conn* Conn, uint16_t Tx, uint16_t Rx)
{
    printk("MTU Updated: TX %u, RX %u\n", Tx, Rx);
    AudioService::GetInstance().SetMtu(Tx);
}

BT_CONN_CB_DEFINE(ConnCallbacks) = {
    .connected        = OnConnected,
    .disconnected     = OnDisconnected,
    .le_param_updated = OnLeParamUpdated,
    .le_phy_updated   = OnPhyUpdated,
};

static bt_gatt_cb GattCallbacks = {
    .att_mtu_updated = OnMtuUpdated,
};

bool Init()
{
    TRY_OR(bt_enable(nullptr), return false);
    bt_gatt_cb_register(&GattCallbacks);
    k_work_init_delayable(&RpcWork, OnUpdateParams);

    TRY_OR(bt_le_adv_start(&AdvParam, AdvertData, ARRAY_SIZE(AdvertData), ScanResp, ARRAY_SIZE(ScanResp)),
           return false);
    return true;
}
} // namespace ble
