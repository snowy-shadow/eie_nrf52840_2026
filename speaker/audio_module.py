from io import BytesIO
from queue import Queue
from threading import Condition, Event, Lock, Thread

import numpy as np
import sounddevice as sd
from pydub import AudioSegment


class AudioReceiver:
    AUDIO_UUID = "a1b2c3d4-1122-3344-5566-778899aabbcd"

    _SAMPLE_DTYPE = {
        1: np.int8,
        2: np.int16,
        4: np.int32,
    }
    
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
        self.next_decode_skip_ms = 0

        self.playback_lock = Lock()
        self.active_pcm = None
        self.active_pos = 0
        self.staging_pcm = None

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
                if self.next_decode_skip_ms > 0:
                    decode_audio = audio[self.next_decode_skip_ms :]
                else:
                    decode_audio = audio

                dtype = self._SAMPLE_DTYPE.get(decode_audio.sample_width)
                if dtype is None:
                    raise ValueError(f"unsupported sample width: {decode_audio.sample_width}")

                pcm_i = np.frombuffer(decode_audio.raw_data, dtype=dtype)
                if pcm_i.size == 0:
                    print("decoder produced no chunks yet")
                    continue

                pcm = pcm_i.astype(np.float32)
                pcm /= float(np.iinfo(dtype).max)
                pcm = pcm.reshape((-1, decode_audio.channels))

                chunk_samples = max(1, int((decode_audio.frame_rate * self.chunk_ms) / 1000))
                for start in range(0, len(pcm), chunk_samples):
                    self.audio_queue.put(
                        (pcm[start : start + chunk_samples], decode_audio.frame_rate)
                    )

                with self.queue_cond:
                    self.queue_cond.notify_all()

                with self.buffer_cond:
                    buffer_size = len(self.mp3_buffer)
                    if buffer_size > self.tail_keep_bytes:
                        overlap_ratio = self.tail_keep_bytes / buffer_size
                        self.next_decode_skip_ms = int(len(audio) * overlap_ratio)
                        tail = self.mp3_buffer[-self.tail_keep_bytes :]
                        self.mp3_buffer.clear()
                        self.mp3_buffer.extend(tail)
                    else:
                        self.next_decode_skip_ms = len(audio)
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

    def _drain_compatible_batch(self, first_pcm, rate):
        batch = [first_pcm]
        channels = first_pcm.shape[1]

        while len(batch) < self.playback_batch_chunks:
            with self.audio_queue.mutex:
                if not self.audio_queue.queue:
                    break

                next_pcm, next_rate = self.audio_queue.queue[0]
                if next_rate != rate or next_pcm.shape[1] != channels:
                    break

            batch.append(self.audio_queue.get()[0])

        if len(batch) > 1:
            return np.concatenate(batch, axis=0)

        return first_pcm

    def _stream_callback(self, outdata, frames, time_info, status):
        del time_info
        if status:
            print(f"stream status: {status}")

        outdata.fill(0.0)

        with self.playback_lock:
            written = 0
            while written < frames:
                if self.active_pcm is None or self.active_pos >= len(self.active_pcm):
                    if self.staging_pcm is None:
                        break

                    self.active_pcm = self.staging_pcm
                    self.staging_pcm = None
                    self.active_pos = 0
                    with self.queue_cond:
                        self.queue_cond.notify_all()

                available = len(self.active_pcm) - self.active_pos
                take = min(available, frames - written)
                outdata[written : written + take, :] = self.active_pcm[
                    self.active_pos : self.active_pos + take, :
                ]
                self.active_pos += take
                written += take

    def _convert_pcm_to_stream_format(self, pcm, src_rate, dst_rate, dst_channels):
        src_channels = pcm.shape[1]

        if src_channels != dst_channels:
            if dst_channels == 1:
                pcm = pcm.mean(axis=1, keepdims=True, dtype=np.float32)
            elif src_channels == 1:
                pcm = np.repeat(pcm, dst_channels, axis=1)
            elif src_channels > dst_channels:
                pcm = pcm[:, :dst_channels]
            else:
                pad = np.repeat(pcm[:, -1:], dst_channels - src_channels, axis=1)
                pcm = np.concatenate((pcm, pad), axis=1)

        if src_rate != dst_rate and len(pcm) > 1:
            dst_len = max(1, int(round((len(pcm) * dst_rate) / src_rate)))
            src_idx = np.arange(len(pcm), dtype=np.float32)
            dst_idx = np.linspace(0, len(pcm) - 1, dst_len, dtype=np.float32)
            converted = np.empty((dst_len, dst_channels), dtype=np.float32)
            for channel in range(dst_channels):
                converted[:, channel] = np.interp(dst_idx, src_idx, pcm[:, channel])
            pcm = converted

        return pcm.astype(np.float32, copy=False)

    def _audio_player(self):
        with self.queue_cond:
            self.queue_cond.wait_for(
                lambda: self.stop_event.is_set()
                or self.audio_queue.qsize() >= self.prebuffer_chunks
            )

            if self.stop_event.is_set():
                return

        pcm, rate = self.audio_queue.get()
        pcm = self._drain_compatible_batch(pcm, rate)
        channels = pcm.shape[1]

        with self.playback_lock:
            self.active_pcm = pcm
            self.active_pos = 0
            self.staging_pcm = None

        with sd.OutputStream(
            samplerate=rate,
            channels=channels,
            dtype="float32",
            callback=self._stream_callback,
            blocksize=0,
        ):
            while not self.stop_event.is_set():
                with self.playback_lock:
                    staging_full = self.staging_pcm is not None

                if staging_full:
                    with self.queue_cond:
                        self.queue_cond.wait(timeout=0.01)
                    continue

                with self.queue_cond:
                    self.queue_cond.wait_for(
                        lambda: self.stop_event.is_set() or not self.audio_queue.empty(),
                        timeout=0.1,
                    )

                    if self.stop_event.is_set():
                        return

                if self.audio_queue.empty():
                    continue

                next_pcm, next_rate = self.audio_queue.get()
                next_pcm = self._drain_compatible_batch(next_pcm, next_rate)
                if next_rate != rate or next_pcm.shape[1] != channels:
                    next_pcm = self._convert_pcm_to_stream_format(
                        next_pcm,
                        src_rate=next_rate,
                        dst_rate=rate,
                        dst_channels=channels,
                    )

                if len(next_pcm) == 0:
                    continue

                while not self.stop_event.is_set():
                    with self.playback_lock:
                        if self.staging_pcm is None:
                            self.staging_pcm = next_pcm
                            break

                    with self.queue_cond:
                        self.queue_cond.wait(timeout=0.01)
