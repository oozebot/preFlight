// Released under AGPLv3 or higher; see the repository LICENSE.
#ifndef slic3r_InterlockingFlow_hpp_
#define slic3r_InterlockingFlow_hpp_

#include <cmath>
#include <stdexcept>

namespace Slic3r
{
// A segment whose XYZ rounds to its start is an extruder-only move, not a
// zero-duration Cartesian move. Its feed is measured along filament distance.
inline double interlocking_motion_length(double cartesian_length, double volume, double filament_area)
{
    if (!std::isfinite(cartesian_length) || cartesian_length < 0 ||
        !std::isfinite(volume) || !std::isfinite(filament_area) || filament_area <= 0)
        throw std::invalid_argument("Invalid interlocking motion input");
    return cartesian_length == 0 && volume > 0 ? volume / filament_area : cartesian_length;
}

// Nominal XY IJ-arc length: directed sweep about the emitted centre, using
// the start radius (the Klipper/Marlin convention). Coordinates are quantized
// by the caller. Small endpoint-radius differences from rounding are retained.
inline double interlocking_arc_length(double dx, double dy, double i, double j, bool ccw)
{
    const double radius = std::hypot(i, j);
    const double end_radius = std::hypot(dx-i, dy-j);
    if (!std::isfinite(radius) || !std::isfinite(end_radius) || radius <= 0 || end_radius <= 0)
        throw std::invalid_argument("Invalid interlocking IJ arc");
    double angle = std::atan2((-i)*(dy-j) - (-j)*(dx-i), (-i)*(dx-i) + (-j)*(dy-j));
    const double turn = 2 * std::acos(-1.);
    if (angle < 0) angle += turn;
    if (!ccw) angle -= turn;
    if (dx == 0 && dy == 0) angle = turn;
    const double length = radius * std::abs(angle);
    if (!std::isfinite(length) || length <= 0)
        throw std::invalid_argument("Degenerate interlocking IJ arc");
    return length;
}

// Inputs describe the emitted move, AFTER E and coordinate quantization.
// Volume already includes extrusion multiplier and any local interlocking flow.
// The writer emits integer mm/min; rounding the cap upward would defeat it.
inline double interlocking_feedrate(double requested_f, double limit, double length, double volume)
{
    if (!std::isfinite(requested_f) || requested_f < 1 || requested_f != std::floor(requested_f) ||
        !std::isfinite(limit) || limit < 0 || !std::isfinite(length) || length < 0 || !std::isfinite(volume))
        throw std::invalid_argument("Invalid interlocking flow-limit input");
    if (limit == 0 || volume <= 0)
        return requested_f;
    if (length == 0)
        throw std::invalid_argument("Cannot limit interlocking extrusion on a zero-length emitted move");

    const auto flow_at = [&](double f) {
        return static_cast<long double>(volume) * (static_cast<long double>(f) / 60) / length;
    };
    if (flow_at(requested_f) <= limit)
        return requested_f;
    double result = static_cast<double>(std::floor(static_cast<long double>(limit) * length / volume * 60));
    if (!std::isfinite(result) || result < 1)
        throw std::invalid_argument("Interlocking volumetric limit requires a feedrate below F1");
    // Correct a possible floating-point rounding at the integer boundary, without
    // a percentage margin. Never raise the feed selected by the existing logic.
    if (result > requested_f)
        result = requested_f;
    if (flow_at(result) > limit)
        --result;
    if (result < 1)
        throw std::invalid_argument("Interlocking volumetric limit requires a feedrate below F1");
    return result;
}
}
#endif
