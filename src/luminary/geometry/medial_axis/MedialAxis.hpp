///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stddef.h>
#include <utility>
#include <vector>
#include <cstddef>

#include "luminary/geometry/voronoi/Voronoi.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

namespace Luminary::Geometry
{

class MedialAxis
{
public:
    MedialAxis(double min_width, double max_width, const ExPolygon &expolygon);
    void build(ThickPolylines *polylines);
    void build(Polylines *polylines);

private:
    // Input
    const ExPolygon &m_expolygon;
    Lines m_lines;
    // for filtering of the skeleton edges
    double m_min_width;
    double m_max_width;

    // Voronoi Diagram.
    using VD = VoronoiDiagram;
    VD m_vd;

    // Annotations of the VD skeleton edges.
    struct EdgeData
    {
        bool active{false};
        double width_start{0};
        double width_end{0};
    };
    // Returns a reference to EdgeData and a "reversed" boolean.
    std::pair<EdgeData &, bool> edge_data(const VD::edge_type &edge)
    {
        size_t edge_id = &edge - &m_vd.edges().front();
        return {m_edge_data[edge_id / 2], (edge_id & 1) != 0};
    }
    std::vector<EdgeData> m_edge_data;

    void process_edge_neighbors(const VD::edge_type *edge, ThickPolyline *polyline);
    bool validate_edge(const VD::edge_type *edge);
};

} // namespace Luminary::Geometry
