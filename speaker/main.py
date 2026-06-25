import asyncio
import signal

from audio_module import AudioReceiver
from ble_module import run_ble

# from controller_module import ControllerSender

# instantiate here so both notification map and lifecycle use same object
audio_receiver = AudioReceiver()
# controller_sender = ControllerSender()

notification_callbacks = {AudioReceiver.AUDIO_UUID: audio_receiver.handle_notification}


async def main():
    print("Starting BLE controller")
    loop = asyncio.get_running_loop()
    shutdown_event = asyncio.Event()

    def request_shutdown():
        shutdown_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, request_shutdown)

    # start the audio receiver directly
    audio_receiver.start()

    try:
        await run_ble(
            notification_callbacks,
            # on_connected=controller_sender.start,
            # on_cleanup=controller_sender.stop,
            stop_event=shutdown_event,
        )
    finally:
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.remove_signal_handler(sig)

        audio_receiver.stop()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
