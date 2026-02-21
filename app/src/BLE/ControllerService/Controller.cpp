#include "Controller.h"
#include <bit>
#include <bitset>

namespace ble
{
// BLE UUIDs
static constexpr bt_uuid_128 ControllerSrvcUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x3f9a2c71, 0x8d44, 0x4e6b, 0x9a52, 0x1b7c3d9eaf10));
static constexpr bt_uuid_128 ControllerCharcUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x3f9a2c71, 0x8d44, 0x4e6b, 0x9a52, 0x1b7c3d9eaf11));

// automagically registered by declaration
BT_GATT_SERVICE_DEFINE(ControllerSrvc,
                       BT_GATT_PRIMARY_SERVICE(&ControllerSrvcUUID),
                       BT_GATT_CHARACTERISTIC(
                           &ControllerCharcUUID.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           nullptr,
                           [](struct bt_conn* conn,
                              const struct bt_gatt_attr* attr,
                              const void* buf,
                              uint16_t len,
                              uint16_t offset,
                              uint8_t flags) -> ssize_t
                           { return Controller::GetInstance().KeyWriteCb(conn, attr, buf, len, offset, flags); },
                           nullptr));

ssize_t Controller::KeyWriteCb(struct bt_conn* /*conn*/,
                               const struct bt_gatt_attr* /*attr*/,
                               const void* buf,
                               uint16_t len,
                               uint16_t offset,
                               uint8_t /*flags*/)
{
    if (offset != 0 || len > sizeof(KeyType))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    LastKeysBuffer = std::bit_cast<KeyType>(buf);
    return len;
}

std::array<Controller::Key, Controller::KeyCount> Controller::GetPressedKeys() const
{
    std::array<Key, KeyCount> Pressed {};
    KeyType Keys = LastKeysBuffer.load(std::memory_order_relaxed);
    std::bitset<32> Bits(Keys);

    size_t Index = 0;
    for (size_t i = 0; i < KeyCount; ++i)
    {
        if (Bits.test(i))
        {
            Pressed[Index++] = static_cast<Key>(i);
        }
    }

    return Pressed;
}

bt_gatt_service& Controller::GetService() const
{
    return const_cast<bt_gatt_service&>(reinterpret_cast<const bt_gatt_service&>(ControllerSrvc));
}
} // namespace ble
