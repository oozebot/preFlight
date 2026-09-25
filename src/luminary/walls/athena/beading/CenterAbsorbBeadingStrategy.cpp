///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "CenterAbsorbBeadingStrategy.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/core/diagnostics/DebugOutput.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/primitives/Point.hpp" // unscaled, which the precompiled header had been supplying

namespace Luminary::Athena
{

CenterAbsorbBeadingStrategy::CenterAbsorbBeadingStrategy(BeadingStrategyPtr parent, const coord_t ext_perimeter_width,
                                                         const coord_t ext_perimeter_spacing)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , ext_perimeter_width(ext_perimeter_width)
    , ext_perimeter_spacing(ext_perimeter_spacing)
{
    name = "CenterAbsorbBeadingStrategy";
}

CenterAbsorbBeadingStrategy::Beading CenterAbsorbBeadingStrategy::compute(const coord_t thickness,
                                                                          const coord_t bead_count) const
{
    Beading ret = parent->compute(thickness, bead_count);
    absorb(ret);
    return ret;
}

void CenterAbsorbBeadingStrategy::absorb(Beading &beading) const
{
    if (beading.bead_widths.empty())
    {
        return;
    }

    // The stack computes positions from spacing (distance between toolpaths); width changes
    // must be expressed through spacing to account for overlap correctly.
    const coord_t nominal_spacing = bead_spacing;
    const coord_t nominal_width = extrusion_width;

    if (nominal_spacing <= 0 || nominal_width <= 0)
    {
        beading.left_over = 0;
        return;
    }

    // Interlocking perimeters use fixed bead widths/positions from the beading strategy.
    // Skip all adjustments to prevent skeleton pinching in tight geometry.
    if (beading.preserve_innermost_position)
    {
        beading.left_over = 0;
        return;
    }

    if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS) && beading.left_over != 0)
    {
        std::string widths = "[";
        char wb[32];
        for (size_t i = 0; i < beading.bead_widths.size(); ++i)
        {
            snprintf(wb, sizeof(wb), "%s%.4f", i ? "," : "", unscaled<double>(beading.bead_widths[i]));
            widths += wb;
        }
        widths += "]";
        dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                "ADJUST_BEGIN layer=%d beads=%zu thickness=%.4fmm left_over=%.4fmm "
                "nominal_w=%.4fmm nominal_s=%.4fmm add_thresh=%.4f split_thresh=%.4f widths=%s",
                debug_layer_id, beading.bead_widths.size(), unscaled<double>(beading.total_thickness),
                unscaled<double>(beading.left_over), unscaled<double>(nominal_width), unscaled<double>(nominal_spacing),
                wall_add_middle_threshold, wall_split_middle_threshold, widths.c_str());
    }

    // EVEN case: Adjust two innermost beads (expand for gap OR contract for overfill)
    // Only applies when the stack did NOT place a center bead (even count)
    if (beading.bead_widths.size() >= 2 && beading.bead_widths.size() % 2 == 0 && beading.left_over != 0)
    {
        size_t mid_idx = beading.bead_widths.size() / 2;
        coord_t current_width = beading.bead_widths[mid_idx - 1];

        if (beading.bead_widths[mid_idx - 1] == 0 || beading.bead_widths[mid_idx] == 0)
        {
            beading.left_over = 0;
            return;
        }

        // Calculate overlap percentage
        double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;

        // Adjustment factor accounts for overlap: factor = 1/(1 - overlap%)
        double adjustment_factor = 1.0 / (1.0 - overlap_pct);

        coord_t new_width;
        coord_t centerline_adjustment;
        bool is_expand;

        // Branch based on sign of left_over
        if (beading.left_over > 0)
        {
            // EXPAND: Positive left_over means gap exists between innermost beads
            // Each of the two innermost beads needs to expand to fill half the gap
            is_expand = true;
            coord_t gap_per_bead = beading.left_over / 2;

            // Apply adjustment: additional_width = gap_per_bead x adjustment_factor
            double additional_width_double = (double) gap_per_bead * adjustment_factor;
            coord_t additional_width = (coord_t) additional_width_double;
            new_width = current_width + additional_width;

            // For a perimeter loop, beads should overlap at center seam for structural strength
            // Target spacing between bead centers: spacing = width x (1 - overlap%)
            // Don't close the full gap - leave room for the overlap region
            coord_t target_spacing = (coord_t) ((double) new_width * (1.0 - overlap_pct));
            coord_t overlap_distance = new_width - target_spacing;
            coord_t gap_to_close_per_bead = gap_per_bead - (overlap_distance / 2);
            centerline_adjustment = gap_to_close_per_bead / 2;
            // Negative overlap: the leftover is the intended gap. Keep the beads at nominal
            // width and position so the gap survives instead of being collapsed shut.
            if (overlap_pct < 0)
            {
                new_width = current_width;
                centerline_adjustment = 0;
            }
        }
        else
        {
            // CONTRACT: Negative left_over means beads are over-filled/overlapping
            // Contract both innermost beads to reduce overlap
            is_expand = false;
            coord_t reduction_per_bead = -beading.left_over / 2; // Make positive, split between 2 beads

            // Apply adjustment: width_reduction = reduction_per_bead x adjustment_factor
            double width_reduction_double = (double) reduction_per_bead * adjustment_factor;
            coord_t width_reduction = (coord_t) width_reduction_double;
            new_width = current_width - width_reduction;

            // For multi-bead walls (>2 beads), don't contract internal beads below
            // min(spacing, width): positive overlap holds them at spacing to overlap
            // neighbors, negative overlap holds them at the configured width so the gap
            // survives. Thin walls (2 beads) are exempt - both beads are external.
            const coord_t internal_floor = std::min(nominal_spacing, nominal_width);
            if (beading.bead_widths.size() > 2 && new_width < internal_floor)
                new_width = internal_floor;

            // Toolpath locations move outward (away from center)
            centerline_adjustment = reduction_per_bead / 2;
        }

        coord_t min_safe_width = nominal_width / 3;
        if (new_width < min_safe_width)
        {
            DBG_COUNT("ADJUST_EVEN_REJECT");
            if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS))
            {
                dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                        "ADJUST_EVEN_REJECT layer=%d new_width=%.4fmm < min_safe=%.4fmm "
                        "left_over=%.4fmm ABANDONED",
                        debug_layer_id, unscaled<double>(new_width), unscaled<double>(min_safe_width),
                        unscaled<double>(beading.left_over));
            }
            beading.left_over = 0;
            return;
        }

        if (mid_idx > 0 && mid_idx < beading.bead_widths.size() && new_width > 0)
        {
            if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS))
            {
                dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                        "ADJUST_EVEN layer=%d %s mid_beads[%zu,%zu] %.4fmm -> %.4fmm "
                        "left_over=%.4fmm",
                        debug_layer_id, is_expand ? "EXPAND" : "CONTRACT", mid_idx - 1, mid_idx,
                        unscaled<double>(current_width), unscaled<double>(new_width),
                        unscaled<double>(beading.left_over));
            }

            // Apply width changes
            beading.bead_widths[mid_idx - 1] = new_width;
            beading.bead_widths[mid_idx] = new_width;

            // Apply toolpath location changes
            if (is_expand)
            {
                // EXPAND: move beads INWARD (toward center)
                beading.toolpath_locations[mid_idx - 1] += centerline_adjustment; // Move RIGHT
                beading.toolpath_locations[mid_idx] -= centerline_adjustment;     // Move LEFT
            }
            else
            {
                // CONTRACT: move beads OUTWARD (away from center)
                beading.toolpath_locations[mid_idx - 1] -= centerline_adjustment; // Move LEFT
                beading.toolpath_locations[mid_idx] += centerline_adjustment;     // Move RIGHT
            }

            beading.left_over = 0;
        }
    }
    // ODD case: Adjust center bead width (expand for gap OR contract for overfill)
    // Handles all odd bead count adjustments (shrink OR expand center bead)
    else if (beading.bead_widths.size() % 2 == 1)
    {
        size_t center_idx = beading.bead_widths.size() / 2;
        coord_t current_center_width = beading.bead_widths[center_idx];

        // LimitedBeadingStrategy inserts a 0-width marker at the center when bead_count
        // equals max_bead_count (e.g. perimeters=1 producing 2 beads). This marker denotes
        // the infill boundary but makes the array odd-sized. The flanking beads are the
        // real innermost pair - apply EVEN-case expansion to them instead.
        if (current_center_width == 0 && beading.bead_widths.size() >= 3 && beading.left_over != 0)
        {
            size_t left_idx = center_idx - 1;
            size_t right_idx = center_idx + 1;
            coord_t current_width = beading.bead_widths[left_idx];

            if (current_width == 0 || beading.bead_widths[right_idx] == 0)
            {
                beading.left_over = 0;
                return;
            }

            double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;
            double adjustment_factor = 1.0 / (1.0 - overlap_pct);

            coord_t new_width;
            coord_t centerline_adjustment;
            bool is_expand;

            if (beading.left_over > 0)
            {
                is_expand = true;
                coord_t gap_per_bead = beading.left_over / 2;
                coord_t additional_width = (coord_t) ((double) gap_per_bead * adjustment_factor);
                new_width = current_width + additional_width;

                coord_t target_spacing = (coord_t) ((double) new_width * (1.0 - overlap_pct));
                coord_t overlap_distance = new_width - target_spacing;
                coord_t gap_to_close_per_bead = gap_per_bead - (overlap_distance / 2);
                centerline_adjustment = gap_to_close_per_bead / 2;
                // Negative overlap: the leftover is the intended gap. Keep the beads at nominal
                // width and position so the gap survives instead of being collapsed shut.
                if (overlap_pct < 0)
                {
                    new_width = current_width;
                    centerline_adjustment = 0;
                }
            }
            else
            {
                is_expand = false;
                coord_t reduction_per_bead = -beading.left_over / 2;
                coord_t width_reduction = (coord_t) ((double) reduction_per_bead * adjustment_factor);
                new_width = current_width - width_reduction;
                const coord_t internal_floor = std::min(nominal_spacing, nominal_width);
                if (new_width < internal_floor)
                    new_width = internal_floor;
                centerline_adjustment = reduction_per_bead / 2;
            }

            coord_t min_safe_width = nominal_width / 3;
            if (new_width < min_safe_width)
            {
                beading.left_over = 0;
                return;
            }

            if (new_width > 0)
            {
                beading.bead_widths[left_idx] = new_width;
                beading.bead_widths[right_idx] = new_width;

                if (is_expand)
                {
                    beading.toolpath_locations[left_idx] += centerline_adjustment;
                    beading.toolpath_locations[right_idx] -= centerline_adjustment;
                }
                else
                {
                    beading.toolpath_locations[left_idx] -= centerline_adjustment;
                    beading.toolpath_locations[right_idx] += centerline_adjustment;
                }

                beading.left_over = 0;
            }
            return;
        }

        // left_over is the total thickness error; the center bead absorbs it by expanding
        // (positive) or contracting (negative).

        // Calculate overlap percentage: overlap% = (width - spacing) / width
        double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;

        coord_t new_center_width;

        // Adjustment factor accounts for overlap: factor = width/spacing = 1/(1 - overlap%)
        double adjustment_factor = 1.0 / (1.0 - overlap_pct);

        if (beading.left_over > 0)
        {
            // Gap exists: EXPAND the center bead
            double width_adjustment_double = (double) beading.left_over * adjustment_factor;
            coord_t width_adjustment = (coord_t) width_adjustment_double;
            new_center_width = current_center_width + width_adjustment;
        }
        else
        {
            // Overfilled: CONTRACT the center bead
            double width_reduction_double = (double) (-beading.left_over) * adjustment_factor;
            coord_t width_reduction = (coord_t) width_reduction_double;
            new_center_width = current_center_width - width_reduction;
        }

        if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS))
        {
            dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                    "ADJUST_ODD layer=%d center_idx=%zu current=%.4fmm new=%.4fmm "
                    "left_over=%.4fmm adj_factor=%.4f",
                    debug_layer_id, center_idx, unscaled<double>(current_center_width),
                    unscaled<double>(new_center_width), unscaled<double>(beading.left_over), adjustment_factor);
        }

        // Center-bead splits are decided upstream as bead-count promotions by
        // SplitPromotionBeadingStrategy, so the skeleton smooths them as count
        // transitions. Only continuous width absorption happens here.

        // Single-bead walls: clamp to actual model wall thickness. Use the external
        // perimeter overlap (which matches the pre-inset) to recover model thickness.
        if (beading.bead_widths.size() == 1 && ext_perimeter_width > 0 && ext_perimeter_spacing > 0)
        {
            coord_t actual_wall = beading.total_thickness + (ext_perimeter_width - ext_perimeter_spacing);
            if (new_center_width > actual_wall)
                new_center_width = actual_wall;
        }

        coord_t min_safe_width = nominal_width / 3;

        if (new_center_width < min_safe_width)
        {
            // Only log real abandonment: left_over != 0 means gap/overfill is being
            // discarded. left_over == 0 is a thin center bead left untouched (a no-op),
            // which would otherwise flood the counter with meaningless fires.
            if (beading.left_over != 0)
                DBG_COUNT("ADJUST_ODD_REJECT");
            if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS) && beading.left_over != 0)
            {
                dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                        "ADJUST_ODD_REJECT layer=%d new_center=%.4fmm < "
                        "min_safe=%.4fmm left_over=%.4fmm ABANDONED",
                        debug_layer_id, unscaled<double>(new_center_width), unscaled<double>(min_safe_width),
                        unscaled<double>(beading.left_over));
            }
            beading.left_over = 0;
            return; // Skip this adjustment - would cause Flow::spacing() to fail
        }

        if (new_center_width > 0)
        {
            if (Luminary::debug_enabled(Luminary::DBG_PERIMETERS))
            {
                dbg_log(Luminary::DBG_PERIMETERS, debug_print_z, "SKEL",
                        "ADJUST_ODD_OK layer=%d center[%zu] %.4fmm -> %.4fmm "
                        "left_over=%.4fmm",
                        debug_layer_id, center_idx, unscaled<double>(current_center_width),
                        unscaled<double>(new_center_width), unscaled<double>(beading.left_over));
            }

            // The center bead's toolpath location is the wall center (total_thickness / 2);
            // the stack placed it assuming full nominal width, so re-center it on the
            // adjusted width.
            coord_t new_toolpath_location = beading.total_thickness / 2;

            beading.bead_widths[center_idx] = new_center_width;
            beading.toolpath_locations[center_idx] = new_toolpath_location;
            beading.left_over = 0;
        }
    }

    // Sanity check: a bead can never be wider than the wall itself (multi-bead only).
    // Single thin-wall beads are exempt - WideningBeadingStrategy intentionally produces
    // beads wider than total_thickness to compensate for the pre-inset polygon shrinkage.
    {
        const size_t n = beading.bead_widths.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (n > 1 && beading.total_thickness > 0 && beading.bead_widths[i] > beading.total_thickness)
                beading.bead_widths[i] = beading.total_thickness;
        }
    }
}

coord_t CenterAbsorbBeadingStrategy::getOptimalBeadCount(const coord_t thickness) const
{
    return parent->getOptimalBeadCount(thickness);
}

coord_t CenterAbsorbBeadingStrategy::getTransitionThickness(const coord_t lower_bead_count) const
{
    return parent->getTransitionThickness(lower_bead_count);
}

coord_t CenterAbsorbBeadingStrategy::getOptimalThickness(const coord_t bead_count) const
{
    return parent->getOptimalThickness(bead_count);
}

coord_t CenterAbsorbBeadingStrategy::getTransitioningLength(const coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float CenterAbsorbBeadingStrategy::getTransitionAnchorPos(const coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> CenterAbsorbBeadingStrategy::getNonlinearThicknesses(const coord_t lower_bead_count) const
{
    return parent->getNonlinearThicknesses(lower_bead_count);
}

std::string CenterAbsorbBeadingStrategy::toString() const
{
    return std::string("CenterAbsorb+") + parent->toString();
}

} // namespace Luminary::Athena
