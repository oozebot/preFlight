"""Bounded paired slicer regression, using original synthetic fixtures only.

Requires explicit baseline/candidate executables and a NEW output directory.
Every export gets its own datadir. No production profiles, printer, or network
are used. A passing report covers this curated matrix, not the native branch
and physical-print gates listed explicitly in unverified_gates.
"""
import argparse
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from audit_gcode import WORDS, audit
from reproduce import box_stl, stepped_stl, cylinder_stl


ROLE = 'Interlocking perimeter'
# At pinned f74dc69, GCode.cpp::arc_welder_enabled returns false, and
# ArcWelder::fit_path(..., circle_tolerance=0) produces polylines only.
# Curved fixtures deliberately request arcs but must remain linear. A future
# upstream activation must fail here and trigger a new firmware-aware audit.
ARC_COVERAGE_CONTRACT = 'disabled_at_f74dc69: curved exports must be linear, not arc coverage'
AUTO_FLOW_ERROR = ('Auto speed requires every filament to have a Max volumetric flow set. '
                   'Check your Filament Settings.')
# Pinned CLI has a pre-existing bool/exit-code bug: ProcessActions returns 1
# (true) for Print::validate errors, so Run reports exit 0 without a G-code.
MISSING_GCODE_ERROR = 'Successful slicer exit without output G-code'
UNVERIFIED_GATES = [
    'Native helper/Writer execution is a separate CTest gate, not established by this Python report',
    'Arc fitting is disabled at the pinned base; enabling it requires firmware-aware integration validation',
    'Recheck dormant gap-fill collection reachability on upstream changes (no population site at pinned base)',
    'Explicit zero-XYZ and below-F1 edges use native guard/Writer tests; not a generated-model coverage claim',
    'Firmware pressure advance, acceleration, heater capacity, and physical print quality',
]
# These empty script arrays are not serialized into the fixture G-code config
# block. All other generated settings must be present and match exactly (with
# numeric formatting normalized). No critical key can be silently skipped.
OMITTED_EMPTY_CONFIG_KEYS = {'filament_start_gcode', 'filament_end_gcode'}


@dataclass
class Case:
    name: str
    overrides: dict = field(default_factory=dict)
    model: str = 'stepped'
    strict_feeds: bool = False
    expect_baseline_violation: bool = False
    expected_rejection: str = ''
    required_coverage: tuple = ()
    require_time_metadata: bool = False
    require_exact_segmentation: bool = False


def read_config(path):
    values = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            key, value = line.split('=', 1)
            if key.strip() in values:
                raise ValueError(f'Duplicate config key: {key}')
            values[key.strip()] = value.strip()
    return values


def case_config(case):
    values = read_config(Path(__file__).with_name('reproduce.ini'))
    unknown = set(case.overrides) - set(values)
    if unknown:
        raise ValueError(f'Unknown fixture configuration keys: {sorted(unknown)}')
    values.update(case.overrides)
    values['start_gcode'] = 'G90\\nM83' if values['use_relative_e_distances'] == '1' else 'G90\\nM82'
    return values


