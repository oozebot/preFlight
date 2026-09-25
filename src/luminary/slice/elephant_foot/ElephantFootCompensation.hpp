///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
///|/ Provisional module: elephant foot compensation belongs with geometry by subject, and sits in slice because it takes a Flow.
///|/
#pragma once

#include <vector>

#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"

namespace Luminary
{

class Flow;

ExPolygon elephant_foot_compensation(const ExPolygon &input, double min_countour_width, const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons &input, double min_countour_width, const double compensation);
ExPolygon elephant_foot_compensation(const ExPolygon &input, const Flow &external_perimeter_flow,
                                     const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons &input, const Flow &external_perimeter_flow,
                                      const double compensation);

} // namespace Luminary
