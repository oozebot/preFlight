"""Unified API test script for the preFlight pre-processor.

Exercises every binding in the preFlight Python API and prints a PASS/FAIL report.

Write tests are always enabled. They make targeted modifications to specific moves
and use searchable markers so you can verify propagation in the exported G-code.

Search the exported G-code for these markers:
  ; PPTEST:FEEDRATE    - 3x, inserted before G1 lines with F600
  ; PPTEST:FAN         - 1x, inserted before G1 with M106 fan change
  ; PPTEST:TEMP        - 1x, inserted before G1 with M104 S195 (0x on profiles
                         where move.temperature reads 0, the test is skipped)
  ; PPTEST:DELTA_E     - 1x, inserted before G1 with doubled E value
  ; PPTEST:INSERT      - 1x, raw G-code inserted after a G1 line
  ; PPTEST:SET_LINE    - Nx, prefixed on all ;WIDTH: comment lines
  ; PPTEST:PREPEND     - 1x, injected before a layer's first move
  ; PPTEST:APPEND      - 1x, injected after a layer's last move
  ; PPTEST:MODIFIED    - 6x (5x when the temperature test is skipped), annotation
                         on all modified G1 lines

The modifications are small and targeted (a handful of moves) so they won't
destroy the print. To run without modifications, remove this script from
the preprocessor folder.
"""

import re
import sys
import preFlight
from preFlight import MoveType, ExtrusionRole, CustomEventType


