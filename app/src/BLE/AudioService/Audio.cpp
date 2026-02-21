#include "Audio.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/printk.h>
#include <cstring>

namespace ble
{
// UUIDs (kept in CPP, not exposed in header)
static constexpr bt_uuid_128 AudioSrvcUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1122, 0x3344, 0x5566, 0x778899aabbcc));
static constexpr bt_uuid_128 AudioCharUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1122, 0x3344, 0x5566, 0x778899aabbcd));

/* GATT service definition (attributes + CCCD routed to singleton instance) */
BT_GATT_SERVICE_DEFINE(
    AudioSrvc,
    BT_GATT_PRIMARY_SERVICE(&AudioSrvcUUID.uuid),
    BT_GATT_CHARACTERISTIC(&AudioCharUUID.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
    BT_GATT_CCC(
        /* CCCD write/read routed to instance */
        [](const struct bt_gatt_attr* attr, uint16_t value) { AudioService::GetInstance().OnCccChanged(attr, value); },
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

AudioService::AudioService() : audio_conn(nullptr) {}

/* CCCD change callback */
void AudioService::OnCccChanged(const struct bt_gatt_attr* attr, uint16_t value)
{
    printk("Audio notification CCCD updated: %u\n", value);
}

/* Called by BLE manager when a new connection is established */
void AudioService::OnConnected(struct bt_conn* conn) { audio_conn = conn; }

/* Update stored MTU (invoked by the BLE manager after exchange) */
void AudioService::SetMtu(uint16_t mtu)
{
    // -3 for header
    AudioMTU.store(mtu - 3, std::memory_order_relaxed);
}

/* Send audio notifications */
void AudioService::Send(const uint8_t* data, size_t len)
{
    if (!audio_conn)
    {
        return;
    }

    uint16_t mtu_payload = AudioMTU.load(std::memory_order_relaxed);
    if (mtu_payload <= 3)
    {
        return;
    }

    for (size_t offset = 0; offset < len; offset += mtu_payload)
    {
        size_t chunk_len = (len - offset > mtu_payload) ? mtu_payload : (len - offset);
        bt_gatt_notify(audio_conn, &AudioSrvc.attrs[2], &data[offset], chunk_len);
    }
}

bt_gatt_service& AudioService::GetService() const
{
    return const_cast<bt_gatt_service&>(reinterpret_cast<const bt_gatt_service&>(AudioSrvc));
}
} // namespace ble