def build_cases():
    cases = [Case('known-stepped', expect_baseline_violation=True,
                  required_coverage=('flow_100', 'flow_intermediate', 'flow_200', 'unchanged_metadata'))]

    def add(name, *, nozzle='0.6', em='1', filament='8', print_cap='0',
            height='0.2', width='0.65', auto='0', interlock='1', relative='1',
            cooling='0', speed=None, control=False, model='stepped'):
        overrides = dict(nozzle_diameter=nozzle, extrusion_multiplier=em,
                         filament_max_volumetric_flow=filament, max_volumetric_flow=print_cap,
                         layer_height=height, first_layer_height=height, auto_speed=auto,
                         interlock_perimeters_enabled=interlock, use_relative_e_distances=relative,
                         cooling=cooling)
        for key in ('extrusion_width', 'perimeter_extrusion_width', 'external_perimeter_extrusion_width',
                    'infill_extrusion_width', 'solid_infill_extrusion_width',
                    'top_infill_extrusion_width', 'first_layer_extrusion_width'):
            overrides[key] = width
        if speed is not None:
            for key in ('perimeter_speed', 'external_perimeter_speed', 'infill_speed',
                        'solid_infill_speed', 'top_solid_infill_speed'):
                overrides[key] = speed
        cases.append(Case(name, overrides, model, control))

    add('n04-em09-filament4', nozzle='0.4', em='0.9', filament='4', height='0.12', width='0.45')
    add('n06-em1-filament8')
    add('n08-em11-filament12', nozzle='0.8', em='1.1', filament='12', height='0.3', width='0.85')
    add('print-only4', em='0.9', filament='0', print_cap='4')
    add('print-only8', nozzle='0.8', filament='0', print_cap='8', height='0.3', width='0.85')
    add('print-only12', nozzle='0.4', em='1.1', filament='0', print_cap='12', width='0.45')
    add('both-filament4-print12', filament='4', print_cap='12')
    add('both-filament12-print4', filament='12', print_cap='4')
    add('both-filament8-print12', filament='8', print_cap='12')
    add('both-filament12-print8', filament='12', print_cap='8')
    add('disabled-manual', filament='0', control=True)
    add('disabled-auto', filament='0', auto='1', control=True)
    cases[-1].expected_rejection = AUTO_FLOW_ERROR
    add('interlock-off-manual', interlock='0', model='box', control=True)
    add('interlock-off-auto', interlock='0', auto='1', filament='4', model='box', control=True)
    add('interlock-off-disabled', interlock='0', filament='0', model='box', control=True)
    add('low-manual4', filament='4', speed='5', control=True)
    add('low-manual8', filament='8', em='1.1', speed='5', control=True)
    add('auto-filament4', filament='4', auto='1')
    add('auto-print8', filament='0', print_cap='8', auto='1')
    cases[-1].expected_rejection = AUTO_FLOW_ERROR
    add('auto-both12', filament='12', print_cap='12', auto='1')
    add('absolute-manual', relative='0')
    add('absolute-auto', auto='1', relative='0')
    add('cooling-manual', cooling='1')
    add('cooling-auto', cooling='1', auto='1')
    cases.append(Case('cooling-min-speed-above-cap', {'cooling': '1',
                      'filament_max_volumetric_flow': '4', 'min_print_speed': '100',
                      'slowdown_below_layer_time': '120'}))
    cases.append(Case('cooling-forced-slowdown', {'cooling': '1',
                      'slowdown_below_layer_time': '120'}))
    add('height03-width075', height='0.3', width='0.75')
    cases.append(Case('comments-on', {'gcode_comments': '1'}, required_coverage=('boundary',)))
    cases.append(Case('comments-on-disabled-control',
                      {'gcode_comments': '1', 'filament_max_volumetric_flow': '0'}, strict_feeds=True))
    cases.append(Case('curved-interlocking', {'arc_fitting': 'emit_center'}, model='cylinder'))
    cases.append(Case('curved-no-interlocking', {'arc_fitting': 'emit_center',
                      'interlock_perimeters_enabled': '0'}, model='cylinder', strict_feeds=True))
    cases.append(Case('curved-no-limit', {'arc_fitting': 'emit_center',
                      'filament_max_volumetric_flow': '0'}, model='cylinder', strict_feeds=True))
    cases.append(Case('scarf-interlocking', {'scarf_seam_placement': 'everywhere',
                      'scarf_seam_on_inner_perimeters': '1', 'scarf_seam_only_on_smooth': '0'},
                      required_coverage=('xyz',)))
    second_tool = dict(nozzle_diameter='0.6,0.6', filament_diameter='1.75,2.85',
                       extrusion_multiplier='1,1.1', perimeter_extruder='2',
                       infill_extruder='2', solid_infill_extruder='2')
    cases.append(Case('second-tool-limit4', dict(second_tool, filament_max_volumetric_flow='0,4')))
    cases.append(Case('second-tool-disabled', dict(second_tool, filament_max_volumetric_flow='4,0'),
                      strict_feeds=True))
    # Keep an exact final-segment gate in addition to the timed fan-split audit.
    for original in ('n08-em11-filament12', 'second-tool-limit4'):
        source = next(c for c in cases if c.name == original)
        cases.append(Case(original + '-no-spinup',
                          dict(source.overrides, fan_spinup_bridge_infill='0'),
                          require_exact_segmentation=True))
    for case in cases:
        case.require_time_metadata = not bool(case.expected_rejection)
    return cases


