"""Robot keywords for the telemetry integration suites (PRD §19.3). Decoding reuses tools/telemetry;
every check raises AssertionError with the numbers that failed. A capture is the raw USART2 byte
stream written by Renode's file backend; it may hold several boots (BOOT frames split it)."""
import json
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import gen_feed  # noqa: E402
from telemetry.accounting import Accounting, in_flight  # noqa: E402
from telemetry.frames import FrameError, decode_frame, encode_command, encode_frame  # noqa: E402
from telemetry.stream import Deframer  # noqa: E402

# Mirror of plaus_cfg in src/app/proc.c: (min, max, roc_max, stuck_n); stuck_n 0 = off.
PLAUS = [(50, 4045, 1000, 50), (50, 4045, 1000, 50), (600, 1400, 50, 0), (1300, 1700, 100, 0)]
RANGE, STUCK, ROC = 1, 2, 4


def expected_status(raws):
    """Independent oracle for the per-sample status word over a contiguous sample sequence."""
    out, last, run = [], [None] * 4, [0] * 4
    for r in raws:
        word = 0
        for c, (lo, hi, roc, n) in enumerate(PLAUS):
            f = RANGE if not lo <= r[c] <= hi else 0
            run[c] = run[c] + 1 if r[c] == last[c] else 1
            if n and run[c] >= n:
                f |= STUCK
            if last[c] is not None and abs(r[c] - last[c]) > roc:
                f |= ROC
            last[c] = r[c]
            word |= f << (4 * c)
        out.append(word)
    return out


def _check(cond, msg):
    if not cond:
        raise AssertionError(msg)


