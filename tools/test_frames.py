import binascii
import struct
import unittest

from telemetry import cobs, frames
from telemetry.frames import FrameError, decode_frame, encode_command, encode_frame


def craft(version=1, type_=0x20, seq=0, t_us=0, payload=b"", plen=None, crc_delta=0):
    raw = struct.pack("<BBHIH", version, type_, seq, t_us, len(payload) if plen is None else plen) + payload
    return cobs.encode(raw + struct.pack("<H", (frames.crc16(raw) + crc_delta) & 0xFFFF))


class CrcTest(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(frames.crc16(b"123456789"), 0x29B1)
        self.assertEqual(binascii.crc_hqx(b"123456789", 0xFFFF), 0x29B1)

    def test_empty_and_incremental(self):
        self.assertEqual(frames.crc16(b""), 0xFFFF)
        data = bytes(range(200))
        self.assertEqual(binascii.crc_hqx(data[77:], frames.crc16(data[:77])), frames.crc16(data))


class FrameTest(unittest.TestCase):
    def decode(self, wire):
        return decode_frame(wire[:-1] if wire.endswith(b"\0") else wire)

    def reason(self, wire):
        with self.assertRaises(FrameError) as cm:
            self.decode(wire)
        return cm.exception.reason

    def test_roundtrip_and_seq_wrap(self):
        for seq in (0xFFFE, 0xFFFF, 0x10000, 0x10001):
            f = self.decode(encode_frame(0x20, seq, 0xFFFFFFFF, b"\x01\x00\x80\x00"))
            self.assertEqual(f["seq"], seq & 0xFFFF)
            self.assertEqual(f["t_us"], 0xFFFFFFFF)
            self.assertEqual(f["payload"], {"cmd_seq": 1, "cmd": 0x80, "status": 0})

    def test_max_payload(self):
        wire = encode_frame(0x55, 1, 2, b"\x55" * frames.PAYLOAD_MAX)
        self.assertLessEqual(len(wire), frames.WIRE_MAX + 1)
        self.assertEqual(self.decode(wire)["payload"], {"raw": "55" * frames.PAYLOAD_MAX})
        with self.assertRaises(ValueError):
            encode_frame(1, 0, 0, bytes(frames.PAYLOAD_MAX + 1))

    def test_rejections_in_order(self):
        self.assertEqual(self.reason(b"\x05\x01\x02"), "cobs")
        self.assertEqual(self.reason(b"\x01"), "cobs")
        self.assertEqual(self.reason(cobs.encode(bytes(11))), "short")
        self.assertEqual(self.reason(craft(payload=bytes(245))), "oversize")
        self.assertEqual(self.reason(b"\x22" * 600), "oversize")
        self.assertEqual(self.reason(craft(crc_delta=1)), "crc")
        self.assertEqual(self.reason(craft(version=2)), "version")
        self.assertEqual(self.reason(craft(payload=b"1234", plen=3)), "len")
        self.assertEqual(self.reason(craft(type_=0x20, payload=b"123")), "type_len")
        self.assertEqual(self.reason(craft(version=7, plen=99, crc_delta=1)), "crc")
        self.assertEqual(self.reason(craft(version=7, plen=99)), "version")

    def test_longer_payload_accepted(self):
        f = self.decode(encode_frame(0x20, 0, 0, b"\x05\x00\x81\x00" + b"future"))
        self.assertEqual(f["payload"]["cmd_seq"], 5)

    def test_count_out_of_range(self):
        for type_, count in ((0x01, 11), (0x02, 9), (0x12, 9)):
            self.assertEqual(self.reason(encode_frame(type_, 0, 0, bytes([count]) + bytes(240))), "type_len")
        health = bytearray(140)
        health[68] = 9
        self.assertEqual(self.reason(encode_frame(0x11, 0, 0, bytes(health))), "type_len")

    def test_commands(self):
        f = self.decode(encode_command("set-rate", 3, 1000))
        self.assertEqual((f["name"], f["seq"], f["payload"]), ("CMD_SET_RATE", 3, {"rate_hz": 1000}))
        f = self.decode(encode_command("fi-arm", 7, 1, 500))
        self.assertEqual(f["payload"], {"fault_id": 1, "fault_param": 500})
        self.assertEqual(self.decode(encode_command("ping", 1))["payload"], {})
        with self.assertRaises(ValueError):
            encode_command("set-rate", 1)


if __name__ == "__main__":
    unittest.main()
