"""Consistent Overhead Byte Stuffing; byte-compatible with src/core/cobs.c (no delimiter)."""


def max_encoded(n: int) -> int:
    return n + n // 254 + 1


def encode(data: bytes) -> bytes:
    out = bytearray(b"\x01")  # out[code_at] is the current block's code
    code_at = 0
    for i, b in enumerate(data):
        if b:
            out.append(b)
            out[code_at] += 1
            if out[code_at] != 0xFF:
                continue
            if i + 1 == len(data):
                break  # full block at the very end: no trailing code byte
        code_at = len(out)
        out.append(1)
    return bytes(out)


def decode(enc: bytes) -> bytes:
    """Raises ValueError on a 0x00 inside the input or a code that runs past the end."""
    out = bytearray()
    i, n = 0, len(enc)
    while i < n:
        code = enc[i]
        i += 1
        if code == 0 or code - 1 > n - i:
            raise ValueError("malformed COBS")
        block = enc[i : i + code - 1]
        if 0 in block:
            raise ValueError("malformed COBS")
        out += block
        i += code - 1
        if code != 0xFF and i < n:
            out.append(0)
    return bytes(out)
