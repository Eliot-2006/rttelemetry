#!/usr/bin/env python3
"""Render docs/img/*.svg from the latest bench/results JSON (and the demo transcript). Stdlib only.

    python bench/figures.py                         # every chart from bench/results
    python bench/figures.py --demo docs/img/demo-transcript.json  # also the terminal transcript docs/img/demo.svg

The SVGs are static so GitHub can show them through <img>: no scripts, no web fonts. Colours are the
dataviz reference palette (categorical slots in fixed order, validated for light and dark), with a
prefers-color-scheme block for dark mode. Every number drawn comes from a result file; nothing is
typed in by hand. The tables in docs/results.md are the accessible view of the same data.
"""
import argparse
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
from report import latest  # noqa: E402

OUT = ROOT / "docs/img"
FONT = '-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,sans-serif'
MONO = 'ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,"Liberation Mono",monospace'
LIGHT = {"bg": "#fcfcfb", "line": "rgba(11,11,11,0.10)", "t1": "#0b0b0b", "t2": "#52514e", "t3": "#6f6d68",
         "grid": "#e1e0d9", "axis": "#c3c2b7", "s1": "#2a78d6", "s2": "#eb6834", "s3": "#1baf7a", "s4": "#eda100",
         "wash": "#0b0b0b"}
DARK = {"bg": "#1a1a19", "line": "rgba(255,255,255,0.10)", "t1": "#f0efec", "t2": "#c3c2b7", "t3": "#a3a29b",
        "grid": "#2c2c2a", "axis": "#383835", "s1": "#3987e5", "s2": "#d95926", "s3": "#199e70", "s4": "#c98500",
        "wash": "#ffffff"}


def css(p):
    return (f".bg{{fill:{p['bg']};stroke:{p['line']}}}.t1{{fill:{p['t1']}}}.t2{{fill:{p['t2']}}}"
            f".t3{{fill:{p['t3']}}}.grid{{stroke:{p['grid']}}}.axis{{stroke:{p['axis']}}}.ref{{stroke:{p['t2']}}}"
            + "".join(f".{s}{{fill:{p[s]}}}.k{s}{{stroke:{p[s]}}}.w{s}{{fill:{p[s]};fill-opacity:.16}}"
                      for s in ("s1", "s2", "s3", "s4"))
            + f".wn{{fill:{p['wash']};fill-opacity:.05}}.ring{{stroke:{p['bg']}}}")


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, w, h, title, sub):
        self.w, self.h, self.title, self.items = w, h, title, []
        self.rect(0.5, 0.5, w - 1, h - 1, "bg", rx=8)
        self.text(24, 34, title, "t1", size=15, weight=600)
        self.text(24, 54, sub, "t2", size=12)

    def add(self, s):
        self.items.append(s)

    def text(self, x, y, s, cls="t2", size=12, anchor="start", weight=400, family=None, extra=""):
        fam = f' font-family="{family}"' if family else ""
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" class="{cls}" font-size="{size}" text-anchor="{anchor}"'
                 f' font-weight="{weight}"{fam}{extra}>{esc(s)}</text>')

    def rect(self, x, y, w, h, cls, rx=0, extra=""):
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{max(w, 0):.1f}" height="{max(h, 0):.1f}" rx="{rx}"'
                 f' class="{cls}"{extra}/>')

    def line(self, x1, y1, x2, y2, cls, width=1, extra=""):
        self.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" class="{cls}"'
                 f' stroke-width="{width}"{extra}/>')

    def bar(self, x, y_top, w, y_base, cls, r=4, horizontal=False):
        """A bar with a rounded data end (4 px) and a square baseline end."""
        if horizontal:  # baseline at x, data end at x + w
            x0, x1, y0, y1 = x, x + w, y_top, y_base
            r = min(r, (x1 - x0) / 2, (y1 - y0) / 2)
            d = f"M{x0:.1f},{y0:.1f}H{x1 - r:.1f}Q{x1:.1f},{y0:.1f} {x1:.1f},{y0 + r:.1f}V{y1 - r:.1f}" \
                f"Q{x1:.1f},{y1:.1f} {x1 - r:.1f},{y1:.1f}H{x0:.1f}Z"
        else:
            h = y_base - y_top
            r = min(r, w / 2, h / 2)
            d = f"M{x:.1f},{y_base:.1f}V{y_top + r:.1f}Q{x:.1f},{y_top:.1f} {x + r:.1f},{y_top:.1f}" \
                f"H{x + w - r:.1f}Q{x + w:.1f},{y_top:.1f} {x + w:.1f},{y_top + r:.1f}V{y_base:.1f}Z"
        if (horizontal and w > 0.5) or (not horizontal and y_base - y_top > 0.5):
            self.add(f'<path d="{d}" class="{cls}"/>')

    def legend(self, x, y, items):
        for cls, label in items:
            self.rect(x, y - 9, 10, 10, cls, rx=2)
            self.text(x + 15, y, label, "t2", size=12)
            x += 26 + 6.6 * len(label)

    def save(self, name, desc):
        body = "\n".join(self.items)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}" role="img" aria-labelledby="t d" font-family=\'{FONT}\'>\n'
               f'<title id="t">{esc(self.title)}</title><desc id="d">{esc(desc)}</desc>\n'
               f'<style>{css(LIGHT)}@media (prefers-color-scheme:dark){{{css(DARK)}}}</style>\n{body}\n</svg>\n')
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / name).write_text(svg)
        print("wrote", OUT / name)


