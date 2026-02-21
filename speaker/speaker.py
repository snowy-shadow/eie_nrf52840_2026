import asyncio

from audio_module import AudioReceiver
from ble_module import run_ble


if __name__ == "__main__":
    audio_receiver = AudioReceiver()
    audio_receiver.start()
    asyncio.run(run_ble(audio_receiver.handle_notification))