def _numeric_text(value):
    try:
        return str(Decimal(value).normalize())
    except InvalidOperation:
        return value


def _trace(text):
    """Keep ordered movement/mode words, including travel, E-only and IJ/R.

    F-only commands update modal feed but are not geometrical events. Numeric
    spelling is normalized; no path merging, total-E comparison or reordering.
    """
    trace, feed = [], None
    for line_number, original in enumerate(text.splitlines(), 1):
        code = original.split(';', 1)[0].strip()
        if not code:
            continue
        command = code.split()[0]
        if command in ('G0', 'G1', 'G2', 'G3', 'G92'):
            tokens = WORDS.findall(code[len(command):])
            if command != 'G92':
                feed = next((float(v) for k, v in tokens if k == 'F'), feed)
            words = tuple(sorted((k, _numeric_text(v)) for k, v in tokens if k != 'F'))
            if words:
                trace.append(dict(line=line_number, command=command, words=words,
                                  feed=feed if command != 'G92' else None))
        elif command in ('G17', 'G18', 'G19', 'G20', 'G21', 'G90', 'G91',
                         'G90.1', 'G91.1', 'G93', 'G94', 'G95', 'M82', 'M83') or re.fullmatch(r'T\d+', command):
            trace.append(dict(line=line_number, command=command, words=(), feed=None))
    return trace


def motion_signature(text):
    return [(event['command'], event['words']) for event in _trace(text)]


def ramp_split_trace(text):
    """Undo only validated XY/E splits surrounding a timed fan-ramp command.

    This is NOT generic path simplification. Require G90/M83, equal modal F,
    adjacent XY/E moves separated by exactly one annotated M106, an interior
    collinear split within XYZ output rounding, and proportional E within the
    propagated XYZ/E rounding bound. Endpoints and Decimal total E stay exact.
    The independent flow audit always consumes the original final G-code.
    """
    lines, trace = text.splitlines(), _trace(text)
    if any(e['command'] in ('G91', 'M82', 'G2', 'G3') for e in trace):
        return trace, []
    if not any(e['command'] == 'G90' for e in trace) or not any(e['command'] == 'M83' for e in trace):
        return trace, []
    xyz, result, evidence = dict(X=0., Y=0., Z=0.), [], []
    starts = []
    for event in trace:
        words = dict(event['words'])
        start = xyz.copy()
        if event['command'] in ('G0', 'G1', 'G92'):
            xyz.update({k: float(v) for k, v in words.items() if k in xyz})
        previous = result[-1] if result else None
        if previous and event['command'] == previous['command'] == 'G1':
            first = dict(previous['words'])
            between = lines[previous['line']:event['line']-1]
            if (set(first) == set(words) == {'X', 'Y', 'E'} and
                    previous['feed'] == event['feed'] and len(between) == 1 and
                    re.fullmatch(r'M106 S[\d.]+ ; Ramping [\d.]+s before [\w ]+', between[0])):
                origin = starts[-1]
                dx, dy = float(words['X'])-origin['X'], float(words['Y'])-origin['Y']
                sx, sy = float(first['X'])-origin['X'], float(first['Y'])-origin['Y']
                length = math.hypot(dx, dy)
                total_e = Decimal(first['E']) + Decimal(words['E'])
                fraction = (sx*dx + sy*dy) / (length*length) if length else -1.
                # XYZ is emitted with 3 decimals, E with 5. Both ends and the
                # split point contribute at most half a quantum per coordinate.
                xyz_bound = math.sqrt(2.) * .001
                e_bound = float(total_e) * xyz_bound / length + .00002 if length else 0.
                if (length > 0 and 0 < fraction < 1 and
                        abs(sx*dy-sy*dx)/length <= xyz_bound and
                        Decimal(first['E']) > 0 and Decimal(words['E']) > 0 and
                        abs(float(first['E']) - float(total_e)*fraction) <= e_bound):
                    merged = dict(event, words=tuple(sorted(dict(words, E=str(total_e.normalize())).items())))
                    result[-1] = merged
                    evidence.append(dict(first_line=previous['line'], second_line=event['line'],
                                         total_e=str(total_e), xyz_bound_mm=xyz_bound, e_bound=e_bound))
                    continue
        result.append(event)
        starts.append(start)
    return result, evidence


