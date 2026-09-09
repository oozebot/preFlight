import unittest
from dataclasses import replace
from audit_gcode import audit
from pressure_equalizer_regression import compare_interlocking


class PressureEqualizerRegressionTests(unittest.TestCase):
    def test_requires_geometry_e_and_feed_invariance(self):
        original = audit('M83\n;TYPE:Interlocking perimeter\nG1 X1 E.01 F19950'.splitlines())
        self.assertTrue(compare_interlocking(original, original, 8)['passed'])
        slowed = [replace(original[0], feed=100, flow=original[0].flow * 100 / 19950)]
        self.assertTrue(compare_interlocking(original, slowed, 8)['passed'])
        for changed in [[], [replace(original[0], endpoint=(2, 0, 0))],
                        [replace(original[0], length=2)],
                        [replace(original[0], delta_e=.02)], [replace(original[0], tool=1)],
                        [replace(original[0], feed=19960)], [replace(original[0], flow=8.00157)]]:
            with self.subTest(changed=changed):
                self.assertFalse(compare_interlocking(original, changed, 8)['passed'])

    def test_does_not_claim_empty_or_non_interlocking_coverage(self):
        moves = audit('M83\n;TYPE:Perimeter\nG1 X1 E.01 F100'.splitlines())
        self.assertFalse(compare_interlocking([], [], 8)['passed'])
        self.assertFalse(compare_interlocking(moves, moves, 8)['passed'])


if __name__ == '__main__':
    unittest.main()