def process(gcode: preFlight.GCode):
    if tuple(int(x) for x in re.sub(r'[^0-9.].*', '', preFlight.version).split(".")) < (0, 9, 13):
        print(f"[api_test] Requires preFlight 0.9.13+ (running {preFlight.version})")
        return

    ok = 0
    fail = 0
    results = []

    def check(name, condition, detail=""):
        nonlocal ok, fail
        if condition:
            ok += 1
            results.append(f"  PASS: {name}")
        else:
            fail += 1
            results.append(f"  FAIL: {name} - {detail}")

    print("=" * 60)
    print("preFlight Pre-Processor API Test")
    print(f"Python {sys.version} [{sys.executable or 'embedded'}]")
    print("=" * 60)

    # ---------------------------------------------------------------
    # 0. Module-level constants
    # ---------------------------------------------------------------
    print("\n--- Module Info ---")

    check("preFlight.version exists", hasattr(preFlight, 'version'))
    check("version is string", isinstance(preFlight.version, str))
    check("version is non-empty", len(preFlight.version) > 0)
    print(f"    preFlight version: {preFlight.version}")

    # ---------------------------------------------------------------
    # 1. Top-level GCode properties
    # ---------------------------------------------------------------
    print("\n--- GCode Properties ---")

    check("layers is list", isinstance(gcode.layers, list) and len(gcode.layers) > 0,
          f"got {type(gcode.layers)}, len={len(gcode.layers) if isinstance(gcode.layers, list) else '?'}")

    check("moves is list", isinstance(gcode.moves, list) and len(gcode.moves) > 0,
          f"len={len(gcode.moves) if isinstance(gcode.moves, list) else '?'}")

    check("max_print_height > 0", gcode.max_print_height > 0,
          f"got {gcode.max_print_height}")

    check("extruder_count >= 1", gcode.extruder_count >= 1,
          f"got {gcode.extruder_count}")

    check("extruder_colors is list", isinstance(gcode.extruder_colors, list),
          f"got {type(gcode.extruder_colors)}")

    check("spiral_vase_mode is bool", isinstance(gcode.spiral_vase_mode, bool),
          f"got {type(gcode.spiral_vase_mode)}")

    check("time_estimate_normal > 0", gcode.time_estimate_normal > 0,
          f"got {gcode.time_estimate_normal}")

    check("time_estimate_stealth accessible", gcode.time_estimate_stealth >= 0,
          f"got {gcode.time_estimate_stealth}")

    check("first_layer_time > 0", gcode.first_layer_time > 0,
          f"got {gcode.first_layer_time}")

    check("z_offset accessible", isinstance(gcode.z_offset, float),
          f"got {type(gcode.z_offset)}")

    check("bed_shape is list", isinstance(gcode.bed_shape, list),
          f"got {type(gcode.bed_shape)}")

    if gcode.bed_shape:
        pt = gcode.bed_shape[0]
        check("bed_shape contains tuples", isinstance(pt, tuple) and len(pt) == 2,
              f"got {type(pt)}")

    check("annotation is empty by default", gcode.annotation == "",
          f"got '{gcode.annotation}'")

    # ---------------------------------------------------------------
    # 1b. Slicer settings (full PrintConfig as dict)
    # ---------------------------------------------------------------
    print("\n--- Slicer Settings ---")

    settings = gcode.settings
    check("settings accessible", settings is not None)
    check("settings has entries", len(settings) > 0, f"got {len(settings)}")
    print(f"    {len(settings)} settings available")

    # Test dot notation access (IDE autocomplete works with this)
    check("settings.nozzle_diameter accessible", "nozzle_diameter" in settings)
    check("settings.layer_height accessible", "layer_height" in settings)
    check("settings.bed_shape accessible", "bed_shape" in settings)
    check("settings.travel_speed accessible", "travel_speed" in settings)
    check("settings.temperature accessible", "temperature" in settings)
    check("settings.retract_length accessible", "retract_length" in settings)

    # Both dot notation and bracket notation work
    check("dot and bracket match", settings.nozzle_diameter == settings["nozzle_diameter"])

    print(f"    nozzle_diameter = {settings.nozzle_diameter}")
    print(f"    layer_height = {settings.layer_height}")
    print(f"    travel_speed = {settings.travel_speed}")
    print(f"    temperature = {settings.temperature}")
    print(f"    bed_shape = {settings.bed_shape[:40]}...")

    # ---------------------------------------------------------------
    # 2. Filament configuration
    # ---------------------------------------------------------------
    print("\n--- Filament Configuration ---")

    check("filament_diameters is list", isinstance(gcode.filament_diameters, list),
          f"got {type(gcode.filament_diameters)}")

    if gcode.filament_diameters:
        check("filament_diameters[0] > 0", gcode.filament_diameters[0] > 0,
              f"got {gcode.filament_diameters[0]}")

    check("filament_densities is list", isinstance(gcode.filament_densities, list),
          f"got {type(gcode.filament_densities)}")

    # ---------------------------------------------------------------
    # 3. Filament usage stats
    # ---------------------------------------------------------------
    print("\n--- Filament Usage ---")

    fbr = gcode.filament_by_role
    check("filament_by_role is dict", isinstance(fbr, dict),
          f"got {type(fbr)}")

    if fbr:
        first_role = list(fbr.keys())[0]
        usage = fbr[first_role]
        check("FilamentUsage has meters", hasattr(usage, 'meters'),
              f"attrs: {dir(usage)}")
        check("FilamentUsage has grams", hasattr(usage, 'grams'))
        check("FilamentUsage has volume_mm3", hasattr(usage, 'volume_mm3'))
        check("FilamentUsage has cost", hasattr(usage, 'cost'))
        print(f"    First role: {first_role} -> {usage.meters:.2f}m, {usage.grams:.2f}g")

    fbe = gcode.filament_by_extruder
    check("filament_by_extruder is dict", isinstance(fbe, dict),
          f"got {type(fbe)}")

    if fbe:
        first_ext = list(fbe.keys())[0]
        usage = fbe[first_ext]
        print(f"    Extruder {first_ext}: {usage.volume_mm3:.1f}mm3, {usage.meters:.2f}m, cost={usage.cost:.2f}")

    fbcc = gcode.filament_by_color_change
    check("filament_by_color_change is list", isinstance(fbcc, list),
          f"got {type(fbcc)}")

    # ---------------------------------------------------------------
    # 4. Custom events
    # ---------------------------------------------------------------
    print("\n--- Custom Events ---")

    events = gcode.custom_events
    check("custom_events is list", isinstance(events, list),
          f"got {type(events)}")

    if events:
        evt = events[0]
        check("CustomEvent has z", hasattr(evt, 'z'))
        check("CustomEvent has type", hasattr(evt, 'type'))
        check("CustomEvent has extruder", hasattr(evt, 'extruder'))
        check("CustomEvent has color", hasattr(evt, 'color'))
        check("CustomEvent has extra", hasattr(evt, 'extra'))
        print(f"    First event: z={evt.z}, type={evt.type}, extruder={evt.extruder}")
    else:
        print("    (no custom events in this print)")

    # ---------------------------------------------------------------
    # 4b. Performance metrics and conflict detection
    # ---------------------------------------------------------------
    print("\n--- Performance Metrics ---")

    rm = gcode.role_metrics
    check("role_metrics is dict", isinstance(rm, dict), f"got {type(rm)}")
    if rm:
        first_role = next(iter(rm))
        check("role_metrics entry has max_commands_per_sec",
              "max_commands_per_sec" in rm[first_role])
        check("role_metrics entry has max_layer", "max_layer" in rm[first_role])
        print(f"    {len(rm)} roles with metrics, first: {first_role} = {rm[first_role]}")

    om = gcode.overall_metrics
    check("overall_metrics is dict", isinstance(om, dict))
    check("overall_metrics has max_commands_per_sec", "max_commands_per_sec" in om)
    check("overall_metrics has max_layer", "max_layer" in om)
    print(f"    Overall: {om}")

    conflict = gcode.conflict
    check("conflict is None or dict", conflict is None or isinstance(conflict, dict))
    if conflict:
        print(f"    Conflict: {conflict['object1']} vs {conflict['object2']} at z={conflict['height']}")
    else:
        print("    (no collision detected)")

    # ---------------------------------------------------------------
    # 5. Enum values
    # ---------------------------------------------------------------
    print("\n--- Enum Bindings ---")

    check("MoveType.Extrude exists", MoveType.Extrude is not None)
    check("MoveType.Travel exists", MoveType.Travel is not None)
    check("MoveType.Retract exists", MoveType.Retract is not None)
    check("MoveType.Noop exists", MoveType.Noop is not None)
    check("MoveType.Wipe exists", MoveType.Wipe is not None)
    check("MoveType.Seam exists", MoveType.Seam is not None)
    check("MoveType.ToolChange exists", MoveType.ToolChange is not None)
    check("MoveType.ColorChange exists", MoveType.ColorChange is not None)
    check("MoveType.PausePrint exists", MoveType.PausePrint is not None)
    check("MoveType.CustomGCode exists", MoveType.CustomGCode is not None)
    check("MoveType.Unretract exists", MoveType.Unretract is not None)

    check("ExtrusionRole.Perimeter exists", ExtrusionRole.Perimeter is not None)
    check("ExtrusionRole.ExternalPerimeter exists", ExtrusionRole.ExternalPerimeter is not None)
    check("ExtrusionRole.InternalInfill exists", ExtrusionRole.InternalInfill is not None)
    check("ExtrusionRole.SolidInfill exists", ExtrusionRole.SolidInfill is not None)
    check("ExtrusionRole.TopSolidInfill exists", ExtrusionRole.TopSolidInfill is not None)
    check("ExtrusionRole.BridgeInfill exists", ExtrusionRole.BridgeInfill is not None)
    check("ExtrusionRole.SupportMaterial exists", ExtrusionRole.SupportMaterial is not None)
    check("ExtrusionRole.SupportMaterialInterface exists", ExtrusionRole.SupportMaterialInterface is not None)
    check("ExtrusionRole.WipeTower exists", ExtrusionRole.WipeTower is not None)
    check("ExtrusionRole.OverhangPerimeter exists", ExtrusionRole.OverhangPerimeter is not None)
    check("ExtrusionRole.InterlockingPerimeter exists", ExtrusionRole.InterlockingPerimeter is not None)
    check("ExtrusionRole.GapFill exists", ExtrusionRole.GapFill is not None)
    check("ExtrusionRole.Skirt exists", ExtrusionRole.Skirt is not None)
    check("ExtrusionRole.Ironing exists", ExtrusionRole.Ironing is not None)
    check("ExtrusionRole.Custom exists", ExtrusionRole.Custom is not None)
    check("ExtrusionRole.NoRole exists", ExtrusionRole.NoRole is not None)

    check("CustomEventType.ColorChange exists", CustomEventType.ColorChange is not None)
    check("CustomEventType.PausePrint exists", CustomEventType.PausePrint is not None)
    check("CustomEventType.ToolChange exists", CustomEventType.ToolChange is not None)
    check("CustomEventType.Template exists", CustomEventType.Template is not None)
    check("CustomEventType.Custom exists", CustomEventType.Custom is not None)

    # ---------------------------------------------------------------
    # 6. Move properties (read-only)
    # ---------------------------------------------------------------
    print("\n--- Move Properties ---")

    test_move = None
    for m in gcode.moves:
        if m.type == MoveType.Extrude and m.distance > 0:
            test_move = m
            break

    if test_move:
        check("move.type accessible", test_move.type == MoveType.Extrude)
        check("move.role accessible", test_move.role is not None)
        check("move.x is float", isinstance(test_move.x, float))
        check("move.y is float", isinstance(test_move.y, float))
        check("move.z is float", isinstance(test_move.z, float))
        check("move.extruder_id accessible", test_move.extruder_id >= 0)
        check("move.color_id accessible", isinstance(test_move.color_id, int))
        check("move.mm3_per_mm accessible", isinstance(test_move.mm3_per_mm, float))
        check("move.actual_feedrate accessible", test_move.actual_feedrate >= 0)
        check("move.volumetric_rate accessible", isinstance(test_move.volumetric_rate, float))
        check("move.actual_volumetric_rate accessible", isinstance(test_move.actual_volumetric_rate, float))
        check("move.gcode_line_id > 0", test_move.gcode_line_id > 0)
        check("move.layer_id accessible", isinstance(test_move.layer_id, int))
        check("move.internal_only is bool", isinstance(test_move.internal_only, bool))
        check("move.time >= 0", test_move.time >= 0)
        check("move.time_stealth >= 0", test_move.time_stealth >= 0)
        check("move.feedrate > 0", test_move.feedrate > 0, f"got {test_move.feedrate}")
        check("move.fan_speed accessible", isinstance(test_move.fan_speed, float))
        check("move.temperature accessible", isinstance(test_move.temperature, float))
        check("move.delta_e accessible", isinstance(test_move.delta_e, float))
        check("move.width accessible", isinstance(test_move.width, float))
        check("move.height accessible", isinstance(test_move.height, float))

        # Motion analysis properties
        check("move.distance >= 0", test_move.distance >= 0, f"got {test_move.distance}")
        check("move.distance > 0 for extrusion", test_move.distance > 0, f"got {test_move.distance}")
        check("move.junction_angle is float", isinstance(test_move.junction_angle, float))
        check("move.acceleration >= 0", test_move.acceleration >= 0, f"got {test_move.acceleration}")
        check("move.acceleration_stealth >= 0", test_move.acceleration_stealth >= 0)
        check("move.max_entry_speed >= 0", test_move.max_entry_speed >= 0, f"got {test_move.max_entry_speed}")
        check("move.max_entry_speed_stealth >= 0", test_move.max_entry_speed_stealth >= 0)

        # Verify junction angle range (should be -180 to 180)
        check("junction_angle in range", -180.0 <= test_move.junction_angle <= 180.0,
              f"got {test_move.junction_angle}")

        # Fill region properties
        check("move.region_area is float", isinstance(test_move.region_area, float))
        check("move.region_area >= 0", test_move.region_area >= 0, f"got {test_move.region_area}")
        check("move.fill_pattern is str", isinstance(test_move.fill_pattern, str))

        print(f"    Sample move: x={test_move.x:.1f} y={test_move.y:.1f} z={test_move.z:.2f} "
              f"F={test_move.feedrate:.1f} role={test_move.role}")
        print(f"    Motion: dist={test_move.distance:.3f}mm jA={test_move.junction_angle:.1f}deg "
              f"accel={test_move.acceleration:.0f}mm/s^2 maxEntry={test_move.max_entry_speed:.1f}mm/s")
        print(f"    Region: area={test_move.region_area:.1f}mm^2 pattern='{test_move.fill_pattern}'")

        # Find an infill move to verify fill_pattern and region_area are populated
        infill_move = None
        for m in gcode.moves:
            if m.type == MoveType.Extrude and m.role == ExtrusionRole.InternalInfill and m.region_area > 0:
                infill_move = m
                break
        if infill_move:
            check("infill region_area > 0", infill_move.region_area > 0,
                  f"got {infill_move.region_area}")
            check("infill fill_pattern non-empty", len(infill_move.fill_pattern) > 0,
                  f"got '{infill_move.fill_pattern}'")
            print(f"    Infill move: area={infill_move.region_area:.1f}mm^2 "
                  f"pattern='{infill_move.fill_pattern}'")
        else:
            print("    (no infill moves with region data found)")

    else:
        check("found extrusion move", False, "no Extrude moves found")

    # ---------------------------------------------------------------
    # 7. Layer properties and methods
    # ---------------------------------------------------------------
    print("\n--- Layer Properties ---")

    test_layer = None
    for layer in gcode.layers:
        if len(layer.moves) > 5:
            test_layer = layer
            break

    if test_layer:
        check("layer.id accessible", isinstance(test_layer.id, int))
        check("layer.z > 0", test_layer.z > 0, f"got {test_layer.z}")
        check("layer.height accessible", isinstance(test_layer.height, float))
        check("layer.time > 0", test_layer.time > 0, f"got {test_layer.time}")
        check("layer.moves is list", isinstance(test_layer.moves, list) and len(test_layer.moves) > 0)

        extrusions = test_layer.moves_by_type(MoveType.Extrude)
        check("moves_by_type returns list", isinstance(extrusions, list))
        travels = test_layer.moves_by_type(MoveType.Travel)
        check("moves_by_type(Travel) returns list", isinstance(travels, list))

        perimeters = test_layer.moves_by_role(ExtrusionRole.Perimeter)
        check("moves_by_role returns list", isinstance(perimeters, list))
        ext_perimeters = test_layer.moves_by_role(ExtrusionRole.ExternalPerimeter)
        check("moves_by_role(ExternalPerimeter) returns list", isinstance(ext_perimeters, list))

        ext_len = test_layer.extrusion_length()
        check("extrusion_length returns float", isinstance(ext_len, float))
        check("extrusion_length > 0", ext_len > 0, f"got {ext_len}")

        trav_dist = test_layer.travel_distance()
        check("travel_distance returns float", isinstance(trav_dist, float))

        print(f"    Layer {test_layer.id}: z={test_layer.z:.2f} moves={len(test_layer.moves)} "
              f"extrusions={len(extrusions)} travels={len(travels)} "
              f"ext_length={ext_len:.1f}mm travel_dist={trav_dist:.1f}mm")
    else:
        check("found test layer", False, "no layers with moves")

    # ---------------------------------------------------------------
    # 8. find_moves()
    # ---------------------------------------------------------------
    print("\n--- find_moves() ---")

    found = gcode.find_moves(type=MoveType.Extrude)
    check("find_moves(type=Extrude) returns list", isinstance(found, list))
    check("find_moves(type=Extrude) has results", len(found) > 0, f"got {len(found)}")

    found_role = gcode.find_moves(role=ExtrusionRole.ExternalPerimeter)
    check("find_moves(role=ExternalPerimeter)", isinstance(found_role, list))

    found_z = gcode.find_moves(type=MoveType.Extrude, z_min=1.0, z_max=2.0)
    check("find_moves with z range", isinstance(found_z, list))

    found_ext = gcode.find_moves(extruder=0)
    check("find_moves(extruder=0)", isinstance(found_ext, list) and len(found_ext) > 0)

    # Combined filters
    found_combo = gcode.find_moves(type=MoveType.Extrude, role=ExtrusionRole.Perimeter, z_min=5.0)
    check("find_moves combined filters", isinstance(found_combo, list))

    print(f"    Extrude moves: {len(found)}")
    print(f"    ExternalPerimeter moves: {len(found_role)}")
    print(f"    z=1-2mm moves: {len(found_z)}")
    print(f"    Extruder 0 moves: {len(found_ext)}")
    print(f"    Perimeter+Extrude z>5mm: {len(found_combo)}")

    # ---------------------------------------------------------------
    # 9. get_line() (raw G-code access)
    # ---------------------------------------------------------------
    print("\n--- get_line() ---")

    if test_move:
        raw_line = gcode.get_line(test_move.gcode_line_id)
        check("get_line returns string", isinstance(raw_line, str))
        check("get_line returns non-empty", len(raw_line) > 0, f"got '{raw_line}'")
        has_g = raw_line.strip().startswith("G0") or raw_line.strip().startswith("G1")
        check("get_line returns G-code", has_g, f"got '{raw_line.strip()[:40]}'")
        print(f"    Line {test_move.gcode_line_id}: {raw_line.strip()[:60]}")

    check("get_line(0) returns empty", gcode.get_line(0) == "",
          f"got '{gcode.get_line(0)}'")

    # ---------------------------------------------------------------
    # 9b. line_count, find_line(), find_lines()
    # ---------------------------------------------------------------
    print("\n--- line_count / find_line / find_lines ---")

    check("line_count > 0", gcode.line_count > 0, f"got {gcode.line_count}")
    print(f"    Virtual file has {gcode.line_count} lines")

    layer_change_1 = gcode.find_line(";LAYER_CHANGE:")
    check("find_line(LAYER_CHANGE) > 0", layer_change_1 > 0, f"got {layer_change_1}")
    if layer_change_1 > 0:
        print(f"    First ;LAYER_CHANGE at line {layer_change_1}: {gcode.get_line(layer_change_1).strip()}")

    all_layer_changes = gcode.find_lines(";LAYER_CHANGE:")
    check("find_lines(LAYER_CHANGE) returns list", isinstance(all_layer_changes, list))
    check("find_lines(LAYER_CHANGE) has results", len(all_layer_changes) > 0,
          f"got {len(all_layer_changes)}")
    print(f"    Found {len(all_layer_changes)} ;LAYER_CHANGE lines")

    check("find_line(nonexistent) returns 0",
          gcode.find_line("THIS_WILL_NEVER_MATCH_ANYTHING") == 0)

    check("find_lines(nonexistent) returns empty",
          len(gcode.find_lines("THIS_WILL_NEVER_MATCH_ANYTHING")) == 0)

    # Track gcode_line_ids already used by write tests so each test targets a unique line
    used_line_ids = set()
    modified_move_count = 0  # count of moves whose properties were actually changed

    def find_unique_target(start_layer, extra_filter=None):
        """Find an extrusion move with a unique gcode_line_id starting from start_layer."""
        for layer in gcode.layers:
            if layer.id < start_layer:
                continue
            for m in layer.moves:
                if (m.type == MoveType.Extrude
                        and not m.internal_only
                        and m.gcode_line_id not in used_line_ids
                        and (extra_filter is None or extra_filter(m))):
                    used_line_ids.add(m.gcode_line_id)
                    return m
        return None

    # ---------------------------------------------------------------
    # 10. Write tests - FEEDRATE (search G-code for PPTEST:FEEDRATE)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: Feedrate ---")

    feedrate_targets = []
    for _ in range(3):
        t = find_unique_target(3, lambda m: m.feedrate > 5.0)
        if t:
            feedrate_targets.append(t)

    for m in feedrate_targets:
        original = m.feedrate
        m.feedrate = 10.0  # Set to exactly 10 mm/s = F600 in G-code
        gcode.insert(m.gcode_line_id, "; PPTEST:FEEDRATE", "before")
        modified_move_count += 1
        print(f"    Line {m.gcode_line_id}: F{original:.1f} -> F10.0 (expect F600 in G-code)")
    check("feedrate modification", len(feedrate_targets) == 3,
          f"found {len(feedrate_targets)} targets")

    # ---------------------------------------------------------------
    # 11. Write tests - FAN SPEED (search G-code for M106 near PPTEST)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: Fan Speed ---")

    fan_target = find_unique_target(5, lambda m: m.fan_speed == 0.0)
    if not fan_target:
        fan_target = find_unique_target(5)

    if fan_target:
        original_fan = fan_target.fan_speed
        new_fan = 75.0 if original_fan == 0.0 else 42.0
        fan_target.fan_speed = new_fan
        gcode.insert(fan_target.gcode_line_id, "; PPTEST:FAN", "before")
        modified_move_count += 1
        expected_s = int(new_fan * 255.0 / 100.0)
        print(f"    Line {fan_target.gcode_line_id}: fan {original_fan}% -> {new_fan}% (expect M106 S{expected_s})")
        check("fan_speed modification", True)
    else:
        check("fan_speed modification", False, "no target found")

    # ---------------------------------------------------------------
    # 12. Write tests - TEMPERATURE (search G-code for M104 near PPTEST)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: Temperature ---")

    temp_target = find_unique_target(7, lambda m: m.temperature > 0)

    if temp_target:
        original_temp = temp_target.temperature
        temp_target.temperature = 195.0
        gcode.insert(temp_target.gcode_line_id, "; PPTEST:TEMP", "before")
        modified_move_count += 1
        print(f"    Line {temp_target.gcode_line_id}: temp {original_temp:.0f}C -> 195C (expect M104 S195)")
        check("temperature modification", True)
    else:
        # Profiles that set the nozzle temperature through a macro or G10 leave
        # move.temperature at 0 everywhere; there is nothing to modify, not a failure.
        print("    (skipped: no move carries a temperature on this profile, see Notes in HOW_TO_USE.txt)")

    # ---------------------------------------------------------------
    # 13. Write tests - DELTA_E (search G-code for PPTEST:DELTA_E)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: Delta E ---")

    delta_e_target = find_unique_target(9, lambda m: m.delta_e > 0.01)

    if delta_e_target:
        original_de = delta_e_target.delta_e
        delta_e_target.delta_e = original_de * 2.0
        gcode.insert(delta_e_target.gcode_line_id, "; PPTEST:DELTA_E", "before")
        modified_move_count += 1
        print(f"    Line {delta_e_target.gcode_line_id}: "
              f"delta_e {original_de:.5f} -> {delta_e_target.delta_e:.5f} (expect E{delta_e_target.delta_e:.5f})")
        check("delta_e modification", delta_e_target.delta_e != original_de)
    else:
        check("delta_e modification", False, "no target found")

    # ---------------------------------------------------------------
    # 14. Write tests - INSERT_GCODE (search G-code for PPTEST:INSERT)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: insert() ---")

    insert_target = find_unique_target(11)

    if insert_target:
        gcode.insert(insert_target.gcode_line_id,
                     "; PPTEST:INSERT - this line was injected by insert()")
        print(f"    Line {insert_target.gcode_line_id}: inserted comment after this G1")
        check("insert", True)
    else:
        check("insert", False, "no target found")

    # ---------------------------------------------------------------
    # 15. Write tests - SET_LINE (search G-code for PPTEST:SET_LINE)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: rewrite() ---")

    # Rewrite ALL ;WIDTH: comment lines - demonstrates find_lines() + rewrite() bulk replacement
    width_lines = gcode.find_lines(";WIDTH:")
    rewrite_count = 0
    for line_id in width_lines:
        original = gcode.get_line(line_id).strip()
        gcode.rewrite(line_id, "; PPTEST:SET_LINE " + original)
        rewrite_count += 1

    check("rewrite (bulk)", rewrite_count > 0, f"no ;WIDTH: lines found")
    check("rewrite count matches find_lines", rewrite_count == len(width_lines))
    print(f"    Rewrote {rewrite_count} ;WIDTH: lines with PPTEST:SET_LINE prefix")

    # ---------------------------------------------------------------
    # 16. Write tests - LAYER PREPEND/APPEND (search for PPTEST:PREPEND/APPEND)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: layer.prepend() / layer.append() ---")

    prepend_layer = None
    for layer in gcode.layers:
        if layer.id >= 5 and len(layer.moves) > 10:
            prepend_layer = layer
            break

    if prepend_layer:
        prepend_layer.prepend("; PPTEST:PREPEND - injected before layer " + str(prepend_layer.id))
        prepend_layer.append("; PPTEST:APPEND - injected after layer " + str(prepend_layer.id))
        first_id = prepend_layer.moves[0].gcode_line_id
        last_id = prepend_layer.moves[-1].gcode_line_id
        print(f"    Layer {prepend_layer.id} (z={prepend_layer.z:.2f}mm): "
              f"prepend before line {first_id}, append after line {last_id}")
        check("layer.prepend", True)
        check("layer.append", True)
    else:
        check("layer.prepend", False, "no suitable layer")
        check("layer.append", False, "no suitable layer")

    # ---------------------------------------------------------------
    # 17. Write tests - REMOVE_MOVES
    # ---------------------------------------------------------------
    print("\n--- Write Tests: remove_moves() ---")

    # Remove nothing (predicate always false)
    removed_zero = gcode.remove_moves(lambda m: False)
    check("remove_moves(always-false) returns 0", removed_zero == 0,
          f"got {removed_zero}")

    # Remove wipe moves in layer 10+ (safe - wipes are cosmetic)
    wipe_layer_id = 10
    removed_wipes = gcode.remove_moves(
        lambda m: m.type == MoveType.Wipe and m.layer_id == wipe_layer_id
    )
    print(f"    Removed {removed_wipes} wipe moves from layer {wipe_layer_id}")
    check("remove_moves(wipes)", removed_wipes >= 0)  # 0 is valid if no wipes

    # ---------------------------------------------------------------
    # 18. Annotation test (global - applied to ALL modified lines at script exit)
    # ---------------------------------------------------------------
    print("\n--- Write Tests: Annotation ---")

    # Annotation is global: whatever value gcode.annotation holds when process()
    # returns gets appended as a comment to every modified G1 line from this script.
    # Set it to a searchable marker so we can verify it appears in the G-code.
    gcode.annotation = "PPTEST:MODIFIED"
    check("annotation set", gcode.annotation == "PPTEST:MODIFIED")

    print(f"    Global annotation set to '{gcode.annotation}'")
    print(f"    Expect {modified_move_count} G1 lines with '; PPTEST:MODIFIED' in exported G-code")

    # ---------------------------------------------------------------
    # Summary
    # ---------------------------------------------------------------
    # ---------------------------------------------------------------
    # 19. API contract: version, error surfacing, layer geometry
    # ---------------------------------------------------------------
    print("\n--- API contract ---")

    check("preFlight.api_version exists", hasattr(preFlight, 'api_version'))
    check("api_version is a positive int",
          isinstance(getattr(preFlight, 'api_version', None), int) and preFlight.api_version >= 1)

    def raises_value_error(fn):
        try:
            fn()
        except ValueError:
            return True
        except Exception:
            return False
        return False

    check("rewrite() out of range raises ValueError",
          raises_value_error(lambda: gcode.rewrite(gcode.line_count + 1000, "; never")))
    check("rewrite() line 0 raises ValueError",
          raises_value_error(lambda: gcode.rewrite(0, "; never")))
    check("insert() out of range raises ValueError",
          raises_value_error(lambda: gcode.insert(gcode.line_count + 1000, "; never")))
    check("insert() unknown position raises ValueError",
          raises_value_error(lambda: gcode.insert(1, "; never", "befroe")))

    # layer.z is the Z the layer extrudes at, never inflated by a z-hop travel
    z_inflated = 0
    negative_heights = 0
    for layer in gcode.layers:
        ext_z = [m.z for m in layer.moves if m.type == MoveType.Extrude]
        if ext_z and layer.z > max(ext_z) + 1e-4:
            z_inflated += 1
        if layer.id > 0 and layer.moves and layer.height < -1e-4:
            negative_heights += 1
    check("layer.z never exceeds the layer's extrusion Z", z_inflated == 0, f"{z_inflated} layers")
    check("no negative layer.height", negative_heights == 0, f"{negative_heights} layers")

    # Layer text ranges: contiguous and ordered, each printed layer holds its own ;LAYER_CHANGE
    prev_last = 0
    range_ok = True
    marker_layers = 0
    marker_ok = 0
    for layer in gcode.layers:
        if layer.first_line != prev_last + 1 or layer.last_line < prev_last:
            range_ok = False
        prev_last = max(prev_last, layer.last_line)
        if layer.moves and layer.id > 0:
            marker_layers += 1
            if layer.find_line(";LAYER_CHANGE") != 0:
                marker_ok += 1
    check("layer line ranges are contiguous and ordered", range_ok)
    check("every printed layer's range holds its ;LAYER_CHANGE marker",
          marker_layers > 0 and marker_ok == marker_layers, f"{marker_ok}/{marker_layers}")
    mid = gcode.layers[len(gcode.layers) // 2]
    check("layer.find_lines equals gcode.find_lines over the layer's range",
          mid.find_lines(";TYPE:") == gcode.find_lines(";TYPE:", mid.first_line, mid.last_line))
    mid_lines = mid.lines()
    check("layer.lines() spans first_line..last_line",
          len(mid_lines) == mid.last_line - mid.first_line + 1 and mid_lines
          and mid_lines[0][0] == mid.first_line and mid_lines[-1][0] == mid.last_line)
    check("gcode.find_line honours start/end",
          gcode.find_line(";LAYER_CHANGE", mid.first_line, mid.last_line) == mid.find_line(";LAYER_CHANGE"))

    print("\n" + "=" * 60)
    total = ok + fail
    print(f"RESULTS: {ok}/{total} passed, {fail} failed")
    if fail > 0:
        print("\nFailed tests:")
        for r in results:
            if r.startswith("  FAIL"):
                print(r)
    print("=" * 60)

    # Verification summary
    print(f"\nVERIFICATION: Search the exported G-code for PPTEST markers:")
    print(f"  PPTEST:FEEDRATE   x{len(feedrate_targets)}")
    print(f"  PPTEST:FAN        x{'1' if fan_target else '0'}")
    print(f"  PPTEST:TEMP       x{'1' if temp_target else '0'}")
    print(f"  PPTEST:DELTA_E    x{'1' if delta_e_target else '0'}")
    print(f"  PPTEST:INSERT     x{'1' if insert_target else '0'}")
    print(f"  PPTEST:SET_LINE   x{rewrite_count}")
    print(f"  PPTEST:PREPEND    x{'1' if prepend_layer else '0'}")
    print(f"  PPTEST:APPEND     x{'1' if prepend_layer else '0'}")
    print(f"  PPTEST:MODIFIED   x{modified_move_count}")

    # Stats
    total_moves = len(gcode.moves)
    total_layers = len(gcode.layers)
    extrude_count = sum(1 for m in gcode.moves if m.type == MoveType.Extrude)
    travel_count = sum(1 for m in gcode.moves if m.type == MoveType.Travel)
    print(f"\nPrint stats: {total_moves} moves, {total_layers} layers, "
          f"{extrude_count} extrusions, {travel_count} travels")
    print(f"Print height: {gcode.max_print_height:.2f}mm")
    print(f"Estimated time: {gcode.time_estimate_normal:.0f}s "
          f"({gcode.time_estimate_normal/60:.1f}min)")

    # Find smallest region area in the print
    smallest_move = None
    for m in gcode.moves:
        if m.type == MoveType.Extrude and m.region_area > 0:
            if smallest_move is None or m.region_area < smallest_move.region_area:
                smallest_move = m
    if smallest_move:
        print(f"\nSmallest region: {smallest_move.region_area:.2f}mm^2 "
              f"z={smallest_move.z:.2f}mm layer={smallest_move.layer_id} "
              f"role={smallest_move.role.name} pattern='{smallest_move.fill_pattern}'")
    else:
        print("\nSmallest region: (no moves with region data)")

    # Dump all slicer settings
    print(f"\n--- All Settings ({len(settings)} keys) ---")
    for key in sorted(settings.keys()):
        val = settings[key]
        if len(val) > 80:
            val = val[:77] + "..."
        print(f"  {key} = {val}")
