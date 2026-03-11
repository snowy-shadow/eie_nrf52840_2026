#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <zephyr/bluetooth/gatt.h>

namespace ble
{
class Controller
{
public:
    // Returns the most recent single key press, if one is pending.
    std::optional<uint8_t> TakeKeyPress();

    // Callback for BLE GATT write
    ssize_t KeyWriteCb(struct bt_conn* conn,
                       const struct bt_gatt_attr* attr,
                       const void* buf,
                       uint16_t len,
                       uint16_t offset,
                       uint8_t flags);

    // Access GATT service
    bt_gatt_service& GetService() const;

public:
    // Access the singleton instance
    static Controller& GetInstance()
    {
        static Controller instance; // created once
        return instance;
    }

    // Delete copy/move constructors
    Controller(const Controller&)            = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&)                 = delete;
    Controller& operator=(Controller&&)      = delete;

private:
    // Private constructor for singleton
    Controller() = default;

    static bool IsLowerAscii(uint8_t key_value);

    using KeyWireType = std::byte;
    using PendingKeyType = int16_t;

    static constexpr PendingKeyType NoKeyPress = -1;

    std::atomic<PendingKeyType> PendingKeyPress {NoKeyPress};
};
} // namespace ble
