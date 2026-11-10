import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

from telemetry.__main__ import main

TOOLS = pathlib.Path(__file__).resolve().parent
OUT = TOOLS.parent / "tests" / "vectors" / "out"


class CliTest(unittest.TestCase):
    def test_decode_file_jsonl_and_summary(self):
        with tempfile.TemporaryDirectory() as d:
            cap = pathlib.Path(d) / "cap.bin"
            cap.write_bytes(b"".join(p.read_bytes() for p in sorted(OUT.glob("*.bin"))) + b"\x05\x01\0")
            jl = pathlib.Path(d) / "out.jsonl"
            buf = io.StringIO()
            with redirect_stdout(buf):
                self.assertEqual(main(["decode", "--file", str(cap), "--jsonl", str(jl), "--summary"]), 0)
            lines = [json.loads(x) for x in jl.read_text().splitlines()]
            self.assertEqual([f["name"] for f in lines], ["ACK", "BOOT", "EVENT", "HEALTH", "IMU_BATCH", "SAMPLE_BATCH"])
            ev = lines[2]["payload"]["events"]
            self.assertEqual([e["name"] for e in ev], ["BOOT", "RESET_REASON", "RING_OVF_ON"])
            s = json.loads(buf.getvalue())
            self.assertEqual(s["frames_ok"], 6)
            self.assertEqual(s["rejects"], {"cobs": 1})
            self.assertEqual(s["samples_received"], 10)

    def test_encode_cmd_as_module(self):
        with tempfile.TemporaryDirectory() as d:
            out = pathlib.Path(d) / "c.bin"
            subprocess.run([sys.executable, "-m", "telemetry", "encode-cmd", "set-rate", "1000", "--seq", "3",
                            "--out", str(out)], cwd=TOOLS, check=True)
            self.assertEqual(out.read_bytes(), (TOOLS.parent / "tests" / "vectors" / "cmd_set_rate_1000.bin").read_bytes())
            r = subprocess.run([sys.executable, "-m", "telemetry", "encode-cmd", "fi-arm", "1", "--out", str(out)],
                               cwd=TOOLS, capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("fi-arm takes 2", r.stderr)


if __name__ == "__main__":
    unittest.main()
