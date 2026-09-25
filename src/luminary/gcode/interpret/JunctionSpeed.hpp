///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace Luminary
{

// Cornering limits of the time estimator's planner: the speed a toolhead may carry through the
// junction of two moves, given the unit direction of each move.

// Calculates the maximum corner speed under the junction deviation model (Marlin M205 J, Klipper's
// square_corner_velocity, GRBL). A circle tangent to both moves, whose distance from the corner is
// the junction deviation, is the path the head is allowed to round the corner on, and the corner
// speed is the centripetal speed on that circle:
//
//   v^2 = acceleration * junction_deviation * sin(theta/2) / (1 - sin(theta/2))
//
// where theta is the angle between the two moves: 180 degrees for a straight continuation (no
// limit), 90 for a square corner, 0 for a reversal (taken from rest). With unit directions,
// cos(theta) = -dot(prev_dir, curr_dir), and the half-angle identity gives sin(theta/2) without
// any inverse trigonometry.
//
// Parameters:
//   junction_deviation: Maximum allowed deviation from ideal path at corners (mm)
//   acceleration: Move acceleration (mm/s^2)
//   prev_dir_x, prev_dir_y: Normalized direction of previous move
//   curr_dir_x, curr_dir_y: Normalized direction of current move
//
// Returns: Maximum corner speed (mm/s), or very large value if no limit needed
inline float calculate_corner_speed_from_junction_deviation(float junction_deviation, float acceleration,
                                                            float prev_dir_x, float prev_dir_y, float curr_dir_x,
                                                            float curr_dir_y)
{
    if (junction_deviation <= 0.0f || acceleration <= 0.0f)
        return std::numeric_limits<float>::max();

    const float dot = prev_dir_x * curr_dir_x + prev_dir_y * curr_dir_y;
    const float cos_theta = std::clamp(-dot, -1.0f, 1.0f);

    // Straight continuation (theta near 180 degrees): no junction limit.
    if (cos_theta < -0.999999f)
        return std::numeric_limits<float>::max();

    // Reversal (theta near 0): the corner is taken from rest.
    if (cos_theta > 0.999999f)
        return 0.0f;

    const float sin_theta_d2 = std::sqrt(0.5f * (1.0f - cos_theta));
    return std::sqrt(acceleration * junction_deviation * sin_theta_d2 / (1.0f - sin_theta_d2));
}

// Klipper has no junction deviation setting of its own: it derives one from square_corner_velocity,
// the speed it allows through a 90 degree corner, and the configured maximum acceleration. At 90
// degrees the corner formula's factor is 1 + sqrt(2), so a deviation of scv^2 * (sqrt(2) - 1) / a
// gives exactly scv back.
inline float klipper_junction_deviation(float square_corner_velocity, float max_accel)
{
    if (square_corner_velocity <= 0.0f || max_accel <= 0.0f)
        return 0.0f;
    constexpr float sqrt2_minus_1 = 0.41421356f;
    return (square_corner_velocity * square_corner_velocity) * sqrt2_minus_1 / max_accel;
}

// Calculates maximum corner speed based on per-axis jerk limits and direction change.
//
// At a corner, the velocity vector changes direction. The change in velocity
// on each axis depends on the direction change and must respect per-axis jerk limits:
//   delta_velocity_x = V * (curr_dir_x - prev_dir_x)
//   delta_velocity_y = V * (curr_dir_y - prev_dir_y)
//
// Per-axis constraints:
//   |V * delta_dir_x| <= jerk_x  =>  V <= jerk_x / |delta_dir_x|
//   |V * delta_dir_y| <= jerk_y  =>  V <= jerk_y / |delta_dir_y|
//
// The corner speed is limited by whichever axis is more constrained.
//
// Parameters:
//   jerk_x, jerk_y: Per-axis jerk limits (mm/s), typically from M566
//   prev_dir_x, prev_dir_y: Normalized direction of previous move
//   curr_dir_x, curr_dir_y: Normalized direction of current move
//
// Returns: Maximum corner speed (mm/s), or very large value if no limit needed
inline float calculate_corner_speed_from_jerk(float jerk_x, float jerk_y, float prev_dir_x, float prev_dir_y,
                                              float curr_dir_x, float curr_dir_y)
{
    // Calculate direction change on each axis
    const float delta_dir_x = curr_dir_x - prev_dir_x;
    const float delta_dir_y = curr_dir_y - prev_dir_y;

    // Check if this is nearly a straight line (very small direction change)
    const float delta_magnitude = std::sqrt(delta_dir_x * delta_dir_x + delta_dir_y * delta_dir_y);
    if (delta_magnitude < 0.01f) // ~0.5 degree angle
        return std::numeric_limits<float>::max();

    // Calculate speed limit from each axis constraint
    // V * |delta_dir_x| <= jerk_x  =>  V <= jerk_x / |delta_dir_x|
    float v_limit = std::numeric_limits<float>::max();

    const float abs_delta_x = std::abs(delta_dir_x);
    const float abs_delta_y = std::abs(delta_dir_y);

    if (abs_delta_x > 0.001f && jerk_x > 0.0f)
    {
        v_limit = std::min(v_limit, jerk_x / abs_delta_x);
    }

    if (abs_delta_y > 0.001f && jerk_y > 0.0f)
    {
        v_limit = std::min(v_limit, jerk_y / abs_delta_y);
    }

    return v_limit;
}

} // namespace Luminary
