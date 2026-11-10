#!/usr/bin/env python3
"""Regenerate docs/results.md from the latest bench/results/<env>/*-<bench>.json (PRD §20.3).

    python bench/report.py
"""
import glob
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from telemetry.frames import RESET_REASONS  # noqa: E402
LABEL = {"emu": "[EMU]", "host": "[HOST]", "build+emu": "[BUILD]/[EMU]"}


def latest(bench):
    files = sorted(glob.glob(str(ROOT / f"bench/results/*/*-{bench}.json")), key=lambda f: pathlib.Path(f).stat().st_mtime)
    return json.loads(pathlib.Path(files[-1]).read_text()) if files else None


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def provenance(d):
    return (f"Commit `{d['commit']}`{' (dirty)' if d['dirty'] else ''}, {d['toolchain']}, Renode {d['renode']}, "
            f"FreeRTOS {d['freertos']}, Python {d['python']}, host {d['host_os']}.")


def b01(d):
    L = LABEL[d["env"]]
    rows = [[f"{r['rate_hz']} Hz", f"{r['triggered']} {L}", f"{r['received']} {L}", f"{r['dropped_overrun']} {L}",
             f"{r['dropped_ring']} {L}", f"{r['dropped_txpool']} {L}", f"{r['cpu_load_permille_mean'] / 10:.1f} % {L}",
             f"{r['l1_max_us_device'] / 1000:.1f} ms {L}", f"{r['l2_max_us_device']} µs {L}", f"{r['ring_hwm']} {L}",
             "yes" if r["conservation"]["ok"] else "**NO**", "yes" if r["zero_loss"] else "no"]
            for r in d["rows"]]
    return (f"### B-01 Rate sweep ({d['seconds_per_rate']:g} s virtual per rate)\n\n"
            + table(["Rate", "Triggered", "Received", "Overrun", "Ring drop", "Pool drop", "CPU (mean)", "L1 max",
                     "L2 max", "Ring HWM", "Conservation exact", "Zero loss"], rows)
            + "\n\nL1/L2 max, CPU and ring HWM are steady-state: they are taken from the last HEALTH frame before "
              "`SET_RATE 0`, because the final partial batch waits for the 20 ms flush by design. At 250 and "
              "500 Hz, a 10-sample batch takes longer than 20 ms to fill, so L1 is bounded by the flush timer "
              "and the proc wake period instead."
            + f"\n\nMax zero-loss rate: **{d['max_zero_loss_rate_hz']} Hz {L}**. The emulated ADC needs 400 µs per "
              "4-channel scan (D13), so 4 kHz and 8 kHz overrun by construction in Renode. The overruns are counted "
              "and conservation still holds. Silicon scan time is about 33 µs `[CALC]`; the zero-loss rate on "
              "hardware is untested. CPU load is FreeRTOS run-time stats in virtual time. Renode is not "
              "cycle-accurate, so this is not a silicon CPU figure.")


def b02(d):
    L = LABEL[d["env"]]
    l1 = d["l1_us"]
    return (f"### B-02 Latency at 1 kHz ({d['samples']} samples)\n\n"
            + table(["Metric", "p50", "p90", "p99", "max"],
                    [["L1 " + L] + [f"{l1[k] / 1000:.2f} ms" for k in ("p50", "p90", "p99", "max")],
                     ["L2 " + L, "-", "-", "-", f"{d['l2_max_us_device']} µs (device max)"]])
            + f"\n\nL1 is {d['l1_definition']}. Target P-03: p99 ≤ 10 ms, max ≤ 12 ms. "
              f"{d['note']}."
            + ("\n\nThe L1 histogram has one spike per 1 ms sample period: a sample's latency is set by its "
               "position in the 10-sample batch. Emulated timers have no jitter, so the spikes are sharp; silicon "
               "would spread them (B-06)." if "l1_hist" in d else ""))


def b04(d):
    sec = table(["Section", "Bytes [BUILD]"], [[k, v] for k, v in d["sections"].items()])
    top = table(["Symbol", "Type", "Bytes [BUILD]"], [[f"`{s['symbol']}`", s["type"], s["bytes"]] for s in d["top20"]])
    st = table(["Task", "Configured (words)", "Min free (words) [EMU]", "Used"],
               [[t, v["configured_words"], v["free_min_words"], f"{v['used_pct']} %"]
                for t, v in d["stacks_after_1khz_run"].items()]
               + [["MSP (bytes)", d["msp"]["configured_bytes"], d["msp"]["free_min_bytes"],
                   f"{100 * (1 - d['msp']['free_min_bytes'] / d['msp']['configured_bytes']):.1f} %"]])
    return (f"### B-04 Memory (`{d['elf']}`)\n\n{sec}\n\nHeap: **{d['heap_bytes']} bytes** (no allocator linked; "
            f"`scripts/check_no_heap.sh`).\n\nTop 20 symbols:\n\n{top}\n\nStacks after the 1 kHz sweep run "
            f"(the 8 kHz stress margins are checked by IT-STRESS-1, ≥ 25 %):\n\n{st}")


