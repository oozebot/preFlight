///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/walls/athena/paths/ExtrusionLine.hpp"

namespace Luminary::VaseWall
{

// How far the nearer end of a fin may sit from the loop and still be spliced into it, in external
// perimeter widths. Safe range 1.0 to 2.0: detached fins sit up to about one width off the loop.
inline constexpr double fin_attach_tolerance_widths = 1.5;

struct FinReport
{
    enum class Outcome
    {
        Spliced,
        Slot,
        DroppedFar,
        DroppedCrossing
    };
    Outcome outcome = Outcome::Spliced;
    coord_t length = 0; // the fin's own centerline length
    coord_t min_width = 0;
    coord_t max_width = 0;
    coord_t gap = 0;  // distance of the attachment end from the loop (0 when on it)
    bool odd = false; // the line's is_odd flag
};

struct Report
{
    size_t loops = 0; // closed lines found; above one the layer is left untouched
    std::vector<FinReport> fins;
    bool changed = false;
    bool degenerate_loop = false; // the one closed line has no ring of segments; nothing was touched
};

// In a vase layer the wall must be one closed loop for the spiral to continue. Every open line
// (a fin: a thin section printed as a single center bead) is folded into the loop as an
// out-and-back excursion at half its width, offset +-w/4 from its centerline, so the volume is
// unchanged; a lone fin with no loop becomes a closed slot loop the same way. A fin whose
// nearer end is farther than `attach_tolerance` from the loop, or whose excursion would cross
// the loop, is dropped. With two or more closed loops nothing is touched. A pass is never
// narrower than `min_pass_width`, the thinnest bead the path conversion keeps: a segment under
// it would be skipped and open the loop, so a fin thinner than twice the floor gains material.
Report make_single_loop(Athena::Perimeters &perimeters, coord_t attach_tolerance, coord_t min_pass_width);

} // namespace Luminary::VaseWall