def _config_evidence(text, expected):
    emitted, duplicates = {}, []
    for line in text.splitlines():
        match = re.fullmatch(r';\s*([a-z_0-9]+)\s*=\s*(.*)', line)
        if match and match[1] in expected:
            if match[1] in emitted:
                duplicates.append(match[1])
            emitted[match[1]] = match[2].strip()
    missing = sorted(set(expected) - set(emitted) - OMITTED_EMPTY_CONFIG_KEYS)
    mismatches = {k: dict(expected=v, emitted=emitted[k]) for k, v in expected.items()
                  if k in emitted and _numeric_text(v) != _numeric_text(emitted[k])}
    return dict(passed=not (missing or mismatches or duplicates), missing=missing,
                mismatches=mismatches, duplicates=duplicates, verified=sorted(set(emitted) - set(mismatches)),
                unserialized_empty_script_arrays=sorted(OMITTED_EMPTY_CONFIG_KEYS - set(emitted)))


def _tool_value(config, key, tool):
    values = [float(value) for value in config[key].split(',')]
    if len(values) != 1 and tool >= len(values):
        raise ValueError('Missing explicit fixture tool value: ' + key)
    return values[0 if len(values) == 1 else tool]


def _effective_limit(config, tool=None):
    if tool is None:
        tool = int(config.get('perimeter_extruder', '1')) - 1
    enabled = [value for value in (_tool_value(config, 'filament_max_volumetric_flow', tool),
                                   float(config['max_volumetric_flow'])) if value > 0]
    return min(enabled, default=0.)


def _audit_summary(moves, limit):
    selected = [m for m in moves if m.role == ROLE]
    violations = [m for m in selected if limit > 0 and m.flow > limit + 1e-9]
    return dict(deposition_moves=len(moves), selected_moves=len(selected),
                layers=max((m.layer for m in moves), default=0),
                max_flow=max((m.flow for m in selected), default=0.), violations=len(violations),
                violation_examples=[dict(line=m.line, layer=m.layer, flow=m.flow,
                                         flow_lower_bound=m.flow_lower_bound) for m in violations[:8]])


def emission_coverage(lines, moves):
    """Markers identify branches only; flow safety always uses parsed E/XYZ/F."""
    by_line = {m.line: m for m in moves}
    counts = dict(flow_100=0, flow_intermediate=0, flow_200=0,
                  unchanged_metadata=0, boundary=0, xyz=0)
    tier = None
    metadata_changed = True
    previous_interlocking = False
    for number, line in enumerate(lines, 1):
        if line.startswith(';LAYER_CHANGE'):
            tier = None
            previous_interlocking = False
        if line.startswith((';WIDTH:', ';HEIGHT:')):
            metadata_changed = True
        tag = re.fullmatch(r';Interlocking: E(\d+)% F[\d.]+%', line.strip())
        if tag:
            tier = int(tag[1])
        move = by_line.get(number)
        if move is None:
            continue
        selected = move.role == ROLE
        if selected:
            if tier == 100:
                counts['flow_100'] += 1
            elif tier == 200:
                counts['flow_200'] += 1
            elif tier is not None and 100 < tier < 200:
                counts['flow_intermediate'] += 1
            if previous_interlocking and not metadata_changed:
                counts['unchanged_metadata'] += 1
            if ';' in line and 'to boundary' in line.split(';', 1)[1]:
                counts['boundary'] += 1
            words = dict(WORDS.findall(line.split(';', 1)[0]))
            if all(axis in words for axis in ('X', 'Y', 'Z')):
                counts['xyz'] += 1
        previous_interlocking = selected
        metadata_changed = False
    return counts