def b05(d):
    L = LABEL[d["env"]]
    wb = table(["Frame type", "Bytes on wire " + L], sorted(d["wire_bytes_by_type"].items()))
    return (f"### B-05 Backpressure: 1 kHz over an emulated 115200-baud link ({d['seconds']:g} s)\n\n"
            f"The link is emulated with FI-02 (`TX_DELAY` = {d['emulated_us_per_byte']} µs/byte, rounded up to 1 ms "
            "per frame). It is not a real 115200-baud UART.\n\n"
            + table(["Metric", "Value"], [
                ["Steady-state ADC samples delivered", f"{d['steady_state_delivered_hz']} Hz {L}"],
                ["`[CALC]` for SAMPLE_BATCH frames alone", f"{d['calc_delivered_hz_sample_frames_only']} Hz"],
                ["Dropped at ring / pool / overrun", f"{d['dropped_ring']} / {d['dropped_txpool']} / "
                                                     f"{d['dropped_overrun']} {L}"],
                ["Conservation exact", "yes" if d["conservation"]["ok"] else "**NO**"]])
            + f"\n\nDrops happen at the frame pool, not the ring: proc keeps up, and the link is the bottleneck. "
              f"{d['note']}.\n\n{wb}")


def b07(d):
    L = LABEL[d["env"]]
    return (f"### B-07 Soak ({d['seconds_virtual'] / 60:g} min virtual at 1 kHz)\n\n"
            + table(["Metric", "Value"], [
                ["Samples received", f"{d['samples_received']} {L}"],
                ["Boots (1 = no reset)", f"{d['boots']} {L}"],
                ["Decoder rejects (CRC/COBS)", f"{sum(d['rejects'].values())} {L}"],
                ["Drops (overrun / ring / pool)", f"{d['dropped_overrun']} / {d['dropped_ring']} / {d['dropped_txpool']} {L}"],
                ["Conservation exact", "yes" if d["conservation"]["ok"] else "**NO**"],
                ["Min task stack free (words)", ", ".join(f"{t} {w}" for t, w in d["stack_free_min_words"].items())
                 + f" {L}"]])
            + "\n\nP-10 (EMU, ≥ 10⁶ samples, 0 CRC errors, 0 unaccounted)."
            + (" The per-minute series (delivered rate, ring HWM, pool and stack headroom, CPU, L1 max) is in "
               "the result JSON and plotted below." if "per_minute" in d else ""))


def b08(d):
    L = LABEL[d["env"]]
    return (f"### B-08 Protocol overhead at 1 kHz\n\n"
            + table(["Metric", "Measured " + L, "`[CALC]` (PRD §12.4)"], [
                ["SAMPLE_BATCH, 10 samples, on wire", f"{', '.join(map(str, d['sample_batch_bytes_full']))} B",
                 f"{d['calc_sample_batch_bytes']} B"],
                ["All frames, bytes/s", d["bytes_per_second_total"], "-"],
                ["Link utilization at 921600 baud", f"{d['link_utilization_pct']} %", "-"]])
            + "\n\n" + table(["Frame type", "Bytes " + L], sorted(d["wire_bytes_by_type"].items())))


def b09(d):
    rows = [[k, v["trials"], v["undetected"], f"{100 * v['detection_rate']:.3f} %", v["neighbour_frames_lost"]]
            for k, v in d["results"].items()]
    return ("### B-09 Corruption Monte Carlo (FI-11, P-11)\n\n"
            + table(["Error", "Trials", "Undetected [HOST]", "Detected [HOST]", "Neighbour frames lost"], rows)
            + f"\n\nStream: {d['stream']}; seed {d['seed']}. The corrupted frame is always rejected. "
              f"{d['note'][:1].upper() + d['note'][1:]}.")


def b10(d):
    L = LABEL[d["env"]]
    c = d["conservation"]
    return (f"### B-10 Ring overflow and drain (FI-01: `proc_task` stalls {d['stall_ms']} ms at "
            f"{d['rate_hz']} Hz)\n\n"
            + table(["Metric", "Value"], [
                ["Dropped at ring / pool / overrun", f"{d['dropped_ring']} / {d['dropped_txpool']} / "
                                                     f"{d['dropped_overrun']} {L}"],
                ["`[CALC]` ring drops", d["calc_expected_ring_drops"]],
                ["Missing sequence numbers at the host", f"{d['seq_gap_total']} {L} (= ring + pool drops)"],
                ["L1 max before the stall / peak", f"{d['steady_l1_max_ms']} ms / {d['peak_l1_ms']} ms {L}"],
                ["Back to steady-state L1", f"{d['back_to_steady_l1_at_ms']} ms after the stall began {L}"],
                ["Conservation exact", f"yes: {c['triggered']} = {c['delivered']} + {c['dropped']}" if c["ok"]
                 else "**NO**"]])
            + "\n\nThe samples already queued wait out the stall and are delivered late. The frame pool "
              "fills first, then the ring; every lost sample is counted where it was dropped, and the host "
              "sees exactly that many missing sequence numbers. The first sample triggered after the stall "
              "already has steady-state latency.")


