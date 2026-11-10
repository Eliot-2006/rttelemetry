"""Byte-stream deframer: splits on 0x00, decodes each frame, counts rejects per reason and
resynchronises at the next delimiter (corruption costs only the frames it touches)."""
from collections import Counter

from .frames import WIRE_MAX, FrameError, decode_frame


class Deframer:
    def __init__(self):
        self._buf = bytearray()
        self._overflow = False  # current frame exceeded WIRE_MAX; discard until delimiter
        self.frames_ok = 0
        self.rejects = Counter()  # reason -> count (see frames.REASONS)

    def feed(self, data: bytes) -> list:
        """Returns the frames completed by `data`, as decode_frame() dicts."""
        out = []
        parts = bytes(data).split(b"\0")
        for i, part in enumerate(parts):
            if not self._overflow:
                self._buf += part
                if len(self._buf) > WIRE_MAX:
                    self._overflow = True
                    self._buf.clear()
            if i == len(parts) - 1:
                break  # no delimiter after the last part (yet)
            if self._overflow:
                self.rejects["oversize"] += 1
            elif self._buf:  # empty frames (back-to-back delimiters) are ignored, as in C
                try:
                    out.append(decode_frame(bytes(self._buf)))
                    self.frames_ok += 1
                except FrameError as e:
                    self.rejects[e.reason] += 1
            self._buf.clear()
            self._overflow = False
        return out