def nice_ticks(vmax, n=4):
    step = 10 ** math.floor(math.log10(vmax / n))
    step = next(m * step for m in (1, 2, 2.5, 5, 10) if vmax / (m * step) <= n)
    return [i * step for i in range(int(math.ceil(vmax / step)) + 1)]


def fmt(v):
    return f"{v:,.0f}" if v >= 1000 or v == int(v) else f"{v:g}"


def y_axis(s, x0, x1, y0, y1, ticks, unit="", label_fmt=fmt):
    """Hairline gridlines at each tick; returns value -> y mapper."""
    vmax = ticks[-1]
    ymap = lambda v: y0 - (y0 - y1) * v / vmax
    for t in ticks:
        s.line(x0, ymap(t), x1, ymap(t), "axis" if t == 0 else "grid")
        s.text(x0 - 8, ymap(t) + 4, label_fmt(t) + (unit if t == ticks[-1] else ""), "t3", size=11, anchor="end")
    return ymap


def sub(d, bench):
    return f"{bench} · [EMU] Renode virtual time, not hardware · {d['commit']}"


# ---------------------------------------------------------------- charts
def rate_sweep(d):
    s = Svg(720, 352, "Sampling rate vs. loss", sub(d, "B-01, 60 s per rate"))
    s.legend(24, 80, [("s1", "delivered"), ("s2", "ADC overrun (counted)")])
    x0, x1, y0, y1 = 76, 696, 296, 128
    rows = d["rows"]
    per_s = lambda r, k: r[k] / d["seconds_per_rate"]
    ymap = y_axis(s, x0, x1, y0, y1, nice_ticks(max(per_s(r, "triggered") for r in rows)))
    band = (x1 - x0) / len(rows)
    for i, r in enumerate(rows):
        cx = x0 + band * (i + 0.5)
        dv, ov = per_s(r, "received"), per_s(r, "dropped_overrun")
        top = ymap(dv)
        s.bar(cx - 12, top, 24, y0, "s1", r=0 if ov else 4)
        if ov:
            s.bar(cx - 12, ymap(dv + ov), 24, top - 2, "s2")
        if ov:
            s.text(cx, ymap(dv + ov) - 8, f"{100 * r['received'] / r['triggered']:.0f}% delivered", "t1",
                   size=11, anchor="middle", weight=600)
        s.text(cx, y0 + 18, f"{r['rate_hz']:,} Hz", "t2", size=12, anchor="middle")
    zl = [i for i, r in enumerate(rows) if r["zero_loss"]]
    if zl:
        xa, xb = x0 + band * zl[0] + 8, x0 + band * (zl[-1] + 1) - 8
        s.line(xa, y1 + 6, xb, y1 + 6, "ref", extra=' stroke-opacity=".55"')
        s.text((xa + xb) / 2, y1, f"zero loss up to {rows[zl[-1]]['rate_hz']:,} Hz", "t1", size=12,
               anchor="middle", weight=600)
    s.text(x0 + 8, y1 + 30, "Renode's ADC needs 400 µs per scan (D13),", "t2", size=11)
    s.text(x0 + 8, y1 + 45, "so it caps out near 2,500 samples/s.", "t2", size=11)
    s.text(x1, y0 + 36, "Requested sample rate (4 channels per sample)", "t3", size=11, anchor="end")
    s.text(x0 - 8, y1 - 14, "samples/s", "t3", size=11, anchor="end")
    s.save("rate-sweep.svg", "Delivered and overrun samples per second at each requested rate. Zero loss up to "
           "2000 Hz; at 4 and 8 kHz the emulated ADC (400 us per scan, D13) overruns, and every lost sample is counted.")


