#include "Audio.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/printk.h>
#include <cstring>

#include "assets/song.h"

static k_work_delayable SendAudio;
static k_work_q AudioWorkQueue;
K_THREAD_STACK_DEFINE(AudioWorkQueueStack, CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);

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

AudioService::AudioService() : audio_conn(nullptr)
{
    k_work_queue_start(&AudioWorkQueue,
                       AudioWorkQueueStack,
                       K_THREAD_STACK_SIZEOF(AudioWorkQueueStack),
                       CONFIG_SYSTEM_WORKQUEUE_PRIORITY,
                       nullptr);
    k_work_init_delayable(&SendAudio, AudioService::BeginSong);
    k_sem_init(&NotifyCredits, NotifyWindow, NotifyWindow);
}

bool AudioService::Start()
{
    if (!audio_conn)
    {
        printk("cannot start audio without a connection\n");
        return false;
    }

    if (!NotificationsEnabled.load(std::memory_order_relaxed))
    {
        printk("cannot start audio before notifications are enabled\n");
        return false;
    }

    const int err = k_work_reschedule_for_queue(&AudioWorkQueue, &SendAudio, K_NO_WAIT);
    if (err < 0)
    {
        printk("failed to queue audio start (err %d)\n", err);
        return false;
    }

    return true;
}

void AudioService::BeginSong(k_work*) { AudioService::GetInstance().Send(song, song_len); }

void AudioService::OnNotifyComplete(struct bt_conn*, void* user_data)
{
    auto* Service = static_cast<AudioService*>(user_data);
    k_sem_give(&Service->NotifyCredits);
}

/* CCCD change callback */
void AudioService::OnCccChanged(const struct bt_gatt_attr* attr, uint16_t value)
{
    ARG_UNUSED(attr);
    NotificationsEnabled.store((value & BT_GATT_CCC_NOTIFY) != 0, std::memory_order_relaxed);
    printk("Audio notification CCCD updated: %u\n", value);
}

/* Called by BLE manager when a new connection is established */
void AudioService::OnConnected(struct bt_conn* conn) { audio_conn = conn; }

void AudioService::OnDisconnected(struct bt_conn* conn)
{
    if (audio_conn == conn)
    {
        audio_conn = nullptr;
        NotificationsEnabled.store(false, std::memory_order_relaxed);
        k_work_cancel_delayable(&SendAudio);

        while (k_sem_count_get(&NotifyCredits) < NotifyWindow)
        {
            k_sem_give(&NotifyCredits);
        }
    }
}

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
        printk("no audio conn\n");
        return;
    }

    size_t offset = 0;
    while (offset < len)
    {
        if (!audio_conn)
        {
            printk("audio conn lost\n");
            return;
        }

        uint16_t mtu_payload = AudioMTU.load(std::memory_order_relaxed);
        if (mtu_payload == 0)
        {
            printk("mtu payload unavailable\n");
            return;
        }

        if (k_sem_take(&NotifyCredits, K_SECONDS(1)) != 0)
        {
            printk("notify credit timeout\n");
            return;
        }

        if (!audio_conn)
        {
            k_sem_give(&NotifyCredits);
            printk("audio conn lost\n");
            return;
        }

        size_t chunk_len = (len - offset > mtu_payload) ? mtu_payload : (len - offset);

        /*
         * The notify call must be given the **value** attribute, not the
         * characteristic declaration.  BT_GATT_SERVICE_DEFINE lays out the
         * attrs array like this:
         *   0 -> primary service
         *   1 -> characteristic declaration
         *   2 -> characteristic value  <-- the one we want
         *   3 ... descriptors (CCCD, etc.)
         *
         * Passing attrs[1] quietly succeeds and increments the ATT TX queue
         * (CONFIG_BT_ATT_TX_COUNT is 10), but the peer never sees anything
         * because we're notifying a handle it doesn't care about.  The
         * central therefore observes "ten packets queued" and then nothing.
         */
        bt_gatt_notify_params NotifyParams {};
        NotifyParams.attr = &AudioSrvc.attrs[2];
        NotifyParams.data = &data[offset];
        NotifyParams.len = chunk_len;
        NotifyParams.func = AudioService::OnNotifyComplete;
        NotifyParams.user_data = this;

        int err = bt_gatt_notify_cb(audio_conn, &NotifyParams);
        if (err)
        {
            /* common errors: -ENOMEM when the TX queue is full */
            printk("notify failed (err %d)\n", err);
            k_sem_give(&NotifyCredits);

            if (err == -ENOMEM)
            {
                /* wait for room in the queue before retrying */
                k_msleep(1);
                continue;
            }

            return;
        }

        offset += chunk_len;
    }

    printk("audio send complete (%u bytes)\n", static_cast<unsigned int>(len));
}

bt_gatt_service& AudioService::GetService() const
{
    return const_cast<bt_gatt_service&>(reinterpret_cast<const bt_gatt_service&>(AudioSrvc));
}
} // namespace ble
