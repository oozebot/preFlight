///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "MixedColorPalette.hpp"
#include "luminary/colour/mixing/ColorMixer.hpp"
#include "luminary/colour/dither/ColorDithering.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

namespace Luminary
{

const MixedColor *MixedColorPalette::find_by_id(int id) const
{
    for (const auto &c : m_colors)
        if (c.id == id)
            return &c;
    return nullptr;
}

int MixedColorPalette::num_enabled() const
{
    int count = 0;
    for (const auto &c : m_colors)
        if (c.enabled)
            ++count;
    return count;
}

// 0-based "T0", "T1", ... tool labels for palette entry names. Matches how firmware and
// most printer interfaces refer to tools (T0/T1/...), so a user reading a tooltip sees the
// same identifier they'd use at the machine.
static std::string extruder_label(int idx)
{
    return "T" + std::to_string(idx);
}

std::string MixedColorPalette::generate_name(int filament_a, int filament_b, float ratio_b)
{
    int pct_b = (int) std::round(ratio_b * 100.0f);
    int pct_a = 100 - pct_b;

    std::ostringstream ss;
    ss << pct_a << "% " << extruder_label(filament_a) << " + " << pct_b << "% " << extruder_label(filament_b);
    return ss.str();
}

// Helper: create a MixedColor entry from filament indices and weights
static MixedColor make_mixed(int &next_id, const std::vector<FilamentOptics> &filaments, float layer_height,
                             int max_cycle_length, const std::vector<int> &indices, const std::vector<float> &weights,
                             const std::string &name)
{
    MixedColor mc;
    mc.id = next_id++;
    mc.name = name;
    mc.auto_generated = true;
    mc.enabled = true;
    mc.user_override = false;

    // Adjust cycle length to be a multiple of the filament count so the pattern
    // repeats cleanly without boundary artifacts (no double-C at cycle boundaries)
    int num_filaments = (int) indices.size();
    int cycle_length = num_filaments > 1 ? (max_cycle_length / num_filaments) * num_filaments : max_cycle_length;
    if (cycle_length < num_filaments)
        cycle_length = num_filaments;

    mc.layer_pattern = build_bresenham_pattern(indices, weights, cycle_length);

    if (num_filaments == 1)
    {
        // Pure-filament entry: report the filament's natural color, not the predict_stack
        // simulation. The user loaded the filament for its color, and that's what the swatch
        // should advertise -- any predict_stack shift (substrate bleed, TD-driven absorption
        // edge cases) would just make the Single Colors tier look wrong vs the spool.
        mc.predicted_color = filaments[indices[0]].color;
    }
    else
    {
        // Simulate enough layers to reach full opacity convergence. At thickness = TD, the
        // filament is ~98% opaque. We need at least max_td / (layer_height * cycle_length)
        // reps to cover that thickness.
        float max_td = 0;
        for (int idx : indices)
            max_td = std::max(max_td, filaments[idx].td);
        int reps = std::max(3, (int) std::ceil(max_td / (layer_height * (float) cycle_length)));

        std::vector<std::pair<int, float>> stack;
        stack.reserve(cycle_length * reps);
        for (int rep = 0; rep < reps; ++rep)
            for (int idx : mc.layer_pattern)
                stack.emplace_back(idx, layer_height);

        mc.predicted_color = ColorMixing::predict_stack(stack, filaments);
    }
    mc.target_color = mc.predicted_color;
    mc.delta_e = 0.0f;

    return mc;
}

void MixedColorPalette::auto_generate(const std::vector<FilamentOptics> &filaments, float layer_height,
                                      int steps_per_pair, int max_cycle_length)
{
    if (filaments.size() < 2)
        return;

    // Remove existing auto-generated entries
    m_colors.erase(std::remove_if(m_colors.begin(), m_colors.end(),
                                  [](const MixedColor &c) { return c.auto_generated; }),
                   m_colors.end());

    // Dedup pure filaments by (color, td) so identical loaded filaments collapse to one swatch.
    // Each unique filament keeps its first physical slot; layer_patterns store physical slot
    // indices so the slicer routes paint to that slot.
    std::vector<int> unique_indices;
    unique_indices.reserve(filaments.size());
    for (int i = 0; i < (int) filaments.size(); ++i)
    {
        bool dup = false;
        for (int u : unique_indices)
            if (filaments[i] == filaments[u])
            {
                dup = true;
                break;
            }
        if (!dup)
            unique_indices.push_back(i);
    }
    const int n = (int) unique_indices.size();

    // Detect special filaments by luminance among unique colors. black_idx / white_idx hold
    // physical slot indices so the loop comparisons (i == black_idx) work directly.
    int black_idx = -1; // Darkest filament - used as darkening agent
    int white_idx = -1; // Brightest filament - used as lightening agent
    {
        float darkest = 999.f;
        float brightest = -1.f;
        for (int phys : unique_indices)
        {
            float luminance = filaments[phys].color.r() * 0.299f + filaments[phys].color.g() * 0.587f +
                              filaments[phys].color.b() * 0.114f;
            if (luminance < darkest)
            {
                darkest = luminance;
                black_idx = phys;
            }
            if (luminance > brightest)
            {
                brightest = luminance;
                white_idx = phys;
            }
        }
        if (darkest > 0.15f)
            black_idx = -1;
        if (brightest < 0.85f)
            white_idx = -1;
        if (black_idx == white_idx)
            white_idx = -1;
    }

    auto fl = [](int idx)
    {
        return extruder_label(idx);
    };

    // --- Pure filament colors (one per unique color) ---
    for (int phys : unique_indices)
        m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length, {phys}, {1.0f}, fl(phys)));

    // Record the boundary between pure filament entries and blend entries. The gizmo uses this
    // for the "Single colors" section header.
    m_num_pure_filaments = (size_t) n;

    // --- ALL pairwise blends ---
    // Chromatic pairs (C+M, C+Y, M+Y) get full steps.
    // K+W (grays), C+K/M+K/Y+K (deep tones), C+W/M+W/Y+W (pastels) get fewer steps.
    int pw_steps = std::max(1, steps_per_pair);
    for (int ui = 0; ui < n; ++ui)
    {
        const int i = unique_indices[ui];
        for (int uj = ui + 1; uj < n; ++uj)
        {
            const int j = unique_indices[uj];
            bool involves_black = (i == black_idx || j == black_idx);
            bool involves_white = (i == white_idx || j == white_idx);
            bool is_chromatic = !involves_black && !involves_white;

            // Chromatic pairs: full steps. Others: fewer steps (tints/shades/grays).
            int steps = is_chromatic ? pw_steps : std::max(1, pw_steps / 2);

            for (int s = 1; s <= steps; ++s)
            {
                float ratio_b = (float) s / (float) (steps + 1);
                float ratio_a = 1.0f - ratio_b;

                std::string name = std::to_string((int) std::round(ratio_a * 100)) + "% " + fl(i) + " + " +
                                   std::to_string((int) std::round(ratio_b * 100)) + "% " + fl(j);

                m_colors.push_back(
                    make_mixed(m_next_id, filaments, layer_height, max_cycle_length, {i, j}, {ratio_a, ratio_b}, name));

                if ((int) m_colors.size() >= 512)
                    return;
            }
        }
    }

    // --- Darkened AND Lightened variants (interleaved for equal representation) ---
    // For each K/W level, generate ALL chromatic pair variants before moving to next level.
    // This ensures both dark and light colors get fair space in the palette.
    float kw_levels[] = {0.10f, 0.20f, 0.30f};

    for (float level : kw_levels)
    {
        // Darkened: chromatic pairs + K
        if (black_idx >= 0)
        {
            for (int ui = 0; ui < n; ++ui)
            {
                const int i = unique_indices[ui];
                if (i == black_idx || i == white_idx)
                    continue;
                for (int uj = ui + 1; uj < n; ++uj)
                {
                    const int j = unique_indices[uj];
                    if (j == black_idx || j == white_idx)
                        continue;
                    for (int s = 1; s <= pw_steps; ++s)
                    {
                        float base_b = (float) s / (float) (pw_steps + 1);
                        float w_i = (1.0f - level) * (1.0f - base_b);
                        float w_j = (1.0f - level) * base_b;
                        std::ostringstream ss;
                        ss << (int) std::round(w_i * 100) << "% " << fl(i) << " + " << (int) std::round(w_j * 100)
                           << "% " << fl(j) << " + " << (int) std::round(level * 100) << "% " << fl(black_idx);
                        m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length,
                                                      {i, j, black_idx}, {w_i, w_j, level}, ss.str()));
                        if ((int) m_colors.size() >= 512)
                            return;
                    }
                }
            }
        }

        // Lightened: chromatic pairs + W
        if (white_idx >= 0)
        {
            for (int ui = 0; ui < n; ++ui)
            {
                const int i = unique_indices[ui];
                if (i == black_idx || i == white_idx)
                    continue;
                for (int uj = ui + 1; uj < n; ++uj)
                {
                    const int j = unique_indices[uj];
                    if (j == black_idx || j == white_idx)
                        continue;
                    for (int s = 1; s <= pw_steps; ++s)
                    {
                        float base_b = (float) s / (float) (pw_steps + 1);
                        float w_i = (1.0f - level) * (1.0f - base_b);
                        float w_j = (1.0f - level) * base_b;
                        std::ostringstream ss;
                        ss << (int) std::round(w_i * 100) << "% " << fl(i) << " + " << (int) std::round(w_j * 100)
                           << "% " << fl(j) << " + " << (int) std::round(level * 100) << "% " << fl(white_idx);
                        m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length,
                                                      {i, j, white_idx}, {w_i, w_j, level}, ss.str()));
                        if ((int) m_colors.size() >= 512)
                            return;
                    }
                }
            }
        }
    }

    // --- Single-color darkened/lightened ---
    for (int ui = 0; ui < n; ++ui)
    {
        const int i = unique_indices[ui];
        if (i == black_idx || i == white_idx)
            continue;
        for (float level : kw_levels)
        {
            if (black_idx >= 0)
            {
                std::string name = std::to_string((int) std::round((1.0f - level) * 100)) + "% " + fl(i) + " + " +
                                   std::to_string((int) std::round(level * 100)) + "% " + fl(black_idx);
                m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length, {i, black_idx},
                                              {1.0f - level, level}, name));
                if ((int) m_colors.size() >= 512)
                    return;
            }
            if (white_idx >= 0)
            {
                std::string name = std::to_string((int) std::round((1.0f - level) * 100)) + "% " + fl(i) + " + " +
                                   std::to_string((int) std::round(level * 100)) + "% " + fl(white_idx);
                m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length, {i, white_idx},
                                              {1.0f - level, level}, name));
                if ((int) m_colors.size() >= 512)
                    return;
            }
        }
    }

    // --- Triple blends (3-filament combinations) ---
    if (n >= 3)
    {
        for (int ui = 0; ui < n; ++ui)
        {
            const int i = unique_indices[ui];
            for (int uj = ui + 1; uj < n; ++uj)
            {
                const int j = unique_indices[uj];
                for (int uk = uj + 1; uk < n; ++uk)
                {
                    const int k = unique_indices[uk];
                    // Skip triples that include black or white - those are handled
                    // above as darkened/lightened variants of the pairwise blends
                    if (i == black_idx || j == black_idx || k == black_idx)
                        continue;
                    if (i == white_idx || j == white_idx || k == white_idx)
                        continue;

                    struct W3
                    {
                        float a, b, c;
                    };
                    std::vector<W3> combos = {
                        {0.34f, 0.33f, 0.33f}, // equal
                        {0.60f, 0.20f, 0.20f}, // dominant A
                        {0.20f, 0.60f, 0.20f}, // dominant B
                        {0.20f, 0.20f, 0.60f}, // dominant C
                        {0.50f, 0.30f, 0.20f}, {0.30f, 0.50f, 0.20f}, {0.20f, 0.30f, 0.50f},
                        {0.50f, 0.20f, 0.30f}, {0.30f, 0.20f, 0.50f}, {0.20f, 0.50f, 0.30f},
                        {0.70f, 0.15f, 0.15f}, {0.15f, 0.70f, 0.15f}, {0.15f, 0.15f, 0.70f},
                    };

                    for (const auto &w : combos)
                    {
                        std::ostringstream ss;
                        ss << (int) std::round(w.a * 100) << "% " << fl(i) << " + " << (int) std::round(w.b * 100)
                           << "% " << fl(j) << " + " << (int) std::round(w.c * 100) << "% " << fl(k);

                        m_colors.push_back(make_mixed(m_next_id, filaments, layer_height, max_cycle_length, {i, j, k},
                                                      {w.a, w.b, w.c}, ss.str()));

                        if ((int) m_colors.size() >= 512)
                            return;
                    }
                }
            }
        }
    }

    // 4-way blends deliberately omitted: they trend grey, produce longer cycle patterns, and
    // are approximated within small delta-E by a 3-way on the dominant filaments. 3-way is the
    // hard cap at every simplification position.

    // Dedupe by layer_pattern. Bresenham rounding collapses adjacent ratios to identical
    // integer layer distributions (e.g. 85%/15% and 77%/23% both become 8:2 at cycle 10),
    // which produces identical predict_stack output and identical slicer behavior. Showing
    // both as separate swatches is visual noise that also inflates the recipe table. Pure
    // filaments are never duplicates (unique_indices already dedupes identical FilamentOptics),
    // so we only need to walk the blend range.
    std::set<std::vector<int>> seen_patterns;
    for (size_t i = 0; i < m_num_pure_filaments && i < m_colors.size(); ++i)
        seen_patterns.insert(m_colors[i].layer_pattern);
    m_colors.erase(std::remove_if(m_colors.begin() + m_num_pure_filaments, m_colors.end(),
                                  [&seen_patterns](const MixedColor &mc)
                                  { return !seen_patterns.insert(mc.layer_pattern).second; }),
                   m_colors.end());
}

