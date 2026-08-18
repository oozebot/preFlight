///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include <numeric>
#include <algorithm>
#include <vector>
#include <cassert>

#include "DistributedBeadingStrategy.hpp"
#include "libslic3r/Athena/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Athena
{

DistributedBeadingStrategy::DistributedBeadingStrategy(const coord_t perimeter_spacing, const coord_t perimeter_width,
                                                       const coord_t default_transition_length,
                                                       const double transitioning_angle,
                                                       const double wall_split_middle_threshold,
                                                       const double wall_add_middle_threshold,
                                                       const int distribution_radius)
    : BeadingStrategy(perimeter_spacing, perimeter_width, wall_split_middle_threshold, wall_add_middle_threshold,
                      default_transition_length, transitioning_angle)
{
    if (distribution_radius >= 2)
        one_over_distribution_radius_squared = 1.0f / (distribution_radius - 1) * 1.0f / (distribution_radius - 1);
    else
        one_over_distribution_radius_squared = 1.0f / 1 * 1.0f / 1;
    name = "DistributedBeadingStrategy";
}

std::vector<float> DistributedBeadingStrategy::calc_normalized_weights(const coord_t to_be_divided,
                                                                       const coord_t bead_count) const
{
    const float middle = static_cast<float>(bead_count - 1) / 2.f;

    const auto calc_weight = [middle, &self = std::as_const(*this)](const size_t bead_idx) -> float
    {
        const float dev_from_middle = static_cast<float>(bead_idx) - middle;
        return std::max(0.f, 1.f - self.one_over_distribution_radius_squared * Slic3r::sqr(dev_from_middle));
    };

    std::vector<float> weights(bead_count);
    for (size_t bead_idx = 0; bead_idx < bead_count; ++bead_idx)
    {
        weights[bead_idx] = calc_weight(bead_idx);
    }

    // Normalize weights.
    const float total_weight = std::accumulate(weights.begin(), weights.end(), 0.f);
    std::transform(weights.begin(), weights.end(), weights.begin(),
                   [total_weight](float weight) { return weight / total_weight; });

    // Find the maximum adjustment needed to prevent negative bead width.
    const float max_allowed_weight = -static_cast<float>(bead_spacing) / static_cast<float>(to_be_divided) /
                                     total_weight;
    float max_adjustment = 0.f;
    size_t adjustment_cnt = 0;

    for (float weight : weights)
    {
        if (weight > max_allowed_weight)
        {
            max_adjustment = std::max(weight - max_allowed_weight, max_adjustment);
            ++adjustment_cnt;
        }
    }

    if (const size_t increaseable_weight_cnt = weights.size() - adjustment_cnt;
        adjustment_cnt > 0 && increaseable_weight_cnt > 0)
    {
        // There is at least one weight that can increased.
        std::vector<float> new_weights;
        for (float &weight : weights)
        {
            if (weight <= max_allowed_weight)
            {
                new_weights.emplace_back(
                    std::max(0.f, weight + (max_adjustment / static_cast<float>(increaseable_weight_cnt))));
            }
        }

        return new_weights;
    }

    return weights;
}

DistributedBeadingStrategy::Beading DistributedBeadingStrategy::compute(const coord_t thickness,
                                                                        coord_t bead_count) const
{
    Beading ret;
    ret.total_thickness = thickness;

    const coord_t to_be_divided = thickness - bead_count * bead_spacing;
    const std::vector<float> normalized_weights = this->calc_normalized_weights(to_be_divided, bead_count);

    // Update the bead count based on calculated weights because the adjustment of bead widths
    // may cause one bead to be entirely removed.
    bead_count = static_cast<coord_t>(normalized_weights.size());

    if (bead_count > 2)
    {
        coord_t accumulated_spacing = 0;
        coord_t prev_spacing = 0;
        for (size_t bead_idx = 0; bead_idx < bead_count; ++bead_idx)
        {
            const coord_t splitup_left_over_spacing = static_cast<coord_t>(static_cast<float>(to_be_divided) *
                                                                           normalized_weights[bead_idx]);
            const coord_t spacing = (bead_idx == bead_count - 1)
                                        ? thickness - accumulated_spacing
                                        : std::max(coord_t(0), bead_spacing + splitup_left_over_spacing);

            // Each bead occupies a slot of its spacing; consecutive centerlines sit half of
            // each adjacent slot apart.
            if (bead_idx == 0)
            {
                ret.toolpath_locations.emplace_back(spacing / 2);
            }
            else
            {
                ret.toolpath_locations.emplace_back(ret.toolpath_locations.back() + (prev_spacing + spacing) / 2);
            }
            prev_spacing = spacing;

            // Athena: All widths are fixed to extrusion_width
            ret.bead_widths.emplace_back(extrusion_width);
            accumulated_spacing += spacing;
        }

        ret.left_over = thickness - accumulated_spacing;
    }
    else if (bead_count == 2)
    {
        const coord_t spacing = thickness / 2;
        ret.bead_widths.emplace_back(extrusion_width); // Fixed width
        ret.bead_widths.emplace_back(extrusion_width); // Fixed width
        ret.toolpath_locations.emplace_back(spacing / 2);
        ret.toolpath_locations.emplace_back(thickness - spacing / 2);
        ret.left_over = thickness - (spacing * 2);
    }
    else if (bead_count == 1)
    {
        ret.bead_widths.emplace_back(extrusion_width); // Fixed width
        ret.toolpath_locations.emplace_back(thickness / 2);
        ret.left_over = thickness - extrusion_width;
    }
    else
    {
        ret.left_over = thickness;
    }

    assert((
        [&ret = std::as_const(ret), thickness]() -> bool
        {
            coord_t total_bead_width = 0;
            for (const coord_t &bead_width : ret.bead_widths)
                total_bead_width += bead_width;
            return (total_bead_width + ret.left_over) == thickness;
        }()));

    return ret;
}

coord_t DistributedBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    const coord_t naive_count = thickness / bead_spacing;             // How many lines we can fit in for sure.
    const coord_t remainder = thickness - naive_count * bead_spacing; // Space left after fitting that many lines.
    coord_t minimum_line_width = bead_spacing *
                                 (naive_count % 2 == 1 ? wall_split_middle_threshold : wall_add_middle_threshold);
    // With perimeter overlap (bead_spacing < extrusion_width), the center pair from
    // opposing walls can overlap when Athena's fixed-width beads can't shrink to fit.
    // Raise the split threshold to half the extrusion width so the center pair has
    // at least 50% bead-width separation. This blocks splits where the pair would
    // overlap while allowing them where two beads genuinely fit.
    if (naive_count % 2 == 1 && bead_spacing < extrusion_width)
        minimum_line_width = std::max(minimum_line_width, extrusion_width / 2);
    return naive_count + (remainder >= minimum_line_width);
}

coord_t DistributedBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    const coord_t lower_ideal = getOptimalThickness(lower_bead_count);
    const coord_t higher_ideal = getOptimalThickness(lower_bead_count + 1);
    const coord_t gap = higher_ideal - lower_ideal; // = bead_spacing
    double threshold = lower_bead_count % 2 == 1 ? wall_split_middle_threshold : wall_add_middle_threshold;
    // Must match getOptimalBeadCount for skeleton consistency
    if (lower_bead_count % 2 == 1 && bead_spacing < extrusion_width)
        threshold = std::max(threshold, double(extrusion_width / 2) / double(gap));
    return lower_ideal + coord_t(threshold * gap);
}

} // namespace Slic3r::Athena
