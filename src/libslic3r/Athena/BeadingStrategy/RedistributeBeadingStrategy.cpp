///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "RedistributeBeadingStrategy.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include "libslic3r/Athena/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Athena
{

RedistributeBeadingStrategy::RedistributeBeadingStrategy(const coord_t ext_perimeter_spacing,
                                                         const coord_t ext_perimeter_width, BeadingStrategyPtr parent,
                                                         int layer_id)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , ext_perimeter_spacing(ext_perimeter_spacing)
    , ext_perimeter_width(ext_perimeter_width)
    , ext_to_first_internal_spacing(0)
    , innermost_spacing(0)
    , max_bead_count(0)
    , debug_layer_id(layer_id)
{
    name = "RedistributeBeadingStrategy";
}

RedistributeBeadingStrategy::RedistributeBeadingStrategy(const coord_t ext_perimeter_spacing,
                                                         const coord_t ext_perimeter_width,
                                                         const coord_t ext_to_first_internal_spacing,
                                                         BeadingStrategyPtr parent, int layer_id)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , ext_perimeter_spacing(ext_perimeter_spacing)
    , ext_perimeter_width(ext_perimeter_width)
    , ext_to_first_internal_spacing(ext_to_first_internal_spacing)
    , innermost_spacing(0)
    , max_bead_count(0)
    , debug_layer_id(layer_id)
{
    name = "RedistributeBeadingStrategy+FirstInternalOverride";
}

RedistributeBeadingStrategy::RedistributeBeadingStrategy(const coord_t ext_perimeter_spacing,
                                                         const coord_t ext_perimeter_width,
                                                         const coord_t ext_to_first_internal_spacing,
                                                         const coord_t innermost_spacing, const coord_t max_bead_count,
                                                         BeadingStrategyPtr parent, int layer_id)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , ext_perimeter_spacing(ext_perimeter_spacing)
    , ext_perimeter_width(ext_perimeter_width)
    , ext_to_first_internal_spacing(ext_to_first_internal_spacing)
    , innermost_spacing(innermost_spacing)
    , max_bead_count(max_bead_count)
    , debug_layer_id(layer_id)
{
    name = "RedistributeBeadingStrategy+FirstInternalOverride+InnermostSpacing";
}

coord_t RedistributeBeadingStrategy::getOptimalThickness(coord_t bead_count) const
{
    const coord_t inner_bead_count = std::max(static_cast<coord_t>(0), bead_count - 2);
    const coord_t outer_bead_count = bead_count - inner_bead_count;

    coord_t thickness = parent->getOptimalThickness(inner_bead_count) + ext_perimeter_spacing * outer_bead_count;

    // When innermost_spacing is set and we have inner beads, the last inner bead uses
    // innermost_spacing instead of bead_spacing, causing the actual thickness needed to differ.
    // Parent's getOptimalThickness() assumes uniform bead_spacing, so we adjust for the difference.
    if (innermost_spacing > 0 && inner_bead_count > 0)
    {
        coord_t adjustment = innermost_spacing - bead_spacing;
        thickness += adjustment;
    }

    return thickness;
}

coord_t RedistributeBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    switch (lower_bead_count)
    {
    case 0:
        return bead_spacing * 0.5; // Athena: Use parent's bead_spacing with 50% threshold
    case 1:
        return (1.0 + parent->getSplitMiddleThreshold()) * ext_perimeter_spacing;
    default:
        return parent->getTransitionThickness(lower_bead_count - 2) + 2 * ext_perimeter_spacing;
    }
}

coord_t RedistributeBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    if (thickness < bead_spacing * 0.5) // Athena: Minimum 50% of spacing
        return 0;
    if (thickness <= 2 * ext_perimeter_spacing)
        return thickness > (1.0 + parent->getSplitMiddleThreshold()) * ext_perimeter_spacing ? 2 : 1;

    return parent->getOptimalBeadCount(thickness - 2 * ext_perimeter_spacing) + 2;
}

coord_t RedistributeBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float RedistributeBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::string RedistributeBeadingStrategy::toString() const
{
    return std::string("RedistributeBeadingStrategy+") + parent->toString();
}

