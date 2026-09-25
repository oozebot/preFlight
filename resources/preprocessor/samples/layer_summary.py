"""Walk the G-code layer by layer and annotate every layer with a summary comment.

Sample script - a demonstration of the layer text range API. Safe on any
model: it only adds comments.

Every Layer owns a range of raw G-code lines (layer.first_line up to
layer.last_line: from the line after the previous layer's last move through
its own last move, so the layer-change block that introduces the layer is
included). The range can be searched with layer.find_line() and
layer.find_lines() or listed with layer.lines(), which is how a script
reaches the text the move API does not cover: ";TYPE:" markers, comments,
temperature commands, custom G-code blocks.

This script counts the feature blocks inside each layer, notes whether the
layer contains bridge infill, and inserts one summary comment before the
layer's ";LAYER_CHANGE" marker:

    ; LAYER 42 z=8.60 h=0.20 t=31.4s features=5 bridge=yes

The same ranges make targeted edits cheap. To boost the fan on bridges only:

    for layer in gcode.layers:
        for line_id in layer.find_lines(";TYPE:Bridge infill"):
            gcode.insert(line_id, "M106 S255", "after")

instead of scanning the whole file once per layer.
"""

import preFlight


def process(gcode: preFlight.GCode):
    annotated = 0
    for layer in gcode.layers:
        if not layer.moves or layer.last_line < layer.first_line:
            continue  # a layer without moves owns no text
        marker = layer.find_line(";LAYER_CHANGE")
        if marker == 0:
            continue  # the pre-print moves before the first layer change
        features = len(layer.find_lines(";TYPE:"))
        bridge = layer.find_line(";TYPE:Bridge infill") != 0
        gcode.insert(marker,
                     f"; LAYER {layer.id} z={layer.z:.2f} h={layer.height:.2f} "
                     f"t={layer.time:.1f}s features={features} bridge={'yes' if bridge else 'no'}",
                     "before")
        annotated += 1
    print(f"[layer_summary] annotated {annotated} of {len(gcode.layers)} layers")
