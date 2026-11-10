"""XV: Python decodes the C-generated vectors bit-exactly; the committed command vectors (decoded
by tests/unit/test_cmd_vectors.c) equal what this tool encodes. Regenerate the command vectors
with `python test_vectors.py` from tools/."""
import json
import pathlib
import unittest

from telemetry.frames import decode_frame, encode_command, encode_frame

VEC = pathlib.Path(__file__).resolve().parent.parent / "tests" / "vectors"

# file -> (command, seq, args); keep in sync with tests/unit/test_cmd_vectors.c
CMD_VECTORS = {
    "cmd_ping.bin": ("ping", 1, ()),
    "cmd_get_status.bin": ("get-status", 2, ()),
    "cmd_set_rate_1000.bin": ("set-rate", 3, (1000,)),
    "cmd_set_rate_0.bin": ("set-rate", 4, (0,)),
    "cmd_reset_counters.bin": ("reset-counters", 5, ()),
    "cmd_reboot.bin": ("reboot", 6, ()),
    "cmd_fi_arm.bin": ("fi-arm", 7, (1, 500)),
    "cmd_fi_clear.bin": ("fi-clear", 8, ()),
    "cmd_set_rate_bad.bin": ("set-rate", 0xFFFF, (123,)),
}


class VectorTest(unittest.TestCase):
    def test_c_vectors_decode_exactly(self):
        bins = sorted((VEC / "out").glob("*.bin"))
        self.assertEqual(len(bins), 6)
        for b in bins:
            with self.subTest(b.name):
                wire = b.read_bytes()
                self.assertEqual(wire[-1], 0)
                self.assertNotIn(0, wire[:-1])
                f = decode_frame(wire[:-1])
                self.assertEqual(f, json.loads(b.with_suffix(".json").read_text()))
                # framing is bit-exact too: re-encoding the payload reproduces the C bytes
                raw_payload = _payload_bytes(wire)
                self.assertEqual(encode_frame(f["type"], f["seq"], f["t_us"], raw_payload), wire)

    def test_command_vectors_match_encoder(self):
        for name, (cmd, seq, args) in CMD_VECTORS.items():
            with self.subTest(name):
                self.assertEqual((VEC / name).read_bytes(), encode_command(cmd, seq, *args))


def _payload_bytes(wire: bytes) -> bytes:
    from telemetry import cobs

    raw = cobs.decode(wire[:-1])
    return raw[10:-2]


if __name__ == "__main__":
    for name, (cmd, seq, args) in CMD_VECTORS.items():
        (VEC / name).write_bytes(encode_command(cmd, seq, *args))
    print(f"wrote {len(CMD_VECTORS)} command vectors to {VEC}")
