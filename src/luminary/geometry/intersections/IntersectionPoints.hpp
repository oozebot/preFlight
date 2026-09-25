///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stdint.h>
#include <vector>
#include <cinttypes>

#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"

namespace Luminary
{

struct IntersectionLines
{
    uint32_t line_index1;
    uint32_t line_index2;
    Vec2d intersection;
};
using IntersectionsLines = std::vector<IntersectionLines>;

// collect all intersecting points
IntersectionsLines get_intersections(const Lines &lines);
IntersectionsLines get_intersections(const Polygon &polygon);
IntersectionsLines get_intersections(const Polygons &polygons);
IntersectionsLines get_intersections(const ExPolygon &expolygon);
IntersectionsLines get_intersections(const ExPolygons &expolygons);

} // namespace Luminary