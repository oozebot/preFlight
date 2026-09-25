///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2014 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
///|/ Provisional module: bridging belongs with fill by subject, and sits in geometry because its includers precede fill.
///|/
#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
#include "luminary/geometry/moments/PrincipalComponents2D.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"

namespace Luminary
{

//return ideal bridge direction and unsupported bridge endpoints distance.
inline std::tuple<Vec2d, double> detect_bridging_direction(const Lines &floating_edges, const Polygons &overhang_area)
{
    if (floating_edges.empty())
    {
        // consider this area anchored from all sides, pick bridging direction that will likely yield shortest bridges
        auto [pc1, pc2] = compute_principal_components(overhang_area);
        if (pc2 == Vec2f::Zero())
        { // overhang may be smaller than resolution. In this case, any direction is ok
            return {Vec2d{1.0, 0.0}, 0.0};
        }
        else
        {
            return {pc2.normalized().cast<double>(), 0.0};
        }
    }

    // Overhang is not fully surrounded by anchors, in that case, find such direction that will minimize the number of bridge ends/180turns in the air
    std::unordered_map<double, Vec2d> directions{};
    for (const Line &l : floating_edges)
    {
        Vec2d normal = l.normal().cast<double>().normalized();
        double quantized_angle = std::ceil(std::atan2(normal.y(), normal.x()) * 1000.0);
        directions.emplace(quantized_angle, normal);
    }
    std::vector<std::pair<Vec2d, double>> direction_costs{};
    // it is acutally cost of a perpendicular bridge direction - we find the minimal cost and then return the perpendicular dir
    for (const auto &d : directions)
    {
        direction_costs.emplace_back(d.second, 0.0);
    }

    for (const Line &l : floating_edges)
    {
        Vec2d line = (l.b - l.a).cast<double>();
        for (auto &dir_cost : direction_costs)
        {
            // the dot product already contains the length of the line. dir_cost.first is normalized.
            dir_cost.second += std::abs(line.dot(dir_cost.first));
        }
    }

    Vec2d result_dir = Vec2d::Ones();
    double min_cost = std::numeric_limits<double>::max();
    for (const auto &cost : direction_costs)
    {
        if (cost.second < min_cost)
        {
            // now flip the orientation back and return the direction of the bridge extrusions
            result_dir = Vec2d{cost.first.y(), -cost.first.x()};
            min_cost = cost.second;
        }
    }

    return {result_dir, min_cost};
};

//return ideal bridge direction and unsupported bridge endpoints distance.
inline std::tuple<Vec2d, double> detect_bridging_direction(const Polygons &to_cover, const Polygons &anchors_area)
{
    Polygons overhang_area = diff(to_cover, anchors_area);
    Lines floating_edges = to_lines(diff_pl(to_polylines(overhang_area), expand(anchors_area, float(SCALED_EPSILON))));
    return detect_bridging_direction(floating_edges, overhang_area);
}

} // namespace Luminary
