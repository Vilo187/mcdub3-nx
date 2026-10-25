"""loop_barrier: back edges get RECOMP_LOOP_BARRIER(), nothing else changes."""
import unittest

from tools.recomp.loop_barrier import rewrite


class LoopBarrierTest(unittest.TestCase):
    def test_back_edge_marked(self):
        code = ("loc_00001000: ;\n"
                "    MEM32(edi + 0x600100) = ecx;\n"
                "    if (TEST_NZ(_fa, _fb)) goto loc_00001000; /* jne */\n")
        out, n = rewrite(code)
        self.assertEqual(n, 1)
        self.assertIn("{ RECOMP_LOOP_BARRIER(); goto loc_00001000; }", out)

    def test_forward_and_spin_left_alone(self):
        code = ("loc_00001000: ;\n"
                "    if (x) goto loc_00002000;\n"
                "    if (y) { RECOMP_SPIN_HINT(); goto loc_00001000; }\n"
                "loc_00002000: ;\n")
        out, n = rewrite(code)
        self.assertEqual(n, 0)
        self.assertEqual(out, code)


if __name__ == "__main__":
    unittest.main()
