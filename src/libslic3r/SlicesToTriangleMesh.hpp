///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef SLICESTOTRIANGLEMESH_HPP
#define SLICESTOTRIANGLEMESH_HPP

#include <vector>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "admesh/stl.h"

namespace Slic3r
{

void slices_to_mesh(indexed_triangle_set &mesh, const std::vector<ExPolygons> &slices, double zmin, double lh,
                    double ilh);

inline indexed_triangle_set slices_to_mesh(const std::vector<ExPolygons> &slices, double zmin, double lh, double ilh)
{
    indexed_triangle_set out;
    slices_to_mesh(out, slices, zmin, lh, ilh);

    return out;
}

} // namespace Slic3r

#endif // SLICESTOTRIANGLEMESH_HPP