int MixedColorPalette::add_custom(const ColorRGB &target, const std::string &name)
{
    MixedColor mc;
    mc.id = m_next_id++;
    mc.name = name.empty() ? ("Custom " + std::to_string(mc.id)) : name;
    mc.target_color = target;
    mc.predicted_color = target; // Will be updated by recompute()
    mc.enabled = true;
    mc.user_override = false;
    mc.auto_generated = false;

    m_colors.push_back(std::move(mc));
    return m_colors.back().id;
}

void MixedColorPalette::remove(int id)
{
    for (auto &c : m_colors)
    {
        if (c.id == id)
        {
            c.enabled = false;
            return;
        }
    }
}

void MixedColorPalette::clear()
{
    m_colors.clear();
    m_next_id = 1;
}

void MixedColorPalette::recompute(const std::vector<FilamentOptics> &filaments, float layer_height,
                                  int max_cycle_length)
{
    for (auto &mc : m_colors)
    {
        if (!mc.enabled)
            continue;

        if (!mc.user_override)
        {
            // Re-optimize the pattern for this target color
            mc.layer_pattern = ColorMixing::optimize_sequence(mc.target_color, filaments, max_cycle_length,
                                                              layer_height);
        }

        // Recompute prediction with enough layers to converge
        if (!mc.layer_pattern.empty())
        {
            float max_td = 0;
            for (int idx : mc.layer_pattern)
                max_td = std::max(max_td, filaments[idx].td);
            int cycle_len = (int) mc.layer_pattern.size();
            int reps = std::max(3, (int) std::ceil(max_td / (layer_height * (float) cycle_len)));

            std::vector<std::pair<int, float>> stack;
            stack.reserve(cycle_len * reps);
            for (int rep = 0; rep < reps; ++rep)
                for (int idx : mc.layer_pattern)
                    stack.emplace_back(idx, layer_height);

            mc.predicted_color = ColorMixing::predict_stack(stack, filaments);
        }

        mc.delta_e = ColorMixing::delta_e(mc.target_color, mc.predicted_color);
    }
}

