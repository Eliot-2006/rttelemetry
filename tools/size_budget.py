#!/usr/bin/env python3
"""Check firmware memory use against tools/budgets.json (PRD §15.4, NFR-MEM-2).

Usage: size_budget.py [--budgets FILE] IMAGE...
IMAGE is an .elf (sized with `arm-none-eabi-size -A -d`) or a saved `*.size.txt` of that output.
Sections are assigned to regions by address, so new sections (.dma_ram, ...) are counted
without edits here. .data also counts toward flash (its initializers live there).
The 2 KiB MSP stack (.msp) sits in CCM and counts toward the CCM budget (PRD §15.2).
Exit status 1 if any region exceeds its budget.
"""
import argparse
import json
import os
import subprocess
import sys

REGIONS = {  # name: (base, length) from src/platform/stm32f407vg.ld
    "flash": (0x08000000, 1024 * 1024),
    "sram": (0x20000000, 128 * 1024),
    "ccm": (0x10000000, 64 * 1024),
}

def parse(text):
    """`size -A -d` output -> {section: (size, addr)}."""
    sections = {}
    for line in text.splitlines():
        f = line.split()
        if len(f) == 3 and f[1].isdigit() and f[2].isdigit():
            sections[f[0]] = (int(f[1]), int(f[2]))
    return sections


def usage(sections):
    used = dict.fromkeys(REGIONS, 0)
    for name, (size, addr) in sections.items():
        for region, (base, length) in REGIONS.items():
            if base <= addr < base + length:
                used[region] += size
        if name == ".data":
            used["flash"] += size
    return used


def image_name(path):
    base = os.path.basename(path)
    for suffix in (".size.txt", ".elf"):
        if base.endswith(suffix):
            return base[: -len(suffix)]
    return base


def read_sizes(path):
    if path.endswith(".elf"):
        return subprocess.run(["arm-none-eabi-size", "-A", "-d", path],
                              check=True, capture_output=True, text=True).stdout
    with open(path) as f:
        return f.read()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--budgets", default=os.path.join(os.path.dirname(__file__), "budgets.json"))
    ap.add_argument("images", nargs="+")
    args = ap.parse_args(argv)
    with open(args.budgets) as f:
        budgets = json.load(f)

    ok = True
    print(f"{'image':<44} {'region':<12} {'used':>8} {'budget':>8} {'use':>6}")
    for path in args.images:
        name = image_name(path)
        limits = {**budgets["default"], **budgets.get(name, {})}
        used = usage(parse(read_sizes(path)))
        for region in REGIONS:
            over = used[region] > limits[region]
            ok &= not over
            pct = 100.0 * used[region] / limits[region]
            print(f"{path:<44} {region:<12} {used[region]:>8} {limits[region]:>8} {pct:>5.1f}%"
                  + ("  OVER BUDGET" if over else ""))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