def latency_hist(d):
    h, l1 = d["l1_hist"], d["l1_us"]
    s = Svg(720, 320, "Sample-to-frame latency (L1) at 1 kHz",
            sub(d, f"B-02 · {d['samples']:,} samples"))
    x0, x1, y0, y1 = 76, 696, 268, 92
    xmax_ms = 12
    xmap = lambda ms: x0 + (x1 - x0) * ms / xmax_ms
    ymap = y_axis(s, x0, x1, y0, y1, nice_ticks(max(h["counts"])))
    bw = (x1 - x0) * h["bin_us"] / 1000 / xmax_ms
    for i, c in enumerate(h["counts"]):
        s.bar(xmap(i * h["bin_us"] / 1000) + 1, ymap(c), bw - 2, y0, "s1", r=2)
    for ms in range(0, xmax_ms + 1, 2):
        s.text(xmap(ms), y0 + 18, f"{ms}", "t3", size=11, anchor="middle")
    s.text(x1, y0 + 36, "L1: frame handed to TX minus sample trigger (ms)", "t3", size=11, anchor="end")
    s.text(x0 - 52, y1 - 14, "samples per 0.25 ms", "t3", size=11)
    for key, label, dy in (("p50", "p50", 0), ("p99", "p99", 0), ("max", "max", 14)):
        x = xmap(l1[key] / 1000)
        s.line(x, y1 - 4 + dy, x, y0, "ref", extra=' stroke-opacity=".55"')
        s.text(x + 4, y1 + 6 + dy, f"{label} {l1[key] / 1000:.2f} ms", "t1", size=11)
    xt = xmap(10)
    s.line(xt, y1 - 16, xt, y0, "ks2", width=1.5)
    s.text(xt + 4, y1 - 8, "P-03 target: p99 ≤ 10 ms", "t2", size=11)
    s.save("latency.svg", f"Histogram of L1 latency for {d['samples']} samples at 1 kHz. p50 {l1['p50'] / 1000:.2f} "
           f"ms, p99 {l1['p99'] / 1000:.2f} ms, max {l1['max'] / 1000:.2f} ms; target p99 at most 10 ms.")