int MixedColorPalette::max_physical_filament() const
{
    int max_idx = -1;
    for (const auto &c : m_colors)
    {
        if (!c.enabled)
            continue;
        for (int idx : c.layer_pattern)
            max_idx = std::max(max_idx, idx);
    }
    return max_idx;
}

// Weighted sRGB Euclidean distance in 0-255 scale (luminance weights R=0.30 G=0.59 B=0.11).
// Preferred over Lab metrics for filament matching because Lab compresses the blue-purple
// region, ranking blue closer to magenta than cyan, which mismatches what printed filament
// produces.
static float srgb_distance_weighted(const ColorRGB &a, const ColorRGB &b)
{
    const float dr = (a.r() - b.r()) * 255.f;
    const float dg = (a.g() - b.g()) * 255.f;
    const float db = (a.b() - b.b()) * 255.f;
    // Luminance weights: R=0.30, G=0.59, B=0.11. Squared values multiplied.
    return std::sqrt(0.30f * dr * dr + 0.59f * dg * dg + 0.11f * db * db);
}

int MixedColorPalette::find_best_match(uint32_t rgb) const
{
    if (m_colors.empty())
        return -1;

    ColorRGB target(float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f, float(rgb & 0xFF) / 255.f);

    int best_idx = -1;
    float best_de = std::numeric_limits<float>::infinity();

    for (int i = 0; i < (int) m_colors.size(); ++i)
    {
        const MixedColor &c = m_colors[i];
        if (!c.enabled)
            continue;
        float de = srgb_distance_weighted(target, c.predicted_color);
        if (de < best_de)
        {
            best_de = de;
            best_idx = i;
        }
    }

    return best_idx;
}

