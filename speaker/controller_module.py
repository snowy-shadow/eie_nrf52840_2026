import asyncio
import struct
import sys

from pynput import keyboard

try:
    import HIServices
except ImportError:
    HIServices = None


class ControllerSender:
    CONTROLLER_UUID = "3f9a2c71-8d44-4e6b-9a52-1b7c3d9eaf11"
    LOWER_A = ord("a")
    LOWER_Z = ord("z")

    def __init__(self):
        self._client = None
        self._listener = None
        self._loop = None

    async def start(self, client):
        if self._listener is not None:
            return

        self._client = client
        self._loop = asyncio.get_running_loop()

        self._listener = keyboard.Listener(
            on_press=self._on_press,
        )
        self._listener.start()
        print(
            "Controller sender ready. Press lowercase keys to send controller events. "
            "Press Esc to quit."
        )

    async def stop(self):
        listener = self._listener

        self._listener = None
        self._client = None
        self._loop = None

        if listener is not None:
            listener.stop()
            listener.join()

    def _on_press(self, key):
        if key == keyboard.Key.esc:
            print("Exit requested, disconnecting...")
            self._disconnect()
            return

        key_value = self._get_key_value(key)
        if key_value is None:
            return

        if self._loop is not None and self._client is not None:
            self._loop.call_soon_threadsafe(
                asyncio.create_task,
                self._send_keypress(self._client, key_value),
            )

    def _disconnect(self):
        if self._loop is None or self._client is None:
            return

        self._loop.call_soon_threadsafe(
            asyncio.create_task,
            self._disconnect_client(self._client),
        )

    def _get_key_value(self, key):
        if not isinstance(key, keyboard.KeyCode) or key.char is None:
            return None

        if len(key.char) != 1:
            return None

        key_value = ord(key.char)
        if key_value < self.LOWER_A or key_value > self.LOWER_Z:
            return None

        return key_value

    async def _disconnect_client(self, client):
        try:
            if client.is_connected:
                await client.disconnect()
        except Exception as exc:
            print(f"Failed to disconnect controller client: {exc}")

    async def _send_keypress(self, client, key_value):
        payload = struct.pack("<B", key_value)
        print(f"Controller key: {chr(key_value)} ({key_value})")

        try:
            await client.write_gatt_char(
                self.CONTROLLER_UUID,
                payload,
                response=False,
            )
        except Exception as exc:
            print(f"Failed to send controller key {key_value}: {exc}")