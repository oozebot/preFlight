///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <admesh/stl.h>
#include <stddef.h>
#include <vector>
#include <cstddef>

#include "luminary/geometry/primitives/Point.hpp"

struct indexed_triangle_set;

namespace Luminary
{

struct TriangleSetSamples
{
    float total_area;
    std::vector<Vec3f> positions;
    std::vector<Vec3f> normals;
    std::vector<size_t> triangle_indices;
};

TriangleSetSamples sample_its_uniform_parallel(size_t samples_count, const indexed_triangle_set &triangle_set);

} // namespace Luminary
