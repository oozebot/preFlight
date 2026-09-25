///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/colour/rgb/Color.hpp"
#include <algorithm>
#include <cmath>

namespace Luminary
{

// Default Transmission Distance in mm. Used wherever a filament TD is missing from config:
// matches the field default in FilamentOptics::td and the fallback in callers that pull TD
// from filament_transmission_distance. Roughly the depth at which a typical PLA reaches ~98%
// opacity at 0.2 mm layers, which is a sensible "I don't know yet" starting point.
inline constexpr float DEFAULT_FILAMENT_TD = 4.0f;

// Optical properties for a physical filament, derived from color + Transmission Distance.
// Uses Beer-Lambert absorption model: at thickness d, the layer's opacity per channel is
//   opacity[c] = 1 - exp(-absorption[c] * d)
// The filament's color shows through proportional to opacity, substrate shows through the rest.
struct FilamentOptics
{
    ColorRGB color;                 // Filament color (from filament_colour setting)
    float td = DEFAULT_FILAMENT_TD; // Transmission Distance in mm (user-provided)
    float absorption[3] = {};       // Per-channel absorption coefficient (derived from color + TD)

    // Recompute absorption from color and TD. Call when color or TD changes.
    void update()
    {
        // absorption[c] is derived so that at thickness=TD, ~98% of the color is visible.
        // For channels where the filament absorbs (color < 1.0), absorption is higher.
        // For channels the filament reflects (color = 1.0), absorption is zero.
        //
        // Model: at thickness d, the visible color channel is:
        //   visible[c] = filament_color[c] * opacity + substrate[c] * (1 - opacity)
        //   opacity = 1 - exp(-absorption_base * d)
        //
        // absorption_base controls how quickly the layer becomes opaque.
        // At d=TD, we want opacity ~= 0.98, so absorption_base = -ln(0.02) / TD ~= 3.91 / TD
        //
        // Per-channel absorption further modulates: channels the filament absorbs
        // (low color value) have additional absorption on top of the base opacity.

        float base_absorption = 3.91f / std::max(td, 0.1f);
        // All channels carry the same TD-derived base absorption; the color value itself
        // carries the spectral component during blending.
        for (int c = 0; c < 3; ++c)
            absorption[c] = base_absorption;
    }

    FilamentOptics() = default;
    FilamentOptics(const ColorRGB &col, float transmission_distance) : color(col), td(transmission_distance)
    {
        update();
    }

    bool operator==(const FilamentOptics &other) const
    {
        return color == other.color && std::abs(td - other.td) < 0.001f;
    }
    bool operator!=(const FilamentOptics &other) const { return !(*this == other); }
};

} // namespace Luminary
