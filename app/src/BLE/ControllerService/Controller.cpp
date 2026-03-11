#include "Controller.h"

#include <zephyr/sys/printk.h>

namespace ble
{
// BLE UUIDs
static constexpr bt_uuid_128 ControllerSrvcUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x3f9a2c71, 0x8d44, 0x4e6b, 0x9a52, 0x1b7c3d9eaf10));
static constexpr bt_uuid_128 ControllerCharcUUID =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x3f9a2c71, 0x8d44, 0x4e6b, 0x9a52, 0x1b7c3d9eaf11));

// automagically registered by declaration
BT_GATT_SERVICE_DEFINE(ControllerSrvc,
                       BT_GATT_PRIMARY_SERVICE(&ControllerSrvcUUID.uuid),
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
    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len < sizeof(KeyWireType))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t KeyValue = std::to_integer<uint8_t>(*static_cast<const KeyWireType*>(buf));
    if (!IsLowerAscii(KeyValue))
    {
        printk("controller write rejected: 0x%02x len=%u\n", KeyValue, len);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    PendingKeyPress.store(static_cast<PendingKeyType>(KeyValue), std::memory_order_relaxed);
    printk("controller key received: %c (0x%02x) len=%u\n", KeyValue, KeyValue, len);
    return len;
}

std::optional<uint8_t> Controller::TakeKeyPress()
{
    const PendingKeyType KeyValue = PendingKeyPress.exchange(NoKeyPress, std::memory_order_relaxed);
    if (KeyValue == NoKeyPress)
    {
        return std::nullopt;
    }

    return static_cast<uint8_t>(KeyValue);
}

bool Controller::IsLowerAscii(uint8_t key_value)
{
    return key_value >= 'a' && key_value <= 'z';
}

bt_gatt_service& Controller::GetService() const
{
    return const_cast<bt_gatt_service&>(reinterpret_cast<const bt_gatt_service&>(ControllerSrvc));
}
} // namespace ble
