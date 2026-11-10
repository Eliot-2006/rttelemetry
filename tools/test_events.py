import unittest

from telemetry import events


class EventsTest(unittest.TestCase):
    def test_header_parsed(self):
        ids = sorted(events.EVENTS)
        self.assertEqual(ids, list(range(1, 18)))
        self.assertEqual(events.name(1), "BOOT")
        self.assertEqual(events.name(3), "RING_OVF_ON")
        self.assertEqual(events.name(17), "IMU_TIMEOUT")
        self.assertEqual(events.name(999), "UNKNOWN_999")
        self.assertTrue(all(text for _, text in events.EVENTS.values()))

    def test_duplicate_rejected(self):
        import tempfile, pathlib

        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "e.h"
            p.write_text("  EVT_A = 1, /* a */\n  EVT_B = 1, /* b */\n")
            with self.assertRaises(ValueError):
                events.load(p)


if __name__ == "__main__":
    unittest.main()
