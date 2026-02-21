from io import BytesIO
from queue import Queue
from threading import Condition, Lock, Thread

import numpy as np
import sounddevice as sd
from pydub import AudioSegment
from pydub.utils import make_chunks


class AudioReceiver:
    def __init__(self):
        self.audio_queue = Queue()
        self.mp3_buffer = bytearray()
        self.buffer_lock = Lock()
        self.buffer_cond = Condition(self.buffer_lock)
        self.queue_cond = Condition()

        self.chunk_ms = 50
        self.prebuffer_chunks = 4
        self.min_decode_bytes = 4096
        self.tail_keep_bytes = 1024

    def start(self):
        Thread(target=self._decoder_loop, daemon=True).start()
        Thread(target=self._audio_player, daemon=True).start()

    def start_player_thread(self):
        self.start()

    def _decoder_loop(self):
        while True:
            with self.buffer_cond:
                self.buffer_cond.wait_for(lambda: len(self.mp3_buffer) >= self.min_decode_bytes)
                data = bytes(self.mp3_buffer)

            try:
                audio = AudioSegment.from_file(BytesIO(data), format="mp3")
                chunks = make_chunks(audio, self.chunk_ms)

                if not chunks:
                    continue

                for chunk in chunks:
                    pcm = np.array(chunk.get_array_of_samples())
                    self.audio_queue.put((pcm, chunk.frame_rate))

                with self.queue_cond:
                    self.queue_cond.notify_all()

                with self.buffer_cond:
                    if len(self.mp3_buffer) > self.tail_keep_bytes:
                        tail = self.mp3_buffer[-self.tail_keep_bytes :]
                        self.mp3_buffer.clear()
                        self.mp3_buffer.extend(tail)
            except Exception:
                with self.buffer_cond:
                    previous_size = len(self.mp3_buffer)
                    self.buffer_cond.wait_for(lambda: len(self.mp3_buffer) > previous_size)

    def _audio_player(self):
        playback_started = False

        while True:
            with self.queue_cond:
                self.queue_cond.wait_for(
                    lambda: (
                        self.audio_queue.qsize() >= self.prebuffer_chunks
                        if not playback_started
                        else not self.audio_queue.empty()
                    )
                )

            pcm, rate = self.audio_queue.get()
            playback_started = True
            sd.play(pcm, samplerate=rate, blocking=True)

    def handle_notification(self, _sender, data):
        with self.buffer_cond:
            self.mp3_buffer.extend(data)
            self.buffer_cond.notify_all()
