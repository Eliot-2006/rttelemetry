"""Loss accounting (PRD §18, NFR-INT-2).

Device conservation: triggered = delivered + dropped_overrun + dropped_ring + dropped_txpool + in_flight,
with 0 <= in_flight <= ring_capacity + batch + pool * batch. Checked as a bound at every HEALTH frame;
after SET_RATE 0 (stop + flush) in_flight must be exactly 0 and the host must have received exactly
`delivered` samples. Assumes capture starts at boot (sample seq 0); a BOOT frame resets the state.
"""
from .unwrap import Unwrapper

U32 = 1 << 32


def in_flight(h: dict) -> int:
    """triggered - delivered - drops, in u32 arithmetic (a negative result shows up as huge)."""
    drops = h["dropped_overrun"] + h["dropped_ring"] + h["dropped_txpool"]
    return (h["triggered"] - h["delivered"] - drops) % U32


class Accounting:
    def __init__(self, batch: int = 10, pool: int = 4):
        self.batch, self.pool = batch, pool
        self.reset()

    def reset(self):
        self.received = 0         # samples received by the host
        self.missing = 0          # seq numbers never seen (device drops + link losses)
        self.order_errors = 0     # duplicate or backwards seq
        self.last_health = None
        self.violations = []      # failed bound checks
        self._seq = Unwrapper(32)
        self._last_seq = -1

    def on_frame(self, f: dict):
        name = f["name"]
        if name == "BOOT":
            self.reset()
        elif name == "SAMPLE_BATCH":
            for s in f["payload"]["samples"]:
                seq = self._seq(s["seq"])
                if seq <= self._last_seq:
                    self.order_errors += 1
                    continue
                self.missing += seq - self._last_seq - 1
                self._last_seq = seq
                self.received += 1
        elif name == "HEALTH":
            self.last_health = f["payload"]
            r = self.check_bound(f["payload"])
            if not r["ok"]:
                self.violations.append(r)

    def check_bound(self, h: dict) -> dict:
        bound = h["ring_capacity"] + self.batch + self.pool * self.batch
        n = in_flight(h)
        return {"ok": n <= bound, "in_flight": n, "bound": bound, "triggered": h["triggered"]}

    def check_exact(self, h: dict = None) -> dict:
        """Call with the first HEALTH after a stop + flush (defaults to the last one seen)."""
        h = h or self.last_health
        if h is None:
            return {"ok": False, "error": "no HEALTH frame"}
        n = in_flight(h)
        drops = h["dropped_overrun"] + h["dropped_ring"] + h["dropped_txpool"]
        return {
            "ok": n == 0 and self.received == h["delivered"] and self.order_errors == 0,
            "triggered": h["triggered"], "delivered": h["delivered"], "dropped": drops, "in_flight": n,
            "received": self.received, "link_lost": h["delivered"] - self.received,
            "missing_seq": self.missing, "order_errors": self.order_errors,
        }
