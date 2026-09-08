import math
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from audit_gcode import audit


class AuditTests(unittest.TestCase):
    def parse(self, code):
        return audit(code.splitlines())

    def test_explicit_zero_xyz_deposition_uses_extruder_distance(self):
        for mode in ('M83', 'M82'):
            moves = self.parse(mode + '\nG1 X0 Y0 E0.00001 F6000')
            self.assertEqual(len(moves), 1)
            self.assertAlmostEqual(moves[0].flow, math.pi * 1.75**2 / 4 * 100)
            self.assertEqual(moves[0].flow_lower_bound, moves[0].flow)
        # Standalone unretraction is not a generated Cartesian deposition path.
        self.assertEqual(self.parse('M83\nG1 E0.1 F6000'), [])

    def test_relative_e_modal_feed_and_numbered_layers(self):
        moves = self.parse('M83\n;LAYER_CHANGE:1\n;TYPE:Interlocking perimeter\nG1 F600\nG1 X10 E1\nG1 X20 E1')
        self.assertEqual(len(moves), 2)
        self.assertEqual(moves[1].feed, 600)
        self.assertEqual(moves[1].layer, 1)
        self.assertAlmostEqual(moves[1].flow, math.pi * 1.75**2 / 4)

    def test_absolute_e_reset_retraction_and_restore(self):
        moves = self.parse('M82\nG1 F600 X10 E1\nG1 E0\nG1 E1\nG92 E0\nG1 X20 E1')
        self.assertEqual([m.delta_e for m in moves], [1, 1])
        self.assertEqual([m.length for m in moves], [10, 10])

    def test_relative_xyz_and_g92(self):
        moves = self.parse('M83\nG92 X100\nG91\nG1 X10 E1 F600\nG1 X10 E1')
        self.assertEqual(moves[-1].endpoint, (120, 0, 0))
        self.assertEqual(moves[-1].length, 10)

    def test_half_arc_and_full_circle(self):
        moves = self.parse('M83\nG1 X10\nG3 X-10 I-10 J0 E1 F600\nG2 X-10 I10 J0 E1')
        self.assertAlmostEqual(moves[0].length, math.pi*10)
        self.assertAlmostEqual(moves[1].length, math.pi*20)

    def test_tiny_segment_not_discarded(self):
        moves = self.parse('M83\nG1 X0.001 E0.001 F600')
        self.assertEqual(len(moves), 1)
        self.assertGreater(moves[0].flow_lower_bound, 20)

    def test_rounding_bound_relative_vs_absolute(self):
        rel = self.parse('M83\nG1 X10 E1 F600')[0]
        abs_move = self.parse('M82\nG1 X10 E1 F600')[0]
        self.assertGreater(rel.flow_lower_bound, abs_move.flow_lower_bound)

    def test_unsupported_arc_fails_closed(self):
        with self.assertRaises(ValueError):
            self.parse('G2 X1 R1 E1 F600')

    def test_unsupported_units_fail_closed(self):
        with self.assertRaises(ValueError):
            self.parse('G20\nG1 X1 E1 F600')

    def test_numbered_layer_is_used_verbatim(self):
        self.assertEqual(self.parse(';LAYER_CHANGE:17\nM83\nG1 X1 E1 F600')[0].layer, 17)

    def test_missing_or_invalid_feed_fails_closed(self):
        for suffix in ('', ' F0', ' F-600'):
            with self.subTest(suffix=suffix), self.assertRaisesRegex(ValueError, 'feed'):
                self.parse('M83\nG1 X1 E1' + suffix)

    def test_unsupported_modes_and_overrides_fail_closed(self):
        for command in ('G93', 'G95', 'G18', 'G19', 'M220 S50', 'M221 S50',
                        'M200 D1.75', 'G90.1', 'SET_GCODE_OFFSET X1',
                        'ACTIVATE_EXTRUDER EXTRUDER=extruder1'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                self.parse(command + '\nM83\nG1 X1 E1 F600')

    def test_extra_e_precision_cannot_silently_pass(self):
        with self.assertRaisesRegex(ValueError, 'precision'):
            self.parse('M83\nG1 X.000001 E.000001 F600')

    def test_declared_six_decimal_relative_e_is_audited(self):
        move = audit('M83\nG1 X.000001 E.000001 F600'.splitlines(), e_decimals=6)[0]
        self.assertGreater(move.flow_lower_bound, 8)

    def test_unresolved_e_rounding_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'precision'):
            audit('M82\nG1 X.000001 E.000001 F600'.splitlines(), e_decimals=6)

    def test_unknown_tool_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'tool'):
            self.parse('M83\nT1\nG1 X1 E1 F600')

    def test_tool_switch_requires_explicit_e_model(self):
        with self.assertRaisesRegex(ValueError, 'E coordinate'):
            audit('T1'.splitlines(), tool_diameters={0: 1.75, 1: 2.85})

    def test_tool_diameter_and_limit_follow_selected_tool(self):
        moves = audit('M83\nG1 X10 E1 F600\nT1\nG1 X20 E1'.splitlines(),
                      tool_diameters={0: 1.75, 1: 2.85}, tool_limits={0: 8, 1: 4},
                      tool_e_mode='reset')
        self.assertEqual([m.tool for m in moves], [0, 1])
        self.assertEqual([m.limit for m in moves], [8, 4])
        self.assertAlmostEqual(moves[1].flow, math.pi * 2.85**2 / 4)

    def test_missing_active_tool_limit_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'limit'):
            audit('M83\nT1\nG1 X1 E1 F600'.splitlines(),
                  tool_diameters={0: 1.75, 1: 2.85}, tool_limits={0: 8}, tool_e_mode='reset')

    def test_absolute_tool_e_models_are_explicit(self):
        code = 'M82\nG1 X10 E1 F600\nT1\nG1 X20 E2\nT0\nG1 X30 E3'
        for mode, expected in [('global', [1, 1, 1]), ('per-tool', [1, 2, 2]),
                               ('reset', [1, 2, 3])]:
            with self.subTest(mode=mode):
                moves = audit(code.splitlines(), tool_diameters={0: 1.75, 1: 1.75},
                              tool_e_mode=mode)
                self.assertEqual([m.delta_e for m in moves], expected)

    def test_klipper_fixture_explicit_reset_and_g92(self):
        code = 'M82\nG1 X10 E1 F600\nT1\nG92 E0\nG1 X20 E1\nT0\nG92 E0\nG1 X30 E1'
        moves = audit(code.splitlines(), tool_diameters={0: 1.75, 1: 1.75}, tool_e_mode='reset')
        self.assertEqual([m.delta_e for m in moves], [1, 1, 1])

    def test_invalid_arc_geometry_fails_closed(self):
        for arc in ('G2 X10 I0 J0 E1 F600', 'G3 X10 I1 J0 E1 F600',
                    'G2 X0 I1 P2 E1 F600', 'G2 X0 I1 K1 E1 F600'):
            with self.subTest(arc=arc), self.assertRaisesRegex(ValueError, 'arc'):
                self.parse('M83\n' + arc)

    def test_malformed_motion_cannot_disappear(self):
        for move in ('G1 XNaN E1 F600', 'G1 X1 E1 F600 F0',
                     'G1 X1 E1 F600 G93', 'G1 X1 E1 F1e3'):
            with self.subTest(move=move), self.assertRaises(ValueError):
                self.parse('M83\n' + move)

    def test_invalid_configuration_fails_closed(self):
        for kwargs in ({'diameter': 0}, {'diameter': float('nan')}, {'e_decimals': -1},
                       {'tool_diameters': {1: 1.75}}, {'tool_limits': {0: -1}},
                       {'tool_limits': {0: float('inf')}}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                audit(['M83', 'G1 X1 E1 F600'], **kwargs)

    def test_supported_modes_and_positive_moving_unretraction(self):
        moves = self.parse('G21\nG94\nG17\nG91.1\nM83\nG1 E-1\nG1 X1 E1 F600')
        self.assertEqual(len(moves), 1)
        self.assertGreater(moves[0].flow, 20)

    def run_cli(self, code, *options):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'fixture.gcode'
            fixture.write_text(code, encoding='utf-8')
            return subprocess.run([sys.executable, str(Path(__file__).with_name('audit_gcode.py')),
                                   str(fixture), *options], capture_output=True, text=True)

    def test_cli_tool_cap_overrides_scalar_cap(self):
        result = self.run_cli(';TYPE:Interlocking perimeter\nM83\nT1\nG1 X10 E1 F600',
                              '--limit', '8', '--tool-diameter', '1=2.85',
                              '--tool-limit', '1=4', '--tool-e-mode', 'reset')
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['violations'], 1)
        self.assertEqual(report['examples'][0]['tool'], 1)
        self.assertEqual(report['examples'][0]['limit'], 4)

    def test_cli_invalid_scalar_limits_cannot_pass(self):
        for limit in ('nan', 'inf', '-1'):
            with self.subTest(limit=limit):
                result = self.run_cli(';TYPE:Interlocking perimeter\nM83\nG1 X10 E1 F600',
                                      '--limit', limit)
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_cli_precision_is_explicit(self):
        result = self.run_cli(';TYPE:Interlocking perimeter\nM83\nG1 X.000001 E.000001 F600',
                              '--limit', '8', '--e-decimals', '6')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(json.loads(result.stdout)['violations'], 1)

    def test_numeric_overflow_cannot_return_zero_flow(self):
        huge = '9' * 308
        with self.assertRaisesRegex(ValueError, 'numeric'):
            self.parse(f'M83\nG92 X-{huge}\nG1 X{huge} E1 F600')

    def test_near_closed_collinear_arc_is_not_full_circle(self):
        with self.assertRaisesRegex(ValueError, 'arc'):
            self.parse('M83\nG2 X.0000001 I1 E1 F600')

    def test_e_coordinates_must_be_resolvable_at_declared_precision(self):
        with self.assertRaisesRegex(ValueError, 'precision'):
            self.parse('M82\nG92 E10000000000000000\nG1 X1 E10000000000000000.00001 F600')

    def test_klipper_g91_makes_m82_extrusion_relative(self):
        code = 'M82\nG91\n;TYPE:Interlocking perimeter\nG1 X10 E1 F600\nG1 X10 E1 F6000'
        moves = self.parse(code)
        self.assertEqual([m.delta_e for m in moves], [1, 1])
        self.assertGreater(moves[1].flow_lower_bound, 8)
        relative = self.parse(code.replace('M82', 'M83'))
        self.assertEqual(moves[1].flow_lower_bound, relative[1].flow_lower_bound)
        result = self.run_cli(code, '--limit', '8')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(json.loads(result.stdout)['violations'], 1)

    def test_klipper_g90_restores_absolute_e_only_with_m82(self):
        code = 'M82\nG91\nG1 X10 E1 F600\nG1 X10 E1\nG90\nG1 X30 E3'
        self.assertEqual([m.delta_e for m in self.parse(code)], [1, 1, 1])

    def test_klipper_g90_preserves_m83_relative_extrusion(self):
        code = 'M83\nG91\nG1 X10 E1 F600\nG90\nG1 X20 E1'
        self.assertEqual([m.delta_e for m in self.parse(code)], [1, 1])


if __name__ == '__main__':
    unittest.main()
