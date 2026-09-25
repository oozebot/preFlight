///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2019 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2015 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/wx.h>
#include "luminary/config/model/Config.hpp"

namespace DSKY
{
using namespace Luminary;

class Bed_2D : public wxPanel
{
    static const int Border = 10;

    bool m_user_drawn_background = true;

    double m_scale_factor;
    Vec2d m_shift = Vec2d::Zero();
    Vec2d m_pos = Vec2d::Zero();

    Point to_pixels(const Vec2d &point, int height);
    void set_pos(const Vec2d &pos);

public:
    explicit Bed_2D(wxWindow *parent);

    void repaint(const std::vector<Vec2d> &shape);
};

} // namespace DSKY

