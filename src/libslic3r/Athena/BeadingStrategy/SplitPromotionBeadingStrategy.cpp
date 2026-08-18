///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#include <algorithm>
#include <cassert>

#include "SplitPromotionBeadingStrategy.hpp"
#include "libslic3r/DebugOutput.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Athena
{

SplitPromotionBeadingStrategy::SplitPromotionBeadingStrategy(BeadingStrategyPtr parent,
                                                             const coord_t ext_perimeter_width,
                                                             const coord_t ext_perimeter_spacing,
                                                             const coord_t ext_split_spacing,
                                                             const coord_t max_bead_width_external)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , ext_perimeter_width(ext_perimeter_width)
    , ext_perimeter_spacing(ext_perimeter_spacing)
    , ext_split_spacing(ext_split_spacing)
    , max_bead_width_external(max_bead_width_external)
{
    name = "SplitPromotionBeadingStrategy";
}

bool SplitPromotionBeadingStrategy::splitEligible(const Beading &beading) const
{
    if (beading.bead_widths.empty() || beading.bead_widths.size() % 2 == 0)
        return false;
    if (beading.preserve_innermost_position)
        return false;
    if (parent->getBeadSpacing() <= 0 || parent->getExtrusionWidth() <= 0)
        return false;
    return true;
}

coord_t SplitPromotionBeadingStrategy::splitPairWidth(const Beading &beading) const
{
    const coord_t nominal_spacing = parent->getBeadSpacing();
    const coord_t nominal_width = parent->getExtrusionWidth();
    const double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;
    const double adjustment_factor = 1.0 / (1.0 - overlap_pct);
    const size_t center_idx = beading.bead_widths.size() / 2;
    const coord_t new_center_width = beading.bead_widths[center_idx] +
                                     (coord_t) ((double) beading.left_over * adjustment_factor);

    double split_overlap = overlap_pct;
    const coord_t split_sp = (ext_split_spacing > 0) ? ext_split_spacing : ext_perimeter_spacing;
    if (beading.bead_widths.size() <= 1 && ext_perimeter_width > 0 && split_sp > 0)
        split_overlap = (double) (ext_perimeter_width - split_sp) / (double) ext_perimeter_width;

    const double loop_width_factor = 2.0 - split_overlap;
    coord_t split_source = new_center_width;
    if (beading.bead_widths.size() <= 1 && ext_perimeter_width > 0 && ext_perimeter_spacing > 0)
        split_source = beading.total_thickness + (ext_perimeter_width - ext_perimeter_spacing);
    return (coord_t) ((double) split_source / loop_width_factor);
}

bool SplitPromotionBeadingStrategy::shouldSplit(const coord_t thickness, const Beading &beading,
                                                coord_t &split_width_out) const
{
    split_width_out = 0;
    if (!splitEligible(beading))
        return false;
    const coord_t nominal_spacing = parent->getBeadSpacing();
    const coord_t nominal_width = parent->getExtrusionWidth();
    // Splitting only ever applies to a center bead expanded by a positive left_over.
    if (beading.left_over <= 0)
        return false;

    const size_t center_idx = beading.bead_widths.size() / 2;
    const coord_t current_center_width = beading.bead_widths[center_idx];
    const double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;
    const double adjustment_factor = 1.0 / (1.0 - overlap_pct);
    const coord_t new_center_width = current_center_width + (coord_t) ((double) beading.left_over * adjustment_factor);

    coord_t split_threshold = nominal_width;
    if (beading.bead_widths.size() <= 1 && ext_perimeter_width > 0 && ext_perimeter_width < nominal_width)
        split_threshold = ext_perimeter_width;
    if (new_center_width <= split_threshold)
        return false;

    const coord_t split_width = splitPairWidth(beading);

    bool force_split = false;
    if (beading.bead_widths.size() <= 1 && max_bead_width_external > 0 && ext_perimeter_width > 0 &&
        ext_perimeter_spacing > 0)
    {
        const coord_t actual_wall = beading.total_thickness + (ext_perimeter_width - ext_perimeter_spacing);
        force_split = (actual_wall > max_bead_width_external + scaled<coord_t>(0.001));
    }

    // Split viability. A single bead with real perimeter overlap serves thin external
    // walls and may split at any printable width. A single bead WITHOUT overlap (the
    // narrow-solid fill band) must not split below full width: its 1-to-2 decision is
    // owned by the bead-count rule, and a sub-width pair there recreates the band
    // shredding. Multi-bead centers may split without a floor when width == spacing
    // (a concentric region's center pair is legitimately narrower than nominal); with
    // overlap they keep the floor so the pair cannot straddle its neighbors.
    const bool zero_overlap = nominal_spacing >= nominal_width;
    const size_t n_beads = beading.bead_widths.size();
    bool split_viable;
    if (n_beads <= 1)
        split_viable = zero_overlap ? split_width >= std::max(nominal_spacing, nominal_width * 9 / 10) : true;
    else
        split_viable = zero_overlap || split_width >= std::max(nominal_spacing, nominal_width * 9 / 10);

    // Bias toward splitting: an over-wide bead prints worse than a slightly
    // under-wide pair, so two beads win unless one bead is clearly better.
    const coord_t one_bead_deviation = new_center_width - split_threshold;
    const coord_t per_bead_deviation = (split_width > split_threshold) ? (split_width - split_threshold)
                                                                       : (split_threshold - split_width);
    const coord_t two_bead_deviation = per_bead_deviation * 2;

    if (!(split_viable && (force_split || two_bead_deviation < one_bead_deviation * 2)))
        return false;
    split_width_out = split_width;
    (void) thickness;
    return true;
}

coord_t SplitPromotionBeadingStrategy::getOptimalBeadCount(const coord_t thickness) const
{
    const coord_t count = parent->getOptimalBeadCount(thickness);
    if (count < 1 || count % 2 == 0)
        return count;
    const Beading beading = parent->compute(thickness, count);
    coord_t split_width;
    if (shouldSplit(thickness, beading, split_width))
        return count + 1;
    return count;
}

SplitPromotionBeadingStrategy::Beading SplitPromotionBeadingStrategy::compute(const coord_t thickness,
                                                                              coord_t bead_count) const
{
    // Provenance: this count is a promotion iff the parent would choose one less AND
    // the promotion owns this count's lower boundary (the published transition sits
    // below the parent's). Promoted counts emit split geometry for the whole owned
    // range: re-checking the worthiness thresholds per thickness would flip the
    // result between split and native form at the promotion boundary, and the
    // transition machinery interpolates against this output, so it must stay
    // continuous in thickness. Where the count change is the parent's own (promotion
    // blocked by viability), the native form is the transition target and passes
    // through untouched.
    if (bead_count >= 2 && bead_count % 2 == 0)
    {
        const coord_t parent_count = parent->getOptimalBeadCount(thickness);
        if (parent_count == bead_count - 1 &&
            getTransitionThickness(bead_count - 1) < parent->getTransitionThickness(bead_count - 1))
        {
            Beading beading = parent->compute(thickness, parent_count);
            const coord_t split_width = splitEligible(beading) ? splitPairWidth(beading) : 0;
            if (split_width > 0)
            {
                const coord_t nominal_spacing = parent->getBeadSpacing();
                const coord_t nominal_width = parent->getExtrusionWidth();
                const double overlap_pct = (double) (nominal_width - nominal_spacing) / (double) nominal_width;
                const size_t center_idx = beading.bead_widths.size() / 2;

                coord_t new_left_pos, new_right_pos;
                if (beading.bead_widths.size() <= 1)
                {
                    // Single-bead wall: position using ext/perimeter overlap spacing.
                    double split_overlap = overlap_pct;
                    const coord_t split_sp = (ext_split_spacing > 0) ? ext_split_spacing : ext_perimeter_spacing;
                    if (ext_perimeter_width > 0 && split_sp > 0)
                        split_overlap = (double) (ext_perimeter_width - split_sp) / (double) ext_perimeter_width;
                    const coord_t split_center_spacing = (coord_t) ((double) split_width * (1.0 - split_overlap));
                    const coord_t center = beading.total_thickness / 2;
                    new_left_pos = center - split_center_spacing / 2;
                    new_right_pos = center + split_center_spacing / 2;
                }
                else
                {
                    // Multi-bead: position relative to center with overlap adjustment.
                    const coord_t split_spacing = (coord_t) ((double) split_width * (1.0 - overlap_pct));
                    const coord_t current_center_pos = beading.toolpath_locations[center_idx];
                    const coord_t inward_adjustment = beading.left_over / 2;
                    const coord_t offset = split_spacing / 2;
                    new_left_pos = current_center_pos - offset + inward_adjustment;
                    new_right_pos = current_center_pos + offset - inward_adjustment;
                }

                beading.bead_widths[center_idx] = split_width;
                beading.toolpath_locations[center_idx] = new_left_pos;
                beading.bead_widths.insert(beading.bead_widths.begin() + center_idx + 1, split_width);
                beading.toolpath_locations.insert(beading.toolpath_locations.begin() + center_idx + 1, new_right_pos);
                beading.left_over = 0;
                return beading;
            }
        }
    }
    return parent->compute(thickness, bead_count);
}

coord_t SplitPromotionBeadingStrategy::getTransitionThickness(const coord_t lower_bead_count) const
{
    for (const auto &cached : boundary_cache)
        if (cached.first == lower_bead_count)
            return cached.second;

    const coord_t parent_boundary = parent->getTransitionThickness(lower_bead_count);
    coord_t boundary = parent_boundary;
    if (lower_bead_count >= 1 && lower_bead_count % 2 == 1)
    {
        // The promotion predicate is monotone in thickness (the expanded center only
        // grows), so the promoted boundary is found by bisection between the counts'
        // ideal thicknesses. Published boundaries must match getOptimalBeadCount
        // exactly for skeleton consistency.
        // Promotion can only move a boundary DOWN (a wall splits before the count
        // strategy would add a bead on its own). Wrapper strategies can report a
        // transition thickness below the count's ideal thickness; skip the search
        // when the bracket inverts rather than publish a raised boundary.
        coord_t lo = parent->getOptimalThickness(lower_bead_count);
        coord_t hi = boundary;
        if (lo < hi)
        {
            if (getOptimalBeadCount(lo) > lower_bead_count)
                boundary = lo;
            else if (getOptimalBeadCount(hi) > lower_bead_count)
            {
                while (hi - lo > 1)
                {
                    const coord_t mid = lo + (hi - lo) / 2;
                    if (getOptimalBeadCount(mid) > lower_bead_count)
                        hi = mid;
                    else
                        lo = mid;
                }
                boundary = hi;
            }
        }
    }
    if (boundary != parent_boundary)
        dbg_log(Slic3r::DBG_PERIMETERS, debug_print_z, "SKEL",
                "SPLIT_PROMOTE_BOUNDARY layer=%d lower=%d t=%.4fmm parent_t=%.4fmm", debug_layer_id,
                (int) lower_bead_count, unscaled<double>(boundary), unscaled<double>(parent_boundary));
    boundary_cache.emplace_back(lower_bead_count, boundary);
    return boundary;
}

coord_t SplitPromotionBeadingStrategy::getOptimalThickness(const coord_t bead_count) const
{
    return parent->getOptimalThickness(bead_count);
}

coord_t SplitPromotionBeadingStrategy::getTransitioningLength(const coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float SplitPromotionBeadingStrategy::getTransitionAnchorPos(const coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> SplitPromotionBeadingStrategy::getNonlinearThicknesses(const coord_t lower_bead_count) const
{
    return parent->getNonlinearThicknesses(lower_bead_count);
}

std::string SplitPromotionBeadingStrategy::toString() const
{
    return std::string("SplitPromotion+") + parent->toString();
}

} // namespace Slic3r::Athena
