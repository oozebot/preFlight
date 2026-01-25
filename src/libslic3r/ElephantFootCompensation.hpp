///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ElephantFootCompensation_hpp_
#define slic3r_ElephantFootCompensation_hpp_

#include <vector>

#include "libslic3r.h"
#include "ExPolygon.hpp"

namespace Slic3r
{

class Flow;

ExPolygon elephant_foot_compensation(const ExPolygon &input, double min_countour_width, const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons &input, double min_countour_width, const double compensation);
ExPolygon elephant_foot_compensation(const ExPolygon &input, const Flow &external_perimeter_flow,
                                     const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons &input, const Flow &external_perimeter_flow,
                                      const double compensation);

} // namespace Slic3r

#endif /* slic3r_ElephantFootCompensation_hpp_ */
