///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ExPolygonSerialize_hpp_
#define slic3r_ExPolygonSerialize_hpp_

#include "ExPolygon.hpp"
#include "Point.hpp" // Cereal serialization of Point
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
void serialize(Archive &archive, Slic3r::Polygon &polygon)
{
    archive(polygon.points);
}

template<class Archive>
void serialize(Archive &archive, Slic3r::ExPolygon &expoly)
{
    archive(expoly.contour, expoly.holes);
}

} // namespace cereal
#endif // slic3r_ExPolygonSerialize_hpp_
