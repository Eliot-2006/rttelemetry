import unittest

from telemetry.unwrap import Unwrapper


class UnwrapTest(unittest.TestCase):
    def test_crosses_u32_wrap(self):
        u = Unwrapper(32)
        raw = [(4294960000 + 1000 * i) & 0xFFFFFFFF for i in range(20)]
        self.assertEqual([u(r) for r in raw], [4294960000 + 1000 * i for i in range(20)])

    def test_multiple_wraps_and_u16(self):
        u = Unwrapper(16)
        vals = [u(i & 0xFFFF) for i in range(0, 5 * 65536, 1000)]
        self.assertEqual(vals, list(range(0, 5 * 65536, 1000)))

    def test_small_backward_step_is_not_a_wrap(self):
        u = Unwrapper(32)
        self.assertEqual([u(x) for x in (10, 5, 20)], [10, 5, 20])
        u = Unwrapper(32)
        self.assertEqual([u(x) for x in (0xFFFFFFF0, 3, 0xFFFFFFFE, 9)],
                         [0xFFFFFFF0, 0x100000003, 0xFFFFFFFE, 0x100000009])


if __name__ == "__main__":
    unittest.main()
