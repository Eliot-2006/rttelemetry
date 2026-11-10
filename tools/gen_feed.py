"""Generate the Renode ADC feed files (PRD §10.2): renode/data/<scenario>/ch{1,8,16,17}.dat.

Every file holds PERIOD raw 12-bit values, one per line, and is fed with `repeat = -1`, so
conversion n of a channel returns line n % PERIOD. Sample seq n therefore carries
raw[k] = SCENARIOS[s][k][n % PERIOD]. Waveforms are closed-form (no RNG), so the files are
reproducible; they are checked in and tests never regenerate them.

nominal      ch1 sine, ch8 triangle, ch16 slow steps, ch17 constant: inside every
             plausibility limit (src/app/proc.c), so no flags are ever raised.
range_fault  nominal, but ch1 = 4090 (> max 4045) for samples 300..309 -> RANGE on exactly those.
stuck_fault  nominal, but ch8 holds its value for samples 400..499 -> STUCK on 449..499
             (the 50th identical sample onward).
roc_fault    nominal, but ch16 jumps +80 (> roc 50) at sample 600 only -> ROC on 600 and 601.

Usage: python tools/gen_feed.py [outdir]   (default renode/data)
"""
import math
import pathlib
import sys

PERIOD = 1000
CHANNELS = (1, 8, 16, 17)  # ADC inputs, in scan order = raw[0..3]


def nominal():
    sine = [round(2048 + 1500 * math.sin(2 * math.pi * n / PERIOD)) for n in range(PERIOD)]
    tri = [500 + 6 * (n if n < 500 else PERIOD - n) for n in range(PERIOD)]
    temp = [930 + 10 * ((n // 250) % 4) for n in range(PERIOD)]
    vref = [1500] * PERIOD
    return [sine, tri, temp, vref]


def range_fault():
    ch = nominal()
    ch[0][300:310] = [4090] * 10
    return ch


def stuck_fault():
    ch = nominal()
    ch[1][400:500] = [ch[1][400]] * 100
    return ch


def roc_fault():
    ch = nominal()
    ch[2][600] += 80
    return ch


SCENARIOS = {f.__name__: f() for f in (nominal, range_fault, stuck_fault, roc_fault)}


def main(out=pathlib.Path("renode/data")):
    for name, chans in SCENARIOS.items():
        d = pathlib.Path(out) / name
        d.mkdir(parents=True, exist_ok=True)
        for ch, vals in zip(CHANNELS, chans):
            (d / f"ch{ch}.dat").write_text("".join(f"{v}\n" for v in vals))


if __name__ == "__main__":
    main(*sys.argv[1:])