def b11(d):
    L = LABEL[d["env"]]
    rows = [[r["fault"], r["injected"], f"`{r['reason']}` (task `{r['task']}`)", f"{r['reset_after_ms']} ms {L}",
             f"{r['boot_to_first_batch_ms']} ms {L}", f"{r['recovery_ms']} ms {L}", r["dropped_overrun"],
             "yes" if r["conservation_after_reset"] else "**NO**"] for r in d["rows"]]
    return (f"### B-11 Fault recovery (P-13, P-14; armed {d['armed_at_s']:g} s after boot)\n\n"
            + table(["Fault", "Injected", "BOOT reset reason", "Fault to reset", "Boot to first batch",
                     "Total recovery", "Overruns after reset", "Conservation after reset"], rows)
            + f"\n\nPower-on to first batch: {d['power_on_to_first_batch_ms']} ms {L}. Targets: P-13 recovery "
              f"≤ {d['target_p13_ms']} ms; P-14 boot to first batch ≤ {d['target_p14_ms']} ms. "
              f"Timing: {d['note']}. The 3 overruns after each reset are the first ADC scan, which stalled "
              "after each of these resets in Renode (D16) and was aborted after 2 ms, plus the triggers that arrived while "
              "it was in flight. All are counted.")


def b12(d):
    c = d["conservation_last_boot"]
    resets = d["boots"][1:]
    return ("### B-12 `make demo` timeline\n\n"
            + table(["t (virtual)", "Step"], [[f"{x['t_s']:g} s", x["note"]] for x in d["script"]])
            + f"\n\nResets observed: {len(resets)} "
              + "".join(f"(at {b['t_s']} s, `{RESET_REASONS[b['reason']]}`, task `{b['task']}`)" for b in resets)
            + f". Conservation over the last boot: {c['triggered']} triggered = {c['delivered']} delivered + "
              f"{c['dropped']} dropped, {'exact' if c['ok'] else '**NOT exact**'}. Decoder rejects: "
              f"{sum(d['rejects'].values())}.")


SECTIONS = [("B-01", b01), ("B-02", b02), ("B-04", b04), ("B-05", b05), ("B-07", b07), ("B-08", b08), ("B-09", b09),
            ("B-10", b10), ("B-11", b11), ("B-12", b12)]
CHART = {"B-01": "rate-sweep", "B-02": "latency", "B-04": "memory", "B-07": "soak", "B-10": "overflow",
         "B-11": "recovery", "B-12": "demo-timeline"}  # docs/img/*.svg from bench/figures.py


def main():
    parts = ["# Results\n",
             "Generated by `bench/report.py` from `bench/results/`. Do not edit by hand. Reproduce with:\n",
             "```sh\nmake fw && . ~/.cache/rttelemetry-tools/env.sh && python bench/bench.py && make results\n```\n",
             "**Labels.** `[EMU]` means Renode 1.17.0 virtual time: deterministic and reproducible, but not "
             "cycle-accurate, so timing and CPU numbers do not predict silicon. `[HOST]` means a host-side "
             "computation. `[BUILD]` means read from the linked ELF. `[CALC]` means a paper calculation. "
             "**No result on this page was measured on hardware.**\n"]
    first = None
    for name, fn in SECTIONS:
        d = latest(name)
        if d is None:
            parts.append(f"### {name}\n\nNot run yet.\n")
            continue
        first = first or d
        img = ROOT / f"docs/img/{CHART.get(name)}.svg"
        chart = f"\n\n![{name} chart](img/{img.name})" if name in CHART and img.exists() else ""
        parts.append(fn(d) + chart + "\n\n" + provenance(d) + "\n")
    parts.append("### Not run: hardware required\n\n"
                 + table(["ID", "Benchmark", "Why"], [
                     ["B-03", "`ring_push`/`pop` vs FreeRTOS queues, DWT cycles", "Renode does not model cycle timing"],
                     ["B-06", "Sampling jitter histogram, 10⁶ samples", "Emulated timers have no jitter"],
                     ["B-02 (HW part)", "End-to-end latency by logic analyzer", "Needs board + analyzer"],
                     ["B-05 (HW part)", "Real 115200-baud link", "Needs board + USB-UART"],
                     ["B-07 (HW part)", "24 h soak", "Needs board"]]) + "\n")
    (ROOT / "docs/results.md").write_text("\n".join(parts))
    print("wrote docs/results.md")


if __name__ == "__main__":
    main()