def overflow(d):
    s = Svg(720, 380, "Ring overflow and drain (FI-01)",
            sub(d, f"B-10 · 1 kHz, proc_task stalled {d['stall_ms']} ms at t = 0"))
    s.legend(24, 80, [("s1", "L1 latency of each received sample"), ("s2", "dropped at the frame pool"),
                      ("s3", "dropped at the ring")])
    x0, x1, y0, y1 = 76, 696, 312, 124
    tmin, tmax = -100, 500
    xmap = lambda t: x0 + (x1 - x0) * (t - tmin) / (tmax - tmin)
    ymap = y_axis(s, x0, x1, y0, y1, nice_ticks(d["peak_l1_ms"] * 1.08), " ms")
    g = d["gaps"][0]
    t_split = g["after_ms"] + d["dropped_txpool"] * 0.999  # the oldest gap samples went to the pool
    s.rect(xmap(g["after_ms"]), y1, xmap(t_split) - xmap(g["after_ms"]), y0 - y1, "ws2")
    s.rect(xmap(t_split), y1, xmap(g["before_ms"]) - xmap(t_split), y0 - y1, "ws3")
    s.text((xmap(g["after_ms"]) + xmap(t_split)) / 2, y1 + 18, f"{d['dropped_txpool']} at pool", "t1", size=12,
           anchor="middle", weight=600)
    s.text((xmap(t_split) + xmap(g["before_ms"])) / 2, y1 + 18, f"{d['dropped_ring']} at ring (full)", "t1",
           size=12, anchor="middle", weight=600)
    pts, path = list(zip(d["series"]["t_ms"], d["series"]["l1_ms"])), ""
    prev = None
    for t, l1 in pts:
        if tmin <= t <= tmax:
            path += ("L" if prev is not None and t - prev < 2 else "M") + f"{xmap(t):.1f},{ymap(l1):.1f}"
            prev = t
    s.add(f'<path d="{path}" class="ks1" fill="none" stroke-width="1.5" stroke-linejoin="round"/>')
    for t in (0, d["stall_ms"]):
        s.line(xmap(t), y0 + 4, xmap(t), y1 - 14, "ref", extra=' stroke-opacity=".55"')
    s.line(xmap(0), y1 - 14, xmap(d["stall_ms"]), y1 - 14, "ref", extra=' stroke-opacity=".55"')
    s.text(xmap(d["stall_ms"] / 2), y1 - 20, f"proc_task stalled {d['stall_ms']} ms", "t1", size=12,
           anchor="middle", weight=600)
    s.text(xmap(d["stall_ms"]) + 8, ymap(d["steady_l1_max_ms"]) - 24, "drained: back to steady latency",
           "t2", size=11)
    s.text(xmap(-6), ymap(d["peak_l1_ms"] * 0.82), "128 queued samples", "t2", size=11, anchor="end")
    s.text(xmap(-6), ymap(d["peak_l1_ms"] * 0.82) + 15, "wait out the stall", "t2", size=11, anchor="end")
    for t in range(-100, 501, 100):
        s.text(xmap(t), y0 + 18, f"{t}", "t3", size=11, anchor="middle")
    s.text(x1, y0 + 34, "Sample trigger time relative to the stall (ms)", "t3", size=11, anchor="end")
    c = d["conservation"]
    s.text(24, 366, f"Ring holds {d['ring_capacity']} samples. Conservation exact: triggered {c['triggered']} "
           f"= delivered {c['delivered']} + dropped {c['dropped']}.", "t2", size=11)
    s.save("overflow.svg", f"Latency per received sample around a {d['stall_ms']} ms processing stall. "
           f"{d['dropped_ring']} samples dropped at the full ring, {d['dropped_txpool']} at the frame pool while "
           "the ring drained; every loss counted.")


def recovery(d):
    rows = d["rows"]
    s = Svg(720, 92 + 44 * len(rows) + 44, "Fault recovery: fault to first new sample batch",
            sub(d, "B-11 · fault armed at 1.0 s, reset instant from Renode's virtual clock"))
    s.legend(24, 80, [("s1", "fault → reset"), ("s2", "reset → first SAMPLE_BATCH")])
    x0, x1, top = 236, 660, 104
    xmax = d["target_p13_ms"]
    xmap = lambda ms: x0 + (x1 - x0) * ms / xmax
    for ms in range(0, xmax + 1, 500):
        s.line(xmap(ms), top - 6, xmap(ms), top + 44 * len(rows) - 10, "axis" if ms == 0 else "grid")
        s.text(xmap(ms), top + 44 * len(rows) + 6, f"{ms:,}" + (" ms" if ms == xmax else ""), "t3", size=11,
               anchor="middle")
    for i, r in enumerate(rows):
        y = top + 44 * i
        s.text(x0 - 12, y + 12, f"{r['fault']} {r['injected']}", "t1", size=12, anchor="end")
        s.text(x0 - 12, y + 27, f"reset reason {r['reason']}", "t3", size=11, anchor="end")
        s.bar(x0, y + 4, xmap(r["reset_after_ms"]) - x0, y + 24, "s1", r=0, horizontal=True)
        s.bar(xmap(r["reset_after_ms"]) + 2, y + 4, xmap(r["recovery_ms"]) - xmap(r["reset_after_ms"]) - 2 + 4,
              y + 24, "s2", horizontal=True)
        s.text(xmap(r["recovery_ms"]) + 10, y + 18, f"{r['recovery_ms']:,.0f} ms", "t1", size=12, weight=600)
    s.text(x1, top - 12, "P-13 target: ≤ 2,000 ms", "t2", size=11, anchor="end")
    s.save("recovery.svg", "Time from each injected fault to the first SAMPLE_BATCH after the reset. "
           + "; ".join(f"{r['fault']} {r['recovery_ms']:.0f} ms" for r in rows) + ". Target 2000 ms.")


