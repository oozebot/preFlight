"""Optimize print quality on overhangs.

Sample script - adjust the constants below for your printer and material.

Detects layers with overhang perimeters and applies aggressive cooling
and speed reduction. Also slows external perimeters on those layers
since they share the same challenging geometry.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

OVERHANG_SPEED_FACTOR = 0.5     # 50% of normal speed
EXTERNAL_SPEED_FACTOR = 0.7     # 70% on external perimeters in overhang layers
OVERHANG_FAN = 100              # full blast
OVERHANG_TEMP_DROP = 10         # degrees below normal
BRIDGE_SPEED_FACTOR = 0.4       # bridges need even more care


def process(gcode: preFlight.GCode):
    base_temp = int(gcode.settings.temperature.split(";")[0])
    modified_layers = 0

    for layer in gcode.layers:
        overhang_moves = layer.moves_by_role(ExtrusionRole.OverhangPerimeter)
        has_overhangs = len(overhang_moves) > 0

        if has_overhangs:
            modified_layers += 1
            for move in layer.moves:
                if move.type != MoveType.Extrude:
                    continue

                if move.role == ExtrusionRole.OverhangPerimeter:
                    move.annotation = "overhang"
                    move.feedrate *= OVERHANG_SPEED_FACTOR
                    move.fan_speed = OVERHANG_FAN
                    move.temperature = base_temp - OVERHANG_TEMP_DROP

                elif move.role == ExtrusionRole.ExternalPerimeter:
                    move.annotation = "external perimeter cooling"
                    move.feedrate *= EXTERNAL_SPEED_FACTOR
                    move.fan_speed = max(move.fan_speed, 80)

                elif move.role == ExtrusionRole.BridgeInfill:
                    move.annotation = "bridge"
                    move.feedrate *= BRIDGE_SPEED_FACTOR
                    move.fan_speed = OVERHANG_FAN
                    move.temperature = base_temp - OVERHANG_TEMP_DROP

        # Fan and temperature return to their original values automatically before
        # the first unmodified extruding move after the tuned moves; no manual restore.

    print(f"[overhang_optimizer] Tuned {modified_layers} layers with overhangs")
