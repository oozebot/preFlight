///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "WideningBeadingStrategy.hpp"

#include <algorithm>
#include <utility>
#include <cstdlib>

#include "BeadingStrategy.hpp"

namespace Luminary::Athena
{

WideningBeadingStrategy::WideningBeadingStrategy(BeadingStrategyPtr parent, const coord_t min_input_width,
                                                 const coord_t min_output_width, const coord_t thin_wall_snap_precision,
                                                 const coord_t ext_perimeter_width, const coord_t ext_perimeter_spacing)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , min_input_width(min_input_width)
    , min_output_width(min_output_width)
    , thin_wall_snap_precision(thin_wall_snap_precision)
    , m_ext_perimeter_width(ext_perimeter_width)
    , m_ext_perimeter_spacing(ext_perimeter_spacing)
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
    // Compare in model-space, not skeleton-space. The pre-inset removed overlap
    // from both sides - use the external perimeter overlap (which matches the pre-inset)
    // to recover the actual model wall thickness.
    coord_t overlap_offset = (m_ext_perimeter_width > 0 && m_ext_perimeter_spacing > 0)
                                 ? (m_ext_perimeter_width - m_ext_perimeter_spacing)
                                 : (extrusion_width - bead_spacing);
    coord_t actual_thickness = thickness + overlap_offset;
    if (bead_count <= 1 && actual_thickness < extrusion_width)
    {
        Beading ret;
        ret.total_thickness = thickness;
        if (actual_thickness >= min_input_width)
        {
            coord_t output_width = actual_thickness;

            coord_t snap_threshold = thin_wall_snap_precision / 2;
            coord_t rounded_width = ((output_width + thin_wall_snap_precision / 2) / thin_wall_snap_precision) *
                                    thin_wall_snap_precision;
            if (std::abs(output_width - rounded_width) <= snap_threshold)
                output_width = rounded_width;

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
    {
        // Convert min_input_width (user's model-space value) to skeleton-space
        // by subtracting the overlap offset that was removed during pre-inset
        coord_t overlap_offset = (m_ext_perimeter_width > 0 && m_ext_perimeter_spacing > 0)
                                     ? (m_ext_perimeter_width - m_ext_perimeter_spacing)
                                     : (extrusion_width - bead_spacing);
        return (min_input_width > overlap_offset) ? (min_input_width - overlap_offset) : 0;
    }
    else
        return parent->getTransitionThickness(lower_bead_count);
}

coord_t WideningBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    coord_t overlap_offset = (m_ext_perimeter_width > 0 && m_ext_perimeter_spacing > 0)
                                 ? (m_ext_perimeter_width - m_ext_perimeter_spacing)
                                 : (extrusion_width - bead_spacing);
    coord_t actual_thickness = thickness + overlap_offset;
    if (actual_thickness < min_input_width)
        return 0;
    coord_t ret = parent->getOptimalBeadCount(thickness);
    if (actual_thickness >= min_input_width && ret < 1)
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

} // namespace Luminary::Athena