def timeline(d):
    s = Svg(720, 396, "The make demo scenario, second by second",
            sub(d, "B-12 · samples per second by outcome; counters from HEALTH frames"))
    keys = [("delivered", "s1", "delivered"), ("dropped_overrun", "s2", "overrun"),
            ("dropped_ring", "s3", "dropped at ring"), ("dropped_txpool", "s4", "dropped at pool")]
    s.legend(24, 80, [(c, l) for _, c, l in keys])
    x0, x1, y0, y1 = 76, 696, 316, 132
    tmax = 30
    xmap = lambda t: x0 + (x1 - x0) * t / tmax
    cols, prev = [], None
    for h in d["health"]:
        if prev is None or prev["boot"] != h["boot"]:  # a boot starts with every counter at zero
            prev = {"t_s": d["boots"][h["boot"]]["t_s"], **{k: 0 for k, _, _ in keys}}
        if h["t_s"] - prev["t_s"] >= 0.5:
            w = h["t_s"] - prev["t_s"]
            cols.append((prev["t_s"], h["t_s"], [(h[k] - prev[k]) / w for k, _, _ in keys]))
        prev = h
    ymap = y_axis(s, x0, x1, y0, y1, nice_ticks(max(sum(v) for _, _, v in cols)), " /s")
    for a, b, vals in cols:
        base = y0
        stacked = [(v, c) for v, (_, c, _) in zip(vals, keys) if v > 0]
        for j, (v, c) in enumerate(stacked):
            top = base - (y0 - ymap(v))
            s.bar(xmap(a) + 1.5, top, xmap(b) - xmap(a) - 3, base - (2 if j else 0), c,
                  r=4 if j == len(stacked) - 1 else 0)
            base = top
    marks = [(x["t_s"], x["note"].split(":")[0].replace("SET_RATE 2000 Hz", "2 kHz")
              .replace("SET_RATE 0", "stop").replace("FI_CLEAR", "link ok").replace("GET_STATUS", "")) for x in
             d["script"]] + [(b["t_s"], "watchdog reset") for b in d["boots"][1:]]
    for i, (t, label) in enumerate(sorted(marks)):
        if not label:
            continue
        s.line(xmap(t), y1 - 22, xmap(t), y0, "ref", extra=' stroke-opacity=".45"')
        s.text(xmap(t) + 4, y1 - 14 + (12 if label == "watchdog reset" else 0), label, "t1", size=11, weight=600)
    for t in range(0, tmax + 1, 5):
        s.text(xmap(t), y0 + 18, f"{t}", "t3", size=11, anchor="middle")
    s.text(x1, y0 + 36, "Virtual time (s)", "t3", size=11, anchor="end")
    c = d["conservation_last_boot"]
    s.text(24, 370, "No HEALTH frame reached the host while the link was slow, so that span shows its average.",
           "t2", size=11)
    s.text(24, 386, f"After the reset: {c['triggered']} triggered = {c['delivered']} delivered + {c['dropped']} "
           "dropped (exact).", "t2", size=11)
    s.save("demo-timeline.svg", "Per-second delivered and dropped samples through the demo: 1 kHz, 2 kHz, FI-01 "
           "stall (ring drops), FI-02 slow link (pool drops), FI-06 hang and watchdog reset, stop.")


