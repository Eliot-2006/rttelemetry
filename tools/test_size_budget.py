import contextlib
import io
import json
import os
import tempfile
import unittest

import size_budget

SAMPLE = """\
build/fw-debug/src/telemetry.elf  :
section            size        addr
.isr_vector         392   134217728
.text              5936   134218120
.rodata             520   134224056
.ARM                  8   134224576
.init_array           4   134224584
.fini_array           4   134224588
.data                 4   536870912
.bss                272   536870916
.noinit               4   536871188
.msp               2048   268435456
.ccm               1024   268437504
.ARM.attributes      48           0
.debug_info       17370           0
Total             49780
"""


class SizeBudgetTest(unittest.TestCase):
    def test_usage_by_region(self):
        used = size_budget.usage(size_budget.parse(SAMPLE))
        self.assertEqual(used["flash"], 392 + 5936 + 520 + 8 + 4 + 4 + 4)
        self.assertEqual(used["sram"], 4 + 272 + 4)
        self.assertEqual(used["ccm"], 2048 + 1024)  # MSP stack counts toward CCM

    def run_main(self, budgets):
        with tempfile.TemporaryDirectory() as d:
            sizes = os.path.join(d, "telemetry.size.txt")
            bfile = os.path.join(d, "budgets.json")
            with open(sizes, "w") as f:
                f.write(SAMPLE)
            with open(bfile, "w") as f:
                json.dump(budgets, f)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = size_budget.main(["--budgets", bfile, sizes])
            return rc, out.getvalue()

    def test_within_budget(self):
        rc, out = self.run_main({"default": {"flash": 65536, "sram": 49152, "ccm": 16384}})
        self.assertEqual(rc, 0)
        self.assertNotIn("OVER", out)

    def test_over_budget_fails(self):
        rc, out = self.run_main({"default": {"flash": 65536, "sram": 279, "ccm": 16384}})
        self.assertEqual(rc, 1)
        self.assertIn("OVER BUDGET", out)

    def test_per_image_override(self):
        rc, _ = self.run_main({"default": {"flash": 65536, "sram": 49152, "ccm": 16384},
                               "telemetry": {"ccm": 1000}})
        self.assertEqual(rc, 1)

    def test_shipped_budgets_match_prd(self):
        path = os.path.join(os.path.dirname(size_budget.__file__), "budgets.json")
        with open(path) as f:
            self.assertEqual(json.load(f)["default"], {"flash": 65536, "sram": 49152, "ccm": 16384})


if __name__ == "__main__":
    unittest.main()
