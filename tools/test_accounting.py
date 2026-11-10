"""UT-ACCT: conservation checker on synthetic streams with gaps, wraps, and drops."""
import unittest

from telemetry.accounting import Accounting, in_flight


def batch(seqs):
    return {"name": "SAMPLE_BATCH", "payload": {"samples": [{"seq": s & 0xFFFFFFFF} for s in seqs]}}


def health(triggered, delivered, overrun=0, ring=0, txpool=0, cap=128):
    return {"name": "HEALTH", "payload": {"triggered": triggered & 0xFFFFFFFF, "delivered": delivered & 0xFFFFFFFF,
                                          "dropped_overrun": overrun, "dropped_ring": ring,
                                          "dropped_txpool": txpool, "ring_capacity": cap}}


class AccountingTest(unittest.TestCase):
    def test_clean_run_exact_after_flush(self):
        a = Accounting()
        for b in range(10):
            a.on_frame(batch(range(b * 10, b * 10 + 10)))
        a.on_frame(health(100, 100))
        r = a.check_exact()
        self.assertTrue(r["ok"], r)
        self.assertEqual((r["received"], r["missing_seq"], r["link_lost"]), (100, 0, 0))

    def test_device_drops_appear_as_gaps(self):
        a = Accounting()
        a.on_frame(batch(range(0, 10)))
        a.on_frame(batch(range(25, 35)))  # 15 dropped: 5 overrun, 10 txpool
        a.on_frame(health(35, 20, overrun=5, txpool=10))
        r = a.check_exact()
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["missing_seq"], r["dropped"])

    def test_link_loss_fails_exact(self):
        a = Accounting()
        a.on_frame(batch(range(0, 10)))  # frame 10..19 lost on the link
        a.on_frame(batch(range(20, 30)))
        a.on_frame(health(30, 30))
        r = a.check_exact()
        self.assertFalse(r["ok"])
        self.assertEqual(r["link_lost"], 10)

    def test_in_flight_not_zero_fails_exact(self):
        a = Accounting()
        a.on_frame(batch(range(10)))
        a.on_frame(health(15, 10))
        self.assertFalse(a.check_exact()["ok"])
        self.assertEqual(a.violations, [])  # within the bound though

    def test_bound_check_at_health(self):
        a = Accounting(batch=10, pool=4)
        a.on_frame(health(1000, 1000 - 178))  # in_flight == 128 + 10 + 40: allowed
        self.assertEqual(a.violations, [])
        a.on_frame(health(1000, 1000 - 179))
        self.assertEqual(len(a.violations), 1)
        a.on_frame(health(1000, 1001))  # delivered > triggered: negative in_flight is a violation
        self.assertEqual(len(a.violations), 2)
        self.assertEqual(in_flight(health(5, 6)["payload"]), 2**32 - 1)

    def test_seq_wrap_and_counter_wrap(self):
        a = Accounting()
        a._last_seq = -1
        start = 2**32 - 5
        a.on_frame(batch(range(start, start + 10)))
        self.assertEqual(a.order_errors, 0)
        self.assertEqual(a.received, 10)
        # u32 counter wrap on the device: triggered wrapped, delivered not yet
        self.assertEqual(in_flight(health(2**32 + 3, 2**32 - 2)["payload"]), 5)

    def test_duplicates_and_reboot(self):
        a = Accounting()
        a.on_frame(batch(range(10)))
        a.on_frame(batch(range(5, 10)))
        self.assertEqual(a.order_errors, 5)
        a.on_frame({"name": "BOOT", "payload": {}})
        self.assertEqual((a.received, a.order_errors), (0, 0))


if __name__ == "__main__":
    unittest.main()
