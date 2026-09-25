///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "SeamPerimeters.hpp"
#include "SeamChoice.hpp"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary
{
namespace Seams
{
namespace Perimeters
{
struct BoundedPerimeter;
} // namespace Perimeters
struct SeamPerimeterChoice;
} // namespace Seams
} // namespace Luminary

namespace Luminary::Seams::Rear
{
namespace Impl
{
struct PerimeterLine
{
    Vec2d a;
    Vec2d b;
    std::size_t previous_index;
    std::size_t next_index;

    using Scalar = Vec2d::Scalar;
    static const constexpr int Dim = 2;
};
} // namespace Impl

// columns: per layer, the painted column position of each perimeter (same order), empty for a
// perimeter off the paint; empty altogether for an unpainted object.
std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    std::vector<std::vector<Perimeters::BoundedPerimeter>> &&perimeters,
    const std::vector<std::vector<std::optional<Vec2d>>> &columns, const double rear_tolerance,
    const double rear_y_offet);
} // namespace Luminary::Seams::Rear