def timing_and_cooling(lines, moves):
    """Reported slicer estimate plus commanded deposition time, not wall time."""
    estimates = {}
    layers = {}
    by_line = {m.line: m for m in moves}
    fan = 0.
    fan_events = []
    for number, line in enumerate(lines, 1):
        match = re.fullmatch(r'; (estimated(?: first layer)? printing time) \(normal mode\) = (.+)', line)
        if match:
            tokens = re.findall(r'(\d+(?:\.\d+)?)\s*([dhms])', match[2])
            if not tokens or re.sub(r'(\d+(?:\.\d+)?)\s*([dhms])', '', match[2]).strip():
                raise ValueError('Unknown slicer time format')
            estimates[match[1]] = sum(float(v) * dict(d=86400, h=3600, m=60, s=1)[unit]
                                     for v, unit in tokens)
        command = line.split(';', 1)[0].strip()
        if command == 'M107' or command.startswith('M106 '):
            words = dict(WORDS.findall(command))
            fan = 0. if command == 'M107' else float(words.get('S', 255))
            fan_events.append(dict(line=number, pwm=fan))
        move = by_line.get(number)
        if move is not None:
            summary = layers.setdefault(str(move.layer), dict(commanded_deposition_seconds=0.,
                        deposition_length_mm=0., fan_pwm_distance_sum=0.))
            summary['commanded_deposition_seconds'] += move.length / (move.feed / 60.)
            summary['deposition_length_mm'] += move.length
            summary['fan_pwm_distance_sum'] += fan * move.length
    return dict(slicer_estimates_seconds=estimates, layers=layers, fan_events=fan_events,
                scope='Nominal deposition only excludes travel, acceleration, heating and firmware PA')


