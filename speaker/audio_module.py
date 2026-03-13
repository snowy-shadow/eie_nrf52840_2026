from io import BytesIO
from collections import deque
import logging
from queue import Queue
from threading import Condition, Event, Lock, Thread
from time import monotonic

import numpy as np
import sounddevice as sd
from pydub import AudioSegment


logger = logging.getLogger(__name__)

class AudioReceiver:
    AUDIO_UUID = "a1b2c3d4-1122-3344-5566-778899aabbcd"

    _SAMPLE_DTYPE = {
        1: np.int8,
        2: np.int16,
        4: np.int32,
    }
    _SAMPLE_SCALE = {
        1: float(np.iinfo(np.int8).max),
        2: float(np.iinfo(np.int16).max),
        4: float(np.iinfo(np.int32).max),
    }
    
    def __init__(self):
        self.audio_queue = Queue()
        self.mp3_buffer = bytearray()
        self.buffer_lock = Lock()
        self.buffer_cond = Condition(self.buffer_lock)
        self.queue_cond = Condition()
        self.stop_event = Event()
        self.decode_event = Event()

        self.chunk_ms = 10
        self.prebuffer_chunks = 1
        self.prebuffer_max_wait_s = 0.1
        self.playback_batch_chunks = 1
        self.min_decode_bytes = 768
        self.decode_trigger_bytes = 128
        self.decode_coalesce_s = 0.01
        self.tail_keep_bytes = 8192
        self.next_decode_skip_ms = 0
        self._decode_last_buffer_size = 0
        self._decode_last_signal_t = 0.0

        self.playback_lock = Lock()
        self.playback_cond = Condition(self.playback_lock)
        self.swapchain_buffers = 3
        self.active_pcm = None
        self.active_pos = 0
        self.ready_pcm = deque()

    def handle_notification(self, sender, data: bytearray):
        """BLE notification callback — appends received bytes to the MP3 buffer."""
        if self.stop_event.is_set():
            return

        signal_decode = False
        with self.buffer_cond:
            self.mp3_buffer.extend(data)
            new_size = len(self.mp3_buffer)
            now = monotonic()
            grew_enough = (new_size - self._decode_last_buffer_size) >= self.decode_trigger_bytes
            if (
                new_size >= self.min_decode_bytes
                and (grew_enough or (now - self._decode_last_signal_t) >= self.decode_coalesce_s)
            ):
                self._decode_last_signal_t = now
                self._decode_last_buffer_size = new_size
                signal_decode = True
            self.buffer_cond.notify_all()

        if signal_decode:
            self.decode_event.set()

    def start(self):
        Thread(target=self._decoder_loop, daemon=True).start()
        Thread(target=self._audio_player, daemon=True).start()

    def start_player_thread(self):
        self.start()

    def stop(self):
        self.stop_event.set()
        sd.stop()
        self.decode_event.set()

        with self.buffer_cond:
            self.buffer_cond.notify_all()

        with self.queue_cond:
            self.queue_cond.notify_all()

        with self.playback_cond:
            self.playback_cond.notify_all()

    def _decoder_loop(self):
        while not self.stop_event.is_set():
            # Event-driven decode wakeup from BLE notifications.
            self.decode_event.wait(timeout=0.5)
            self.decode_event.clear()

            if self.stop_event.is_set():
                return

            with self.buffer_cond:
                if len(self.mp3_buffer) < self.min_decode_bytes:
                    continue
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
                scale = self._SAMPLE_SCALE[decode_audio.sample_width]

                pcm_i = np.frombuffer(decode_audio.raw_data, dtype=dtype)
                if pcm_i.size == 0:
                    continue

                pcm = pcm_i.astype(np.float32)
                pcm /= scale
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
                    # Keep draining available buffered data without waiting for new BLE packets.
                    if len(self.mp3_buffer) >= self.min_decode_bytes:
                        self.decode_event.set()
            except Exception as exc:
                logger.debug("MP3 decode failed; waiting for more data: %s", exc)
                continue

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

                # consume under the same lock to avoid TOCTOU between peek and get
                self.audio_queue.queue.popleft()
                self.audio_queue.not_full.notify()

            batch.append(next_pcm)

        if len(batch) > 1:
            return np.concatenate(batch, axis=0)

        return first_pcm

    def _stream_callback(self, outdata, frames, time_info, status):
        del time_info, status
        outdata.fill(0.0)

        with self.playback_lock:
            written = 0
            while written < frames:
                if self.active_pcm is None or self.active_pos >= len(self.active_pcm):
                    if not self.ready_pcm:
                        break

                    self.active_pcm = self.ready_pcm.popleft()
                    self.active_pos = 0
                    self.playback_cond.notify_all()

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
            dst_idx = np.linspace(0, len(pcm) - 1, dst_len, dtype=np.float32)
            left_idx = np.floor(dst_idx).astype(np.int32)
            right_idx = np.minimum(left_idx + 1, len(pcm) - 1)
            frac = (dst_idx - left_idx).astype(np.float32)
            pcm = (
                pcm[left_idx] * (1.0 - frac)[:, None]
                + pcm[right_idx] * frac[:, None]
            )

        return pcm.astype(np.float32, copy=False)

    def _audio_player(self):
        start_wait = monotonic()
        with self.queue_cond:
            while not self.stop_event.is_set():
                queued = self.audio_queue.qsize()
                waited = monotonic() - start_wait
                if queued >= self.prebuffer_chunks:
                    break
                if queued > 0 and waited >= self.prebuffer_max_wait_s:
                    break
                self.queue_cond.wait(timeout=0.1)

            if self.stop_event.is_set() or self.audio_queue.empty():
                return

        pcm, rate = self.audio_queue.get()
        pcm = self._drain_compatible_batch(pcm, rate)
        channels = pcm.shape[1]

        with self.playback_lock:
            self.active_pcm = pcm
            self.active_pos = 0
            self.ready_pcm.clear()
        try:
            with sd.OutputStream(
                samplerate=rate,
                channels=channels,
                dtype="float32",
                callback=self._stream_callback,
                blocksize=0,
            ):
                while not self.stop_event.is_set():
                    with self.queue_cond:
                        self.queue_cond.wait_for(
                            lambda: self.stop_event.is_set() or not self.audio_queue.empty()
                        )

                        if self.stop_event.is_set():
                            return

                    # get() is outside queue_cond to avoid nesting queue_cond -> audio_queue.mutex
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

                    with self.playback_cond:
                        self.playback_cond.wait_for(
                            lambda: self.stop_event.is_set()
                            or len(self.ready_pcm) < (self.swapchain_buffers - 1)
                        )

                        if self.stop_event.is_set():
                            return

                        self.ready_pcm.append(next_pcm)
        except Exception as exc:
            logger.error("Audio output stream failed: %s", exc)
