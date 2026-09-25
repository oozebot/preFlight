///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "DoubleSliderForGcode.hpp"
#include "GUI_App.hpp"
#include "ImGuiWrapper.hpp"

#include <algorithm>

namespace DoubleSlider
{

// DPI scaling helpers for slider positioning
static float GetScaledLeftMargin(float scale)
{
    return (13.0f + 100.0f) * scale; // avoid thumbnail toolbar, scaled for DPI
}

static float GetScaledHorizontalSliderHeight(float scale)
{
    return 40.0f * scale; // slider height scaled for DPI
}

void DSForGcode::Render(const int canvas_width, const int canvas_height, float extra_scale /* = 0.1f*/,
                        float offset /* = 0.f*/)
{
    if (!m_ctrl.IsShown())
        return;
    m_scale = extra_scale * 0.1f * m_em;

    // Use DPI-scaled margins and heights
    const float scaled_left_margin = GetScaledLeftMargin(m_scale);
    const float scaled_slider_height = GetScaledHorizontalSliderHeight(m_scale);
    // Centered in the canvas minus the left inset, as if the canvas started past it. The margins
    // are symmetric so the slider shares its center with the scene and the notifications; in a
    // narrow window they still keep clear of the layers slider column on the right.
    const float usable_width = canvas_width - m_left_inset;
    const float margin = std::min(std::max({scaled_left_margin, 0.2f * usable_width, 80.0f * m_scale}),
                                  0.5f * usable_width);
    ImVec2 pos = ImVec2{m_left_inset + margin, canvas_height - scaled_slider_height};
    ImVec2 size = ImVec2(usable_width - 2 * margin, scaled_slider_height);

    // Use legend font for slider labels (matches G-code legend sidebar)
    ImFont *legend_font = DSKY::wxGetApp().imgui()->get_legend_font();
    if (legend_font)
        m_ctrl.set_label_font(legend_font);

    m_ctrl.Init(pos, size, m_scale);
    if (m_ctrl.render())
        process_thumb_move();
}

} // namespace DoubleSlider
