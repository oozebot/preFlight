///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "PreciseWalls.hpp"
#include "luminary/walls/athena/paths/ExtrusionLine.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Luminary
{
namespace preFlight
{

// ================================================================================
// Helper Functions
// ================================================================================

float PreciseWalls::apply_overlap(float width, float height, const ConfigOptionFloatOrPercent &overlap)
{
    // Calculate the overlap amount
    float overlap_amount;

    if (overlap.percent)
    {
        // Percentage mode: Overlap is calculated from both layer height and extrusion width.
        //
        // The geometric constant (1 - pi/4), about 21.46% of layer height, is what the semicircular
        // cross-section of extruded plastic needs for full bead bonding.
        //
        // The user's percentage is scaled so that:
        //   - 10.73% (default) = full bonding (internally 21.46% of height)
        //   - 100% = complete overlap (spacing = 0) for typical width = 2*height
        //
        // Formula: overlap_amount = height * (user_percent * 2 / 100)
        // So 100% of user input is 200% of height, an overlap equal to width when width = 2h.
        float clamped_percent = std::min(overlap.value, 100.0);
        overlap_amount = height * (clamped_percent * 2.0f / 100.0f);
    }
    else
    {
        // Absolute mode: Use the specified mm value directly
        overlap_amount = std::min(float(overlap.value), width);
    }

    // Spacing = width - overlap
    float spacing = width - overlap_amount;

    // Very small spacing can cause issues in skeletal trapezoidation.
    // Use minimum 20% of width (max 80% overlap) to ensure stability.
    // The UI also limits perimeter/perimeter overlap to 80% max.
    float min_spacing = width * 0.20f; // 20% of width minimum (max 80% overlap)
    if (spacing < min_spacing)
    {
        spacing = min_spacing;
    }

    return spacing;
}

// ================================================================================
// Public API
// ================================================================================

coord_t PreciseWalls::calculate_external_spacing(const Flow &ext_flow, const Flow &int_flow,
                                                 const ConfigOptionFloatOrPercent &overlap)
{
    // Calculate individual spacings with the specified overlap
    float ext_spacing = apply_overlap(ext_flow.width(), ext_flow.height(), overlap);
    float int_spacing = apply_overlap(int_flow.width(), int_flow.height(), overlap);

    // Average the two spacings (matches ext_perimeter_spacing2 calculation)
    float avg_spacing = 0.5f * (ext_spacing + int_spacing);

    return coord_t(scale_(avg_spacing));
}

coord_t PreciseWalls::calculate_perimeter_spacing(const Flow &flow, const ConfigOptionFloatOrPercent &overlap)
{
    float spacing = apply_overlap(flow.width(), flow.height(), overlap);
    return coord_t(scale_(spacing));
}

ConfigOptionFloatOrPercent PreciseWalls::get_effective_external_overlap(const ConfigOptionFloatOrPercent &user_overlap,
                                                                        int perimeter_count)
{
    // External perimeter overlap only matters with 2+ perimeters
    // (need external + at least one internal perimeter)
    if (perimeter_count < 2)
    {
        // Return default overlap - user setting doesn't apply
        return ConfigOptionFloatOrPercent(get_standard_overlap_percent(), true);
    }
    return user_overlap;
}

ConfigOptionFloatOrPercent PreciseWalls::get_effective_perimeter_overlap(const ConfigOptionFloatOrPercent &user_overlap,
                                                                         int perimeter_count)
{
    // Perimeter/perimeter overlap only matters with 3+ perimeters
    // (need at least two internal perimeters adjacent to each other)
    if (perimeter_count < 3)
    {
        // Return default overlap - user setting doesn't apply
        return ConfigOptionFloatOrPercent(get_standard_overlap_percent(), true);
    }
    // Higher values cause crashes in skeletal trapezoidation algorithm
    if (user_overlap.percent && user_overlap.value > 80.0)
    {
        return ConfigOptionFloatOrPercent(80.0, true);
    }
    return user_overlap;
}

// With spacing/width separation, BeadingStrategy outputs explicit widths:
// - Fixed widths (from extrusion_width parameter) = exactly nominal
// - Gap-filled widths (explicitly set by applyGapFillingAdjustments) = intentionally different
// We only need to snap floating-point drift to exact nominal values.
void PreciseWalls::enforce_exact_widths(std::vector<Athena::VariableWidthLines> &perimeters, coord_t ext_width,
                                        coord_t int_width, coord_t snap_precision)
{
    // Snap widths that are close to nominal back to exact values.
    // Uses the same snap precision grid as thin wall snapping so that percentage-derived
    // widths (which may not land on clean float values) get snapped consistently.
    coord_t snap_threshold = snap_precision / 2;

    for (auto &perimeter_level : perimeters)
    {
        for (auto &extrusion_line : perimeter_level)
        {
            coord_t target_width = (extrusion_line.inset_idx == 0) ? ext_width : int_width;

            for (auto &junction : extrusion_line.junctions)
            {
                if (std::abs(junction.w - target_width) <= snap_threshold)
                {
                    junction.w = target_width;
                }
            }
        }
    }
}

} // namespace preFlight
} // namespace Luminary
