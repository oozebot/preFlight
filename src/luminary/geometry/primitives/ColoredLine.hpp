///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// A line segment carrying a colour index: the primitive the painted-segmentation Voronoi
// construction runs over. It lives with the geometry so the Voronoi code does not include the
// segmentation module that consumes it.

#include <boost/polygon/polygon.hpp>
#include <vector>

#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary
{

struct ColoredLine
{
    Line line;
    int color;
    int poly_idx = -1;
    int local_line_idx = -1;
};

using ColoredLines = std::vector<ColoredLine>;

} // namespace Luminary

namespace boost::polygon
{
template<>
struct geometry_concept<Luminary::ColoredLine>
{
    typedef segment_concept type;
};

template<>
struct segment_traits<Luminary::ColoredLine>
{
    typedef coord_t coordinate_type;
    typedef Luminary::Point point_type;

    static inline point_type get(const Luminary::ColoredLine &line, const direction_1d &dir)
    {
        return dir.to_int() ? line.line.b : line.line.a;
    }
};
} // namespace boost::polygon
