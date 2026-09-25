///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once
#include <random>
#include <cstddef>
#include <optional>
#include <vector>

#include "SeamGeometry.hpp"
#include "SeamChoice.hpp"
#include "SeamPerimeters.hpp"

namespace Luminary
{
namespace Seams
{
struct SeamPerimeterChoice;
} // namespace Seams
} // namespace Luminary

namespace Luminary::Seams::Random
{
namespace Impl
{
struct PerimeterSegment
{
    double begin{};
    double end{};
    std::size_t begin_index{};

    double length() const { return end - begin; }
};

struct Random
{
    std::mt19937 &random_engine;

    std::optional<SeamChoice> operator()(const Perimeters::Perimeter &perimeter, const Perimeters::PointType point_type,
                                         const Perimeters::PointClassification point_classification) const;
};
} // namespace Impl
std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(Perimeters::LayerPerimeters &&perimeters,
                                                               const unsigned fixed_seed);
} // namespace Luminary::Seams::Random
