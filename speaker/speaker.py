import asyncio
import threading
import time
from io import BytesIO
from queue import Empty, Queue

import numpy as np
import sounddevice as sd
from bleak import BleakClient, BleakScanner
from pydub import AudioSegment
from pydub.utils import make_chunks

# Shared queue for audio frames
audio_queue = Queue()

# Buffer for incoming MP3 bytes
mp3_buffer = bytearray()


# Thread to continuously play PCM chunks from the queue
def audio_player():
    while True:
        try:
            pcm, rate = audio_queue.get(timeout=1)
            sd.play(pcm, samplerate=rate, blocking=True)
        except Empty:
            continue


# BLE notification callback
def handle_notification(sender, data):
    global mp3_buffer
    mp3_buffer.extend(data)

    try:
        audio = AudioSegment.from_file(BytesIO(mp3_buffer), format="mp3")
        chunks = make_chunks(audio, 100)  # 100ms chunks

        for chunk in chunks:
            pcm = np.array(chunk.get_array_of_samples())
            audio_queue.put((pcm, chunk.frame_rate))

        mp3_buffer.clear()  # clear buffer after decoding
    except Exception:
        # Not enough data yet, wait for more
        pass


# Scan and let user select a BLE device
async def select_device():
    while True:
        print("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=5.0)
        if not devices:
            print("No BLE devices found, retrying in 3 seconds...\n")
            time.sleep(3)
            continue

        for i, dev in enumerate(devices):
            print(f"{i}: {dev.name} [{dev.address}]")

        choice = input(
            "Select a device by number (or press Enter to retry scan): "
        ).strip()
        if choice == "":
            print("Retrying scan...\n")
            time.sleep(3)
            continue

        try:
            choice_idx = int(choice)
            selected_device = devices[choice_idx]
            print(f"Selected {selected_device.name} [{selected_device.address}]\n")
            return selected_device.address
        except (ValueError, IndexError):
            print("Invalid choice, retrying scan...\n")
            time.sleep(3)


# Main BLE connection and notification loop
async def run_ble():
    device_address = await select_device()

    async with BleakClient(device_address) as client:
        char_uuid = input("Enter BLE characteristic UUID for audio: ").strip()
        print(f"Connected to {device_address}")
        await client.start_notify(char_uuid, handle_notification)
        print("Listening for MP3 audio...")
        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":
    # Start audio player thread
    threading.Thread(target=audio_player, daemon=True).start()

    # Run BLE event loop
    asyncio.run(run_ble())
