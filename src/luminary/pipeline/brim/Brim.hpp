///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/geometry/contours/Polygon.hpp" // Polygons is an alias, so the definition has to be visible here.
#include "luminary/toolpath/extrusion/ExtrusionEntityCollection.hpp" // The return type, returned by value.
#include "luminary/layer/print/PrintBase.hpp"                        // PrintTryCancel is taken by value.

namespace Luminary
{

class Print;

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area);

} // namespace Luminary
