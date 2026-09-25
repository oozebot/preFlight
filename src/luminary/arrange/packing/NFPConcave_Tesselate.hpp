///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <luminary/geometry/contours/ExPolygon.hpp>

#include "luminary/geometry/contours/Polygon.hpp"

namespace Luminary
{

Polygons convex_decomposition_tess(const Polygon &expoly);
Polygons convex_decomposition_tess(const ExPolygon &expoly);
Polygons convex_decomposition_tess(const ExPolygons &expolys);
ExPolygons nfp_concave_concave_tess(const ExPolygon &fixed, const ExPolygon &movable);

} // namespace Luminary
