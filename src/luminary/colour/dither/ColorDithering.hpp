///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

namespace Luminary
{

enum class DitherMethod
{
    Ordered,  // Fixed cadence: repeat the pattern directly (AABBAABB)
    Bresenham // Evenly distributed minority filament (default)
};

struct DitherConfig
{
    DitherMethod method = DitherMethod::Bresenham;
    bool phase_rotation = false; // Disabled: creates worse banding at cycle boundaries than it solves
};

// Given a mixed color's layer pattern and a layer index, return the physical filament index.
// pattern: repeating filament indices, e.g., {0, 0, 2} = Cyan, Cyan, Yellow
// layer_index: absolute layer number for this region
// config: dithering configuration
int resolve_layer_filament(const std::vector<int> &pattern, int layer_index, const DitherConfig &config = {});

// Build a Bresenham-distributed pattern from filament weights.
// filament_indices: which filaments participate
// weights: normalized weights (must sum to 1.0), same length as filament_indices
// cycle_length: total layers in one cycle
// Returns: cycle_length filament indices, distributed evenly by Bresenham
std::vector<int> build_bresenham_pattern(const std::vector<int> &filament_indices, const std::vector<float> &weights,
                                         int cycle_length);

// Build a simple ordered pattern from filament weights.
// filament_indices and weights as above.
// cycle_length: total layers in one cycle
// Returns: cycle_length filament indices, grouped by filament
std::vector<int> build_ordered_pattern(const std::vector<int> &filament_indices, const std::vector<float> &weights,
                                       int cycle_length);

} // namespace Luminary
