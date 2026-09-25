///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2021 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2016 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <utility>

#include "luminary/core/Prelude.hpp"
#include "luminary/fill/contract/FillBase.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

namespace Luminary
{

class Surface;

class FillLine : public Fill
{
public:
    Fill *clone() const override { return new FillLine(*this); };
    ~FillLine() override = default;
    bool is_self_crossing() override { return false; }

protected:
    void _fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                              const std::pair<float, Point> &direction, ExPolygon expolygon,
                              Polylines &polylines_out) override;

    coord_t _min_spacing;
    coord_t _line_spacing;
    // distance threshold for allowing the horizontal infill lines to be connected into a continuous path
    coord_t _diagonal_distance;
    // only for line infill
    coord_t _line_oscillation;

    Line _line(int i, coord_t x, coord_t y_min, coord_t y_max) const
    {
        coord_t osc = (i & 1) ? this->_line_oscillation : 0;
        return Line(Point(x - osc, y_min), Point(x + osc, y_max));
    }

    bool _can_connect(coord_t dist_X, coord_t dist_Y)
    {
        const auto TOLERANCE = coord_t(10 * SCALED_EPSILON);
        return (dist_X >= (this->_line_spacing - this->_line_oscillation) - TOLERANCE) &&
               (dist_X <= (this->_line_spacing + this->_line_oscillation) + TOLERANCE) &&
               (dist_Y <= this->_diagonal_distance);
    }
};

}; // namespace Luminary
