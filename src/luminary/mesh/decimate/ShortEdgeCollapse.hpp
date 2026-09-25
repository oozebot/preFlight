///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stddef.h>
#include <cstddef>

#include "luminary/mesh/core/TriangleMesh.hpp"

struct indexed_triangle_set;

namespace Luminary
{

// Decimates the model by collapsing short edges. It starts with very small edges and gradually increases the collapsible length,
// until the target triangle count is reached (the algorithm will certainly undershoot the target count, result will have less triangles than target count)
//  The algorithm does not check for triangle flipping, disconnections, self intersections or any other degeneration that can appear during mesh processing.
void its_short_edge_collpase(indexed_triangle_set &mesh, size_t target_triangle_count);

} // namespace Luminary