class TelemetryLibrary:
    ROBOT_LIBRARY_SCOPE = "TEST"

    def __init__(self):
        self.frames, self.rejects, self._seq = [], {}, 0

    # ---- capture and commands ----
    def load_capture(self, path):
        d = Deframer()
        self.frames = d.feed(pathlib.Path(path).read_bytes())
        self.rejects = dict(d.rejects)
        return len(self.frames)

    @staticmethod
    def uart_inject(data):
        """Monitor command that queues `data` on USART2 RX in one call (delivered as emulation runs)."""
        return f"python \"u = monitor.Machine['sysbus.usart2']; [u.WriteChar(b) for b in {list(data)}]\""

    def command_inject(self, name, *args):
        self._seq += 1
        return self.uart_inject(encode_command(name, self._seq, *(int(a) for a in args)))

    def malformed_uplink_chunks(self, seed=1):
        """FI-05: 1000 malformed frames as injection commands, 5 per chunk (oversize ones alone).
        250 corrupted, 250 truncated, 200 random, 200 valid-CRC with invalid arguments, 100
        oversize. Each is classified by the Python decoder (independent of the C one); the
        expected counters are kept for `malformed_counts_should_match`."""
        rng = random.Random(int(seed))
        valid = lambda: encode_command("set-rate", rng.randrange(1, 60000), 1000)[:-1]  # noqa: E731
        def corrupt():
            w = bytearray(valid())
            i = rng.randrange(1, len(w))
            x = w[i] ^ (1 << rng.randrange(8))
            w[i] = x or w[i] ^ 0x80  # stay non-zero (0x00 is the delimiter) and always differ
            return bytes(w)
        def nz(n):
            return bytes(rng.randrange(1, 256) for _ in range(n))
        invalid = [
            lambda: encode_command("set-rate", 1, 333),                           # ACK_ERR_RANGE
            lambda: encode_frame(0x80, 1, 0, b"x"),                               # ACK_ERR_LEN
            lambda: encode_frame(0x85, 1, 0, b""),                                # ACK_ERR_UNKNOWN
            lambda: encode_command("fi-arm", 1, 99, 0),                           # unknown fault id
        ]
        kinds = {
            "corrupt": (250, corrupt),
            "truncated": (250, lambda: valid()[: rng.randrange(1, 10)]),
            "random": (200, lambda: nz(rng.randrange(1, 40))),
            "invalid": (200, lambda: invalid[rng.randrange(len(invalid))]()[:-1]),
        }
        small = []
        for i in range(250):
            for k, (n, make) in kinds.items():
                if i < n:
                    small.append(make())
        oversize = [nz(rng.randrange(260, 400)) for _ in range(100)]
        ok = bad = 0
        for w in small + oversize:
            try:
                decode_frame(w)
                ok += 1
            except FrameError:
                bad += 1
        self.expect = {"rx_frames_ok": ok, "rx_frames_bad": bad, "nacks": ok}
        chunks = [b"".join(w + b"\0" for w in small[i:i + 5]) for i in range(0, len(small), 5)]
        chunks += [w + b"\0" for w in oversize]
        return [self.uart_inject(c) for c in chunks]

    def malformed_counts_should_match(self, base_ok=0, base_bad=0):
        p = self.last_health()
        e = self.expect
        _check(p["rx_frames_bad"] - int(base_bad) == e["rx_frames_bad"],
               f"rx_frames_bad {p['rx_frames_bad']} - {base_bad} != expected {e['rx_frames_bad']}")
        _check(p["rx_frames_ok"] - int(base_ok) >= e["rx_frames_ok"],
               f"rx_frames_ok {p['rx_frames_ok']} - {base_ok} < expected {e['rx_frames_ok']}")
        _check(p["rx_bytes_dropped"] == 0, f"rx_bytes_dropped {p['rx_bytes_dropped']}")
        nacks = [a for a in self.acks() if a["status"] != 0]
        _check(len(nacks) == e["nacks"], f"{len(nacks)} error ACKs, expected {e['nacks']}")
        return e

    @staticmethod
    def symbol_address(elf, name):
        out = subprocess.run(["arm-none-eabi-nm", str(elf)], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[2] == name:
                return int(parts[0], 16)
        raise AssertionError(f"symbol {name} not in {elf}")

    @staticmethod
    def source_line(path, pattern):
        for i, line in enumerate(pathlib.Path(path).read_text().splitlines(), 1):
            if re.search(pattern, line):
                return i
        raise AssertionError(f"{pattern!r} not found in {path}")

    def save_jsonl(self, path, drop=("git_sha",)):
        with open(path, "w") as f:
            for fr in self.frames:
                if fr["name"] == "BOOT":
                    fr = {**fr, "payload": {k: v for k, v in fr["payload"].items() if k not in drop}}
                f.write(json.dumps(fr, sort_keys=True) + "\n")

    # ---- selectors ----
    def frames_of(self, name):
        return [f for f in self.frames if f["name"] == name]

    def boots(self):
        return [f["payload"] for f in self.frames_of("BOOT")]

    def samples(self, boot=-1):
        """Samples of one boot (default: the last)."""
        segs, cur = [], None
        for f in self.frames:
            if f["name"] == "BOOT":
                cur = []
                segs.append(cur)
            elif f["name"] == "SAMPLE_BATCH" and cur is not None:
                cur.extend(f["payload"]["samples"])
        return segs[int(boot)] if segs else []

    def events(self):
        return [e for f in self.frames_of("EVENT") for e in f["payload"]["events"]]

    def last_health(self):
        h = self.frames_of("HEALTH")
        _check(h, "no HEALTH frame")
        return h[-1]["payload"]

    def acks(self):
        return [f["payload"] for f in self.frames_of("ACK")]

    # ---- checks ----
    def no_rejects(self):
        _check(not self.rejects, f"decoder rejected frames: {self.rejects}")

    def boot_should_be(self, index=0, reason=0, max_t_us=100000):
        frames = self.frames_of("BOOT")
        _check(len(frames) > int(index), f"only {len(frames)} BOOT frame(s)")
        f = frames[int(index)]
        b = f["payload"]
        _check(b["reset_reason"] == int(reason), f"reset_reason {b['reset_reason']} != {reason}: {b}")
        _check(f["t_us"] <= int(max_t_us), f"BOOT at {f['t_us']} us > {max_t_us}")
        _check((b["ver_major"], b["ver_minor"]) == (1, 0), f"version {b}")
        _check(len(b["git_sha"]) == 8 and int(b["git_sha"], 16) >= 0, f"git_sha {b['git_sha']!r}")
        return b

    def health_cadence_should_be(self, count, period_us=1000000, tol_us=1000):
        h = self.frames_of("HEALTH")
        _check(abs(len(h) - int(count)) <= 1, f"{len(h)} HEALTH frames, expected {count} +- 1")
        gaps = [b["t_us"] - a["t_us"] for a, b in zip(h, h[1:])]
        bad = [g for g in gaps if abs(g - int(period_us)) > int(tol_us)]
        _check(not bad, f"HEALTH spacing outside {period_us} +- {tol_us} us: {bad}")
        keys = ("triggered", "delivered", "frames_sent", "uptime_ms")
        for a, b in zip(h, h[1:]):
            for k in keys:
                _check(b["payload"][k] >= a["payload"][k], f"{k} not monotonic: {a['payload'][k]} -> {b['payload'][k]}")
        p = h[-1]["payload"]
        _check(all(w > 0 for w in p["stack_free_min_words"]), f"stack minimum 0: {p['stack_free_min_words']}")
        _check(p["msp_free_bytes"] > 0, "MSP exhausted")
        return gaps

    def stack_margins_should_be_at_least(self, pct, sizes):
        """sizes: task stack sizes in words, in HEALTH order (comma-separated)."""
        sizes = [int(s) for s in str(sizes).split(",")]
        p = self.last_health()
        free = p["stack_free_min_words"]
        low = [(i, f, s) for i, (f, s) in enumerate(zip(free, sizes)) if f * 100 < int(pct) * s]
        _check(not low, f"stack margin < {pct}% (task, free, size): {low}")
        _check(p["msp_free_bytes"] * 100 >= int(pct) * 2048, f"MSP free {p['msp_free_bytes']} < {pct}%")

    def samples_should_be_contiguous(self, period_us, after_t_us=0, boot=-1):
        """Samples triggered at or after after_t_us: seq +1 and t_us +period_us each step."""
        s = [x for x in self.samples(boot) if x["t_us"] >= int(after_t_us)]
        _check(len(s) > 1, f"no samples after {after_t_us} us")
        for a, b in zip(s, s[1:]):
            _check(b["seq"] == a["seq"] + 1, f"seq gap {a['seq']} -> {b['seq']}")
            _check(b["t_us"] - a["t_us"] == int(period_us), f"dt {b['t_us'] - a['t_us']} != {period_us} at seq {b['seq']}")
        return len(s)

    def samples_should_match_feed(self, scenario, boot=-1):
        """raw values equal the feed files and status equals the oracle, sample by sample."""
        feed = gen_feed.SCENARIOS[scenario]
        s = self.samples(boot)
        _check(s and s[0]["seq"] == 0, "samples must start at seq 0")
        want_raw = [[feed[c][x["seq"] % gen_feed.PERIOD] for c in range(4)] for x in s]
        want_st = expected_status(want_raw)
        for x, r, st in zip(s, want_raw, want_st):
            _check(x["raw"] == r, f"seq {x['seq']}: raw {x['raw']} != feed {r}")
            _check(x["status"] == st, f"seq {x['seq']}: status {x['status']:#06x} != expected {st:#06x}")
        return len(s)

    def flagged_seqs(self, channel, flag):
        sh = 4 * int(channel)
        return [x["seq"] for x in self.samples() if (x["status"] >> sh) & int(flag)]

    def conservation_should_be_exact(self):
        """After SET_RATE 0 + GET_STATUS: in_flight 0 and the host received exactly `delivered`."""
        a = Accounting()
        for f in self.frames:
            a.on_frame(f)
        r = a.check_exact()
        _check(r["ok"], f"conservation failed: {r}")
        _check(not a.violations, f"in-flight bound violated: {a.violations}")
        return r

    def conservation_bound_should_hold(self):
        a = Accounting()
        for f in self.frames:
            a.on_frame(f)
        _check(not a.violations, f"in-flight bound violated: {a.violations}")
        _check(a.order_errors == 0, f"{a.order_errors} order errors")

    def ack_should_be(self, cmd, status=0):
        acks = [a for a in self.acks() if a["cmd"] == int(cmd)]
        _check(acks, f"no ACK for cmd {int(cmd):#x}; acks: {self.acks()}")
        _check(acks[-1]["status"] == int(status), f"ACK {acks[-1]} status != {status}")

    def last_ack_t_us(self, cmd):
        f = [f for f in self.frames_of("ACK") if f["payload"]["cmd"] == int(cmd)]
        _check(f, f"no ACK for cmd {int(cmd):#x}")
        return f[-1]["t_us"]

    def batch_sizes(self, last=20):
        return [f["payload"]["count"] for f in self.frames_of("SAMPLE_BATCH")][-int(last):]

    def health_count(self):
        return len(self.frames_of("HEALTH"))

    def samples_after_reboot(self):
        return len(self.samples(1)) if len(self.boots()) > 1 else 0

    def seq_gaps(self):
        """Sequence numbers missing from the received samples (last boot)."""
        s = self.samples()
        return sum(b["seq"] - a["seq"] - 1 for a, b in zip(s, s[1:]))

    def imu_samples_should_be(self, acc, gyr, temp, min_count):
        """Every IMU record equals the fed values (LSB); seq contiguous within each boot."""
        acc, gyr = [int(v) for v in str(acc).split(",")], [int(v) for v in str(gyr).split(",")]
        s = [x for f in self.frames_of("IMU_BATCH") for x in f["payload"]["samples"]]
        _check(len(s) >= int(min_count), f"{len(s)} IMU samples < {min_count}")
        for x in s:
            _check(x["acc"] == acc and x["gyr"] == gyr and x["temp"] == int(temp), f"IMU record {x}")
        return len(s)

    def event_ids(self):
        return [e["id"] for e in self.events()]

    def health_value(self, key):
        return self.last_health()[key]
