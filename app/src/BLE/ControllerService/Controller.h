#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <zephyr/bluetooth/gatt.h>

namespace ble
{
class Controller
{
public:
    // Enum for keys
    enum Key : uint8_t
    {
        KeyA = 0,
        KeyB,
        KeyC,
        KeyD,
        KeyE,
        KeyF,
        KeyG,
        KeyH,
        // Add more keys up to 31
        KeyCount
    };

public:
    // Returns an array of currently pressed keys
    std::array<Key, KeyCount> GetPressedKeys() const;

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

    using KeyType = uint32_t;
    std::atomic<KeyType> LastKeysBuffer {0};
};
} // namespace ble
