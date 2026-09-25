///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "ImGuiDoubleSlider.hpp"

namespace DoubleSlider
{

class DSForGcode : public Manager<unsigned int>
{
public:
    DSForGcode() : Manager<unsigned int>() {}
    DSForGcode(int lowerPos, int higherPos, int minPos, int maxPos)
    {
        Init(lowerPos, higherPos, minPos, maxPos, "moves_slider", true);
    }
    ~DSForGcode() {}

    void Render(const int canvas_width, const int canvas_height, float extra_scale = 1.f, float offset = 0.f) override;

    void set_render_as_disabled(bool value) { m_render_as_disabled = value; }
    bool is_rendering_as_disabled() const { return m_render_as_disabled; }
    // Canvas pixels on the left covered by a floating panel; the slider is laid out past them.
    void set_left_inset(float inset) { m_left_inset = inset; }

private:
    bool m_render_as_disabled{false};
    float m_left_inset{0.0f};
};

} // namespace DoubleSlider