BeadingStrategy::Beading RedistributeBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    Beading ret;

    // Take care of all situations in which no lines are actually produced:
    if (bead_count == 0 || thickness < bead_spacing * 0.5)
    { // Athena: 50% minimum threshold
        ret.left_over = thickness;
        ret.total_thickness = thickness;
        return ret;
    }

    // When ext_to_first_internal_spacing is specified, it represents the SPACING between external and first internal.
    // This allows independent control of external/internal overlap vs internal/internal overlap.

    if (ext_to_first_internal_spacing > 0 && bead_count >= 2)
    {
        // preFlight precise walls mode: Use custom spacing for first internal bead
        const coord_t custom_spacing = ext_to_first_internal_spacing;

        // Add external bead - fixed width, spacing-based position
        ret.bead_widths.push_back(ext_perimeter_width);              // Athena: actual extrusion width
        ret.toolpath_locations.push_back(ext_perimeter_spacing / 2); // Position based on spacing

        // Add first internal bead at the specified spacing from external
        ret.bead_widths.push_back(extrusion_width); // Parent's extrusion width
        ret.toolpath_locations.push_back(custom_spacing + ext_perimeter_spacing / 2);

        // Compute remaining internal beads (if any)
        const coord_t inner_bead_count = bead_count - 2;
        if (inner_bead_count > 0)
        {
            Beading spacing_reference = parent->compute(bead_spacing * inner_bead_count, inner_bead_count);

            coord_t internal_spacing = bead_spacing; // default to parent's spacing if parent fails
            if (spacing_reference.toolpath_locations.size() >= 2)
            {
                // Use the spacing between first two beads from parent's calculation
                internal_spacing = spacing_reference.toolpath_locations[1] - spacing_reference.toolpath_locations[0];
            }

            // The innermost inner bead is determined by max_bead_count (requested shells), not inner_bead_count (actual generated)
            // This ensures that in wide sections with many beads, we apply custom spacing to the REQUESTED innermost, not the absolute innermost
            // Calculate the requested innermost index: max_bead_count - 2 (for Shell 0/1) - 1 (for 0-indexing)
            const coord_t requested_innermost_index = (max_bead_count > 2) ? (max_bead_count - 3) : -1;
            // In narrow sections, use whatever is actually generated (since requested might be more than fits)
            const coord_t actual_innermost_inner_index = (requested_innermost_index >= 0 &&
                                                          requested_innermost_index < inner_bead_count)
                                                             ? requested_innermost_index
                                                             : ((inner_bead_count > 0) ? (inner_bead_count - 1) : -1);
            const bool use_custom_innermost = (innermost_spacing > 0 && actual_innermost_inner_index >= 0);

            // Now position ALL remaining internal beads
            coord_t current_position = custom_spacing + ext_perimeter_spacing / 2; // CENTER of first internal
            for (coord_t i = 0; i < inner_bead_count; ++i)
            {
                // Determine spacing to use for this bead
                coord_t spacing_to_use = internal_spacing;
                if (use_custom_innermost && i == actual_innermost_inner_index)
                {
                    // Actual innermost bead: use innermost_spacing
                    spacing_to_use = innermost_spacing;
                }

                current_position += spacing_to_use;         // Move to next bead center
                ret.bead_widths.push_back(extrusion_width); // Parent's extrusion width
                ret.toolpath_locations.push_back(current_position);
            }
        }
        // Calculate using SPACING (assumes symmetric layout)
        coord_t accumulated_spacing = 0;
        if (!ret.toolpath_locations.empty())
        {
            accumulated_spacing = ret.toolpath_locations.back() + ret.toolpath_locations.front();
        }
        coord_t spacing_based_leftover = thickness - accumulated_spacing;

        // WARNING: Using spacing-based which assumes symmetric layout!
        // Branch 1 may create asymmetric layouts with custom spacing
        ret.left_over = spacing_based_leftover;
    }
    else
    {
        // Standard behavior: symmetric outer walls
        const coord_t inner_bead_count = bead_count - 2;
        const coord_t inner_thickness = thickness - 2 * ext_perimeter_spacing;
        if (inner_bead_count > 0 && inner_thickness > 0)
        {
            ret = parent->compute(inner_thickness, inner_bead_count);
            for (auto &toolpath_location : ret.toolpath_locations)
                toolpath_location += ext_perimeter_spacing;
            // Parent calculated left_over using spacing, and it's valid for full thickness (math below)
            // Total spacing = 2*ext_spacing + (inner_thickness - parent_left_over)
            // left_over = thickness - total_spacing
            //           = thickness - 2*ext_spacing - inner_thickness + parent_left_over
            //           = thickness - 2*ext_spacing - (thickness - 2*ext_spacing) + parent_left_over
            //           = parent_left_over
            // So parent's left_over is already correct - don't modify it
        }
        else
        {
            // Parent not called, only outer beads - calculate based on spacing
            ret.left_over = thickness - (bead_count * ext_perimeter_spacing);
        }

        const coord_t actual_outer_spacing = bead_count > 2 ? std::min(thickness / 2, ext_perimeter_spacing)
                                                            : thickness / bead_count;

        // Original code used insert() at begin(), causing O(n) shift of all elements.
        // This fix builds new vectors with correct order to avoid shifting overhead.
        //
        // Performance impact: 10-50x faster for bead computation with many beads.
        // Context: Athena: Fixed width for external perimeters
        {
            std::vector<coord_t> new_widths;
            std::vector<coord_t> new_locations;
            new_widths.reserve(ret.bead_widths.size() + (bead_count > 1 ? 2 : 1));
            new_locations.reserve(ret.toolpath_locations.size() + (bead_count > 1 ? 2 : 1));

            // Add first outer bead
            new_widths.push_back(ext_perimeter_width);
            new_locations.push_back(actual_outer_spacing / 2);

            // Add existing beads from parent computation
            new_widths.insert(new_widths.end(), ret.bead_widths.begin(), ret.bead_widths.end());
            new_locations.insert(new_locations.end(), ret.toolpath_locations.begin(), ret.toolpath_locations.end());

            // Add second outer bead if needed
            if (bead_count > 1)
            {
                new_widths.push_back(ext_perimeter_width);
                new_locations.push_back(thickness - actual_outer_spacing / 2);
            }

            // Swap in the new vectors - O(1) operation
            ret.bead_widths.swap(new_widths);
            ret.toolpath_locations.swap(new_locations);
        }
    }

    // Ensure correct total and left over thickness.
    ret.total_thickness = thickness;

    return ret;
}

} // namespace Slic3r::Athena