uint64_t MixedColorPalette::cache_key(const std::vector<FilamentOptics> &filaments, float layer_height)
{
    // FNV-1a 64-bit over a canonical byte stream of (color, td) pairs + layer_height.
    // Not cryptographically strong, just collision-resistant enough to key a runtime cache.
    auto mix = [](uint64_t h, uint64_t v)
    {
        h ^= v;
        h *= 0x100000001B3ULL;
        return h;
    };
    uint64_t h = 0xCBF29CE484222325ULL;

    for (const FilamentOptics &f : filaments)
    {
        uint32_t r = uint32_t(std::clamp(f.color.r(), 0.f, 1.f) * 65535.f);
        uint32_t g = uint32_t(std::clamp(f.color.g(), 0.f, 1.f) * 65535.f);
        uint32_t b = uint32_t(std::clamp(f.color.b(), 0.f, 1.f) * 65535.f);
        h = mix(h, uint64_t(r) | (uint64_t(g) << 16) | (uint64_t(b) << 32));
        uint32_t td_bits;
        float td = f.td;
        std::memcpy(&td_bits, &td, sizeof(td_bits));
        h = mix(h, uint64_t(td_bits));
    }
    uint32_t lh_bits;
    std::memcpy(&lh_bits, &layer_height, sizeof(lh_bits));
    h = mix(h, uint64_t(lh_bits));
    return h;
}

} // namespace Luminary
