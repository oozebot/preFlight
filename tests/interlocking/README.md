# Interlocking volumetric-limit regression

These tests use original synthetic models and isolated slicer data directories.
They do not connect to a printer, load production presets, or start prints.
Run commands below from the repository root. Python 3.11+ is required.

## Status and scope

Passing helper/interpreter tests does **not**
establish that the complete slicer builds or emits safe final G-code. The paired
export below must use separately built baseline and candidate executables.
An installed release supplied for both arguments is only a runner self-check.

The limit concerns nominal commanded flow, not acceleration-dependent flow,
pressure advance, hotend capacity measurements, or physical print quality.
No reduction in E, bead geometry, or saved profile speeds is intended.

## Fast tests

```powershell
python -m unittest discover -s tests/interlocking -p 'test_*.py'
.\tests\interlocking\run_flow_limit_test.bat
```

The batch compiles the production scalar helper with MSVC and runs runtime
checks that remain enabled in Release. `test_writer.cpp` instead exercises the
real GCodeWriter, including output quantization and absolute E bookkeeping.
It requires the full dependency build and libslic3r; it is not covered by the
standalone batch.

## Full native build and tests

Use the repository wrappers: `build_deps.bat`, then `build.bat`. Build an
unmodified `f74dc696b190a9fe5aabf868f357e14881a66b96` in a separate worktree for
the baseline; do not compare against another source revision. Local Build Tools
discovery changes are separate from the flow patch.

Do not compile the application while a dependency install is still running.
The Windows wrapper also builds optional Debug dependencies after Release;
both variants share installed headers, so a completed Release stage alone does
not make concurrent application compilation safe.

After `build.bat -config` succeeds, enable `PREFLIGHT_INTERLOCKING_TESTS=ON` in
that candidate build's CMake cache, reconfigure and build through `build.bat`.
The option defaults OFF and adds only the three focused test targets. In a
developer environment with CMake on PATH:

```powershell
ctest --test-dir build --output-on-failure -R '^interlocking_'
```

On Windows, `tests\interlocking\run_native_tests.bat` builds only those native
test targets and their library dependencies, then runs CTest. The third target
exercises the real PressureEqualizer with both E modes, both selected tools,
print/filament limits and a legacy disabled-limit control. Initial tool selection
is tested both inside the stream and in the header outside the filter. It selects the
same VS 2026 compiler. This permits early native checks while a GUI-only build
dependency is unavailable; it does not disable GUI in the application config,
and cannot replace a successful full `build.bat` plus GUI/export validation.

Do not use `-clean` to address an observation timeout. Preserve the active build
handle and verify its status before attempting recovery.

## Paired final-export audit

Supply absolute executable paths and a **new** evidence directory:

```powershell
python tests/interlocking/regression.py --baseline C:/isolated/baseline/preFlight-console.exe --candidate C:/isolated/candidate/preFlight-console.exe --output C:/isolated/evidence/run-001
```

The runner records binary SHA-256, commands, return codes, configuration, models,
logs and G-code. Each export gets a distinct `--datadir`. It compares ordered
motion, E, G92 and tool/mode changes, allowing only feed changes. Manual
deposition may not speed up; disabled-limit/interlocking and low-speed controls
require unchanged modal motion feeds. Cooling-related secondary changes are
reported separately. Unknown G-code semantics fail closed.

The matrix includes nozzle/height/width/EM combinations, print and filament
limits, auto/manual speed, M82/M83, cooling, comments, boundary splitting,
scarf XYZE motion, and a selected second extruder with a different filament
diameter and limit. Coverage markers identify exercised branches only;
volumetric flow is calculated independently from emitted E, XYZ and modal F.
The full report also lists gates not established by this export matrix.

The curated matrix contains 38 cases: 36 export pairs and two expected existing
validation rejections. The final flow audit always uses raw emitted G-code.
Timed bridge-fan spinup can split a linear move at a different point after its
feed changes. Only G90/M83 XY/E pairs immediately surrounding one annotated
`M106 ... ; Ramping ...` may be normalized for motion comparison: equal modal
F, an interior collinear split, and proportional E within propagated 3-decimal
XYZ / 5-decimal E rounding are mandatory. The merged endpoint and Decimal sum
of E must match exactly. No arbitrary simplification or total-filament-only
comparison is allowed. Negative tests reject geometry, E, feed and mode changes.
Two additional no-spinup cases require identical final segmentation, without
normalization. This preserves testing of the original generator independently
of the existing time-dependent fan postprocessor; no production setting changes.

Two unsupported configurations intentionally test rejection, not G-code:
auto speed with zero filament limit (with and without a print-level limit).
At the pinned base, `Print::validate` rejects them, but a bool/exit-code bug in
the CLI returns exit 0 without a file. The rejection test requires that exact
message, absent file and known return behavior; it never reports an export as
successful. Unexpected errors or artifacts fail.

Arc fitting is hard-disabled at the pinned base (`arc_welder_enabled` returns
false). Cylinder cases request `emit_center` but verify the existing linear
export contract, including non-interlocking controls. They explicitly do not
claim G2/G3 integration coverage. Unexpected generated arcs fail and require
reassessment of firmware segmentation. Scalar and Writer arc tests are separate.

## Downstream PressureEqualizer

```powershell
python tests/interlocking/pressure_equalizer_regression.py --baseline C:/isolated/baseline/preFlight-console.exe --candidate C:/isolated/candidate/preFlight-console.exe --output C:/isolated/evidence/pressure-001
```

This additional gate enables positive/negative volumetric slope limits and
compares the same candidate's final output with smoothing off/on. It requires
identical ordered interlocking endpoints/E and non-increasing feeds, including
feeds below F60, absolute E and the second tool. Separate baseline controls
require unchanged motion/feeds when the volumetric limit or interlocking is off.
The ordinary paired matrix remains an independent gate with smoothing disabled.
The additional matrix also covers scarf XYZE and forced cooling. The latter may
dominate both runs and leave identical final feeds; all other enabled cases
must show an observed slowdown, not merely a configured slope limit.

For limited interlocking, PressureEqualizer applies its average correction to
the original whole segment. It does not subdivide or reformat E/XYZ: doing so
could invalidate the cap calculated from the generator's emitted coordinates.
The final integer feed is floored and cannot exceed the incoming cap, even
below the equalizer's legacy F60 minimum. This trades sub-segment smoothing
resolution on long interlocking moves for exact deposited geometry/volume.
Other extrusion roles and tools without a positive cap keep their existing path.
The filter is seeded with the initial tool (also between sequential objects),
because the header's tool-change command bypasses the layer postprocessor.

The auditor supports the synthetic fixture's explicit tool-coordinate model;
it does not infer real Klipper T macros. E precision is five decimal places.
The pass/fail decision uses the actual emitted flow with only a 1e-9 mm3/s
floating arithmetic tolerance. E rounding bounds are diagnostic only: emitted
E is a known command, not uncertainty that can excuse an overshoot. Explicit XYZ moves that
round to zero displacement use extruder-only timing; plain E-only unretractions
are excluded from deposition. Any unsupported mode is an error.

## Isolation and rollback

Keep experimental binaries outside the installed slicer directory. Launch any
GUI with an explicit new `--datadir`; never copy printer API keys or physical
printer profiles. A local build is not an official signed oozeBot release.
Returning to the existing installation requires only closing the isolated app;
no printer config or production profile changes belong to this test procedure.