def analyze_pair(baseline, candidate, case, config):
    limit = _effective_limit(config)
    result = dict(passed=False, failures=[], limit=limit, deposition_feeds=[],
                  cooling_secondary_feed_changes=[])
    parsed = {}
    for label, text in (('baseline', baseline), ('candidate', candidate)):
        config_check = _config_evidence(text, config)
        if not config_check['passed']:
            result['failures'].append(label + '_emitted_config_mismatch')
        try:
            tool_count = len(config['nozzle_diameter'].split(','))
            diameters = {t: _tool_value(config, 'filament_diameter', t) for t in range(tool_count)}
            limits = {t: _effective_limit(config, t) for t in range(tool_count)}
            # These synthetic exports have no T macros; explicitly model the
            # slicer's reset-on-toolchange E convention, not a real Klipper T macro.
            moves = audit(text.splitlines(), tool_diameters=diameters, tool_limits=limits,
                          tool_e_mode='reset' if tool_count > 1 else None)
            parsed[label] = moves
            result[label] = _audit_summary(moves, limit)
            result[label]['tool_limits'] = limits
            result[label]['tool_diameters'] = diameters
            required_tool = int(config.get('perimeter_extruder', '1')) - 1
            actual_tools = sorted({m.tool for m in moves if m.role == ROLE})
            result[label]['interlocking_tools'] = actual_tools
            if config['interlock_perimeters_enabled'] == '1' and actual_tools != [required_tool]:
                result['failures'].append(label + '_unexpected_interlocking_tools')
            source_lines = text.splitlines()
            result[label]['timing_and_cooling'] = timing_and_cooling(source_lines, moves)
            if case.require_time_metadata and 'estimated printing time' not in result[label]['timing_and_cooling']['slicer_estimates_seconds']:
                result['failures'].append(label + '_missing_slicer_time_estimate')
            coverage = emission_coverage(source_lines, moves)
            result[label]['emission_coverage'] = coverage
            for requirement in case.required_coverage:
                if coverage.get(requirement, 0) == 0:
                    result['failures'].append(label + '_missing_coverage_' + requirement)
            arc_moves = [m for m in moves if source_lines[m.line-1].lstrip().startswith(('G2 ', 'G3 '))]
            result[label]['arc_moves'] = len(arc_moves)
            result[label]['interlocking_arc_moves'] = sum(m.role == ROLE for m in arc_moves)
            if case.model == 'cylinder':
                result[label]['arc_coverage_contract'] = ARC_COVERAGE_CONTRACT
                if arc_moves:
                    result['failures'].append(label + '_unexpected_arc_activation')
            if not moves:
                result['failures'].append(label + '_missing_deposition')
            selected = result[label]['selected_moves']
            if config['interlock_perimeters_enabled'] == '1' and not selected:
                result['failures'].append(label + '_missing_interlocking_coverage')
            elif config['interlock_perimeters_enabled'] == '0' and selected:
                result['failures'].append(label + '_unexpected_interlocking')
        except ValueError as error:
            result[label] = dict(error=str(error))
            result['failures'].append(label + '_audit_failed')
        result[label]['config_validation'] = config_check
    if case.expect_baseline_violation and not result['baseline'].get('violations', 0):
        result['failures'].append('known_baseline_failure_not_reproduced')
    if result['candidate'].get('violations', 0):
        result['failures'].append('candidate_volumetric_limit_exceeded')
    base_trace, candidate_trace = _trace(baseline), _trace(candidate)
    same_motion = motion_signature(baseline) == motion_signature(candidate)
    exact_motion = same_motion
    ramp_evidence = {}
    if not same_motion:
        base_normalized, base_ramps = ramp_split_trace(baseline)
        candidate_normalized, candidate_ramps = ramp_split_trace(candidate)
        if base_ramps and candidate_ramps and (
                [(e['command'], e['words']) for e in base_normalized] ==
                [(e['command'], e['words']) for e in candidate_normalized]):
            same_motion = True
            base_trace, candidate_trace = base_normalized, candidate_normalized
            ramp_evidence = dict(baseline=base_ramps, candidate=candidate_ramps)
    result['motion_invariance'] = dict(passed=same_motion and len(parsed) == 2,
                                      status='verified' if len(parsed) == 2 else 'unverified_due_to_audit_failure',
                                      exact_segmentation=exact_motion, validated_timed_fan_splits=ramp_evidence,
                                      baseline_events=len(base_trace), candidate_events=len(candidate_trace))
    if not same_motion:
        result['failures'].append('motion_geometry_or_e_changed')
        result['first_motion_difference'] = next((dict(event=i, baseline=a, candidate=b)
            for i, (a, b) in enumerate(zip(base_trace, candidate_trace))
            if (a['command'], a['words']) != (b['command'], b['words'])),
            dict(baseline_count=len(base_trace), candidate_count=len(candidate_trace)))
    if case.require_exact_segmentation and not exact_motion:
        result['failures'].append('exact_segmentation_required')
    if same_motion:
        result['motion_feed_changes'] = [dict(event=i, baseline_line=a['line'], candidate_line=b['line'],
                                             command=a['command'], baseline=a['feed'], candidate=b['feed'])
                                         for i, (a, b) in enumerate(zip(base_trace, candidate_trace))
                                         if a['feed'] != b['feed']]
        if case.strict_feeds and result['motion_feed_changes']:
            result['failures'].append('control_motion_feeds_changed')
    if same_motion and len(parsed) == 2:
        first, second = parsed['baseline'], parsed['candidate']
        if len(first) != len(second) or any(a.role != b.role for a, b in zip(first, second)):
            result['failures'].append('deposition_role_or_coverage_changed')
        else:
            result['deposition_feeds'] = [dict(baseline_line=a.line, candidate_line=b.line, role=a.role,
                                              baseline=a.feed, candidate=b.feed)
                                          for a, b in zip(first, second)]
            if config['auto_speed'] == '0' and any(b.feed > a.feed + 1e-9 for a, b in zip(first, second)):
                result['failures'].append('manual_deposition_speed_increased')
            if config['cooling'] == '1':
                # These are changes observed with cooling enabled, not a proof
                # that cooling alone caused them. Keep separate for review.
                result['cooling_secondary_feed_changes'] = [x for x in result['deposition_feeds']
                                                             if x['baseline'] != x['candidate']]
    result['passed'] = not result['failures']
    return result


def _export(executable, output, config, model):
    output.mkdir()
    datadir = output / 'datadir'
    datadir.mkdir()
    gcode = output / 'fixture.gcode'
    command = [str(executable), '--datadir', str(datadir), '--load', str(config),
               '--export-gcode', '--output', str(gcode), str(model)]
    evidence = dict(command=command, returncode=None, gcode=str(gcode), log=str(output / 'slice.log'))
    try:
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, timeout=300, check=False)
        evidence['returncode'] = process.returncode
        log = process.stdout or ''
        if process.returncode == 0 and not gcode.is_file():
            evidence['error'] = MISSING_GCODE_ERROR
    except (OSError, subprocess.TimeoutExpired) as error:
        evidence['error'] = str(error)
        log = str(error)
    Path(evidence['log']).write_text(log, encoding='utf-8')
    return evidence


