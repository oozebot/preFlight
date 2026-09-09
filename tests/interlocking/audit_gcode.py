"""Independent audit of emitted G-code, not slicer WIDTH/HEIGHT metadata.

Checks nominal commanded deposition, not acceleration or firmware pressure advance.
Supports the fixture's XY linear/IJ arc moves, absolute/relative E, and G92.
This is a bounded fixture interpreter: unknown commands fail closed. E values
must fit the declared precision (default five places, including suppressed
trailing zeroes). Tool changes require explicit global/per-tool/reset E
semantics; Klipper T macros are not inferred. Slicer reset-E fixtures use reset
plus the emitted G92 E0. Initial XYZ/E and tool are zero, as for these fixtures.
Coordinate modes follow Klipper: E is absolute only when both G90 and M82
are active. G91 makes E relative; G90 does not cancel a preceding M83.
Explicit XYZ deposition commands that round to no XYZ movement are audited
as extruder-only moves. Standalone E-only retraction/unretraction is excluded.
"""
import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import re

WORDS = re.compile(r'([A-Z])([-+]?(?:\d+(?:\.\d*)?|\.\d+))')


@dataclass
class Move:
    line: int
    layer: int
    role: str
    length: float
    delta_e: float
    feed: float
    flow: float
    flow_lower_bound: float
    endpoint: tuple
    tool: int = 0
    limit: float | None = None


