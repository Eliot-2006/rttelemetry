"""`make demo` (PRD §27): emulated end-to-end demo with a live dashboard. No hardware.

Renode runs headless with its monitor on a TCP port; USART2 is streamed over a TCP socket
terminal and decoded live. A scripted sequence steps virtual time one second at a time:
nominal 1 kHz -> SET_RATE 2000 -> FI-01 stall -> FI-02 slow link -> FI-06 task hang -> watchdog
reset -> recovery, then stop + flush and a conservation check. All numbers are [EMU].

The PRD's sequence uses 4 kHz; on Renode 1.17.0 the ADC model needs 400 us per scan (D13), so
4 kHz overruns by construction. The demo uses 2 kHz so the drops it shows come from the faults.

    python -m telemetry.demo [--transcript FILE.json]

--transcript also saves every printed line with its kind; bench/figures.py renders it to
docs/img/demo.svg. Colour is used only on a terminal, and NO_COLOR turns it off.
"""
import argparse
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import threading
import time

from .accounting import Accounting
from .frames import RESET_REASONS, encode_command
from .stream import Deframer

ROOT = pathlib.Path(__file__).resolve().parents[2]
ELF = ROOT / "build/fw-debug/src/telemetry.elf"
MON_PORT, UART_PORT = 34560, 34561
EVT = {3: "RING_OVF_ON", 4: "RING_OVF_OFF", 5: "TXPOOL_EMPTY_ON", 6: "TXPOOL_EMPTY_OFF", 13: "WDT_STALL"}
STACKS = [384, 384, 384, 384, 384, 128]
# The scripted sequence, shared with bench B-12: (virtual seconds to run first, command, args, note).
SCRIPT = [
    (10, "set-rate", (2000,), "SET_RATE 2000 Hz"),
    (4, "fi-arm", (1, 300), "FI-01: proc_task stalls 300 ms -> ring overflows, then drains"),
    (3, "fi-arm", (2, 200), "FI-02: slow link (200 us/byte) -> drops move to the frame pool"),
    (4, "fi-clear", (), "FI_CLEAR: link back to normal"),
    (3, "fi-arm", (3, 2), "FI-06: cmd_task hangs -> supervisor stops kicking -> IWDG reset"),
    (4, "set-rate", (0,), "SET_RATE 0: stop + flush, then GET_STATUS for the final counters"),
    (1, "get-status", (), "GET_STATUS"),
]
TAIL_S = 1
HIST_EDGES_MS = [0.5, 1, 2, 5, 10, 20, 50, float("inf")]
DROPS = ("dropped_overrun", "dropped_ring", "dropped_txpool")
COLS = (f"{'boot':>4} {'up':>6}  {'rate':>5}  {'samples/s':>9}  {'frames/s':>8}  {'ring/hwm':>8}"
        f"  {'overrun':>7} {'ring':>5} {'pool':>5}  {'L1 p99':>7}  {'CPU':>6}  {'stack':>5}")
ANSI = {"phase": "1;36", "event": "33", "boot": "1;35", "head": "2", "quiet": "2", "pass": "1;32",
        "fail": "1;31", "drop": "1;31", "title": "1"}


class Monitor:
    def __init__(self, port):
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                time.sleep(0.1)
        else:
            sys.exit("Renode monitor did not open its port")
        self.buf = b""
        self.wait_prompt()

    def wait_prompt(self):
        while not re.search(rb"\((monitor|telem)\) $", self.buf.replace(b"\x1b[0m", b"")):
            chunk = self.s.recv(65536)
            if not chunk:
                sys.exit("Renode monitor closed")
            self.buf = (self.buf + chunk)[-4096:]
        self.buf = b""

    def __call__(self, line):
        self.s.sendall(line.encode() + b"\n")
        self.wait_prompt()


