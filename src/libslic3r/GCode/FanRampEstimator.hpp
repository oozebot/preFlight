///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_FanRampEstimator_hpp_
#define slic3r_FanRampEstimator_hpp_

namespace Slic3r
{

//
// This class estimates the time required for a small DC brushless fan (typical of 3D printer
// part cooling) to transition between arbitrary speed setpoints (e.g., 0→100%, 60→100%, 80→30%).
//
// Model: Acceleration decreases at higher speeds.
//   acceleration(s) ∝ (1 - s)^p   where p varies by fan type
//
// Closed-form solution for acceleration:
//   time(s1 → s2) = [(1-s1)^exp - (1-s2)^exp] × t_up
//   where exp = 1 - p (configurable via fan response type)
//
// Coast-down (deceleration) uses inverse-speed model:
//   time(s1 → s2) = (1/s2 - 1/s1) × t_down / 19
//
// Key properties:
//   - accel_time(0, 100) == t_up (exactly)
//   - Segments sum correctly: accel_time(a,b) + accel_time(b,c) == accel_time(a,c)
//   - Higher speed ranges take proportionally longer (but not absurdly so)
//
// Example (t_up = 1.0s, exponent = 0.7 axial fan):
//   0% → 100%:  1.000s (100%)
//   0% → 20%:   0.145s (14.5%)
//   20% → 80%:  0.531s (53.1%)
//   80% → 100%: 0.324s (32.4%)

class FanRampEstimator
{
public:
    // Construct with measured fan characteristics
    // t_up: Time in seconds for 0% → 100% spin-up (from fan spec or measured)
    // exponent: Curve shape (0.7=axial/fast, 0.5=blower, 0.4=high-inertia)
    // t_down: Time in seconds for 100% → ~5% coast (0 = derive from 2x t_up)
    FanRampEstimator(float t_up, float exponent = 0.5f, float t_down = 0.f);

    // Default constructor - disabled (all zeros)
    FanRampEstimator() : m_enabled(false), m_t_up(0.f), m_exponent(0.5f), m_t_down(0.f) {}

    // Check if the estimator is enabled (t_up > 0)
    bool enabled() const { return m_enabled; }

    // Get the user-specified spinup time (0->100%)
    float spinup_time() const { return m_t_up; }

    // Estimate time to transition between two fan speed percentages (0-100)
    // Returns time in seconds, or 0 if no delay needed
    float transition_time(int pct_start, int pct_end) const;

private:
    bool m_enabled;   // True if t_up > 0
    float m_t_up;     // Measured 0→100% time (seconds)
    float m_exponent; // Curve shape exponent (0.7=fast, 0.5=moderate, 0.4=slow)
    float m_t_down;   // Measured 100%→~5% coast time (seconds)
};

} // namespace Slic3r

#endif // slic3r_FanRampEstimator_hpp_
