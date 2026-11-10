"""`make demo` (PRD §27): emulated end-to-end demo with a live dashboard. No hardware.

Renode runs headless with its monitor on a TCP port; USART2 is streamed over a TCP socket
terminal and decoded live. A scripted sequence steps virtual time one second at a time:
nominal 1 kHz -> SET_RATE 2000 -> FI-01 stall -> FI-02 slow link -> FI-06 task hang -> watchdog
reset -> recovery, then stop + flush and a conservation check. All numbers are [EMU].

The PRD's sequence uses 4 kHz; on Renode 1.17.0 the ADC model needs 400 us per scan (D13), so
4 kHz overruns by construction. The demo uses 2 kHz so the drops it shows come from the faults.
"""
import os
import pathlib
import re
import socket
import subprocess
import sys
import threading
import time

from .accounting import Accounting
from .frames import encode_command
from .stream import Deframer

ROOT = pathlib.Path(__file__).resolve().parents[2]
ELF = ROOT / "build/fw-debug/src/telemetry.elf"
MON_PORT, UART_PORT = 34560, 34561
EVT = {3: "RING_OVF_ON", 4: "RING_OVF_OFF", 5: "TXPOOL_EMPTY_ON", 6: "TXPOOL_EMPTY_OFF", 13: "WDT_STALL"}
REASON = {0: "POWER_ON", 1: "CMD", 2: "WDT_TASK_STALL", 3: "HARDFAULT", 4: "STACK_OVF", 5: "ASSERT"}
STACKS = [384, 384, 384, 384, 384, 128]
HIST_EDGES_MS = [0.5, 1, 2, 5, 10, 20, 50, float("inf")]


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
    def __init__(self):
        self.d, self.acct, self.prev, self.lock = Deframer(), Accounting(), None, threading.Lock()
        self.log = []

    def feed(self, data):
        for f in self.d.feed(data):
            with self.lock:
                self.acct.on_frame(f)
                if f["name"] == "BOOT":
                    b = f["payload"]
                    self.log.append(f"BOOT reason={REASON.get(b['reset_reason'], b['reset_reason'])} "
                                    f"task={b['task'] or '-'} boot#{b['boot_count']}")
                    print(f"  >> {self.log[-1]}")
                    self.prev = None
                elif f["name"] == "EVENT":
                    for e in f["payload"]["events"]:
                        if e["id"] in EVT:
                            print(f"  >> event {EVT[e['id']]} arg={e['arg']}")
                elif f["name"] == "HEALTH":
                    self.line(f["payload"])

    def line(self, h):
        p = self.prev or {k: 0 for k in h}
        self.prev = h
        n = sum(h["l1_hist"]) or 1
        acc, p99 = 0, "-"
        for c, edge in zip(h["l1_hist"], HIST_EDGES_MS):
            acc += c
            if acc >= 0.99 * n:
                p99 = f"<={edge:g}ms"
                break
        margin = min(100 * f // s for f, s in zip(h["stack_free_min_words"], STACKS))
        print(f"[EMU] t={h['uptime_ms'] / 1000:5.0f}s rate={h['sample_rate_hz']:4d}Hz "
              f"samples/s={h['delivered'] - p['delivered']:5d} frames/s={h['frames_sent'] - p['frames_sent']:4d} "
              f"ring={h['ring_occ']:3d}/hwm {h['ring_hwm']:3d} drops ovr/ring/pool="
              f"{h['dropped_overrun']}/{h['dropped_ring']}/{h['dropped_txpool']} "
              f"L1p99{p99} cpu={h['cpu_load_permille']}‰ stack-min={margin}%", flush=True)


def main():
    if not ELF.exists():
        sys.exit(f"{ELF} missing: run `make fw`")
    renode = subprocess.Popen([os.environ["RENODE_BIN"], "--disable-gui", "--hide-log", "-P", str(MON_PORT)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        run(Monitor(MON_PORT))
    finally:
        renode.terminate()
        renode.wait(timeout=10)


def run(mon):
    for line in [f"$elf=@{ELF}", f"include @{ROOT}/renode/telemetry.resc",
                 *[f"sysbus.adc1 FeedSample @{ROOT}/renode/data/nominal/ch{c}.dat {c} -1" for c in (1, 8, 16, 17)],
                 "sysbus.i2c1.imu FeedAccelerationSample 0.5 -0.25 1.0 1000000",
                 "sysbus.i2c1.imu FeedAngularRateSample 10 -20 250 1000000",
                 f'emulation CreateServerSocketTerminal {UART_PORT} "telemetry" false',
                 "connector Connect sysbus.usart2 telemetry"]:
        mon(line)
    dash = Dash()
    uart = socket.create_connection(("127.0.0.1", UART_PORT))

    def reader():
        while chunk := uart.recv(65536):
            dash.feed(chunk)
    threading.Thread(target=reader, daemon=True).start()

    seq = [0]

    def cmd(name, *args, note):
        seq[0] += 1
        print(f"\n== {note}", flush=True)
        mon(f"python \"u = monitor.Machine['sysbus.usart2']; "
            f"[u.WriteChar(b) for b in {list(encode_command(name, seq[0], *args))}]\"")

    def run_s(n):
        for _ in range(n):
            mon('emulation RunFor "1"')
            time.sleep(0.05)  # let the reader print before the next step

    print("== nominal: 1 kHz, 4 ADC channels + IMU at 200 Hz", flush=True)
    run_s(10)
    cmd("set-rate", 2000, note="SET_RATE 2000 Hz")
    run_s(4)
    cmd("fi-arm", 1, 300, note="FI-01: proc_task stalls 300 ms -> ring overflows, then drains")
    run_s(3)
    cmd("fi-arm", 2, 200, note="FI-02: slow link (200 us/byte) -> drops move to the frame pool")
    run_s(4)
    cmd("fi-clear", note="FI_CLEAR: link back to normal")
    run_s(3)
    cmd("fi-arm", 3, 2, note="FI-06: cmd_task hangs -> supervisor stops kicking -> IWDG reset")
    run_s(4)
    cmd("set-rate", 0, note="SET_RATE 0: stop + flush, then GET_STATUS for the final counters")
    run_s(1)
    cmd("get-status", note="GET_STATUS")
    run_s(1)
    time.sleep(0.5)

    with dash.lock:
        r = dash.acct.check_exact()
        fi_ok = any("WDT_TASK_STALL" in x and "task=cmd" in x for x in dash.log)
    print("\n== summary [EMU] (Renode virtual time, not hardware)")
    print(f"conservation since last boot: {'PASS' if r['ok'] else 'FAIL'} triggered={r['triggered']} "
          f"delivered={r['delivered']} dropped={r['dropped']} in_flight={r['in_flight']} received={r['received']}")
    print(f"FI-06 watchdog reset named the hung task: {'PASS' if fi_ok else 'FAIL'}")
    print(f"decoder rejects: {dict(dash.d.rejects) or 0}")
    sys.exit(0 if r["ok"] and fi_ok else 1)


if __name__ == "__main__":
    main()
