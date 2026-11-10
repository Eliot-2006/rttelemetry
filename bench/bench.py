#!/usr/bin/env python3
"""Benchmarks (PRD §20.2) that can run without hardware: [EMU] on Renode, [HOST], [BUILD].

    python bench/bench.py [--only B-01,B-09] [--sweep-seconds 60] [--soak-seconds 1800]

Each benchmark writes bench/results/<env>/<YYYYMMDD>-<shortsha>-<bench>.json (PRD §20.3) with
the environment, firmware configuration, raw data and summary. [EMU] values come from Renode
virtual time and are deterministic per commit; they are NOT hardware measurements.
B-03 (DWT cycle counts) and B-06 (sampling jitter) need silicon and are not run here.
Run from the repo root after `make fw` with ~/.cache/rttelemetry-tools/env.sh sourced.
"""
import argparse
import datetime
import json
import os
import pathlib
import platform
import random
import statistics
import subprocess
import sys
import tempfile
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from telemetry.accounting import Accounting  # noqa: E402
from telemetry.frames import FrameError, decode_frame  # noqa: E402
from telemetry.stream import Deframer  # noqa: E402

ELF = ROOT / "build/fw-debug/src/telemetry.elf"
RESULTS = ROOT / "bench/results"
STACK_WORDS = {"proc": 384, "tx": 384, "cmd": 384, "health": 384, "imu": 384, "idle": 128}
TASKS = list(STACK_WORDS)


