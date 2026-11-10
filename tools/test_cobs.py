import random
import unittest

from telemetry import cobs


class CobsTest(unittest.TestCase):
    def test_roundtrip_lengths_0_to_1024(self):
        rng = random.Random(1)
        for n in range(1025):
            data = bytes(0 if rng.random() < 0.2 else rng.randrange(1, 256) for _ in range(n))
            enc = cobs.encode(data)
            self.assertNotIn(0, enc)
            self.assertLessEqual(len(enc), n + -(-n // 254) + 1)
            self.assertEqual(cobs.decode(enc), data)

    def test_block_boundaries(self):
        for n in (253, 254, 255, 508, 509):
            for data in (b"\x5a" * n, b"\0" + b"\x5a" * n, b"\x5a" * n + b"\0"):
                self.assertEqual(cobs.decode(cobs.encode(data)), data)
        self.assertEqual(len(cobs.encode(b"\x01" * 254)), 255)
        self.assertEqual(len(cobs.encode(b"\x01" * 255)), 257)

    def test_exact(self):
        self.assertEqual(cobs.encode(b""), b"\x01")
        self.assertEqual(cobs.encode(b"\x11\x22\x00\x33"), b"\x03\x11\x22\x02\x33")
        self.assertEqual(cobs.encode(b"\0\0"), b"\x01\x01\x01")

    def test_malformed(self):
        for bad in (b"\x03\x11\x00\x02\x33", b"\x05\x11\x22", b"\xff\x11", b"\x00"):
            with self.assertRaises(ValueError):
                cobs.decode(bad)


if __name__ == "__main__":
    unittest.main()