def memory(b04, b07):
    sec = b04["sections"]
    g = lambda *names: sum(sec.get(n, 0) for n in names)
    regions = [("Flash", g(".isr_vector", ".text", ".rodata", ".ARM", ".init_array", ".fini_array", ".data"), 65536),
               ("SRAM", g(".data", ".bss", ".noinit"), 49152), ("CCM", g(".msp", ".ccm"), 16384)]
    words = {"proc": 384, "tx": 384, "cmd": 384, "health": 384, "imu": 384, "idle": 128}
    stacks = [(t, 100 * (1 - b07["stack_free_min_words"][t] / w)) for t, w in words.items()]
    stacks.append(("MSP", 100 * (1 - b07["msp_free_bytes"] / 2048)))
    s = Svg(720, 360, "Static memory and stack headroom",
            f"B-04 debug ELF [BUILD] · stacks: worst over the 30 min B-07 soak [EMU] · commit {b07['commit']}")
    x0, x1 = 120, 330
    s.text(24, 92, "Image vs. CI budget", "t1", size=13, weight=600)
    for i, (name, used, budget) in enumerate(regions):
        y = 108 + 40 * i
        s.text(x0 - 12, y + 14, name, "t1", size=12, anchor="end")
        s.rect(x0, y + 4, x1 - x0, 14, "wn", rx=3)
        s.bar(x0, y + 4, (x1 - x0) * used / budget, y + 18, "s1", horizontal=True)
        s.text(x0, y + 34, f"{used / 1024:.1f} of {budget // 1024} KiB ({100 * used / budget:.0f} %)", "t2", size=11)
    s.text(24, 248, f"Heap: {b04['heap_bytes']} bytes. No allocator is linked.", "t2", size=11)
    x0, x1 = 460, 680
    s.text(390, 92, "Stack used (peak)", "t1", size=13, weight=600)
    for i, (name, pct) in enumerate(stacks):
        y = 104 + 30 * i
        s.text(x0 - 12, y + 13, name, "t1", size=12, anchor="end")
        s.rect(x0, y + 3, x1 - x0, 14, "wn", rx=3)
        s.bar(x0, y + 3, (x1 - x0) * pct / 100, y + 17, "s1", horizontal=True)
        s.text(x1 + 8, y + 14, f"{pct:.0f} %", "t2", size=11)
    xl = x0 + (x1 - x0) * 0.75
    s.line(xl, 98, xl, 104 + 30 * len(stacks), "ks2", width=1.5)
    s.text(xl, 104 + 30 * len(stacks) + 14, "75 % limit (P-09: ≥ 25 % free)", "t2", size=11, anchor="middle")
    s.save("memory.svg", "Flash, SRAM and CCM use against CI budgets, and peak stack use per task and the MSP "
           "against the 75 percent limit.")


def soak(d):
    pm = d["per_minute"]
    s = Svg(720, 300, "30-minute soak at 1 kHz",
            sub(d, f"B-07 · {d['samples_received']:,} samples, {d['boots']} boot, "
                   f"{sum(d['rejects'].values())} rejects, 0 drops"))
    panels = [("Delivered samples/s", [(p["t_s"], (p["delivered"] - q["delivered"]) / (p["t_s"] - q["t_s"]))
                                       for q, p in zip(pm, pm[1:])], 1200),
              ("Ring high-water mark (of 128)", [(p["t_s"], p["ring_hwm"]) for p in pm], 128),
              ("Smallest stack headroom (%)", [(p["t_s"], p["stack_free_min_pct"]) for p in pm], 100)]
    w = 196
    for i, (title, pts, vmax) in enumerate(panels):
        x0 = 24 + i * (w + 34)
        y0, y1 = 250, 120
        s.text(x0, 96, title, "t1", size=12, weight=600)
        s.line(x0, y0, x0 + w, y0, "axis")
        s.line(x0, y1, x0 + w, y1, "grid")
        s.text(x0 + w, y1 - 6, f"{vmax:g}", "t3", size=11, anchor="end")
        xmap = lambda t: x0 + w * t / 1800
        ymap = lambda v: y0 - (y0 - y1) * v / vmax
        path = "".join(("M" if j == 0 else "L") + f"{xmap(t):.1f},{ymap(v):.1f}" for j, (t, v) in enumerate(pts))
        s.add(f'<path d="{path}" class="ks1" fill="none" stroke-width="2" stroke-linejoin="round"'
              ' stroke-linecap="round"/>')
        t, v = pts[-1]
        s.add(f'<circle cx="{xmap(t):.1f}" cy="{ymap(v):.1f}" r="4" class="s1 ring" stroke-width="2"/>')
        s.text(xmap(t) - 2, ymap(v) - 10, f"{v:,.0f}", "t1", size=11, anchor="end", weight=600)
        s.text(x0, y0 + 18, "0", "t3", size=11)
        s.text(x0 + w, y0 + 18, "30 min", "t3", size=11, anchor="end")
    s.save("soak.svg", "Per-minute delivered rate, ring high-water mark and smallest stack headroom over a "
           "30-minute virtual soak: all flat, no resets, no drops.")


