"""make demo dashboard rows: drop highlighting and missing-HEALTH detection (no Renode needed)."""
import unittest

from telemetry.demo import Dash


def health(up_ms, delivered, ring=0, pool=0, frames_dropped=0):
    return {"uptime_ms": up_ms, "delivered": delivered, "frames_sent": delivered // 10, "sample_rate_hz": 1000,
            "ring_occ": 0, "ring_hwm": 1, "dropped_overrun": 0, "dropped_ring": ring, "dropped_txpool": pool,
            "frames_dropped": frames_dropped, "l1_hist": [0, 0, 0, 0, 1, 0, 0, 0], "cpu_load_permille": 51,
            "stack_free_min_words": [250] * 5 + [100]}


class DashRows(unittest.TestCase):
    def setUp(self):
        self.d = Dash([])
        self.d.color, self.d.boots = False, [{}]
        self.d.out = lambda kind, text, spans=(): self.d.transcript.append(
            {"kind": kind, "text": text, "spans": list(spans)})

    def test_rising_drop_counters_are_highlighted(self):
        self.d.row(health(1000, 1000))
        self.d.row(health(2000, 2000, ring=472, pool=97))
        row = self.d.transcript[-1]
        self.assertEqual([row["text"][a:b] for a, b in row["spans"]], ["472", "97"])
        self.assertEqual(self.d.transcript[0]["spans"], [])  # first row of a boot: nothing to compare

    def test_missing_health_frames_are_reported(self):
        self.d.row(health(17000, 17000, frames_dropped=0))
        self.d.row(health(22000, 19000, frames_dropped=50))
        self.assertEqual(self.d.transcript[1]["kind"], "quiet")
        self.assertIn("4 HEALTH frame(s) missing; frames were dropped", self.d.transcript[1]["text"])


if __name__ == "__main__":
    unittest.main()
