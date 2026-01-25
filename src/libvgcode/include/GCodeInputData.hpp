///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef VGCODE_GCODEINPUTDATA_HPP
#define VGCODE_GCODEINPUTDATA_HPP

#include "PathVertex.hpp"
#include <functional>

namespace libvgcode
{

struct GCodeInputData
{
    //
    // Whether or not the gcode was generated with spiral vase mode enabled.
    // Required to properly detect fictitious layer changes when spiral vase mode is enabled.
    //
    bool spiral_vase_mode{false};
    //
    // List of path vertices (gcode moves)
    // See: PathVertex
    //
    std::vector<PathVertex> vertices;
    //
    // Palette for extruders colors
    //
    Palette tools_colors;
    //
    // Palette for color print colors
    //
    Palette color_print_colors;
    //
    // Optional callback to yield control to UI thread during long operations
    // Called periodically during vertex processing and GPU upload
    // Parameter: progress percentage (0.0 to 1.0), -1.0 means just yield without progress update
    //
    std::function<void(float)> progress_callback;
};

} // namespace libvgcode

#endif // VGCODE_BITSET_HPP
