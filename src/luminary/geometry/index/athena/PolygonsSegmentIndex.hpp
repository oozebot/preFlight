///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2020 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#pragma once

#include <vector>

#include "PolygonsPointIndex.hpp"

namespace Luminary::Athena
{

/*!
 * A class for iterating over the points in one of the polygons in a \ref Polygons object
 */
class PolygonsSegmentIndex : public PolygonsPointIndex
{
public:
    PolygonsSegmentIndex() : PolygonsPointIndex() {};
    PolygonsSegmentIndex(const Polygons *polygons, unsigned int poly_idx, unsigned int point_idx)
        : PolygonsPointIndex(polygons, poly_idx, point_idx) {};

    Point from() const { return PolygonsPointIndex::p(); }

    Point to() const { return PolygonsSegmentIndex::next().p(); }
};

} // namespace Luminary::Athena

namespace boost::polygon
{

template<>
struct geometry_concept<Luminary::Athena::PolygonsSegmentIndex>
{
    typedef segment_concept type;
};

template<>
struct segment_traits<Luminary::Athena::PolygonsSegmentIndex>
{
    typedef coord_t coordinate_type;
    typedef Luminary::Point point_type;

    static inline point_type get(const Luminary::Athena::PolygonsSegmentIndex &CSegment, direction_1d dir)
    {
        return dir.to_int() ? CSegment.to() : CSegment.from();
    }
};

} // namespace boost::polygon
