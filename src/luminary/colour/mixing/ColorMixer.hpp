///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/colour/rgb/Color.hpp"
#include "FilamentOptics.hpp"
#include <vector>
#include <utility>

namespace Luminary
{
namespace ColorMixing
{

// Predict the visible color of N stacked layers viewed from above.
// layers: bottom-to-top, each {filament_index, layer_height_mm}
// substrate: color beneath the stack (default white)
ColorRGB predict_stack(const std::vector<std::pair<int, float>> &layers, const std::vector<FilamentOptics> &filaments,
                       ColorRGB substrate = ColorRGB::WHITE());

// Predict the blend of two filaments at a given ratio.
// ratio_b: 0.0 = all A, 1.0 = all B
// num_layers: how many alternating layers in one blend cycle
// layer_height: height of each layer in mm
ColorRGB predict_blend(const FilamentOptics &a, const FilamentOptics &b, float ratio_b, int num_layers,
                       float layer_height);

// Simple two-filament K-M blend at a ratio (no layer stacking, pure K-space mix).
// Fast approximation for palette swatch display.
ColorRGB km_blend(const FilamentOptics &a, const FilamentOptics &b, float ratio_b);

// CIELAB color space
struct Lab
{
    float L, a, b;
};

// Convert sRGB (0-1) to CIELAB
Lab rgb_to_lab(const ColorRGB &rgb);

// CIEDE2000 Delta-E: perceptual color difference with lightness/chroma/hue weighting and a
// blue-region rotation term that corrects Lab Euclidean's mis-ranking of blue/magenta.
// 0 = identical, ~1 = just-noticeable, >5 = obviously different. See ColorMixer.cpp for the
// full Sharma/Wu/Dalal reference.
float delta_e(const ColorRGB &a, const ColorRGB &b);

// Find the best repeating layer pattern for a target color.
// Returns filament indices for one cycle of the pattern.
std::vector<int> optimize_sequence(const ColorRGB &target, const std::vector<FilamentOptics> &available,
                                   int max_cycle_length, float layer_height);

} // namespace ColorMixing
} // namespace Luminary
