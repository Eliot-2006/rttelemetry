"""Extend wrapping counters (u32 µs timestamps, u16 frame seq) to unbounded ints.

Each step is interpreted as the signed difference closest to zero, so forward wraps are
followed and small backward steps (reordering) do not look like a full wrap."""


class Unwrapper:
    def __init__(self, bits: int = 32):
        self._mod = 1 << bits
        self._last = None
        self.value = None

    def __call__(self, raw: int) -> int:
        if self._last is None:
            self.value = raw
        else:
            d = (raw - self._last) % self._mod
            if d >= self._mod // 2:
                d -= self._mod
            self.value += d
        self._last = raw
        return self.value
