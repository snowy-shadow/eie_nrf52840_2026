from io import BytesIO
from queue import Queue
from threading import Condition, Event, Lock, Thread

import numpy as np
import sounddevice as sd
from pydub import AudioSegment
from pydub.utils import make_chunks


class AudioReceiver:
    AUDIO_UUID = "a1b2c3d4-1122-3344-5566-778899aabbcd"
    
    def __init__(self):
        self.audio_queue = Queue()
        self.mp3_buffer = bytearray()
        self.buffer_lock = Lock()
        self.buffer_cond = Condition(self.buffer_lock)
        self.queue_cond = Condition()
        self.stop_event = Event()

        self.chunk_ms = 140
        self.prebuffer_chunks = 22
        self.playback_batch_chunks = 5
        self.min_decode_bytes = 16384
        self.tail_keep_bytes = 8192

    def handle_notification(self, sender, data: bytearray):
        """BLE notification callback — appends received bytes to the MP3 buffer."""
        if self.stop_event.is_set():
            return

        with self.buffer_cond:
            self.mp3_buffer.extend(data)
            self.buffer_cond.notify_all()

    def start(self):
        Thread(target=self._decoder_loop, daemon=True).start()
        Thread(target=self._audio_player, daemon=True).start()

    def start_player_thread(self):
        self.start()

    def stop(self):
        self.stop_event.set()
        sd.stop()

        with self.buffer_cond:
            self.buffer_cond.notify_all()

        with self.queue_cond:
            self.queue_cond.notify_all()

    def _decoder_loop(self):
        while not self.stop_event.is_set():
            with self.buffer_cond:
                self.buffer_cond.wait_for(
                    lambda: self.stop_event.is_set()
                    or len(self.mp3_buffer) >= self.min_decode_bytes
                )

                if self.stop_event.is_set():
                    return

                data = bytes(self.mp3_buffer)

            try:
                audio = AudioSegment.from_file(BytesIO(data), format="mp3")
                chunks = make_chunks(audio, self.chunk_ms)

                if not chunks:
                    print("decoder produced no chunks yet")
                    continue

                for chunk in chunks:
                    pcm = np.array(chunk.get_array_of_samples(), dtype=np.float32)
                    pcm /= np.iinfo(chunk.array_type).max

                    if chunk.channels > 1:
                        pcm = pcm.reshape((-1, chunk.channels))

                    self.audio_queue.put((pcm, chunk.frame_rate))

                with self.queue_cond:
                    self.queue_cond.notify_all()

                with self.buffer_cond:
                    if len(self.mp3_buffer) > self.tail_keep_bytes:
                        tail = self.mp3_buffer[-self.tail_keep_bytes :]
                        self.mp3_buffer.clear()
                        self.mp3_buffer.extend(tail)
            except Exception as exc:
                print(f"decode waiting for more data: {exc}")
                with self.buffer_cond:
                    previous_size = len(self.mp3_buffer)
                    self.buffer_cond.wait_for(
                        lambda: self.stop_event.is_set()
                        or len(self.mp3_buffer) > previous_size
                    )

                    if self.stop_event.is_set():
                        return

    def _audio_player(self):
        playback_started = False

        while not self.stop_event.is_set():
            with self.queue_cond:
                self.queue_cond.wait_for(
                    lambda: self.stop_event.is_set()
                    or (
                        self.audio_queue.qsize() >= self.prebuffer_chunks
                        if not playback_started
                        else not self.audio_queue.empty()
                    )
                )

                if self.stop_event.is_set():
                    return

            pcm, rate = self.audio_queue.get()
            batch = [pcm]

            while len(batch) < self.playback_batch_chunks:
                with self.audio_queue.mutex:
                    if not self.audio_queue.queue:
                        break

                    next_pcm, next_rate = self.audio_queue.queue[0]
                    if next_rate != rate or next_pcm.ndim != pcm.ndim:
                        break

                batch.append(self.audio_queue.get()[0])

            if len(batch) > 1:
                pcm = np.concatenate(batch, axis=0)

            playback_started = True
            sd.play(pcm, samplerate=rate, blocking=True)
