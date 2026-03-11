#include "BLE.h"
#include "AudioService/Audio.h"

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

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
static k_work_delayable RestartAdvWork;
static bool ConnParamUpdateRequested = false;
static bool PhyUpdateRequested = false;
static bool DataLenUpdateRequested = false;
static bool MtuExchangeRequested = false;

static void ResetLinkUpdateState()
{
    ConnParamUpdateRequested = false;
    PhyUpdateRequested = false;
    DataLenUpdateRequested = false;
    MtuExchangeRequested = false;
}

static int StartAdvertising()
{
    int Err = bt_le_adv_start(&AdvParam, AdvertData, ARRAY_SIZE(AdvertData), ScanResp, ARRAY_SIZE(ScanResp));
    if (Err && Err != -EALREADY)
    {
        printk("bt_le_adv_start failed (err %d)\n", Err);
    }
    return Err;
}

static bool IsTransientBtError(int Err) { return Err == -EBUSY || Err == -ENOMEM; }

static bool HandleBtRequestResult(const char* Name, int Err)
{
    if (!Err)
    {
        printk("%s requested\n", Name);
        return true;
    }

    if (Err == -EALREADY)
    {
        printk("%s already completed\n", Name);
        return true;
    }

    printk("%s request failed (err %d)\n", Name, Err);
    return false;
}

static void OnMtuExchangeResult(struct bt_conn* Conn, uint8_t Err, struct bt_gatt_exchange_params* Params)
{
    ARG_UNUSED(Params);

    if (Err)
    {
        printk("MTU exchange callback failed (att err %u)\n", Err);
        return;
    }

    printk("MTU exchange complete, negotiated MTU %u\n", bt_gatt_get_mtu(Conn));
}

static void OnUpdateParams(k_work* Work)
{
    ARG_UNUSED(Work);

    if (!ActiveConn)
    {
        return;
    }

    bool Retry = false;

    // Request the shortest connection interval for best throughput.
    static constexpr bt_le_conn_param ConnParams = BT_LE_CONN_PARAM_INIT(6, 6, 0, 400);

    if (!ConnParamUpdateRequested)
    {
        int Err = bt_conn_le_param_update(ActiveConn, &ConnParams);
        if (HandleBtRequestResult("Conn params update", Err))
        {
            ConnParamUpdateRequested = true;
        }
        else if (IsTransientBtError(Err))
        {
            Retry = true;
        }
    }

    // Request 2M PHY for high throughput
    static const bt_conn_le_phy_param PhyParams = {
        .options     = BT_CONN_LE_PHY_OPT_NONE,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    if (!PhyUpdateRequested)
    {
        int Err = bt_conn_le_phy_update(ActiveConn, &PhyParams);
        if (HandleBtRequestResult("PHY update", Err))
        {
            PhyUpdateRequested = true;
        }
        else if (IsTransientBtError(Err))
        {
            Retry = true;
        }
    }

    // Request maximum data length for the controller
    constexpr bt_conn_le_data_len_param DataLenParams = {
        .tx_max_len  = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };

    if (!DataLenUpdateRequested)
    {
        int Err = bt_conn_le_data_len_update(ActiveConn, &DataLenParams);
        if (HandleBtRequestResult("Data length update", Err))
        {
            DataLenUpdateRequested = true;
        }
        else if (IsTransientBtError(Err))
        {
            Retry = true;
        }
    }

    // Initiate MTU exchange for the GATT layer
    static bt_gatt_exchange_params ExchangeParams;
    ExchangeParams.func = OnMtuExchangeResult;

    if (!MtuExchangeRequested)
    {
        int Err = bt_gatt_exchange_mtu(ActiveConn, &ExchangeParams);
        if (HandleBtRequestResult("MTU exchange", Err))
        {
            MtuExchangeRequested = true;
        }
        else if (IsTransientBtError(Err))
        {
            Retry = true;
        }
    }

    if (Retry)
    {
        k_work_reschedule(&RpcWork, K_MSEC(200));
    }
}

static void OnRestartAdvertising(k_work* Work)
{
    ARG_UNUSED(Work);
    StartAdvertising();
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
    ResetLinkUpdateState();

    k_work_cancel_delayable(&RestartAdvWork);

    // Start link updates quickly so MTU/PHY/data-length are negotiated before streaming ramps up.
    constexpr auto UpdateDelay = K_MSEC(100);
    k_work_reschedule(&RpcWork, UpdateDelay);

    printk("Connected\n");
}

static void OnDisconnected(bt_conn* Conn, uint8_t Reason)
{
    AudioService::GetInstance().OnDisconnected(Conn);

    if (ActiveConn == Conn)
    {
        k_work_cancel_delayable(&RpcWork);
        bt_conn_unref(ActiveConn);
        ActiveConn = nullptr;
    }
    ResetLinkUpdateState();
    printk("Disconnected (reason %u)\n", Reason);

    k_work_reschedule(&RestartAdvWork, K_MSEC(200));
}

static void OnLeParamUpdated(struct bt_conn* Conn, uint16_t Interval, uint16_t Latency, uint16_t Timeout)
{
    printk("LE Params Updated: Interval %u, Latency %u, Timeout %u\n", Interval, Latency, Timeout);
}

static void OnPhyUpdated(struct bt_conn* Conn, struct bt_conn_le_phy_info* Param)
{
    printk("PHY Updated: TX %u, RX %u\n", Param->tx_phy, Param->rx_phy);
}

static void OnDataLenUpdated(struct bt_conn* Conn, struct bt_conn_le_data_len_info* Info)
{
    printk("Data length updated: TX %u/%u us, RX %u/%u us\n",
           Info->tx_max_len,
           Info->tx_max_time,
           Info->rx_max_len,
           Info->rx_max_time);
}

static void OnMtuUpdated(struct bt_conn* Conn, uint16_t Tx, uint16_t Rx)
{
    uint16_t effective = (Tx < Rx) ? Tx : Rx;
    printk("MTU Updated: TX %u, RX %u, using %u\n", Tx, Rx, effective);
    AudioService::GetInstance().SetMtu(effective);
}

BT_CONN_CB_DEFINE(ConnCallbacks) = {
    .connected        = OnConnected,
    .disconnected     = OnDisconnected,
    .le_param_updated = OnLeParamUpdated,
    .le_phy_updated   = OnPhyUpdated,
    .le_data_len_updated = OnDataLenUpdated,
};

static bt_gatt_cb GattCallbacks = {
    .att_mtu_updated = OnMtuUpdated,
};

bool Init()
{
    TRY_OR(bt_enable(nullptr), return false);
    bt_gatt_cb_register(&GattCallbacks);
    k_work_init_delayable(&RpcWork, OnUpdateParams);
    k_work_init_delayable(&RestartAdvWork, OnRestartAdvertising);

    TRY_OR(StartAdvertising(), return false);
    return true;
}
} // namespace ble
