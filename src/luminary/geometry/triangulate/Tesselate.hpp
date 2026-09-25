///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <admesh/stl.h>
#include <vector>

#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary
{

const bool constexpr NORMALS_UP = false;
const bool constexpr NORMALS_DOWN = true;

extern std::vector<Vec3d> triangulate_expolygon_3d(const ExPolygon &poly, coordf_t z = 0, bool flip = NORMALS_UP);
extern std::vector<Vec3d> triangulate_expolygons_3d(const ExPolygons &polys, coordf_t z = 0, bool flip = NORMALS_UP);
extern std::vector<Vec2d> triangulate_expolygon_2d(const ExPolygon &poly, bool flip = NORMALS_UP);
extern std::vector<Vec2d> triangulate_expolygons_2d(const ExPolygons &polys, bool flip = NORMALS_UP);
extern std::vector<Vec2f> triangulate_expolygon_2f(const ExPolygon &poly, bool flip = NORMALS_UP);
extern std::vector<Vec2f> triangulate_expolygons_2f(const ExPolygons &polys, bool flip = NORMALS_UP);

} // namespace Luminary
