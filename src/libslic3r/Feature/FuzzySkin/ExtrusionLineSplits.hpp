///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#ifndef slic3r_Feature_FuzzySkin_ExtrusionLineSplits_hpp_
#define slic3r_Feature_FuzzySkin_ExtrusionLineSplits_hpp_

// Works with both Arachne::ExtrusionLine and Athena::ExtrusionLine
// This eliminates ~500 lines of duplicated code between the two implementations

#include <type_traits>
#include <vector>

#include "../../Point.hpp"
#include "../../Polygon.hpp"
#include "../../Layer.hpp"
#include "FuzzySkin.hpp"

namespace Slic3r::Feature::FuzzySkin
{

// Result of splitting an ExtrusionLine at visibility/overhang boundaries
template<typename ExtrusionLineT>
struct SplitResult
{
    ExtrusionLineT ext;
    bool is_overhang;
    bool should_skip; // true = skip fuzzy (visible surface or overhang)
};

// ============================================================================
// Point-based helper functions (non-templated, shared by all paths)
// ============================================================================

inline bool point_is_overhang(const Point &pt, const Polygons *lower_slices)
{
    if (!lower_slices || lower_slices->empty())
        return false;
    for (const Polygon &poly : *lower_slices)
    {
        if (poly.contains(pt))
            return false;
    }
    return true;
}

inline bool point_should_skip_visibility(const Point &pt, const Layer *layer, const FuzzySkinConfig &config,
                                         coord_t check_diameter)
{
    if (!layer)
        return false;
    if (config.on_top)
        return false; // Fuzzy allowed on top, no need to check
    return layer->is_visible_from_top_or_bottom(pt, check_diameter, true, false); // check_top=true, check_bottom=false
}

// ============================================================================
// Templated helper functions for ExtrusionJunction types
// ============================================================================

// Find exact overhang boundary using binary search
// Works with any junction type that has .p (Point), .w (width), .perimeter_index
template<typename JunctionT>
JunctionT find_overhang_boundary(const JunctionT &j1, const JunctionT &j2, const Polygons *lower_slices)
{
    JunctionT inside = j1;
    JunctionT outside = j2;
    if (point_is_overhang(j1.p, lower_slices))
        std::swap(inside, outside);

    // Binary search to find crossing point (precision ~0.01mm)
    for (int i = 0; i < 14; ++i)
    { // 2^14 = 16384, gives sub-micron precision
        Point mid_p((inside.p.x() + outside.p.x()) / 2, (inside.p.y() + outside.p.y()) / 2);
        coord_t mid_w = (inside.w + outside.w) / 2;
        if (point_is_overhang(mid_p, lower_slices))
        {
            outside = {mid_p, mid_w, static_cast<coord_t>(inside.perimeter_index)};
        }
        else
        {
            inside = {mid_p, mid_w, static_cast<coord_t>(inside.perimeter_index)};
        }
    }
    return {Point((inside.p.x() + outside.p.x()) / 2, (inside.p.y() + outside.p.y()) / 2), (inside.w + outside.w) / 2,
            static_cast<coord_t>(inside.perimeter_index)};
}

// Find exact visibility boundary using binary search
// Uses coarse diameter for initial check, fine diameter for refinement
template<typename JunctionT>
JunctionT find_visibility_boundary(const JunctionT &j1, const JunctionT &j2, const FuzzySkinConfig &config,
                                   const Layer *layer, coord_t check_diameter_coarse, coord_t check_diameter_fine)
{
    JunctionT visible_j = j1;
    JunctionT hidden_j = j2;
    // Use coarse diameter for initial state check (matching detection phase)
    if (point_should_skip_visibility(j1.p, layer, config, check_diameter_coarse))
        std::swap(visible_j, hidden_j);

    // Stop when interval < fine diameter (no point being more precise than probe size)
    const double min_precision = unscale<double>(check_diameter_fine);
    double distance = unscale<double>((hidden_j.p - visible_j.p).cast<double>().norm());

    // Binary search uses FINE diameter for precision
    while (distance > min_precision)
    {
        Point mid_p((visible_j.p.x() + hidden_j.p.x()) / 2, (visible_j.p.y() + hidden_j.p.y()) / 2);
        coord_t mid_w = (visible_j.w + hidden_j.w) / 2;
        if (point_should_skip_visibility(mid_p, layer, config, check_diameter_fine))
        {
            hidden_j = {mid_p, mid_w, static_cast<coord_t>(visible_j.perimeter_index)};
        }
        else
        {
            visible_j = {mid_p, mid_w, static_cast<coord_t>(visible_j.perimeter_index)};
        }
        distance = unscale<double>((hidden_j.p - visible_j.p).cast<double>().norm());
    }
    return {Point((visible_j.p.x() + hidden_j.p.x()) / 2, (visible_j.p.y() + hidden_j.p.y()) / 2),
            (visible_j.w + hidden_j.w) / 2, static_cast<coord_t>(visible_j.perimeter_index)};
}

// ============================================================================
// Main templated split functions for ExtrusionLine types
// ============================================================================

// Split ExtrusionLine at exact overhang boundaries
template<typename ExtrusionLineT>
std::vector<SplitResult<ExtrusionLineT>> split_at_overhang_boundaries(const ExtrusionLineT &ext,
                                                                      const Polygons *lower_slices)
{
    // Use decltype to get junction type from the junctions vector
    using JunctionT = typename std::decay<decltype(ext.junctions[0])>::type;
    std::vector<SplitResult<ExtrusionLineT>> result;

    if (!lower_slices || lower_slices->empty() || ext.size() < 2)
    {
        result.push_back({ext, false, false});
        return result;
    }

    bool current_overhang = point_is_overhang(ext.junctions[0].p, lower_slices);
    ExtrusionLineT current_ext(ext.inset_idx, ext.is_odd, false);
    current_ext.junctions.push_back(ext.junctions[0]);

    for (size_t i = 1; i < ext.junctions.size(); ++i)
    {
        const auto &prev = ext.junctions[i - 1];
        const auto &curr = ext.junctions[i];
        bool curr_overhang = point_is_overhang(curr.p, lower_slices);

        if (curr_overhang != current_overhang)
        {
            auto boundary = find_overhang_boundary(prev, curr, lower_slices);
            current_ext.junctions.push_back(boundary);
            result.push_back({std::move(current_ext), current_overhang, current_overhang});
            current_ext = ExtrusionLineT(ext.inset_idx, ext.is_odd, false);
            current_ext.junctions.push_back(boundary);
            current_overhang = curr_overhang;
        }
        current_ext.junctions.push_back(curr);
    }

    if (!current_ext.empty())
    {
        result.push_back({std::move(current_ext), current_overhang, current_overhang});
    }
    return result;
}

// Split ExtrusionLine at visibility boundaries using interval sampling + binary search refinement
template<typename ExtrusionLineT>
std::vector<SplitResult<ExtrusionLineT>> split_by_visibility(const ExtrusionLineT &ext, const FuzzySkinConfig &config,
                                                             const Layer *layer, coord_t check_diameter_coarse,
                                                             coord_t check_diameter_fine)
{
    // Use decltype to get junction type from the junctions vector
    using JunctionT = typename std::decay<decltype(ext.junctions[0])>::type;
    std::vector<SplitResult<ExtrusionLineT>> result;

    if (!layer || ext.size() < 2)
    {
        result.push_back({ext, false, false});
        return result;
    }

    const double sample_interval = config.visibility_detection_interval;
    bool current_skip = point_should_skip_visibility(ext.junctions[0].p, layer, config, check_diameter_coarse);
    ExtrusionLineT current_ext(ext.inset_idx, ext.is_odd, false);
    current_ext.junctions.push_back(ext.junctions[0]);
    JunctionT last_known_state_j = ext.junctions[0];

    for (size_t i = 1; i < ext.junctions.size(); ++i)
    {
        const auto &prev = ext.junctions[i - 1];
        const auto &curr = ext.junctions[i];
        double seg_len = unscale<double>((curr.p - prev.p).cast<double>().norm());

        if (seg_len <= sample_interval)
        {
            bool end_skip = point_should_skip_visibility(curr.p, layer, config, check_diameter_coarse);
            if (end_skip != current_skip)
            {
                auto boundary = find_visibility_boundary(last_known_state_j, curr, config, layer, check_diameter_coarse,
                                                         check_diameter_fine);
                current_ext.junctions.push_back(boundary);
                result.push_back({std::move(current_ext), false, current_skip});
                current_ext = ExtrusionLineT(ext.inset_idx, ext.is_odd, false);
                current_ext.junctions.push_back(boundary);
                current_skip = end_skip;
            }
            current_ext.junctions.push_back(curr);
            last_known_state_j = curr;
        }
        else
        {
            Vec2d direction = (curr.p - prev.p).cast<double>();
            double dir_len = direction.norm();
            Vec2d dir_unit = direction / dir_len;

            double distance_along = sample_interval;
            while (distance_along < seg_len)
            {
                double frac = distance_along / seg_len;
                Point sample_pt(prev.p.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                                prev.p.y() + coord_t(dir_unit.y() * scaled(distance_along)));
                coord_t sample_w = prev.w + coord_t((curr.w - prev.w) * frac);
                JunctionT sample_j{sample_pt, sample_w, static_cast<coord_t>(prev.perimeter_index)};

                if (point_should_skip_visibility(sample_pt, layer, config, check_diameter_coarse) != current_skip)
                {
                    auto boundary = find_visibility_boundary(last_known_state_j, sample_j, config, layer,
                                                             check_diameter_coarse, check_diameter_fine);
                    current_ext.junctions.push_back(boundary);
                    result.push_back({std::move(current_ext), false, current_skip});
                    current_ext = ExtrusionLineT(ext.inset_idx, ext.is_odd, false);
                    current_ext.junctions.push_back(boundary);
                    current_skip = !current_skip;
                }
                last_known_state_j = sample_j;
                distance_along += sample_interval;
            }
            bool end_skip = point_should_skip_visibility(curr.p, layer, config, check_diameter_coarse);
            if (end_skip != current_skip)
            {
                auto boundary = find_visibility_boundary(last_known_state_j, curr, config, layer, check_diameter_coarse,
                                                         check_diameter_fine);
                current_ext.junctions.push_back(boundary);
                result.push_back({std::move(current_ext), false, current_skip});
                current_ext = ExtrusionLineT(ext.inset_idx, ext.is_odd, false);
                current_ext.junctions.push_back(boundary);
                current_skip = end_skip;
            }
            current_ext.junctions.push_back(curr);
            last_known_state_j = curr;
        }
    }
    if (!current_ext.empty())
        result.push_back({std::move(current_ext), false, current_skip});
    return result;
}

// Combined split: overhang boundaries first (exact), then visibility (interval-based)
// This is the main entry point for splitting an ExtrusionLine
template<typename ExtrusionLineT>
std::vector<SplitResult<ExtrusionLineT>> split_extrusion_by_visibility_and_overhang(
    const ExtrusionLineT &extrusion, const Polygons *lower_slices, const Layer *layer, const FuzzySkinConfig &cfg,
    coord_t check_diameter_coarse, coord_t check_diameter_fine)
{
    // Step 1: Split at exact overhang boundaries
    auto overhang_splits = split_at_overhang_boundaries(extrusion, lower_slices);

    // Step 2: For non-overhang segments, apply visibility splitting
    std::vector<SplitResult<ExtrusionLineT>> result;
    for (auto &seg : overhang_splits)
    {
        if (seg.is_overhang)
        {
            result.push_back(std::move(seg));
        }
        else if (layer)
        {
            auto vis_splits = split_by_visibility(seg.ext, cfg, layer, check_diameter_coarse, check_diameter_fine);
            for (auto &vs : vis_splits)
                result.push_back(std::move(vs));
        }
        else
        {
            result.push_back(std::move(seg));
        }
    }
    return result;
}

} // namespace Slic3r::Feature::FuzzySkin

#endif // slic3r_Feature_FuzzySkin_ExtrusionLineSplits_hpp_