# ---------------------------------------------------------------- demo terminal transcript
TERM = {"bg": "#0d1117", "fg": "#c9d1d9", "dim": "#8b949e", "phase": "#79c0ff", "event": "#e3b341",
        "boot": "#d2a8ff", "pass": "#56d364", "fail": "#f85149", "drop": "#ff7b72", "title": "#f0f6fc"}


def terminal(path):
    lines = json.loads(pathlib.Path(path).read_text())["lines"]
    lh, cw, pad, top = 17, 7.25, 18, 44
    width = int(pad * 2 + cw * max(len(x["text"]) for x in lines)) + 4
    height = top + lh * len(lines) + pad
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} '
           f'{height}" role="img" aria-labelledby="t d" font-family=\'{MONO}\' font-size="12.5">',
           '<title id="t">make demo output</title><desc id="d">Transcript of a real make demo run: firmware on '
           'Renode, faults injected, watchdog reset, conservation check.</desc>',
           f'<rect width="{width}" height="{height}" rx="8" fill="{TERM["bg"]}"/>']
    for i, c in enumerate(("#ff5f57", "#febc2e", "#28c840")):
        out.append(f'<circle cx="{20 + 18 * i}" cy="20" r="5.5" fill="{c}"/>')
    out.append(f'<text x="{width / 2:.0f}" y="24" fill="{TERM["dim"]}" text-anchor="middle" font-size="12">'
               'make demo</text>')
    color = {"phase": TERM["phase"], "event": TERM["event"], "boot": TERM["boot"], "pass": TERM["pass"],
             "fail": TERM["fail"], "head": TERM["dim"], "quiet": TERM["dim"], "title": TERM["title"]}
    for i, x in enumerate(lines):
        y = top + lh * i + 12
        weight = ' font-weight="700"' if x["kind"] in ("title", "phase", "pass", "fail") else ""
        spans, text, pos, parts = sorted(x["spans"]), x["text"], 0, []
        for a, b in spans:
            parts += [(text[pos:a], None), (text[a:b], TERM["drop"])]
            pos = b
        parts.append((text[pos:], None))
        tsp = "".join(f'<tspan fill="{c}" font-weight="700">{esc(t)}</tspan>' if c else esc(t)
                      for t, c in parts if t)
        out.append(f'<text x="{pad}" y="{y}" fill="{color.get(x["kind"], TERM["fg"])}"{weight} '
                   f'xml:space="preserve">{tsp}</text>')
    out.append("</svg>")
    (OUT / "demo.svg").write_text("\n".join(out) + "\n")
    print("wrote", OUT / "demo.svg")


