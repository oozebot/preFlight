"""Generate an original, non-private fixture and slice it with an isolated datadir.

Usage: python reproduce.py --slicer path/to/preFlight-console.exe --output NEW_DIR
No printer connection, upload, or print is performed. Output must not exist.
"""
import argparse
from pathlib import Path
import subprocess
import math


def cylinder_stl(sides=180, radius=15., height=6.):
    lines = ['solid curved_interlocking_fixture']
    def triangle(a, b, c):
        lines.extend(['facet normal 0 0 0', 'outer loop'])
        lines.extend('vertex %.9f %.9f %.9f' % p for p in (a, b, c))
        lines.extend(['endloop', 'endfacet'])
    for n in range(sides):
        angle, next_angle = n * math.tau / sides, (n+1) * math.tau / sides
        a = (radius + radius*math.cos(angle), radius + radius*math.sin(angle), 0.)
        b = (radius + radius*math.cos(next_angle), radius + radius*math.sin(next_angle), 0.)
        c, d = (a[0], a[1], height), (b[0], b[1], height)
        triangle(a, b, d)
        triangle(a, d, c)
        triangle((radius, radius, 0.), b, a)
        triangle((radius, radius, height), c, d)
    return '\n'.join(lines + ['endsolid curved_interlocking_fixture', ''])


def box_stl(width=30, depth=25, height=6, taper=False):
    inset = 4 if taper else 0
    vertices = [(0, 0, 0), (width, 0, 0), (width, depth, 0), (0, depth, 0),
                (inset, 0, height), (width-inset, 0, height),
                (width-inset, depth, height), (inset, depth, height)]
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
             (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    lines = ['solid interlocking_fixture']
    for face in faces:
        lines.extend(['facet normal 0 0 0', 'outer loop'])
        lines.extend('vertex %g %g %g' % vertices[i] for i in face)
        lines.extend(['endloop', 'endfacet'])
    return '\n'.join(lines + ['endsolid interlocking_fixture', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slicer', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--taper', action='store_true', help='Vary layer outlines to exercise zone crossings')
    parser.add_argument('--stepped', action='store_true', help='Narrow middle with a wider roof')
    args = parser.parse_args()
    slicer = args.slicer.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    datadir = output / 'datadir'
    datadir.mkdir()
    model = output / 'box.stl'
    model.write_text(stepped_stl() if args.stepped else box_stl(taper=args.taper), encoding='ascii')
    config = Path(__file__).with_name('reproduce.ini').resolve(strict=True)
    command = [str(slicer), '--datadir', str(datadir), '--load', str(config),
               '--export-gcode', '--output', str(output / 'box.gcode'), str(model)]
    with (output / 'slice.log').open('w', encoding='utf-8') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                timeout=300, check=False)
    print(f'Slicer exit code: {result.returncode}; evidence: {output}')
    raise SystemExit(result.returncode)


def stepped_stl():
    # Union of grid cells; emit only exposed faces, with matching grid vertices.
    xs, ys, zs = [0, 5, 25, 30], [0, 25], [0, 2, 4, 6]
    cells = {(x, 0, z) for x in range(3) for z in range(3)
             if z != 1 or x == 1}
    faces = [((-1, 0, 0), (0, 4, 7, 3)), ((1, 0, 0), (1, 2, 6, 5)),
             ((0, -1, 0), (0, 1, 5, 4)), ((0, 1, 0), (3, 7, 6, 2)),
             ((0, 0, -1), (0, 3, 2, 1)), ((0, 0, 1), (4, 5, 6, 7))]
    lines = ['solid stepped_interlocking_fixture']
    for x, y, z in sorted(cells):
        vertices = [(xs[x+dx], ys[y+dy], zs[z+dz]) for dx, dy, dz in
                    [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
                     (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]]
        for (dx, dy, dz), face in faces:
            if (x+dx, y+dy, z+dz) in cells:
                continue
            for tri in [(face[0], face[1], face[2]), (face[0], face[2], face[3])]:
                lines.extend(['facet normal 0 0 0', 'outer loop'])
                lines.extend('vertex %g %g %g' % vertices[i] for i in tri)
                lines.extend(['endloop', 'endfacet'])
    return '\n'.join(lines + ['endsolid stepped_interlocking_fixture', ''])


if __name__ == '__main__':
    main()
