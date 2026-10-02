///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2021 Vojtěch Bubník @bubnikv
///|/
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "luminary/core/Prelude.hpp"
#include "luminary/gcode/interpret/GCodeReader.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

namespace Luminary
{

class SpiralVase
{
public:
    SpiralVase() = delete;

    explicit SpiralVase(const PrintConfig &config) : m_config(config)
    {
        m_reader.z() = (float) m_config.z_offset;
        m_reader.apply_config(m_config);

        const double max_nozzle_diameter = *std::max_element(config.nozzle_diameter.values.begin(),
                                                             config.nozzle_diameter.values.end());
        m_max_xy_smoothing = float(2. * max_nozzle_diameter);
    };

    void enable(bool enable) { m_enabled = enable; }

    std::string process_layer(const std::string &gcode, bool last_layer);

private:
    // Feeds a layer the spiral does not transform to the reader and emits the pending ramp-down pass ahead of it.
    std::string end_spiral(const std::string &gcode);

    const PrintConfig &m_config;
    GCodeReader m_reader;
    float m_max_xy_smoothing = 0.f;

    bool m_enabled = false;
    // The previous layer was a spiral layer that extruded. When it was not, the next spiral layer ramps
    // its extrusion up from zero.
    bool m_spiral_live = false;
    // Ramp-down pass of the last spiral layer, emitted ahead of the next layer if the spiral stops there.
    std::string m_pending_ramp;
    // Whether to interpolate XY coordinates with the previous layer. Results in no seam at layer changes
    bool m_smooth_spiral = true;
    std::vector<Vec2f> m_previous_layer;
};
} // namespace Luminary