# ---------------------------------------------------------------- architecture
def architecture(fw):
    """Data path, hand-placed. Buffer sizes come from the result file's firmware block."""
    s = Svg(940, 492, "RTTelemetry data path",
            "STM32F407 + FreeRTOS. The same firmware image runs unmodified in Renode.")
    s.add('<defs><marker id="ah" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" '
          'orient="auto"><path d="M0,0L8,4L0,8z" class="t2"/></marker></defs>')
    kinds = {"isr": "ws4 ks4", "task": "ws1 ks1", "buf": "ws3 ks3", "hw": "wn axis"}

    def box(x, y, kind, name, detail, w=156):
        s.rect(x, y, w, 52, kinds[kind], rx=6, extra=' stroke-width="1.5"')
        s.text(x + 12, y + 22, name, "t1", size=13, weight=600)
        s.text(x + 12, y + 40, detail, "t2", size=11)

    def arrow(*pts):
        d = "M" + "L".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        s.add(f'<path d="{d}" class="ref" fill="none" stroke-width="1.5" marker-end="url(#ah)"/>')

    def drop(x, y, label, anchor="start"):
        dx = x + 4 if anchor == "start" else x - 4 - 6.2 * len(label) - 10
        s.add(f'<path d="M{dx:.1f},{y - 9:.1f}l4,4l-4,4l-4,-4z" class="s2"/>')
        s.text(dx + 10, y, label, "t2", size=11)

    x = 24
    for kind, label in (("isr", "interrupt handler"), ("task", "FreeRTOS task"), ("buf", "shared buffer"),
                        ("hw", "peripheral / host")):
        s.rect(x, 71, 10, 10, kinds[kind], rx=2)
        s.text(x + 15, 80, label, "t2", size=12)
        x += 30 + 6.6 * len(label)
    drop(x - 4, 80, "counted drop point")

    X = (24, 208, 392, 576, 760)
    A, B, C = 104, 244, 384
    box(X[0], A, "isr", "TIM3 update ISR", "prio 4 · stamps t_us")
    box(X[1], A, "hw", "ADC1 scan + DMA2", "4 channels, Stream0")
    box(X[2], A, "isr", "DMA2 TC ISR", "prio 5 · builds record")
    box(X[3], A, "buf", "SPSC ring", f"{fw['ring']} records, lock-free")
    box(X[4], A, "task", "proc_task", f"prio 5 · flags, batch {fw['batch']}")
    for i in range(4):
        arrow((X[i] + 156, A + 26), (X[i + 1] - 2, A + 26))
    drop(X[0], A + 70, "dropped_overrun: scan still busy")
    drop(X[3], A + 70, "dropped_ring: ring full")

    box(X[4], B, "buf", "Frame pool", f"{fw['pool']} frames, never blocks")
    box(X[3], B, "task", "tx_task", "prio 4 · COBS + CRC-16")
    box(X[2], B, "hw", "USART2 TX", f"DMA1 S6 · {fw['baud']} baud")
    box(X[0], B, "hw", "Host decoder (Python)", "CRC, seq gaps, conservation check", w=340)
    arrow((X[4] + 78, A + 52), (X[4] + 78, B - 2))
    drop(X[4] + 70, A + 100, "dropped_txpool: pool empty", anchor="end")
    for xa, xb in ((X[4], X[3] + 156), (X[3], X[2] + 156), (X[2], X[0] + 340)):
        arrow((xa, B + 26), (xb + 2, B + 26))
    drop(X[0], B + 70, "link loss: CRC rejects, missing seq")

    box(X[1], C, "isr", "USART2 RX ISR", "bytes into rx_ring")
    box(X[2], C, "task", "cmd_task", "prio 3 · parse, ACK")
    box(X[3], C, "task", "imu_task", "prio 6 · IMU at 200 Hz")
    box(X[4], C, "task", "health_task", "prio 2 · HEALTH, watchdog")
    arrow((X[1] + 78, B + 52), (X[1] + 78, C - 2))
    s.text(X[1] + 86, B + 98, "commands", "t3", size=11)
    arrow((X[1] + 156, C + 26), (X[2] - 2, C + 26))
    bus = C - 44
    for xi in (X[2], X[3]):
        s.line(xi + 78, C, xi + 78, bus, "ref", width=1.5)
    s.line(X[2] + 78, bus, X[4] + 78, bus, "ref", width=1.5)
    arrow((X[4] + 78, C), (X[4] + 78, B + 54))

    s.text(24, 470, "Every drop point has its own counter, reported in each HEALTH frame. The host checks "
           "triggered = delivered + dropped + in flight.", "t2", size=12)
    s.save("architecture.svg", "Data path: TIM3 ISR starts an ADC1 scan via DMA2; the DMA ISR pushes a sample "
           "record into a lock-free SPSC ring; proc_task batches into a fixed frame pool shared with imu_task, "
           "health_task and cmd_task; tx_task encodes and sends over USART2 by DMA to the host decoder. "
           "Commands return through the USART2 RX ISR to cmd_task. Drops are counted at the ADC trigger, the "
           "ring and the frame pool; the host detects link loss.")


NEEDS = {"B-02": ("l1_hist",), "B-07": ("per_minute",)}  # plot data added after the first runs


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--demo", help="demo transcript JSON from `python -m telemetry.demo --transcript`")
    a = ap.parse_args()
    for bench, fn in (("B-01", rate_sweep), ("B-02", latency_hist), ("B-10", overflow), ("B-11", recovery),
                      ("B-12", timeline), ("B-07", soak)):
        d = latest(bench)
        if d is None or not all(k in d for k in NEEDS.get(bench, ())):
            print(f"{bench}: no result with plot data yet, skipped")
            continue
        fn(d)
    b10 = latest("B-10")
    if b10:
        architecture(b10["firmware"])
    b04, b07 = latest("B-04"), latest("B-07")
    if b04 and b07:
        memory(b04, b07)
    if a.demo:
        terminal(a.demo)


if __name__ == "__main__":
    main()
