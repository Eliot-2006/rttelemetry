"""Unit tests for rta.py. Run: cd tools && python3 -m unittest test_rta -v

Expected values come from textbook task sets. Where a book publishes the
numbers they are cited; where we extend a textbook set (blocking, jitter, ISR)
the expected value is hand-derived and the iteration is shown in a comment so
it can be checked without running code.
"""
import json
import math
import os
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr
from io import StringIO

import rta

HERE = os.path.dirname(os.path.abspath(__file__))


def task(name, prio, T, C, D=None, B=0, J=0):
    t = {"name": name, "prio": prio, "period_us": T, "wcet_us": C, "blocking_us": B, "jitter_us": J}
    if D is not None:
        t["deadline_us"] = D
    return t


def resp(result):
    return {t["name"]: t["response_us"] for t in result["tasks"]}


def ok(result):
    return {t["name"]: t["schedulable"] for t in result["tasks"]}


# Burns & Wellings, "Real-Time Systems and Programming Languages" (4th ed.),
# ch. 11 (scheduling), response-time analysis worked example:
#   a: T=7  C=3 (highest)   b: T=12 C=3   c: T=20 C=5 (lowest)
#   U = 0.929 > 3(2^(1/3)-1) = 0.780, yet R_a=3, R_b=6, R_c=20 <= D.
BW_RTA = [task("a", 3, 7, 3), task("b", 2, 12, 3), task("c", 1, 20, 5)]


