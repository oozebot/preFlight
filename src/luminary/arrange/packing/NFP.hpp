///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stdint.h>
#include <boost/variant.hpp>
#include <cinttypes>

#include <luminary/geometry/contours/ExPolygon.hpp>
#include <luminary/geometry/primitives/Point.hpp>
#include <luminary/geometry/contours/Polygon.hpp>

#include <luminary/arrange/packing/Beds.hpp>

namespace Luminary
{

template<class Unit = int64_t, class T>
Unit dotperp(const Vec<2, T> &a, const Vec<2, T> &b)
{
    return Unit(a.x()) * Unit(b.y()) - Unit(a.y()) * Unit(b.x());
}

// Convex-Convex nfp in linear time (fixed.size() + movable.size()),
// no memory allocations (if out param is used).
// Sliver triangles can produce a wrong result; nfp_convex_convex_legacy is the slower fallback.
Polygon nfp_convex_convex(const Polygon &fixed, const Polygon &movable);
void nfp_convex_convex(const Polygon &fixed, const Polygon &movable, Polygon &out);
Polygon nfp_convex_convex_legacy(const Polygon &fixed, const Polygon &movable);

Polygon ifp_convex_convex(const Polygon &fixed, const Polygon &movable);

ExPolygons ifp_convex(const arr2::RectangleBed &bed, const Polygon &convexpoly);
ExPolygons ifp_convex(const arr2::CircleBed &bed, const Polygon &convexpoly);
ExPolygons ifp_convex(const arr2::IrregularBed &bed, const Polygon &convexpoly);
inline ExPolygons ifp_convex(const arr2::InfiniteBed &bed, const Polygon &convexpoly)
{
    return {};
}

inline ExPolygons ifp_convex(const arr2::ArrangeBed &bed, const Polygon &convexpoly)
{
    ExPolygons ret;
    auto visitor = [&ret, &convexpoly](const auto &b)
    {
        ret = ifp_convex(b, convexpoly);
    };
    boost::apply_visitor(visitor, bed);

    return ret;
}

Vec2crd reference_vertex(const Polygon &outline);
Vec2crd reference_vertex(const ExPolygon &outline);
Vec2crd reference_vertex(const Polygons &outline);
Vec2crd reference_vertex(const ExPolygons &outline);

Vec2crd min_vertex(const Polygon &outline);

} // namespace Luminary