# ---------------------------------------------------------------- environment / output
def sh(*cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()


def env_info():
    return {
        "commit": sh("git", "rev-parse", "--short=8", "HEAD"),
        "dirty": bool(sh("git", "status", "--porcelain", "--untracked-files=no")),
        "toolchain": sh("arm-none-eabi-gcc", "--version").splitlines()[0],
        "renode": pathlib.Path(os.environ.get("RENODE_BIN", "renode")).as_posix().split("renode-")[-1].split("/")[0],
        "freertos": sh("git", "describe", "--tags", cwd=ROOT / "third_party/FreeRTOS-Kernel"),
        "python": platform.python_version(),
        "host_os": f"{platform.system()} {platform.release()} {platform.machine()}",
        "firmware": {"default_rate_hz": 1000, "batch": 10, "baud": 921600, "ring": 128, "pool": 4,
                     "elf": str(ELF.relative_to(ROOT))},
    }


_ENV = None


def save(bench, env, data):
    global _ENV
    info = _ENV = _ENV or env_info()  # recorded once, at the first save of the run
    out = RESULTS / env / f"{datetime.date.today():%Y%m%d}-{info['commit']}-{bench}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"bench": bench, "env": env, **info, **data}, indent=1) + "\n")
    print(f"{bench}: wrote {out.relative_to(ROOT)}")


# ---------------------------------------------------------------- Renode runner
def cmd_bytes(name, seq, *args):
    from telemetry.frames import encode_command
    return list(encode_command(name, seq, *args))


def run_emu(steps, scenario="nominal"):
    """steps: ("run", seconds) | ("cmd", name, *args). Returns the raw USART2 capture."""
    with tempfile.TemporaryDirectory() as d:
        cap, resc = pathlib.Path(d) / "uart.bin", pathlib.Path(d) / "run.resc"
        lines = [f"$elf=@{ELF}", f"include @{ROOT}/renode/telemetry.resc"]
        lines += [f"sysbus.adc1 FeedSample @{ROOT}/renode/data/{scenario}/ch{c}.dat {c} -1" for c in (1, 8, 16, 17)]
        lines += ["sysbus.i2c1.imu FeedAccelerationSample 0.5 -0.25 1.0 1000000",
                  "sysbus.i2c1.imu FeedAngularRateSample 10 -20 250 1000000",
                  "sysbus.i2c1.imu Temperature 30",
                  f"sysbus.usart2 CreateFileBackend @{cap} true"]
        seq = 0
        for st in steps:
            if st[0] == "run":
                lines.append(f'emulation RunFor "{st[1]}"')
            else:
                seq += 1
                b = cmd_bytes(st[1], seq, *st[2:])
                lines.append(f"python \"u = monitor.Machine['sysbus.usart2']; [u.WriteChar(x) for x in {b}]\"")
        lines.append("quit")
        resc.write_text("\n".join(lines) + "\n")
        r = subprocess.run([os.environ["RENODE_BIN"], "--disable-gui", "-P", "-1", "-e", f"include @{resc}"],
                           capture_output=True, text=True, timeout=7200)
        if r.returncode:
            sys.exit(f"Renode exited {r.returncode}:\n" + "\n".join((r.stdout + r.stderr).splitlines()[-30:]))
        return cap.read_bytes()


STOP_FLUSH = [("cmd", "set-rate", 0), ("run", 0.1), ("cmd", "get-status"), ("run", 0.15)]


def decode(raw):
    d = Deframer()
    frames = d.feed(raw)
    return frames, dict(d.rejects)


def wire_bytes_by_type(raw):
    """Bytes on the wire (incl. delimiter) per frame type."""
    out = Counter()
    for part in raw.split(b"\0"):
        if part:
            try:
                out[decode_frame(part)["name"]] += len(part) + 1
            except FrameError:
                out["rejected"] += len(part) + 1
    return dict(out)


def full_batch_sizes(raw):
    """Distinct wire sizes (incl. delimiter) of SAMPLE_BATCH frames carrying 10 samples."""
    out = set()
    for part in raw.split(b"\0"):
        try:
            f = decode_frame(part) if part else None
        except FrameError:
            continue
        if f and f["name"] == "SAMPLE_BATCH" and f["payload"]["count"] == 10:
            out.add(len(part) + 1)
    return out


def summarize_run(raw, stop_us=None):
    """stop_us: latency/CPU/HWM come from the last HEALTH before this time (the final stop-and-flush
    batch waits FLUSH_US by design and is not steady state). Counters come from the final HEALTH."""
    frames, rejects = decode(raw)
    acct = Accounting()
    for f in frames:
        acct.on_frame(f)
    health = [f["payload"] for f in frames if f["name"] == "HEALTH"]
    h = health[-1]
    hs = [x for x in health if stop_us is None or x["uptime_ms"] * 1000 < stop_us] or health
    steady = hs[-1]
    samples, l1 = [], []
    for f in frames:
        if f["name"] == "SAMPLE_BATCH":
            for s in f["payload"]["samples"]:
                samples.append(s)
                l1.append(((f["t_us"] - s["t_us"]) % (1 << 32), s["t_us"]))
    drops = {k: h[k] for k in ("dropped_overrun", "dropped_ring", "dropped_txpool")}
    return frames, {
        "boots": sum(f["name"] == "BOOT" for f in frames),
        "rejects": rejects,
        "triggered": h["triggered"], "delivered": h["delivered"], "received": acct.received, **drops,
        "zero_loss": not any(drops.values()) and h["triggered"] == acct.received,
        "conservation": acct.check_exact(h),
        "cpu_load_permille_mean": round(statistics.mean(x["cpu_load_permille"] for x in hs), 1),
        "l1_max_us_device": steady["l1_max_us"], "l2_max_us_device": steady["l2_max_us"],
        "ring_hwm": steady["ring_hwm"], "txpool_free_min": steady["txpool_free_min"],
        "stack_free_min_words": dict(zip(TASKS, h["stack_free_min_words"])), "msp_free_bytes": h["msp_free_bytes"],
        "imu_samples": h["imu_samples"], "imu_dropped": h["imu_dropped"],
        "_l1": l1, "_samples": len(samples),
    }


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100 * (len(xs) - 1))))]