class TestRta(unittest.TestCase):
    def test_ll_bound_values(self):
        self.assertAlmostEqual(rta.ll_bound(1), 1.0)
        self.assertAlmostEqual(rta.ll_bound(2), 0.8284, places=4)
        self.assertAlmostEqual(rta.ll_bound(3), 0.7798, places=4)
        self.assertAlmostEqual(rta.ll_bound(10**6), math.log(2), places=6)

    def test_passes_ll(self):
        # Burns & Wellings ch. 11, utilization-test "Task set B":
        #   a: T=80 C=32, b: T=40 C=5, c: T=16 C=4 ; U = 0.775 <= 0.780 (passes).
        # Response times (hand-derived): R_c=4, R_b=5+4=9,
        #   R_a: 32 -> 45 -> 54 -> 58 -> 58.
        r = rta.analyze({"tasks": [task("a", 1, 80, 32), task("b", 2, 40, 5), task("c", 3, 16, 4)]})
        self.assertAlmostEqual(r["utilization"], 0.775)
        self.assertTrue(r["ll_holds"])
        self.assertEqual(resp(r), {"c": 4, "b": 9, "a": 58})
        self.assertTrue(r["schedulable"])

    def test_fails_ll_passes_exact(self):
        r = rta.analyze({"tasks": BW_RTA})
        self.assertAlmostEqual(r["utilization"], 3 / 7 + 3 / 12 + 5 / 20)
        self.assertFalse(r["ll_holds"])
        self.assertEqual(resp(r), {"a": 3, "b": 6, "c": 20})
        self.assertTrue(r["schedulable"])
        self.assertEqual([t["slack_us"] for t in r["tasks"]], [4, 6, 0])

    def test_unschedulable(self):
        # Burns & Wellings ch. 11, utilization-test "Task set A":
        #   a: T=50 C=12 (lowest), b: T=40 C=10, c: T=30 C=10 ; U = 0.823.
        # Published: task a misses its deadline at t=50. RTA: 12 -> 32 -> 42 -> 52 > 50.
        r = rta.analyze({"tasks": [task("a", 1, 50, 12), task("b", 2, 40, 10), task("c", 3, 30, 10)]})
        self.assertFalse(r["ll_holds"])
        self.assertEqual(ok(r), {"c": True, "b": True, "a": False})
        self.assertEqual(resp(r)["b"], 20)
        self.assertGreater(resp(r)["a"], 50)
        self.assertFalse(r["schedulable"])

    def test_deadline_less_than_period(self):
        # Buttazzo, "Hard Real-Time Computing Systems" (3rd ed.), §4.5
        # deadline-monotonic response-time example:
        #   t1 C=1 T=4 D=3, t2 C=1 T=5 D=4, t3 C=2 T=6 D=5, t4 C=1 T=11 D=10
        #   published R4 = 10 = D4 (schedulable, zero slack).
        r = rta.analyze({"tasks": [task("t1", 4, 4, 1, D=3), task("t2", 3, 5, 1, D=4),
                                   task("t3", 2, 6, 2, D=5), task("t4", 1, 11, 1, D=10)]})
        self.assertEqual(resp(r), {"t1": 1, "t2": 2, "t3": 4, "t4": 10})
        self.assertTrue(r["schedulable"])
        # Shrinking D4 below the response time must flip it to unschedulable.
        r = rta.analyze({"tasks": [task("t1", 4, 4, 1, D=3), task("t2", 3, 5, 1, D=4),
                                   task("t3", 2, 6, 2, D=5), task("t4", 1, 11, 1, D=9)]})
        self.assertFalse(ok(r)["t4"])

    def test_blocking(self):
        # BW_RTA plus blocking (hand-derived):
        #   b with B=2: 5 -> 3+2+ceil(5/7)*3 = 8 -> 3+2+ceil(8/7)*3 = 11 -> 11.
        #   c with B=1 (zero slack before): 6 -> 12 -> 15 -> 21 > 20 => miss.
        s = [dict(t) for t in BW_RTA]
        s[1]["blocking_us"] = 2
        self.assertEqual(resp(rta.analyze({"tasks": s}))["b"], 11)
        s = [dict(t) for t in BW_RTA]
        s[2]["blocking_us"] = 1
        r = rta.analyze({"tasks": s})
        self.assertFalse(ok(r)["c"])
        self.assertEqual(resp(r)["c"], 21)

    def test_release_jitter(self):
        # Burns & Wellings jitter form: w uses ceil((w + J_j)/T_j), then R_i = w_i + J_i.
        #   hi: T=10 C=2 J=3 -> R = 2 + 3 = 5
        #   lo: T=20 C=7     -> 7 -> 7+ceil(10/10)*2 = 9 -> 7+ceil(12/10)*2 = 11 -> 11
        #   (without jitter lo would be 9: 7 -> 9 -> 9).
        r = rta.analyze({"tasks": [task("hi", 2, 10, 2, J=3), task("lo", 1, 20, 7)]})
        self.assertEqual(resp(r), {"hi": 5, "lo": 11})
        r = rta.analyze({"tasks": [task("hi", 2, 10, 2), task("lo", 1, 20, 7)]})
        self.assertEqual(resp(r), {"hi": 2, "lo": 9})

    def test_isr_interference(self):
        # ISR T=5 C=1 preempts every task, including the highest-priority one.
        #   hi: C=1 -> 1+ceil(1/5) = 2 -> 2
        #   lo: C=6 -> 6+ceil(6/10)*1+ceil(6/5)*1 = 9 -> 6+1+2 = 9
        r = rta.analyze({"tasks": [task("hi", 2, 10, 1), task("lo", 1, 20, 6)],
                         "isrs": [{"name": "irq", "period_us": 5, "wcet_us": 1}]})
        self.assertEqual(resp(r), {"hi": 2, "lo": 9})
        self.assertAlmostEqual(r["utilization"], 0.2 + 0.1 + 0.3)

    def test_equal_priority_interferes(self):
        # FreeRTOS time-slices equal priorities, so each sees the other as interference.
        r = rta.analyze({"tasks": [task("x", 1, 10, 2), task("y", 1, 10, 3)]})
        self.assertEqual(resp(r), {"x": 5, "y": 5})

    def test_cli_observed_and_optional(self):
        with tempfile.TemporaryDirectory() as d:
            ts, obs, out = (os.path.join(d, n) for n in ("ts.json", "obs.json", "out.json"))
            with open(ts, "w") as f:
                json.dump({"tasks": BW_RTA + [dict(task("opt", 9, 7, 3), optional=True)]}, f)
            for observed, rc in (({"a": 3, "c": 10}, 0), ({"c": 21}, 1)):
                with open(obs, "w") as f:
                    json.dump(observed, f)
                with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                    self.assertEqual(rta.main([ts, "--observed", obs, "--json", out]), rc)
            with open(out) as f:
                c = {t["name"]: t for t in json.load(f)["tasks"]}["c"]
            self.assertTrue(c["unsafe"])
            self.assertAlmostEqual(c["ratio"], 20 / 21)
            # The optional task makes the set unschedulable -> exit 2 only when included.
            with redirect_stdout(StringIO()):
                self.assertEqual(rta.main([ts, "--include-optional"]), 2)

    def test_nominal_taskset(self):
        with open(os.path.join(HERE, "tasksets", "telemetry_nominal.json")) as f:
            ts = json.load(f)
        self.assertIn("PLACEHOLDER", ts["_comment"])
        names = {t["name"] for t in rta.analyze(ts)["tasks"]}
        self.assertNotIn("imu_task", names)
        r = rta.analyze(ts, include_optional=True)
        self.assertEqual(r["tasks"][0]["name"], "imu_task")
        self.assertTrue(r["schedulable"])


if __name__ == "__main__":
    unittest.main()
