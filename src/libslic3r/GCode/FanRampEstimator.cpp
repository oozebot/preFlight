///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "FanRampEstimator.hpp"
#include <algorithm>
#include <cmath>

namespace Slic3r
{

FanRampEstimator::FanRampEstimator(float t_up, float exponent, float t_down)
    : m_enabled(t_up > 0.f)
    , m_t_up(t_up)
    , m_exponent(exponent)
    , m_t_down(t_down > 0.f ? t_down : t_up * 2.f) // Default: coast-down = 2x spin-up
{
}

float FanRampEstimator::transition_time(int pct_start, int pct_end) const
{
    if (!m_enabled)
    {
        return 0.f;
    }

    // Normalize to 0-1 range
    float s1 = std::clamp(pct_start / 100.f, 0.f, 1.f);
    float s2 = std::clamp(pct_end / 100.f, 0.f, 1.f);

    float t = 0.f;

    if (s2 > s1)
    {
        // Accelerating: acceleration decreases at higher speeds
        // Model: acceleration(s) ∝ (1 - s)^p
        // Closed-form solution: time = [(1-s1)^exp - (1-s2)^exp] × t_up
        // Exponent values: 0.7 (axial/fast), 0.5 (blower), 0.4 (high-inertia)
        float term1 = std::pow(1.f - s1, m_exponent);
        float term2 = std::pow(1.f - s2, m_exponent);
        t = (term1 - term2) * m_t_up;
    }
    else if (s2 < s1)
    {
        // Decelerating (coast-down): inverse-speed model from drag physics
        // ω(t) = ω₀ / (1 + k×ω₀×t)
        // Solving: t = (1/s2 - 1/s1) × t_down / 19
        // where 19 = (1/0.05 - 1/1.0), the factor for 100%→5%
        float s1_clamped = std::max(0.01f, s1);
        float s2_clamped = std::max(0.01f, s2);
        t = (1.f / s2_clamped - 1.f / s1_clamped) * m_t_down / 19.f;
    }

    return std::max(0.f, t);
}

} // namespace Slic3r
