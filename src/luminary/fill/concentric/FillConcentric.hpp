///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2016 Alessandro Ranellucci @alranel
///|/
///|/ Copyright (c) Prusa Research 2016 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <utility>

#include "luminary/fill/contract/FillBase.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

namespace Luminary
{
class Point;

class FillConcentric : public Fill
{
public:
    ~FillConcentric() override = default;
    bool is_self_crossing() override { return false; }

protected:
    Fill *clone() const override { return new FillConcentric(*this); };
    void _fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                              const std::pair<float, Point> &direction, ExPolygon expolygon,
                              Polylines &polylines_out) override;

    void _fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                              const std::pair<float, Point> &direction, ExPolygon expolygon,
                              ThickPolylines &thick_polylines_out) override;

    bool no_sort() const override { return true; }
};

} // namespace Luminary
