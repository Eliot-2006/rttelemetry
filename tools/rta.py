#!/usr/bin/env python3
"""Response-time analysis (RTA) for fixed-priority preemptive scheduling, single core.

Model (see docs/scheduling.md):
  * Tasks have fixed priorities; higher number = higher priority (FreeRTOS).
    Tasks of EQUAL priority are counted as interference (FreeRTOS time-slices
    them, so either may run first) -- conservative.
  * ISRs preempt every task; they are modelled as highest-priority periodic
    (or sporadic, period = minimum inter-arrival) work with no jitter.
  * Recurrence (Audsley et al.; Burns & Wellings form with release jitter):
        w = C_i + B_i + sum_{j in hp(i)} ceil((w + J_j) / T_j) * C_j
                      + sum_{isr k}     ceil(w / T_k) * C_k
        R_i = w + J_i
    iterated from w = C_i + B_i to a fixed point, or until R_i > D_i.

Usage: python tools/rta.py tasks.json [--observed obs.json] [--json out.json]
                                      [--include-optional]
Exit codes: 0 ok; 1 an observed response exceeds the prediction (analysis
unsafe); 2 the task set is unschedulable.
"""
import argparse
import json
import sys


def _ceil_div(a, b):
    return -(-a // b)


def ll_bound(n):
    """Liu & Layland utilization bound for n tasks under rate-monotonic priorities."""
    return n * (2 ** (1 / n) - 1)


def load(taskset, include_optional=False):
    """Validate a task-set dict and fill defaults. Returns (tasks, isrs)."""
    def keep(e):
        return include_optional or not e.get("optional", False)

    tasks, isrs, names = [], [], set()
    for kind, src, dst in (("task", taskset.get("tasks", []), tasks),
                           ("isr", taskset.get("isrs", []), isrs)):
        for e in filter(keep, src):
            try:
                name, period, wcet = e["name"], e["period_us"], e["wcet_us"]
            except KeyError as k:
                raise ValueError(f"{kind} {e!r}: missing field {k}") from None
            if name in names:
                raise ValueError(f"duplicate name {name!r}")
            names.add(name)
            if period <= 0 or wcet < 0:
                raise ValueError(f"{name}: need period_us > 0 and wcet_us >= 0")
            item = {"name": name, "period_us": period, "wcet_us": wcet}
            if kind == "task":
                item.update(prio=e["prio"],
                            deadline_us=e.get("deadline_us", period),
                            blocking_us=e.get("blocking_us", 0),
                            jitter_us=e.get("jitter_us", 0))
            dst.append(item)
    if not tasks:
        raise ValueError("task set has no tasks")
    return tasks, isrs


def response_time(t, tasks, isrs):
    """Worst-case response time of task t. Returns (R, schedulable)."""
    hp = [j for j in tasks if j is not t and j["prio"] >= t["prio"]]
    base = t["wcet_us"] + t["blocking_us"]
    w = base
    while True:
        nxt = (base
               + sum(_ceil_div(w + j["jitter_us"], j["period_us"]) * j["wcet_us"] for j in hp)
               + sum(_ceil_div(w, k["period_us"]) * k["wcet_us"] for k in isrs))
        if nxt + t["jitter_us"] > t["deadline_us"]:
            return nxt + t["jitter_us"], False
        if nxt == w:
            return w + t["jitter_us"], True
        w = nxt


def analyze(taskset, include_optional=False):
    tasks, isrs = load(taskset, include_optional)
    items = tasks + isrs
    u = sum(e["wcet_us"] / e["period_us"] for e in items)
    bound = ll_bound(len(items))
    out = {"utilization": u, "ll_bound": bound, "ll_holds": u <= bound,
           "isrs": [k["name"] for k in isrs], "tasks": []}
    for t in sorted(tasks, key=lambda t: -t["prio"]):
        r, ok = response_time(t, tasks, isrs)
        out["tasks"].append({"name": t["name"], "prio": t["prio"],
                             "wcet_us": t["wcet_us"], "deadline_us": t["deadline_us"],
                             "response_us": r, "slack_us": t["deadline_us"] - r,
                             "schedulable": ok})
    out["schedulable"] = all(t["schedulable"] for t in out["tasks"])
    return out


def compare(result, observed):
    """Attach predicted/observed ratios. Returns names whose observed > predicted."""
    by_name = {t["name"]: t for t in result["tasks"]}
    unknown = set(observed) - set(by_name)
    if unknown:
        raise ValueError(f"observed tasks not in task set: {sorted(unknown)}")
    unsafe = []
    for name, obs in observed.items():
        t = by_name[name]
        t["observed_us"] = obs
        t["ratio"] = t["response_us"] / obs if obs > 0 else None
        t["unsafe"] = obs > t["response_us"]
        if t["unsafe"]:
            unsafe.append(name)
    return unsafe


def report(r, f=None):
    f = f or sys.stdout
    verdict = "holds" if r["ll_holds"] else "does not hold (exact RTA decides)"
    print(f"Utilization {r['utilization']:.4f}; Liu & Layland bound "
          f"{r['ll_bound']:.4f} -> {verdict}", file=f)
    if r["isrs"]:
        print(f"ISR interference from: {', '.join(r['isrs'])}", file=f)
    print(f"{'task':<14}{'prio':>5}{'C':>9}{'D':>10}{'R':>10}{'slack':>10}"
          f"{'obs':>10}{'R/obs':>7}  status", file=f)
    for t in r["tasks"]:
        obs = t.get("observed_us")
        ratio = f"{t['ratio']:.2f}" if t.get("ratio") is not None else "-"
        status = "UNSCHEDULABLE" if not t["schedulable"] else "ok"
        if t.get("unsafe"):
            status += " UNSAFE(observed > predicted)"
        print(f"{t['name']:<14}{t['prio']:>5}{t['wcet_us']:>9g}{t['deadline_us']:>10g}"
              f"{t['response_us']:>10g}{t['slack_us']:>10g}"
              f"{'-' if obs is None else format(obs, 'g'):>10}{ratio:>7}  {status}", file=f)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("taskset")
    ap.add_argument("--observed", help='JSON {"task": max_observed_response_us}')
    ap.add_argument("--json", help="write results as JSON to this path")
    ap.add_argument("--include-optional", action="store_true",
                    help='include entries marked "optional": true')
    a = ap.parse_args(argv)
    with open(a.taskset) as f:
        result = analyze(json.load(f), a.include_optional)
    unsafe = []
    if a.observed:
        with open(a.observed) as f:
            unsafe = compare(result, json.load(f))
    report(result)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(result, f, indent=2)
    if unsafe:
        print(f"UNSAFE: observed > predicted for {', '.join(unsafe)}", file=sys.stderr)
        return 1
    return 0 if result["schedulable"] else 2


if __name__ == "__main__":
    sys.exit(main())
