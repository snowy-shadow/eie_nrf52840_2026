#pragma once

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ble
{
class AudioService
{
public:
    /** Called when a client writes the CCCD (enable/disable notifications) */
    void OnCccChanged(const struct bt_gatt_attr* attr, uint16_t value);

    /** Called when a connection is established; binds conn to this service. */
    void OnConnected(struct bt_conn* conn);

    /** Update stored MTU (called by BLE manager after successful exchange) */
    void SetMtu(uint16_t mtu);

    /** Get reference to the registered GATT service */
    bt_gatt_service& GetService() const;

public:
    // Access the singleton instance
    static AudioService& GetInstance()
    {
        static AudioService instance; // created once
        return instance;
    }

    // Delete copy/move constructors
    AudioService(const AudioService&)            = delete;
    AudioService& operator=(const AudioService&) = delete;
    AudioService(AudioService&&)                 = delete;
    AudioService& operator=(AudioService&&)      = delete;

protected:
    /** Send raw audio data to client (chunked to MTU - 3) */
    void Send(const uint8_t* data, size_t len);

private:
    // Private constructor for singleton
    AudioService();

    std::atomic<uint16_t> AudioMTU {23};
    struct bt_conn* audio_conn {nullptr};
};
} // namespace ble