def audit(lines, diameter=1.75, e_decimals=5, *, tool_diameters=None,
          tool_limits=None, tool_e_mode=None):
    diameters = dict(tool_diameters) if tool_diameters is not None else {0: diameter}
    if 0 not in diameters or any(type(t) is not int or t < 0 or
                                  not math.isfinite(d) or d <= 0 for t, d in diameters.items()):
        raise ValueError('Invalid tool diameter configuration (initial tool 0 required)')
    if type(e_decimals) is not int or not 0 <= e_decimals <= 12:
        raise ValueError('E precision must be an integer from 0 to 12')
    if tool_e_mode not in (None, 'global', 'per-tool', 'reset'):
        raise ValueError('Unsupported tool E coordinate model')
    if tool_limits is not None and any(t not in diameters or not math.isfinite(v) or v < 0
                                       for t, v in tool_limits.items()):
        raise ValueError('Invalid tool limit configuration')
    xyz = (0., 0., 0.)
    e, feed, tool = 0., None, 0
    tool_positions = {t: 0. for t in diameters}
    absolute_e = absolute_xyz = True
    role, layer = 'unknown', 0
    quantum = 10**(-e_decimals)
    moves = []
    for number, original in enumerate(lines, 1):
        line = original.strip()
        if line.startswith(';TYPE:'):
            role = line[6:]
        if line.startswith(';LAYER_CHANGE'):
            marker = re.fullmatch(r';LAYER_CHANGE(?::(\d+))?', line)
            if not marker:
                raise ValueError(f'Invalid layer marker at line {number}')
            layer = int(marker[1]) if marker[1] is not None else layer + 1
        code = line.split(';', 1)[0].strip()
        if not code:
            continue
        command = code.split()[0]
        # Only these non-motion commands have no effect on nominal XYZ/E/F.
        if command in ('M104', 'M109', 'M140', 'M190', 'M106', 'M107', 'M400', 'G4'):
            continue
        words = code[len(command):]
        tokens = WORDS.findall(words)
        if WORDS.sub('', words).strip() or len({k for k, _ in tokens}) != len(tokens):
            raise ValueError(f'Malformed or duplicate parameters at line {number}')
        args = {k: float(v) for k, v in tokens}
        if any(not math.isfinite(v) for v in args.values()):
            raise ValueError(f'Non-finite parameters at line {number}')
        for key, value in tokens:
            if key == 'E' and len(value.partition('.')[2]) > e_decimals:
                raise ValueError(f'E exceeds declared precision at line {number}')
        if 'E' in args and math.ulp(args['E']) >= quantum:
            raise ValueError(f'E coordinate exceeds numeric precision at line {number}')
        allowed = ('XYZEFIJ' if command in ('G2', 'G3') else
                   'XYZEF' if command in ('G0', 'G1') else
                   'XYZE' if command == 'G92' else '')
        if set(args) - set(allowed):
            kind = 'arc' if command in ('G2', 'G3') else 'command'
            raise ValueError(f'Unsupported {kind} parameters at line {number}: {command}')
        if re.fullmatch(r'T\d+', command):
            new_tool = int(command[1:])
            if new_tool not in diameters:
                raise ValueError(f'Unknown tool {new_tool} at line {number}')
            if new_tool != tool:
                if tool_e_mode is None:
                    raise ValueError(f'Tool switch requires explicit E coordinate model at line {number}')
                tool_positions[tool] = e
                e = (tool_positions[new_tool] if tool_e_mode == 'per-tool' else
                     0. if tool_e_mode == 'reset' else e)
                tool = new_tool
        elif command in ('G21', 'G94', 'G17', 'G91.1'):
            pass
        elif command == 'M82':
            absolute_e = True
        elif command == 'M83':
            absolute_e = False
        elif command == 'G90':
            absolute_xyz = True
        elif command == 'G91':
            absolute_xyz = False
        elif command == 'G92':
            if not args:
                raise ValueError(f'Unsupported empty G92 at line {number}')
            e = args.get('E', e)
            xyz = tuple(args.get(k, xyz[i]) for i, k in enumerate('XYZ'))
        elif command in ('G0', 'G1', 'G2', 'G3'):
            new = tuple(args.get(k, xyz[i]) if absolute_xyz else xyz[i]+args.get(k, 0.)
                        for i, k in enumerate('XYZ'))
            feed = args.get('F', feed)
            effective_absolute_e = absolute_xyz and absolute_e
            de = args.get('E', e if effective_absolute_e else 0.) - (e if effective_absolute_e else 0.)
            if 'E' in args:
                e = args['E'] if effective_absolute_e else e + args['E']
            length = math.dist(new, xyz)
            if command in ('G2', 'G3'):
                if not ('I' in args or 'J' in args):
                    raise ValueError(f'Unsupported arc at line {number}')
                cx, cy = xyz[0]+args.get('I', 0.), xyz[1]+args.get('J', 0.)
                radius = math.hypot(xyz[0]-cx, xyz[1]-cy)
                end_radius = math.hypot(new[0]-cx, new[1]-cy)
                if radius <= 0 or not math.isclose(radius, end_radius, rel_tol=1e-6, abs_tol=1e-6):
                    raise ValueError(f'Invalid arc radius/end point at line {number}')
                a = math.atan2(xyz[1]-cy, xyz[0]-cx)
                b = math.atan2(new[1]-cy, new[0]-cx)
                angle = ((a-b) if command == 'G2' else (b-a)) % (2*math.pi)
                if angle == 0 and radius > 0:
                    if new[:2] != xyz[:2]:
                        raise ValueError(f'Ambiguous non-closed arc at line {number}')
                    angle = 2*math.pi
                length = math.hypot(radius*angle, new[2]-xyz[2])
            if not all(math.isfinite(v) for v in (*new, e, de, length)):
                raise ValueError(f'Non-finite numeric motion at line {number}')
            extrusion_only = length == 0 and de > 0 and any(k in args for k in 'XYZ')
            if extrusion_only:
                # Klipper toolhead.Move uses filament distance when XYZ is zero.
                # Both duration and volume scale with E; its rounding cancels.
                length = de
            if length > 0 and de > 0:
                if feed is None or feed <= 0:
                    raise ValueError(f'Missing or nonpositive deposition feed at line {number}')
                if tool_limits is not None and tool not in tool_limits:
                    raise ValueError(f'Missing limit for tool {tool} at line {number}')
                # One rounded E in relative mode; difference of two in absolute mode.
                uncertainty = quantum * (1. if effective_absolute_e else .5)
                if de <= uncertainty and not extrusion_only:
                    raise ValueError(f'E precision cannot resolve positive deposition at line {number}')
                area = math.pi * diameters[tool]**2 / 4
                factor = area * feed / (60 * length)
                if not math.isfinite(factor) or not math.isfinite(de*factor) or factor <= 0:
                    raise ValueError(f'Unresolvable numeric flow at line {number}')
                moves.append(Move(number, layer, role, length, de, feed, de*factor,
                                  de*factor if extrusion_only else max(0., de-uncertainty)*factor, new, tool,
                                  tool_limits[tool] if tool_limits is not None else None))
            xyz = new
        else:
            raise ValueError(f'Unsupported command or mode at line {number}: {command}')
    return moves


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gcode', type=Path)
    parser.add_argument('--limit', type=float, required=True)
    parser.add_argument('--diameter', type=float, default=1.75)
    parser.add_argument('--e-decimals', type=int, default=5,
                        help='Declared E decimal precision; extra places fail closed')
    parser.add_argument('--tool-diameter', action='append', default=[], metavar='ID=MM')
    parser.add_argument('--tool-limit', action='append', default=[], metavar='ID=MM3/S',
                        help='Per-tool override of --limit; zero disables that tool cap')
    parser.add_argument('--tool-e-mode', choices=('global', 'per-tool', 'reset'))
    parser.add_argument('--role', default='Interlocking perimeter')
    args = parser.parse_args()
    try:
        if not math.isfinite(args.limit) or args.limit < 0:
            raise ValueError('Limit must be finite and nonnegative (zero disables the cap)')
        diameters = {0: args.diameter}
        for entry in args.tool_diameter:
            tool, value = entry.split('=')
            diameters[int(tool)] = float(value)
        limits = {t: args.limit for t in diameters}
        for entry in args.tool_limit:
            tool, value = entry.split('=')
            limits[int(tool)] = float(value)
        moves = audit(args.gcode.read_text(encoding='utf-8').splitlines(), args.diameter,
                      args.e_decimals, tool_diameters=diameters, tool_limits=limits,
                      tool_e_mode=args.tool_e_mode)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    selected = [m for m in moves if m.role == args.role]
    # E has already been emitted: its quantization is not uncertainty in the
    # commanded volume. Keep the lower bound diagnostic-only.
    violations = [m for m in selected if m.limit > 0 and m.flow > m.limit + 1e-9]
    print(json.dumps(dict(moves=len(selected), layers=max((m.layer for m in moves), default=0),
                         limit=args.limit, tool_limits=limits,
                         max_flow=max((m.flow for m in selected), default=0),
                         violations=len(violations), examples=[asdict(m) for m in violations[:6]]), indent=2))
    # Missing coverage is a failure, not a green audit.
    raise SystemExit(1 if violations or not selected else 0)


if __name__ == '__main__':
    main()
