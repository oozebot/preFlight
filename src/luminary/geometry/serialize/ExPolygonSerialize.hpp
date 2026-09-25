///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Point.hpp" // Cereal serialization of Point
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

/// <summary>
/// External Cereal serialization of ExPolygons
/// </summary>

// Serialization through the Cereal library
#include <cereal/access.hpp>
namespace cereal
{

template<class Archive>
void serialize(Archive &archive, Luminary::Polygon &polygon)
{
    archive(polygon.points);
}

template<class Archive>
void serialize(Archive &archive, Luminary::ExPolygon &expoly)
{
    archive(expoly.contour, expoly.holes);
}

} // namespace cereal
