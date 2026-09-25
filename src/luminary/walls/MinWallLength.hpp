///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/core/Prelude.hpp"

namespace Luminary
{

// The floor under which an odd open wall is not printed: the unpaired centre bead the beading
// strategy places between two features, left open by the stitcher. The engine floor is half the
// wall's own narrowest width. The Minimum wall length setting raises it, as a percentage of that
// width or as an absolute length. A surface layer (the first or the topmost) keeps the engine
// floor whatever the setting says, so its skin does not open gaps. Fill-side wall generation
// never sets it, so concentric and ensuring fills keep the engine floor.
struct MinWallLength
{
    bool percent = true;        // `value` is a share of the wall's own width, else a scaled length
    double value = 50.;         // the engine floor
    bool surface_layer = false; // the first or the topmost layer of the object

    // The length under which a wall whose narrowest bead is `min_width` is dropped. At the default
    // this is `min_width / 2` exactly: the product of an integer and 50 divided by 100 is an integer
    // or an integer and a half, both exact in a double, and the truncation floors it.
    coord_t threshold(coord_t min_width) const
    {
        if (surface_layer)
            return min_width / 2;
        if (percent)
            return coord_t(double(min_width) * value / 100.);
        return coord_t(value);
    }
};

} // namespace Luminary
