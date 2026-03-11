import asyncio

from bleak import BleakClient, BleakScanner


async def select_device():
    while True:
        print("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=3.0)
        if not devices:
            print("No BLE devices found, retrying in 3 seconds...\n")
            await asyncio.sleep(3)
            continue

        for i, dev in enumerate(devices):
            print(f"{i}: {dev.name} [{dev.address}]")

        choice = input(
            "Select a device by number (or press Enter to retry scan): "
        ).strip()
        if choice == "":
            print("Retrying scan...\n")
            await asyncio.sleep(3)
            continue

        try:
            choice_idx = int(choice)
            selected_device = devices[choice_idx]
            print(f"Selected {selected_device.name} [{selected_device.address}]\n")
            return selected_device.address
        except (ValueError, IndexError):
            print("Invalid choice, retrying scan...\n")
            await asyncio.sleep(3)


async def run_ble(
    notification_callbacks: dict,
    on_connected=None,
    on_cleanup=None,
    stop_event: asyncio.Event | None = None,
):
    """Connect to a BLE device and start notifications for each UUID.

    ``notification_callbacks`` should be a mapping of characteristic UUID strings
    to callback functions. Each callback will be registered with ``start_notify``.

    The connection will remain open until the peripheral disconnects, at which
    point all subscriptions are stopped automatically.
    """

    device_address = await select_device()
    disconnected_event = asyncio.Event()

    def on_disconnect(_client):
        print("BLE disconnected, shutting down listener...")
        disconnected_event.set()

    async with BleakClient(
        device_address,
        mtu_size=251,
        disconnected_callback=on_disconnect,
    ) as client:
        print(f"Connected to {device_address}, MTU: {client.mtu_size}")

        # subscribe to each characteristic
        for char_uuid, callback in notification_callbacks.items():
            await client.start_notify(char_uuid, callback)
            print(f"Started notify on {char_uuid}")

        try:
            if on_connected is not None:
                await on_connected(client)

            print("Listening for notifications...")
            if stop_event is None:
                await disconnected_event.wait()
            else:
                disconnect_waiter = asyncio.create_task(disconnected_event.wait())
                stop_waiter = asyncio.create_task(stop_event.wait())

                done, pending = await asyncio.wait(
                    {disconnect_waiter, stop_waiter},
                    return_when=asyncio.FIRST_COMPLETED,
                )

                for task in pending:
                    task.cancel()

                if pending:
                    await asyncio.gather(*pending, return_exceptions=True)

                if stop_waiter in done and client.is_connected:
                    print("Shutdown requested, disconnecting BLE client...")
                    await client.disconnect()
        finally:
            if on_cleanup is not None:
                await on_cleanup()

            if client.is_connected:
                for char_uuid in notification_callbacks.keys():
                    try:
                        await client.stop_notify(char_uuid)
                    except Exception as e:
                        print(f"Failed to stop notify for {char_uuid}: {e}")
