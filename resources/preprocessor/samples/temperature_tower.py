"""Generate a temperature tower from a single-temp sliced model.

Sample script - set TEMPS to the range you want to test.

Slices at your baseline temperature, then this script steps through
temperature changes at specified layer intervals. Works with any
model - no need for special temperature tower STLs.
"""

import preFlight
from preFlight import MoveType

# Temperature steps from bottom to top
TEMPS = [220, 215, 210, 205, 200, 195]

# How many layers per temperature step
LAYERS_PER_STEP = 25

# Skip this many layers before starting (raft, brim, first layers)
SKIP_LAYERS = 5


def process(gcode: preFlight.GCode):
    total_layers = len(gcode.layers)
    needed = SKIP_LAYERS + len(TEMPS) * LAYERS_PER_STEP
    if total_layers < needed:
        print(f"[temp_tower] WARNING: model has {total_layers} layers, "
              f"need {needed} for {len(TEMPS)} steps. "
              f"Reduce LAYERS_PER_STEP or use a taller model.")

    changes = 0
    for layer in gcode.layers:
        if layer.id <= SKIP_LAYERS:
            continue

        step = (layer.id - SKIP_LAYERS - 1) // LAYERS_PER_STEP
        if step >= len(TEMPS):
            break

        target_temp = TEMPS[step]

        # Set temperature for all extrusion moves in this layer
        for move in layer.moves:
            if move.type == MoveType.Extrude:
                move.annotation = f"temp tower {target_temp}C"
                move.temperature = target_temp

        # The M104 for the transition is written automatically before the first
        # move whose temperature changes; only count the step boundaries here.
        if (layer.id - SKIP_LAYERS - 1) % LAYERS_PER_STEP == 0:
            changes += 1

    print(f"[temp_tower] Applied {changes} temperature steps: "
          f"{', '.join(f'{t}C' for t in TEMPS[:changes])}")
