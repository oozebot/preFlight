"""Final-export gate for the flow limiter followed by PressureEqualizer.

Compare the same candidate with smoothing off/on. Interlocking geometry and E
must be identical, and smoothing must not raise any capped deposition feed.
Other roles retain the existing equalizer's segmentation behavior. Separate
baseline controls verify unchanged behavior when the new cap is not applicable.
All exports use synthetic models, fresh datadirs and no printer connection.
"""
import argparse
import hashlib
import json
from pathlib import Path

from regression import (Case, ROLE, _config_evidence, _export,
                        _tool_value, analyze_pair, audit, case_config)
from reproduce import stepped_stl


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def compare_interlocking(before, after, limit):
    old = [m for m in before if m.role == ROLE]
    new = [m for m in after if m.role == ROLE]
    same = bool(old) and len(old) == len(new) and all(
        (a.endpoint, a.length, a.delta_e, a.tool) == (b.endpoint, b.length, b.delta_e, b.tool)
        for a, b in zip(old, new))
    return dict(passed=same and all(b.feed <= a.feed for a, b in zip(old, new)) and
                all(m.flow <= limit + 1e-9 for m in new),
                same_interlocking_geometry_and_e=same,
                selected_moves=len(new),
                slowed_moves=sum(b.feed < a.feed for a, b in zip(old, new)) if same else 0,
                below_f60=sum(m.feed < 60 for m in new),
                max_flow=max((m.flow for m in new), default=0.))


def run(baseline, candidate, output):
    output.mkdir(parents=True, exist_ok=False)
    report = dict(passed=False, cases=[], controls=[], executables={})
    for label, exe in [('baseline', baseline), ('candidate', candidate)]:
        report['executables'][label] = dict(path=str(exe), sha256=sha256(exe),
            library_sha256=sha256(exe.with_name('preFlight.dll')) if exe.with_name('preFlight.dll').exists() else None)
    model = output / 'fixture.stl'
    model.write_text(stepped_stl(), encoding='ascii')
    for name, relative, positive, negative, limit, tool in [
        ('positive-relative', '1', '0.01', '0', '8', 0),
        ('negative-relative', '1', '0', '0.01', '8', 0),
        ('both-absolute', '0', '0.01', '0.01', '8', 0),
        ('sub-f60-relative', '1', '0.00001', '0.00001', '0.2', 0),
        ('sub-f60-absolute', '0', '0.00001', '0.00001', '0.2', 0),
        ('second-tool', '1', '0.01', '0.01', '4', 1),
        ('forced-cooling', '1', '0.01', '0.01', '8', 0),
        ('scarf-xyze', '1', '0.01', '0.01', '8', 0),
    ]:
        root = output / name
        root.mkdir()
        overrides = dict(use_relative_e_distances=relative, fan_spinup_bridge_infill='0',
                         filament_max_volumetric_flow=limit)
        if name == 'forced-cooling':
            overrides.update(cooling='1', slowdown_below_layer_time='120')
        if name == 'scarf-xyze':
            overrides.update(scarf_seam_placement='everywhere', scarf_seam_on_inner_perimeters='1',
                             scarf_seam_only_on_smooth='0')
        if tool:
            overrides.update(nozzle_diameter='0.6,0.6', filament_diameter='1.75,2.85',
                extrusion_multiplier='1,1.1', filament_max_volumetric_flow='0,' + limit,
                perimeter_extruder='2', infill_extruder='2', solid_infill_extruder='2')
        off = case_config(Case(name, overrides))
        on = dict(off, max_volumetric_extrusion_rate_slope_positive=positive,
                  max_volumetric_extrusion_rate_slope_negative=negative)
        exports, parsed = {}, {}
        for label, config in [('off', off), ('on', on)]:
            cfg = root / (label + '.ini')
            cfg.write_text(''.join(f'{k} = {v}\n' for k, v in config.items()), encoding='utf-8')
            exports[label] = _export(candidate, root / label, cfg, model)
            if exports[label]['returncode'] or 'error' in exports[label]:
                raise RuntimeError(f'{name}/{label} export failed: {exports[label]}')
            text = Path(exports[label]['gcode']).read_text(encoding='utf-8')
            if not _config_evidence(text, config)['passed']:
                raise RuntimeError(f'{name}/{label}: effective config mismatch')
            count = len(config['nozzle_diameter'].split(','))
            parsed[label] = audit(text.splitlines(),
                tool_diameters={t: _tool_value(config, 'filament_diameter', t) for t in range(count)},
                tool_e_mode='reset' if tool else None)
        evidence = compare_interlocking(parsed['off'], parsed['on'], float(limit))
        evidence.update(name=name, exports=exports)
        # Forced cooling can dominate both runs and leave identical final feeds.
        # All other cases must demonstrate a real, observable slope correction.
        evidence['requires_visible_slowdown'] = name != 'forced-cooling'
        if evidence['requires_visible_slowdown']:
            evidence['passed'] &= evidence['slowed_moves'] > 0
        if name.startswith('sub-f60'):
            evidence['passed'] &= evidence['below_f60'] > 0
        report['cases'].append(evidence)
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    for name, overrides in [('disabled-limit', {'filament_max_volumetric_flow': '0'}),
                             ('interlocking-off', {'interlock_perimeters_enabled': '0'})]:
        root = output / name
        root.mkdir()
        case = Case(name, dict(overrides, max_volumetric_extrusion_rate_slope_positive='0.01',
                    max_volumetric_extrusion_rate_slope_negative='0.01', fan_spinup_bridge_infill='0'),
                    strict_feeds=True)
        config = case_config(case)
        cfg = root / 'fixture.ini'
        cfg.write_text(''.join(f'{k} = {v}\n' for k, v in config.items()), encoding='utf-8')
        exports = {label: _export(exe, root / label, cfg, model)
                   for label, exe in [('baseline', baseline), ('candidate', candidate)]}
        if any(item['returncode'] or 'error' in item for item in exports.values()):
            raise RuntimeError(f'{name}: control export failed')
        evidence = analyze_pair(*(Path(exports[label]['gcode']).read_text(encoding='utf-8')
            for label in ('baseline', 'candidate')), case, config)
        report['controls'].append(dict(name=name, passed=evidence['passed'], failures=evidence['failures'],
            exports=exports, motion_invariance=evidence['motion_invariance']))
    report['passed'] = all(item['passed'] for item in report['cases'] + report['controls'])
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = run(args.baseline.resolve(), args.candidate.resolve(), args.output.resolve())
    print(json.dumps({k: [{kk: vv for kk, vv in item.items() if kk != 'exports'} for item in report[k]]
                      for k in ('cases', 'controls')}, indent=2))
    raise SystemExit(0 if report['passed'] else 1)