# ---------------------------------------------------------------- benchmarks
def b01_b02_b04_b08(sweep_s):
    rows, one_khz = [], None
    for hz in (250, 500, 1000, 2000, 4000, 8000):
        raw = run_emu([("run", 0.1), ("cmd", "set-rate", hz), ("run", sweep_s)] + STOP_FLUSH)
        frames, s = summarize_run(raw, stop_us=(0.1 + sweep_s) * 1e6)
        if hz == 1000:
            one_khz = (raw, frames, s)
        rows.append({"rate_hz": hz, **{k: v for k, v in s.items() if not k.startswith("_")}})
    zl = [r["rate_hz"] for r in rows if r["zero_loss"]]
    save("B-01", "emu", {"seconds_per_rate": sweep_s, "rows": rows,
                         "max_zero_loss_rate_hz": max(zl) if zl else None})

    raw, frames, s = one_khz
    stop_us = (0.1 + sweep_s) * 1e6  # exclude the final stop-and-flush partial batch (20 ms flush)
    l1 = [d for d, t in s["_l1"] if t < stop_us - 50_000]
    save("B-02", "emu", {"rate_hz": 1000, "seconds": sweep_s, "samples": len(l1),
                         "l1_us": {"p50": pct(l1, 50), "p90": pct(l1, 90), "p99": pct(l1, 99), "max": max(l1)},
                         "l1_definition": "SAMPLE_BATCH frame t_us (handed to TX) minus sample t_us (trigger), "
                                          "per sample, steady state (excludes the final stop-and-flush batch)",
                         "l2_max_us_device": s["l2_max_us_device"],
                         "note": "L2 percentiles and E2E need the per-frame TX start time (HW logic analyzer); device reports L2 max only"})

    wb = wire_bytes_by_type(raw)
    dur = sweep_s
    n_batches = sum(f["name"] == "SAMPLE_BATCH" for f in frames)
    save("B-08", "emu", {"rate_hz": 1000, "seconds": dur, "wire_bytes_by_type": wb,
                         "sample_batch_bytes_mean": round(wb["SAMPLE_BATCH"] / n_batches, 2),
                         "sample_batch_bytes_full": sorted(full_batch_sizes(raw)),
                         "calc_sample_batch_bytes": 196,
                         "bytes_per_second_total": round(sum(wb.values()) / dur),
                         "link_capacity_bytes_per_second": 92160,
                         "link_utilization_pct": round(100 * sum(wb.values()) / dur / 92160, 2)})

    size = sh("arm-none-eabi-size", "-A", "-d", str(ELF))
    sections = {l.split()[0]: int(l.split()[1]) for l in size.splitlines()[2:]
                if l.split() and not l.startswith((".debug", ".comment", ".ARM.attributes", "Total"))}
    syms = []
    for line in sh("arm-none-eabi-nm", "-S", "--size-sort", "-r", str(ELF)).splitlines():
        p = line.split()
        if len(p) == 4:
            syms.append({"symbol": p[3], "type": p[2], "bytes": int(p[1], 16)})
    stacks = {t: {"configured_words": w, "free_min_words": s["stack_free_min_words"][t],
                  "used_pct": round(100 * (w - s["stack_free_min_words"][t]) / w, 1)}
              for t, w in STACK_WORDS.items()}
    save("B-04", "build+emu", {"elf": str(ELF.relative_to(ROOT)), "sections": sections, "top20": syms[:20],
                               "stacks_after_1khz_run": stacks, "msp": {"configured_bytes": 2048,
                                                                        "free_min_bytes": s["msp_free_bytes"]},
                               "heap_bytes": 0})


def b05(seconds):
    us_per_byte = 87  # 10 bits / 115200 baud = 86.8 us; firmware rounds each frame's delay up to 1 ms
    raw = run_emu([("run", 0.1), ("cmd", "fi-arm", 2, us_per_byte), ("run", seconds), ("cmd", "fi-clear"),
                   ("run", 0.3)] + STOP_FLUSH)
    frames, s = summarize_run(raw)
    t_end = (0.1 + seconds) * 1e6
    t_start = t_end - (seconds - 5) * 1e6  # skip the first 5 s (pool filling)
    rx = [x for f in frames if f["name"] == "SAMPLE_BATCH" and t_start <= f["t_us"] < t_end
          for x in f["payload"]["samples"]]
    wb = wire_bytes_by_type(raw)
    s.pop("_l1"), s.pop("_samples")
    save("B-05", "emu", {"rate_hz": 1000, "seconds": seconds, "emulated_us_per_byte": us_per_byte,
                         "steady_state_delivered_hz": round(len(rx) / (seconds - 5), 1),
                         "calc_delivered_hz_sample_frames_only": 587,
                         "wire_bytes_by_type": wb, **s,
                         "note": "IMU, HEALTH and EVENT frames share the emulated 115200-baud link, so the ADC "
                                 "sample rate delivered is below the samples-only [CALC] figure"})


