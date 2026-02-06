///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "WideningBeadingStrategy.hpp"

#include <algorithm>
#include <utility>
#include <cstdlib>

#include "libslic3r/Athena/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Athena
{

WideningBeadingStrategy::WideningBeadingStrategy(BeadingStrategyPtr parent, const coord_t min_input_width,
                                                 const coord_t min_output_width)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , min_input_width(min_input_width)
    , min_output_width(min_output_width)
{
}

std::string WideningBeadingStrategy::toString() const
{
    return std::string("Widening+") + parent->toString();
}

WideningBeadingStrategy::Beading WideningBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    // Using bead_spacing makes thin wall detection dependent on overlap settings, which is wrong.
    // A thin wall is anything that can't fit 2 full perimeters, regardless of overlap.
    // Use extrusion_width instead of bead_spacing to make thin walls independent of overlap.
    if (thickness < extrusion_width)
    {
        Beading ret;
        ret.total_thickness = thickness;
        if (thickness >= min_input_width)
        {
            // Athena's spacing/width separation allows thin walls to use exact detected width
            // instead of enforcing a minimum like Arachne does (variable width perimeters)
            //
            // CRITICAL: The input geometry was pre-shrunk by (width/2 - spacing/2) per side before
            // skeletal trapezoidation. For thin walls (single bead), this shrinkage is incorrect
            // because there's nothing to overlap with. We must add back the overlap offset.
            //
            // Overlap offset per side = (extrusion_width/2 - bead_spacing/2)
            // Total offset (both sides) = extrusion_width - bead_spacing
            coord_t overlap_offset = extrusion_width - bead_spacing;
            coord_t actual_thickness = thickness + overlap_offset;
            coord_t output_width = actual_thickness; // Use actual detected width

            // Snap to nearest 0.01mm if within 5μm to correct floating-point precision errors
            // from the skeletal trapezoidation geometry processing.
            // Examples: 0.210356mm → 0.21mm, 0.199987mm → 0.20mm
            constexpr coord_t snap_precision = 10000; // 0.01mm in nanometers
            constexpr coord_t snap_threshold = 5000;  // 5 microns
            coord_t rounded_width = ((output_width + snap_precision / 2) / snap_precision) * snap_precision;
            if (std::abs(output_width - rounded_width) <= snap_threshold)
            {
                output_width = rounded_width;
            }

            ret.bead_widths.emplace_back(output_width);
            ret.toolpath_locations.emplace_back(thickness / 2);
            ret.left_over = 0;
        }
        else
        {
            ret.left_over = thickness;
        }

        return ret;
    }
    else
        return parent->compute(thickness, bead_count);
}

coord_t WideningBeadingStrategy::getOptimalThickness(coord_t bead_count) const
{
    return parent->getOptimalThickness(bead_count);
}

coord_t WideningBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    if (lower_bead_count == 0)
        return min_input_width;
    else
        return parent->getTransitionThickness(lower_bead_count);
}

coord_t WideningBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    if (thickness < min_input_width)
        return 0;
    coord_t ret = parent->getOptimalBeadCount(thickness);
    if (thickness >= min_input_width && ret < 1)
        return 1;
    return ret;
}

coord_t WideningBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float WideningBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> WideningBeadingStrategy::getNonlinearThicknesses(coord_t lower_bead_count) const
{
    std::vector<coord_t> ret;
    ret.emplace_back(min_output_width);
    std::vector<coord_t> pret = parent->getNonlinearThicknesses(lower_bead_count);
    ret.insert(ret.end(), pret.begin(), pret.end());
    return ret;
}

} // namespace Slic3r::Athena
