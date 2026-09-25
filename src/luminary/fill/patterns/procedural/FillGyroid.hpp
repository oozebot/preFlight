///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2020 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) SuperSlicer 2018 Remi Durand @supermerill
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <utility>

#include "luminary/core/Prelude.hpp"
#include "luminary/fill/contract/FillBase.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

namespace Luminary
{
class Point;

class FillGyroid : public Fill
{
public:
    FillGyroid() {}
    Fill *clone() const override { return new FillGyroid(*this); }

    // require bridge flow since most of this pattern hangs in air
    bool use_bridge_flow() const override { return false; }
    bool is_self_crossing() override { return false; }

    // Correction applied to regular infill angle to maximize printing
    // speed in default configuration (degrees)
    static constexpr float CorrectionAngle = -45.;

    // Density adjustment to have a good %of weight.
    static constexpr double DensityAdjust = 2.44;

    // Gyroid upper resolution tolerance (mm^-2)
    static constexpr double PatternTolerance = 0.2;

protected:
    void _fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                              const std::pair<float, Point> &direction, ExPolygon expolygon,
                              Polylines &polylines_out) override;
};

} // namespace Luminary
