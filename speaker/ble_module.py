import asyncio

from bleak import BleakClient, BleakScanner


async def select_device():
    while True:
        print("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=5.0)
        if not devices:
            print("No BLE devices found, retrying in 3 seconds...\n")
            await asyncio.sleep(3)
            continue

        for i, dev in enumerate(devices):
            print(f"{i}: {dev.name} [{dev.address}]")

        choice = input("Select a device by number (or press Enter to retry scan): ").strip()
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


async def run_ble(notification_callback):
    device_address = await select_device()

    async with BleakClient(device_address) as client:
        char_uuid = input("Enter BLE characteristic UUID for audio: ").strip()
        print(f"Connected to {device_address}")
        await client.start_notify(char_uuid, notification_callback)
        print("Listening for MP3 audio...")

        while True:
            await asyncio.sleep(1)
