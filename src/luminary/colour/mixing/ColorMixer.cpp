///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ColorMixer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace Luminary
{
namespace ColorMixing
{

// Multiplicative transmittance model for stacked translucent filament layers.
// Each layer acts as a colored filter: it absorbs certain wavelengths and transmits others.
// Stacking is multiplicative per channel - this correctly produces subtractive CMYK mixing:
//   Cyan + Yellow = Green (red absorbed by Cyan, blue absorbed by Yellow, green passes both)
//   Cyan + Magenta = Blue
//   Magenta + Yellow = Red
ColorRGB predict_stack(const std::vector<std::pair<int, float>> &layers, const std::vector<FilamentOptics> &filaments,
                       ColorRGB substrate)
{
    if (layers.empty())
        return substrate;

    // Running transmittance per channel (starts at 1.0 = fully transparent)
    float T_r = 1.0f;
    float T_g = 1.0f;
    float T_b = 1.0f;

    for (const auto &[filament_idx, height] : layers)
    {
        assert(filament_idx >= 0 && filament_idx < (int) filaments.size());
        const FilamentOptics &fil = filaments[filament_idx];

        // Layer opacity from Beer-Lambert
        float opacity = 1.0f - std::exp(-fil.absorption[0] * height);
        opacity = std::clamp(opacity, 0.0f, 1.0f);

        float fr = std::max(fil.color.r(), 0.01f);
        float fg = std::max(fil.color.g(), 0.01f);
        float fb = std::max(fil.color.b(), 0.01f);

        T_r *= (1.0f - opacity) + opacity * fr;
        T_g *= (1.0f - opacity) + opacity * fg;
        T_b *= (1.0f - opacity) + opacity * fb;
    }

    ColorRGB result(std::clamp(substrate.r() * T_r, 0.0f, 1.0f), std::clamp(substrate.g() * T_g, 0.0f, 1.0f),
                    std::clamp(substrate.b() * T_b, 0.0f, 1.0f));

    return result;
}

ColorRGB predict_blend(const FilamentOptics &a, const FilamentOptics &b, float ratio_b, int num_layers,
                       float layer_height)
{
    if (num_layers <= 0)
        return a.color;

    int count_b = std::max(0, std::min(num_layers, (int) std::round(ratio_b * num_layers)));

    std::vector<FilamentOptics> filaments = {a, b};

    // Bresenham distribution
    std::vector<std::pair<int, float>> stack;
    stack.reserve(num_layers);
    float error = 0.0f;
    float step = (num_layers > 0) ? (float) count_b / (float) num_layers : 0.0f;
    for (int i = 0; i < num_layers; ++i)
    {
        error += step;
        if (error >= 0.5f)
        {
            stack.emplace_back(1, layer_height);
            error -= 1.0f;
        }
        else
        {
            stack.emplace_back(0, layer_height);
        }
    }

    return predict_stack(stack, filaments);
}

// Geometric mean blend - correct subtractive (CMYK) color mixing.
// Cyan(0,1,1) + Yellow(1,1,0) at 50% gives (0, 1, 0) = Green
// Cyan + Magenta at 50% gives (0, 0, 1) = Blue
// Magenta + Yellow at 50% gives (1, 0, 0) = Red
ColorRGB km_blend(const FilamentOptics &a, const FilamentOptics &b, float ratio_b)
{
    ratio_b = std::clamp(ratio_b, 0.0f, 1.0f);
    float rat_a = 1.0f - ratio_b;

    // Geometric mean: each channel's transmittance is color^ratio
    // Clamp to 0.01 so absorption is strong but not absolute
    auto geo_mix = [](float ca, float cb, float ra, float rb) -> float
    {
        float a = std::max(ca, 0.01f);
        float b = std::max(cb, 0.01f);
        return std::clamp(std::pow(a, ra) * std::pow(b, rb), 0.0f, 1.0f);
    };

    return ColorRGB(geo_mix(a.color.r(), b.color.r(), rat_a, ratio_b),
                    geo_mix(a.color.g(), b.color.g(), rat_a, ratio_b),
                    geo_mix(a.color.b(), b.color.b(), rat_a, ratio_b));
}

// sRGB gamma decode
static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Lab rgb_to_lab(const ColorRGB &rgb)
{
    float lr = srgb_to_linear(rgb.r());
    float lg = srgb_to_linear(rgb.g());
    float lb = srgb_to_linear(rgb.b());

    float X = 0.4124564f * lr + 0.3575761f * lg + 0.1804375f * lb;
    float Y = 0.2126729f * lr + 0.7151522f * lg + 0.0721750f * lb;
    float Z = 0.0193339f * lr + 0.1191920f * lg + 0.9503041f * lb;

    X /= 0.95047f;
    Y /= 1.00000f;
    Z /= 1.08883f;

    auto f = [](float t) -> float
    {
        const float delta = 6.0f / 29.0f;
        return (t > delta * delta * delta) ? std::cbrt(t) : t / (3.0f * delta * delta) + 4.0f / 29.0f;
    };

    Lab lab;
    lab.L = 116.0f * f(Y) - 16.0f;
    lab.a = 500.0f * (f(X) - f(Y));
    lab.b = 200.0f * (f(Y) - f(Z));
    return lab;
}

// CIEDE2000 color difference: Lab-space metric with lightness/chroma/hue weighting and a
// blue-region rotation term (RT) that corrects plain Lab Euclidean's mis-ranking of
// blue/magenta. Reference: Sharma, Wu, Dalal (2004), "The CIEDE2000 color-difference
// formula: implementation notes, supplementary test data, and mathematical observations."
float delta_e(const ColorRGB &a, const ColorRGB &b)
{
    const Lab la = rgb_to_lab(a);
    const Lab lb = rgb_to_lab(b);

    const float kL = 1.0f, kC = 1.0f, kH = 1.0f;
    const float deg = 0.017453292519943295f; // pi / 180

    float C1 = std::sqrt(la.a * la.a + la.b * la.b);
    float C2 = std::sqrt(lb.a * lb.a + lb.b * lb.b);
    float C_bar = 0.5f * (C1 + C2);

    float C7 = C_bar * C_bar * C_bar * C_bar * C_bar * C_bar * C_bar;
    float G = 0.5f * (1.0f - std::sqrt(C7 / (C7 + 6103515625.0f))); // 25^7 = 6103515625

    float a1p = (1.0f + G) * la.a;
    float a2p = (1.0f + G) * lb.a;
    float C1p = std::sqrt(a1p * a1p + la.b * la.b);
    float C2p = std::sqrt(a2p * a2p + lb.b * lb.b);

    auto wrap_deg = [](float h)
    {
        while (h < 0.0f)
            h += 360.0f;
        while (h >= 360.0f)
            h -= 360.0f;
        return h;
    };

    float h1p = (C1p < 1e-6f) ? 0.0f : wrap_deg(std::atan2(la.b, a1p) / deg);
    float h2p = (C2p < 1e-6f) ? 0.0f : wrap_deg(std::atan2(lb.b, a2p) / deg);

    float dLp = lb.L - la.L;
    float dCp = C2p - C1p;

    float dhp;
    if (C1p * C2p < 1e-6f)
        dhp = 0.0f;
    else
    {
        float raw = h2p - h1p;
        if (raw > 180.0f)
            raw -= 360.0f;
        else if (raw < -180.0f)
            raw += 360.0f;
        dhp = raw;
    }
    float dHp = 2.0f * std::sqrt(C1p * C2p) * std::sin(0.5f * dhp * deg);

    float Lp_bar = 0.5f * (la.L + lb.L);
    float Cp_bar = 0.5f * (C1p + C2p);

    float hp_bar;
    if (C1p * C2p < 1e-6f)
        hp_bar = h1p + h2p;
    else
    {
        if (std::abs(h1p - h2p) <= 180.0f)
            hp_bar = 0.5f * (h1p + h2p);
        else if (h1p + h2p < 360.0f)
            hp_bar = 0.5f * (h1p + h2p + 360.0f);
        else
            hp_bar = 0.5f * (h1p + h2p - 360.0f);
    }

    float T = 1.0f - 0.17f * std::cos((hp_bar - 30.0f) * deg) + 0.24f * std::cos(2.0f * hp_bar * deg) +
              0.32f * std::cos((3.0f * hp_bar + 6.0f) * deg) - 0.20f * std::cos((4.0f * hp_bar - 63.0f) * deg);

    float dtheta = 30.0f * std::exp(-(((hp_bar - 275.0f) / 25.0f) * ((hp_bar - 275.0f) / 25.0f)));
    float Cp_bar7 = Cp_bar * Cp_bar * Cp_bar * Cp_bar * Cp_bar * Cp_bar * Cp_bar;
    float RC = 2.0f * std::sqrt(Cp_bar7 / (Cp_bar7 + 6103515625.0f));
    float RT = -std::sin(2.0f * dtheta * deg) * RC;

    float L50 = Lp_bar - 50.0f;
    float SL = 1.0f + (0.015f * L50 * L50) / std::sqrt(20.0f + L50 * L50);
    float SC = 1.0f + 0.045f * Cp_bar;
    float SH = 1.0f + 0.015f * Cp_bar * T;

    float L_term = dLp / (kL * SL);
    float C_term = dCp / (kC * SC);
    float H_term = dHp / (kH * SH);

    return std::sqrt(L_term * L_term + C_term * C_term + H_term * H_term + RT * C_term * H_term);
}

std::vector<int> optimize_sequence(const ColorRGB &target, const std::vector<FilamentOptics> &available,
                                   int max_cycle_length, float layer_height)
{
    if (available.empty())
        return {};
    if (available.size() == 1)
        return {0};

    std::vector<int> best_pattern;
    float best_de = std::numeric_limits<float>::max();

    for (int i = 0; i < (int) available.size(); ++i)
    {
        for (int j = i + 1; j < (int) available.size(); ++j)
        {
            for (int count_j = 0; count_j <= max_cycle_length; ++count_j)
            {
                int cycle_len = max_cycle_length;

                float error = 0.0f;
                float step = (cycle_len > 0) ? (float) count_j / (float) cycle_len : 0.0f;
                std::vector<int> one_cycle;
                one_cycle.reserve(cycle_len);

                for (int k = 0; k < cycle_len; ++k)
                {
                    error += step;
                    if (error >= 0.5f)
                    {
                        one_cycle.push_back(j);
                        error -= 1.0f;
                    }
                    else
                    {
                        one_cycle.push_back(i);
                    }
                }

                // Simulate enough layers to reach full opacity convergence
                float max_td = 0;
                for (int idx : one_cycle)
                    max_td = std::max(max_td, available[idx].td);
                int reps = std::max(3, (int) std::ceil(max_td / (layer_height * (float) cycle_len)));

                std::vector<std::pair<int, float>> stack;
                stack.reserve(cycle_len * reps);
                for (int rep = 0; rep < reps; ++rep)
                    for (int idx : one_cycle)
                        stack.emplace_back(idx, layer_height);

                ColorRGB predicted = predict_stack(stack, available);
                float de = delta_e(predicted, target);

                if (de < best_de)
                {
                    best_de = de;
                    best_pattern = one_cycle;
                }
            }
        }
    }

    for (int i = 0; i < (int) available.size(); ++i)
    {
        float de = delta_e(available[i].color, target);
        if (de < best_de)
        {
            best_de = de;
            best_pattern = {i};
        }
    }

    return best_pattern;
}

} // namespace ColorMixing
} // namespace Luminary
