///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <math.h>
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#include "SmoothPath.hpp"
#include "luminary/toolpath/arc_welder/ArcWelder.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

namespace Luminary
{

class GCodeGenerator;

namespace GCode
{

class Wipe
{
public:
    using Path = Luminary::Geometry::ArcWelder::Path;

    Wipe() = default;

    void init(const PrintConfig &config, const std::vector<unsigned int> &extruders);
    void enable(double wipe_len_max)
    {
        m_enabled = true;
        m_wipe_len_max = wipe_len_max;
    }
    void disable() { m_enabled = false; }
    bool enabled() const { return m_enabled; }

    const Path &path() const { return m_path; }
    bool has_path() const
    {
        assert(m_path.empty() || m_path.size() > 1);
        return !m_path.empty();
    }
    void reset_path()
    {
        m_path.clear();
        m_offset = Point::Zero();
    }
    void set_path(const Path &path)
    {
        assert(path.empty() || path.size() > 1);
        this->reset_path();
        if (this->enabled() && path.size() > 1)
            m_path = path;
    }
    void set_path(Path &&path)
    {
        assert(path.empty() || path.size() > 1);
        this->reset_path();
        if (this->enabled() && path.size() > 1)
            m_path = std::move(path);
    }
    void set_path(SmoothPath &&path);
    void offset_path(const Point &v) { m_offset += v; }

    std::string wipe(GCodeGenerator &gcodegen, bool toolchange);

    // Reduce feedrate a bit; travel speed is often too high to move on existing material.
    // Too fast = ripping of existing material; too slow = short wipe path, thus more blob.
    static double calc_wipe_speed(const GCodeConfig &config) { return config.travel_speed.value * 0.8; }
    // Reduce retraction length a bit so the effective retraction speed does not exceed the
    // configured one after rounding.
    static double calc_xy_to_e_ratio(const GCodeConfig &config, unsigned int extruder_id)
    {
        return 0.95 * floor(config.retract_speed.get_at(extruder_id) + 0.5) / calc_wipe_speed(config);
    }

private:
    bool m_enabled{false};
    // Maximum length of a path to accumulate. Only wipes shorter than this threshold will be requested.
    double m_wipe_len_max{0.};
    Path m_path;
    // Offset from m_path to the current PrintObject active.
    Point m_offset{Point::Zero()};
};

// Make a little move inwards before leaving loop.
std::optional<Point> wipe_hide_seam(const SmoothPath &path, bool is_hole, double wipe_length);

} // namespace GCode
} // namespace Luminary