def analyze_rejection(exports, expected):
    """A known Print::validate rejection is not a successful G-code audit."""
    failures = []
    for label, item in exports.items():
        if (item['returncode'] != 0 or item.get('error') != MISSING_GCODE_ERROR or Path(item['gcode']).exists()
                or Path(item['log']).read_text(encoding='utf-8').strip() != expected):
            failures.append(label + '_unexpected_rejection_behavior')
    return dict(passed=not failures, failures=failures,
                validation='expected_config_rejection', gcode_audit='not_applicable_no_export',
                expected_rejection=expected)


def run_matrix(baseline, candidate, output, cases):
    baseline, candidate = Path(baseline).resolve(strict=True), Path(candidate).resolve(strict=True)
    if not baseline.is_file() or not candidate.is_file():
        raise ValueError('Baseline and candidate must be executable files')
    if not cases or len({c.name for c in cases}) != len(cases):
        raise ValueError('Select at least one case with unique names')
    for case in cases:
        if not re.fullmatch(r'[a-z0-9][a-z0-9-]*', case.name) or case.model not in ('stepped', 'box', 'cylinder'):
            raise ValueError('Invalid synthetic case name or model')
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schema_version=1, passed=False, cases=[],
                  selected_cases=[case.name for case in cases],
                  complete_curated_matrix={case.name for case in cases} == {case.name for case in build_cases()},
                  unverified_gates=UNVERIFIED_GATES,
                  scope='Curated synthetic matrix; no claim of complete native or physical coverage',
                  executables={})
    for label, executable in (('baseline', baseline), ('candidate', candidate)):
        with executable.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        report['executables'][label] = dict(path=str(executable), sha256=digest)
        # On Windows the executable is a thin launcher; the slicer is in the
        # adjacent DLL. Record it separately, not just the launcher identity.
        core = executable.with_name('preFlight.dll')
        if core.is_file():
            with core.open('rb') as stream:
                core_digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            report['executables'][label]['core_library'] = dict(path=str(core), sha256=core_digest)
    for case in cases:
        directory = output / case.name
        directory.mkdir()
        config = case_config(case)
        config_path = directory / 'fixture.ini'
        config_path.write_text('\n'.join(f'{k} = {v}' for k, v in config.items()) + '\n', encoding='utf-8')
        model = directory / 'fixture.stl'
        model.write_text({'stepped': stepped_stl, 'box': box_stl, 'cylinder': cylinder_stl}[case.model](), encoding='ascii')
        exports = {label: _export(executable, directory / label, config_path, model)
                   for label, executable in (('baseline', baseline), ('candidate', candidate))}
        failures = [label + '_export_failed' for label, item in exports.items()
                    if item['returncode'] != 0 or 'error' in item]
        evidence = dict(passed=False, failures=failures)
        if case.expected_rejection:
            evidence = analyze_rejection(exports, case.expected_rejection)
        elif not failures:
            evidence = analyze_pair(*(Path(exports[label]['gcode']).read_text(encoding='utf-8')
                                      for label in ('baseline', 'candidate')), case, config)
        evidence.update(name=case.name, model=case.model, expected_config=config, exports=exports,
                        strict_feeds=case.strict_feeds, expect_baseline_violation=case.expect_baseline_violation)
        report['cases'].append(evidence)
        # Preserve completed cases even if a later export is interrupted.
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    report['passed'] = all(case['passed'] for case in report['cases'])
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--case', action='append', choices=[c.name for c in build_cases()],
                        help='Run selected cases only; omitted means all curated cases')
    args = parser.parse_args()
    cases = [c for c in build_cases() if args.case is None or c.name in args.case]
    try:
        report = run_matrix(args.baseline, args.candidate, args.output, cases)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps(dict(passed=report['passed'], cases=len(report['cases']),
                          failures={c['name']: c['failures'] for c in report['cases'] if not c['passed']},
                          report=str(args.output.resolve() / 'report.json')), indent=2))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