def b07(seconds):
    # 60 s steps keep each RunFor short. The earlier SIGABRT was Renode's --console stdin reader
    # (SemaphoreFullException after ~12 min wall time with stdin at EOF), fixed in run_emu by -P -1.
    raw = run_emu([("run", 60)] * int(seconds // 60) + STOP_FLUSH)
    frames, s = summarize_run(raw, stop_us=seconds * 1e6)
    s.pop("_l1")
    s["samples_received"] = s.pop("_samples")
    save("B-07", "emu", {"rate_hz": 1000, "seconds_virtual": seconds, **s})


def b09(trials=100_000, seed=1):
    """FI-11 / P-11: flip 1-3 random bits (and 4-16-bit bursts) in one frame of a 3-frame stream."""
    raw = run_emu([("run", 1.2)])
    wires = [p + b"\0" for p in raw.split(b"\0") if p][:200]
    rng = random.Random(seed)
    res = {}
    for kind in ("1bit", "2bit", "3bit", "burst<=16"):
        undetected = lost_neighbours = affected_lost = 0
        for _ in range(trials // 4):
            i = rng.randrange(1, len(wires) - 1)
            prev, tgt, nxt = wires[i - 1], bytearray(wires[i]), wires[i + 1]
            nbits = len(tgt) * 8
            if kind == "burst<=16":
                ln = rng.randrange(4, 17)
                start = rng.randrange(0, nbits - ln)
                flips = {start, start + ln - 1} | {b for b in range(start + 1, start + ln - 1) if rng.random() < 0.5}
            else:
                flips = set(rng.sample(range(nbits), int(kind[0])))
            for b in flips:
                tgt[b // 8] ^= 1 << (b % 8)
            d = Deframer()
            out = d.feed(prev + bytes(tgt) + nxt)
            originals = {json.dumps(decode_frame(w[:-1]), sort_keys=True) for w in (prev, wires[i], nxt)}
            got = [json.dumps(f, sort_keys=True) for f in out]
            undetected += sum(g not in originals for g in got)
            affected_lost += json.dumps(decode_frame(wires[i][:-1]), sort_keys=True) not in got
            lost_neighbours += sum(json.dumps(decode_frame(w[:-1]), sort_keys=True) not in got for w in (prev, nxt))
        n = trials // 4
        res[kind] = {"trials": n, "undetected": undetected, "detection_rate": 1 - undetected / n,
                     "target_frame_lost": affected_lost, "neighbour_frames_lost": lost_neighbours}
    save("B-09", "host", {"stream": "first 200 frames of a 1.2 s nominal capture", "seed": seed, "results": res,
                          "note": "neighbour loss happens only when a flip creates or destroys a 0x00 delimiter"})


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--only", default="B-01,B-05,B-07,B-09")
    ap.add_argument("--sweep-seconds", type=float, default=60)
    ap.add_argument("--b05-seconds", type=float, default=30)
    ap.add_argument("--soak-seconds", type=float, default=1800)
    a = ap.parse_args()
    only = a.only.split(",")
    if "B-01" in only:  # also writes B-02, B-04, B-08 from the same runs
        b01_b02_b04_b08(a.sweep_seconds)
    if "B-05" in only:
        b05(a.b05_seconds)
    if "B-09" in only:
        b09()
    if "B-07" in only:
        b07(a.soak_seconds)


if __name__ == "__main__":
    main()