class Dash:
    """Decodes the live stream and prints one row per HEALTH frame. Thread-safe: the UART reader
    thread and the main (script) thread both print through out()."""

    def __init__(self, transcript=None):
        self.d, self.acct, self.lock = Deframer(), Accounting(), threading.Lock()
        self.prev, self.last, self.boots = None, None, []
        self.color = sys.stdout.isatty() and not os.environ.get("NO_COLOR")
        self.transcript = transcript

    def out(self, kind, text, spans=()):
        """spans: (start, end) character ranges to highlight as drops in this line."""
        if self.transcript is not None:
            self.transcript.append({"kind": kind, "text": text, "spans": list(spans)})
        if self.color:
            for a, b in sorted(spans, reverse=True):
                text = f"{text[:a]}\x1b[{ANSI['drop']}m{text[a:b]}\x1b[0m{text[b:]}"
            if kind in ANSI and not spans:
                text = f"\x1b[{ANSI[kind]}m{text}\x1b[0m"
        print(text, flush=True)

    def feed(self, data):
        for f in self.d.feed(data):
            with self.lock:
                self.acct.on_frame(f)
                if f["name"] == "BOOT":
                    b = f["payload"]
                    self.boots.append(b)
                    self.prev = None
                    self.out("boot", f"{len(self.boots):>4} {f['t_us'] / 1e6:6.2f}  BOOT #{b['boot_count']}  reason "
                                     f"{RESET_REASONS[b['reset_reason']]}" + (f"  task {b['task']}" if b["task"] else ""))
                elif f["name"] == "EVENT":
                    for e in f["payload"]["events"]:
                        if e["id"] in EVT:
                            self.out("event", f"{len(self.boots):>4} {e['t_us'] / 1e6:6.2f}  event {EVT[e['id']]}"
                                              f" (arg {e['arg']})")
                elif f["name"] == "HEALTH":
                    self.row(f["payload"])

    def row(self, h):
        had_prev, p = self.prev is not None, self.prev or {**{k: 0 for k in h}, "uptime_ms": h["uptime_ms"] - 1000}
        self.prev = self.last = h
        n = sum(h["l1_hist"]) or 1
        acc, p99 = 0, "-"
        for c, edge in zip(h["l1_hist"], HIST_EDGES_MS):
            acc += c
            if acc >= 0.99 * n:
                p99 = f"<={edge:g}ms"
                break
        margin = min(100 * f // s for f, s in zip(h["stack_free_min_words"], STACKS))
        window = h["uptime_ms"] - p["uptime_ms"]
        if had_prev and window > 1500:  # HEALTH frames that never reached the host
            lost = h["frames_dropped"] > p["frames_dropped"]
            self.out("quiet", f"{'':>4} {'':>6}  ({round(window / 1000) - 1} HEALTH frame(s) missing"
                              + ("; frames were dropped at the full frame pool)" if lost else ")"))
        per_s = lambda k: round((h[k] - p[k]) * 1000 / window) if window >= 500 else "-"
        cells = [f"{len(self.boots):>4}", f"{h['uptime_ms'] / 1000:6.2f} ",
                 f"{h['sample_rate_hz']:>5} ", f"{per_s('delivered'):>9} ", f"{per_s('frames_sent'):>8} ",
                 f"{h['ring_occ']:>4}/{h['ring_hwm']:<3} ", f"{h['dropped_overrun']:>7}",
                 f"{h['dropped_ring']:>5}", f"{h['dropped_txpool']:>5} ", f"{p99:>7} ",
                 f"{h['cpu_load_permille'] / 10:5.1f}% ", f"{margin:>4}%"]
        text, spans = "", []
        for i, c in enumerate(cells):
            text += (" " if text else "") + c
            k = DROPS[i - 6] if 6 <= i <= 8 else None
            if k and had_prev and h[k] > p[k]:
                start = len(text) - len(c) + len(c) - len(c.lstrip())   # the digits, not the padding
                spans.append((start, start + len(c.strip())))
        if window < 500:
            text += "  (GET_STATUS reply: short window)"
        self.out("row", text, spans)


def main():
    ap = argparse.ArgumentParser(description="make demo: emulated end-to-end telemetry demo")
    ap.add_argument("--transcript", type=pathlib.Path, help="save the printed lines as JSON")
    a = ap.parse_args()
    if not ELF.exists():
        sys.exit(f"{ELF} missing: run `make fw`")
    renode = subprocess.Popen([os.environ["RENODE_BIN"], "--disable-gui", "--hide-log", "-P", str(MON_PORT)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lines = [] if a.transcript else None
    try:
        ok = run(Monitor(MON_PORT), lines)
    finally:
        renode.terminate()
        renode.wait(timeout=10)
    if a.transcript:
        a.transcript.write_text(json.dumps({"columns": COLS, "lines": lines}, indent=0) + "\n")
    sys.exit(0 if ok else 1)


def run(mon, transcript):
    for line in [f"$elf=@{ELF}", f"include @{ROOT}/renode/telemetry.resc",
                 *[f"sysbus.adc1 FeedSample @{ROOT}/renode/data/nominal/ch{c}.dat {c} -1" for c in (1, 8, 16, 17)],
                 "sysbus.i2c1.imu FeedAccelerationSample 0.5 -0.25 1.0 1000000",
                 "sysbus.i2c1.imu FeedAngularRateSample 10 -20 250 1000000",
                 f'emulation CreateServerSocketTerminal {UART_PORT} "telemetry" false',
                 "connector Connect sysbus.usart2 telemetry"]:
        mon(line)
    dash = Dash(transcript)
    uart = socket.create_connection(("127.0.0.1", UART_PORT))

    def reader():
        while chunk := uart.recv(65536):
            dash.feed(chunk)
    threading.Thread(target=reader, daemon=True).start()

    seq, snaps, clock = [0], {}, [0]

    def snapshot():
        h = dash.last or {k: 0 for k in DROPS}
        return {**{k: h[k] for k in DROPS}, "boots": len(dash.boots)}

    def cmd(name, *args, note):
        seq[0] += 1
        with dash.lock:
            snaps[note.split(":")[0]] = snapshot()
            dash.out("blank", "")
            dash.out("phase", f"t = {clock[0]:g} s (virtual)  >> {note}")
        mon(f"python \"u = monitor.Machine['sysbus.usart2']; "
            f"[u.WriteChar(b) for b in {list(encode_command(name, seq[0], *args))}]\"")

    def run_s(n):
        for _ in range(n):
            mon('emulation RunFor "1"')
            clock[0] += 1
            time.sleep(0.05)  # let the reader print before the next step

    with dash.lock:
        dash.out("title", "rttelemetry demo [EMU]: firmware on Renode 1.17.0, virtual time (not hardware)")
        dash.out("blank", "")
        dash.out("phase", "t = 0 s (virtual)  >> nominal: 1 kHz, 4 ADC channels + IMU at 200 Hz")
        dash.out("head", COLS)
    for secs, name, args, note in SCRIPT:
        run_s(secs)
        cmd(name, *args, note=note)
    run_s(TAIL_S)
    time.sleep(0.5)

    with dash.lock:
        r = dash.acct.check_exact()
        d = lambda a, b, k: snaps[b][k] - snaps[a][k]
        checks = [
            ("Conservation since the last boot", r["ok"],
             f"triggered {r['triggered']} = delivered {r['delivered']} + dropped {r['dropped']} "
             f"+ in flight {r['in_flight']}; received {r['received']}"),
            ("FI-01 stall: drops at the ring", d("FI-01", "FI-02", "dropped_ring") > 0,
             f"ring +{d('FI-01', 'FI-02', 'dropped_ring')}, pool +{d('FI-01', 'FI-02', 'dropped_txpool')} "
             "(the drained ring bursts into the 4-frame pool)"),
            # no HEALTH reaches the host while the link is slow, so measure up to the next phase
            ("FI-02 slow link: drops move to the pool", d("FI-02", "FI-06", "dropped_txpool") > 0
             and d("FI-02", "FI-06", "dropped_ring") == 0,
             f"pool +{d('FI-02', 'FI-06', 'dropped_txpool')}, ring +{d('FI-02', 'FI-06', 'dropped_ring')}"),
            ("FI-06 hang: watchdog reset names the task", len(dash.boots) == 2
             and RESET_REASONS[dash.boots[-1]["reset_reason"]] == "WDT_TASK_STALL" and dash.boots[-1]["task"] == "cmd",
             f"BOOT #{dash.boots[-1]['boot_count']} reason {RESET_REASONS[dash.boots[-1]['reset_reason']]}, "
             f"task {dash.boots[-1]['task'] or '-'}; sampling resumed"),
            ("Decoder rejects (CRC/COBS)", not dash.d.rejects, str(dict(dash.d.rejects) or 0)),
        ]
        dash.out("blank", "")
        dash.out("title", "Summary [EMU] (Renode virtual time, not hardware)")
        for name, ok, detail in checks:
            dash.out("pass" if ok else "fail", f"  {'PASS' if ok else 'FAIL'}  {name}: {detail}")
    return all(ok for _, ok, _ in checks)


if __name__ == "__main__":
    main()
