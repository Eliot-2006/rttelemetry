import random
import unittest

from telemetry.frames import encode_frame
from telemetry.stream import Deframer


def frames_stream(n, rng):
    """n frames of mixed types with realistic payloads; returns (list of wire frames)."""
    out = []
    for i in range(n):
        kind = i % 3
        if kind == 0:  # SAMPLE_BATCH, 10 samples
            p = bytes([10, 0x0F]) + bytes(rng.randrange(256) for _ in range(180))
            out.append(encode_frame(0x01, i, i * 10000, p))
        elif kind == 1:  # ACK
            out.append(encode_frame(0x20, i, i * 10000, bytes([i & 0xFF, 0, 0x80, 0])))
        else:  # EVENT with 2 records
            p = bytes([2]) + bytes(rng.randrange(256) for _ in range(24))
            out.append(encode_frame(0x12, i, i * 10000, p))
    return out


class DeframerTest(unittest.TestCase):
    def test_split_across_chunks(self):
        wires = frames_stream(30, random.Random(2))
        data = b"".join(wires)
        d = Deframer()
        got = []
        for i in range(0, len(data), 7):
            got += d.feed(data[i : i + 7])
        self.assertEqual([f["seq"] for f in got], list(range(30)))
        self.assertEqual(d.frames_ok, 30)
        self.assertEqual(sum(d.rejects.values()), 0)

    def test_resync_after_junk_and_reason_counts(self):
        good = encode_frame(0x20, 9, 0, b"\x01\x00\x80\x00")
        junk = b"\x05\x01\x02\0" + b"\x22" * 1000 + b"\0" + b"\0\0" + good[:-1].replace(b"\x80", b"\x81") + b"\0"
        d = Deframer()
        got = d.feed(b"garbage-mid-frame" + b"\0" + junk + good)
        self.assertEqual([f["seq"] for f in got], [9])
        self.assertEqual(d.rejects["oversize"], 1)
        self.assertEqual(d.rejects["crc"], 1)
        self.assertGreaterEqual(d.rejects["cobs"], 1)

    def test_bit_flips_detected_and_only_affected_frames_lost(self):
        """FI-11 style: flip 1-3 random bits in a captured stream, 10^4 trials. Every frame
        whose bytes (or bounding delimiters) were hit must be rejected, never mis-decoded;
        every other frame must decode unchanged."""
        rng = random.Random(11)
        wires = frames_stream(6, rng)
        stream = b"\0" + b"".join(wires)  # leading delimiter: the first frame also has one before it
        bounds, pos = [], 1
        for w in wires:  # frame i occupies [pos, pos+len-1), delimiter at pos+len-1
            bounds.append((pos - 1, pos + len(w) - 1))  # include both bounding delimiters
            pos += len(w)
        expected = [Deframer().feed(w)[0] for w in wires]
        for _ in range(10_000):
            data = bytearray(stream)
            hit = set()
            for bit in rng.sample(range(len(data) * 8), rng.randint(1, 3)):  # distinct bits
                data[bit // 8] ^= 1 << (bit % 8)
                hit.add(bit // 8)
            unaffected = [i for i, (a, b) in enumerate(bounds) if not any(a <= h <= b for h in hit)]
            got = Deframer().feed(bytes(data))
            self.assertEqual(got, [expected[i] for i in unaffected], f"hit bytes {sorted(hit)}")


if __name__ == "__main__":
    unittest.main()
