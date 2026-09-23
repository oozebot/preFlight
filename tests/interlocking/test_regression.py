"""Runner contract tests use synthetic G-code and a fake slicer process."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


class RegressionTests(unittest.TestCase):
    def test_timed_fan_split_requires_collinear_motion_and_proportional_exact_e(self):
        prefix = 'G90\nM83\nG1 X0 Y0 F3000\nG1 F600\n'
        baseline = prefix + 'G1 X4 Y0 E.4\nM106 S255 ; Ramping 0.500s before Bridge infill\nG1 X10 Y0 E.6\n'
        candidate = prefix + 'G1 X6 Y0 E.6\nM106 S255 ; Ramping 0.500s before Bridge infill\nG1 X10 Y0 E.4\n'
        signature = lambda text: [(e['command'], e['words']) for e in self.runner.ramp_split_trace(text)[0]]
        self.assertEqual(signature(baseline), signature(candidate))
        self.assertEqual(len(self.runner.ramp_split_trace(candidate)[1]), 1)
        for bad in (candidate.replace('X6 Y0', 'X6 Y.01'),
                    candidate.replace('E.6', 'E.7').replace('E.4', 'E.3'),
                    candidate.replace('X10', 'X11'),
                    candidate.replace('X6', 'X12'),
                    candidate.replace('Y0 E.4', 'Y0 E.4 F500'),
                    candidate.replace(' ; Ramping 0.500s before Bridge infill', ''),
                    candidate.replace('M106', 'M400\nM106'),
                    candidate.replace('M83', 'M82')):
            with self.subTest(bad=bad):
                self.assertNotEqual(signature(baseline), signature(bad))

    def test_summary_rejects_small_commanded_overshoot(self):
        for mode in ('M82', 'M83'):
            for feed, expected in ((19950, 0), (19960, 1)):
                with self.subTest(mode=mode, feed=feed):
                    moves = self.runner.audit(
                        f'G90\n{mode}\n;TYPE:Interlocking perimeter\nG1 X1 E0.01000 F{feed}'.splitlines())
                    summary = self.runner._audit_summary(moves, 8)
                    self.assertEqual(summary['violations'], expected)
                    self.assertEqual(self.runner._audit_summary(moves, 0)['violations'], 0)

    def test_cooling_minimum_speed_cannot_override_volumetric_cap_case(self):
        cases = {c.name: c for c in self.runner.build_cases()}
        config = self.runner.case_config(cases['cooling-min-speed-above-cap'])
        self.assertEqual(config['cooling'], '1')
        self.assertEqual(config['filament_max_volumetric_flow'], '4')
        self.assertEqual(config['min_print_speed'], '100')
        self.assertEqual(config['slowdown_below_layer_time'], '120')
        forced = self.runner.case_config(cases['cooling-forced-slowdown'])
        self.assertEqual(forced['cooling'], '1')
        self.assertEqual(forced['min_print_speed'], '10')
        self.assertEqual(forced['slowdown_below_layer_time'], '120')

    def test_timing_report_separates_estimate_and_commanded_deposition(self):
        config = self.runner.case_config(self.case())
        text = self.fixture(config, extra='; estimated printing time (normal mode) = 1h 2m 3s')
        result = self.runner.analyze_pair(text, text, self.case(require_time_metadata=True), config)
        self.assertTrue(result['passed'], result)
        timing = result['candidate']['timing_and_cooling']
        self.assertEqual(timing['slicer_estimates_seconds']['estimated printing time'], 3723)
        self.assertAlmostEqual(timing['layers']['17']['commanded_deposition_seconds'], 1.5)
        missing = self.fixture(config)
        result = self.runner.analyze_pair(missing, missing, self.case(require_time_metadata=True), config)
        self.assertIn('candidate_missing_slicer_time_estimate', result['failures'])

    def test_selected_tool_limit_and_diameter_are_not_tool_zero(self):
        cases = {c.name: c for c in self.runner.build_cases()}
        config = self.runner.case_config(cases['second-tool-limit4'])
        self.assertEqual(self.runner._effective_limit(config), 4)
        self.assertEqual(self.runner._effective_limit(config, 0), 0)
        self.assertEqual(self.runner._tool_value(config, 'filament_diameter', 1), 2.85)
        text = 'T1\n' + self.fixture(config, feed=100,
                                   extra='; estimated printing time (normal mode) = 1m')
        result = self.runner.analyze_pair(text, text, cases['second-tool-limit4'], config)
        self.assertTrue(result['passed'], result)
        self.assertEqual(result['candidate']['interlocking_tools'], [1])
        wrong = self.fixture(config)
        result = self.runner.analyze_pair(wrong, wrong, cases['second-tool-limit4'], config)
        self.assertIn('candidate_unexpected_interlocking_tools', result['failures'])

    def test_emission_coverage_requires_deposition_not_empty_markers(self):
        config = self.runner.case_config(self.case())
        text = self.fixture(config, extra=';Interlocking: E200% F50.0%')
        result = self.runner.analyze_pair(text, text, self.case(required_coverage=('flow_200',)), config)
        self.assertIn('candidate_missing_coverage_flow_200', result['failures'])
        text = self.fixture(config, extra=';Interlocking: E200% F50.0%\nG1 X25 Y1 Z0.2 E1 F600 ; to boundary')
        result = self.runner.analyze_pair(text, text, self.case(required_coverage=('flow_200', 'xyz', 'boundary')), config)
        self.assertTrue(result['passed'], result)
        self.assertEqual(result['candidate']['emission_coverage']['flow_200'], 1)

    def test_known_rejections_require_exact_error_exit_and_no_gcode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / 'slice.log'
            gcode = root / 'fixture.gcode'
            expected = self.runner.AUTO_FLOW_ERROR
            log.write_text(expected + '\n', encoding='utf-8')
            item = dict(returncode=0, log=str(log), gcode=str(gcode), error=self.runner.MISSING_GCODE_ERROR)
            result = self.runner.analyze_rejection({'baseline': item, 'candidate': item}, expected)
            self.assertTrue(result['passed'])
            self.assertEqual(result['gcode_audit'], 'not_applicable_no_export')
            for bad in (dict(item, returncode=1), dict(item, returncode=2),
                        dict(item, error='timeout')):
                self.assertFalse(self.runner.analyze_rejection({'candidate': bad}, expected)['passed'])
            log.write_text('Different error', encoding='utf-8')
            self.assertFalse(self.runner.analyze_rejection({'candidate': item}, expected)['passed'])
            log.write_text(expected, encoding='utf-8')
            gcode.touch()
            self.assertFalse(self.runner.analyze_rejection({'candidate': item}, expected)['passed'])
        rejected = [c for c in self.runner.build_cases() if c.expected_rejection]
        self.assertEqual({c.name for c in rejected}, {'disabled-auto', 'auto-print8'})

    def test_curved_cases_report_disabled_arc_contract_not_arc_coverage(self):
        import regression
        curved = [c for c in regression.build_cases() if c.model == 'cylinder']
        self.assertGreaterEqual(len(curved), 3)
        for case in curved:
            self.assertEqual(regression.case_config(case)['arc_fitting'], 'emit_center')
        config = regression.case_config(curved[0])
        text = self.fixture(config, extra='; estimated printing time (normal mode) = 1m')
        result = regression.analyze_pair(text, text, curved[0], config)
        self.assertTrue(result['passed'], result)
        self.assertIn('disabled_at_f74dc69', result['candidate']['arc_coverage_contract'])
        arc = self.fixture(config, extra='G2 X25 Y5 I5 J0 E1 F600')
        result = regression.analyze_pair(arc, arc, curved[0], config)
        self.assertIn('candidate_unexpected_arc_activation', result['failures'])
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('regression'),
                             'The bounded regression runner must exist')
        import regression
        self.runner = regression

    def fixture(self, config, feed=600, *, interlock=True, second='X20 E1', extra=''):
        role = 'Interlocking perimeter' if interlock else 'Perimeter'
        return '\n'.join(['G90', 'M83', ';LAYER_CHANGE:17', ';TYPE:' + role,
                          f'G1 X10 E1 F{feed}', 'G1 E-1 F1200', 'G1 X15 F3000',
                          'G1 E1 F1200', f'G1 {second} F{feed}', extra,
                          '; preflight_config = begin',
                          *[f'; {k} = {v}' for k, v in config.items()],
                          '; preflight_config = end'])

    def case(self, **kwargs):
        return self.runner.Case('test-case', **kwargs)

    def test_matrix_covers_requested_dimensions_without_cartesian_explosion(self):
        cases = self.runner.build_cases()
        self.assertTrue(20 <= len(cases) <= 40)
        self.assertEqual(len({c.name for c in cases}), len(cases))
        configs = [self.runner.case_config(c) for c in cases]
        for key, expected in {
            'nozzle_diameter': {'0.4', '0.6', '0.8'},
            'extrusion_multiplier': {'0.9', '1', '1.1'},
            'filament_max_volumetric_flow': {'0', '4', '8', '12'},
            'max_volumetric_flow': {'0', '4', '8', '12'},
            'auto_speed': {'0', '1'}, 'interlock_perimeters_enabled': {'0', '1'},
            'use_relative_e_distances': {'0', '1'}, 'cooling': {'0', '1'},
            'gcode_comments': {'0', '1'},
        }.items():
            with self.subTest(key=key):
                self.assertTrue(expected <= {c[key] for c in configs})
        self.assertGreaterEqual(len({(c['layer_height'], c['extrusion_width']) for c in configs}), 2)
        self.assertEqual(sum(c.expect_baseline_violation for c in cases), 1)
        self.assertTrue(any(c.strict_feeds and float(self.runner.case_config(c)['perimeter_speed']) < 10
                            for c in cases))

    def test_pair_accepts_feed_reduction_with_exact_motion_and_e(self):
        case = self.case(expect_baseline_violation=True)
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config, 6000), self.fixture(config, 600), case, config)
        self.assertTrue(result['passed'], result)
        self.assertGreater(result['baseline']['violations'], 0)
        self.assertEqual(result['candidate']['violations'], 0)
        self.assertEqual(result['candidate']['layers'], 17)
        self.assertEqual(len(result['deposition_feeds']), 2)

    def test_pair_detects_e_change_even_when_total_e_is_unchanged(self):
        case = self.case()
        config = self.runner.case_config(case)
        base = self.fixture(config)
        candidate = base.replace('X10 E1', 'X10 E0.5').replace('X20 E1', 'X20 E1.5')
        result = self.runner.analyze_pair(base, candidate, case, config)
        self.assertFalse(result['passed'])
        self.assertIn('motion_geometry_or_e_changed', result['failures'])

    def test_motion_comparison_includes_travel_retract_modes_and_arc_geometry(self):
        for before, after in [('G1 X1', 'G1 X2'), ('G1 E-1', 'G1 E-2'),
                              ('G90', 'G91'), ('M82', 'M83'), ('T0', 'T1'),
                              ('G2 X1 I1 J0 E1', 'G3 X1 I1 J0 E1'),
                              ('G2 X1 R1 E1', 'G2 X1 R2 E1'),
                              ('G2 X1 I1 J0 E1', 'G2 X1 I2 J0 E1')]:
            with self.subTest(before=before):
                self.assertNotEqual(self.runner.motion_signature(before),
                                    self.runner.motion_signature(after))
        self.assertEqual(self.runner.motion_signature('G1 F600\nG1 X1 E1'),
                         self.runner.motion_signature('G1 X1.0 E1.00000 F600'))

    def test_selected_role_coverage_cannot_disappear(self):
        case = self.case()
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config), self.fixture(config, interlock=False), case, config)
        self.assertFalse(result['passed'])
        self.assertIn('candidate_missing_interlocking_coverage', result['failures'])

    def test_disabled_interlocking_requires_ordinary_deposition(self):
        case = self.case(overrides={'interlock_perimeters_enabled': '0'}, strict_feeds=True)
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config, interlock=False),
                                          self.fixture(config, interlock=False), case, config)
        self.assertTrue(result['passed'], result)
        empty = '\n'.join(f'; {k} = {v}' for k, v in config.items())
        failed = self.runner.analyze_pair(empty, empty, case, config)
        self.assertFalse(failed['passed'])
        self.assertIn('candidate_missing_deposition', failed['failures'])

    def test_controls_require_identical_travel_and_retraction_feeds(self):
        case = self.case(overrides={'filament_max_volumetric_flow': '0'}, strict_feeds=True)
        config = self.runner.case_config(case)
        base = self.fixture(config)
        for candidate in (base.replace('X15 F3000', 'X15 F2400'), base.replace('E-1 F1200', 'E-1 F600')):
            with self.subTest(candidate=candidate):
                result = self.runner.analyze_pair(base, candidate, case, config)
                self.assertFalse(result['passed'])
                self.assertIn('control_motion_feeds_changed', result['failures'])

    def test_manual_mode_cannot_speed_up_deposition(self):
        case = self.case()
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config, 300), self.fixture(config, 600), case, config)
        self.assertFalse(result['passed'])
        self.assertIn('manual_deposition_speed_increased', result['failures'])

    def test_cooling_feed_changes_are_reported_separately(self):
        case = self.case(overrides={'cooling': '1'})
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config, 900), self.fixture(config, 600), case, config)
        self.assertTrue(result['passed'], result)
        self.assertEqual(len(result['cooling_secondary_feed_changes']), 2)

    def test_config_mismatch_and_unknown_auditor_modes_fail_closed(self):
        case = self.case()
        config = self.runner.case_config(case)
        for candidate in (self.fixture(config).replace('; cooling = 0', '; cooling = 1'),
                          self.fixture(config, extra='G93')):
            with self.subTest(candidate=candidate):
                result = self.runner.analyze_pair(self.fixture(config), candidate, case, config)
                self.assertFalse(result['passed'])

    def test_known_baseline_failure_must_actually_reproduce(self):
        case = self.case(expect_baseline_violation=True)
        config = self.runner.case_config(case)
        result = self.runner.analyze_pair(self.fixture(config), self.fixture(config), case, config)
        self.assertFalse(result['passed'])
        self.assertIn('known_baseline_failure_not_reproduced', result['failures'])

    def test_runner_records_commands_and_uses_distinct_isolated_datadirs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline, candidate = root / 'baseline.exe', root / 'candidate.exe'
            baseline.touch()
            candidate.touch()
            (root / 'preFlight.dll').write_bytes(b'synthetic core library')
            output = root / 'new-evidence'
            case = self.case(expect_baseline_violation=True)
            calls = []

            def fake_run(command, **kwargs):
                calls.append(command)
                config_path = Path(command[command.index('--load') + 1])
                config = self.runner.read_config(config_path)
                destination = Path(command[command.index('--output') + 1])
                destination.write_text(self.fixture(config, 6000 if command[0] == str(baseline) else 600),
                                       encoding='utf-8')
                return subprocess.CompletedProcess(command, 0, 'synthetic slice log')

            with patch.object(self.runner.subprocess, 'run', side_effect=fake_run):
                report = self.runner.run_matrix(baseline, candidate, output, [case])
            self.assertTrue(report['passed'], report)
            self.assertEqual(len(calls), 2)
            self.assertEqual(report['executables']['baseline']['core_library']['path'],
                             str(root / 'preFlight.dll'))
            self.assertNotEqual(report['executables']['baseline']['sha256'],
                                report['executables']['baseline']['core_library']['sha256'])
            datadirs = [Path(c[c.index('--datadir') + 1]) for c in calls]
            self.assertNotEqual(*datadirs)
            self.assertTrue(all(p.is_dir() and p.is_relative_to(output) for p in datadirs))
            self.assertEqual(report['cases'][0]['exports']['baseline']['returncode'], 0)
            self.assertEqual(report['cases'][0]['exports']['candidate']['command'], calls[1])
            self.assertEqual(json.loads((output / 'report.json').read_text())['passed'], True)
            self.assertTrue(report['unverified_gates'])
            self.assertIn('complete_curated_matrix', report)
            self.assertFalse(report['complete_curated_matrix'])
            self.assertEqual(report['selected_cases'], ['test-case'])
            with self.assertRaises(FileExistsError):
                self.runner.run_matrix(baseline, candidate, output, [case])

    def test_export_failure_is_recorded_without_false_green(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / 'slicer.exe'
            executable.touch()
            with patch.object(self.runner.subprocess, 'run', return_value=subprocess.CompletedProcess([], 7, 'failed')):
                report = self.runner.run_matrix(executable, executable, root / 'evidence', [self.case()])
            self.assertFalse(report['passed'])
            self.assertEqual(report['cases'][0]['exports']['baseline']['returncode'], 7)
            self.assertIn('baseline_export_failed', report['cases'][0]['failures'])

    def test_failed_audit_does_not_leave_invariance_marked_passed(self):
        case = self.case()
        config = self.runner.case_config(case)
        invalid = self.fixture(config, extra='G93')
        result = self.runner.analyze_pair(invalid, invalid, case, config)
        self.assertFalse(result['passed'])
        self.assertFalse(result['motion_invariance']['passed'])
        self.assertEqual(result['motion_invariance']['status'], 'unverified_due_to_audit_failure')


if __name__ == '__main__':
    unittest.main()
