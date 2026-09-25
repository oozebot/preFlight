///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "../include/ColorRange.hpp"

#include "Utils.hpp"

#include <algorithm>
#include <assert.h>
#include <cmath>

namespace libvgcode
{

const ColorRange ColorRange::DUMMY_COLOR_RANGE = ColorRange();

ColorRange::ColorRange(EColorRangeType type) : m_type(type), m_palette(DEFAULT_RANGES_COLORS) {}

EColorRangeType ColorRange::get_type() const
{
    return m_type;
}

const Palette &ColorRange::get_palette() const
{
    return m_palette;
}

void ColorRange::set_palette(const Palette &palette)
{
    if (palette.size() > 1)
        m_palette = palette;
}

// Band-based color lookup: find which band the value belongs to
// and return that band's solid palette color (no interpolation within bands).
Color ColorRange::get_color_at(float value) const
{
    if (m_bands.empty())
    {
        // Fallback if not finalized
        return m_palette[m_palette.size() / 2];
    }

    if (m_bands.size() == 1)
    {
        return m_palette[0];
    }

    // Find the nearest band by distance to its edge (linear scan, N <= 11).
    // Raw vertex values may not exactly match round_to_bin band boundaries,
    // so we pick the band whose range is closest rather than using <= threshold.
    size_t band_idx = 0;
    float best_dist = std::abs(value - m_bands[0].low);
    for (size_t i = 0; i < m_bands.size(); ++i)
    {
        float dist;
        if (value < m_bands[i].low)
            dist = m_bands[i].low - value;
        else if (value > m_bands[i].high)
            dist = value - m_bands[i].high;
        else
        {
            band_idx = i;
            break; // Value is inside this band, exact match
        }

        if (dist < best_dist)
        {
            best_dist = dist;
            band_idx = i;
        }
    }

    // Map band index to palette index: spread bands evenly across full palette
    const size_t palette_max = m_palette.size() - 1;
    const size_t num_bands = m_bands.size();
    const size_t palette_idx = (num_bands == 1) ? 0 : band_idx * palette_max / (num_bands - 1);

    return m_palette[palette_idx];
}

const std::array<float, 2> &ColorRange::get_range() const
{
    return m_range;
}

// Return one representative value per band (midpoint).
std::vector<float> ColorRange::get_values() const
{
    std::vector<float> ret;

    if (!m_finalized || m_bands.empty())
    {
        // Legacy fallback
        if (m_range[0] < FLT_MAX)
            ret.emplace_back(m_range[0]);
        return ret;
    }

    for (const auto &band : m_bands)
    {
        ret.emplace_back((band.low + band.high) * 0.5f);
    }

    return ret;
}

const std::vector<ColorBand> &ColorRange::get_bands() const
{
    return m_bands;
}

size_t ColorRange::size_in_bytes_cpu() const
{
    size_t ret = STDVEC_MEMSIZE(m_palette, Color);
    ret += STDVEC_MEMSIZE(m_bands, ColorBand);
    // Approximate map overhead
    ret += m_value_counts.size() * (sizeof(float) + sizeof(size_t) + 3 * sizeof(void *));
    return ret;
}

void ColorRange::update(float value)
{
    // Track min/max for backward compat (get_range())
    m_range[0] = std::min(m_range[0], value);
    m_range[1] = std::max(m_range[1], value);

    // Accumulate frequency data for band computation
    m_value_counts[value] += 1;
    m_finalized = false;
}

void ColorRange::reset()
{
    m_range = {FLT_MAX, -FLT_MAX};
    m_count = 0;
    m_value_counts.clear();
    m_bands.clear();
    m_finalized = false;
}

// Compute frequency-based bands from collected value data.
// Step 1: Merge nearby values (within ~2% tolerance) to prevent floating-point
//         drift from creating false distinctions (e.g., 0.620 vs 0.630).
// Step 2: If merged count <= 10: each merged group becomes a band.
//         Otherwise: frequency-weighted quantile grouping into 10 bands.
void ColorRange::finalize()
{
    m_bands.clear();

    if (m_value_counts.empty())
    {
        m_finalized = true;
        return;
    }

    // Step 1: Merge nearby values into groups.
    // This prevents round_to_bin artifacts like 0.62 and 0.63 appearing as separate bands.
    // The band span (high - low) is capped at 2% of the band's starting value.
    // This prevents cascading merges where 0.095, 0.10, 0.11, ..., 0.40 all chain together.
    struct MergedGroup
    {
        float low;
        float high;
        size_t count;
    };
    std::vector<MergedGroup> merged;

    for (const auto &[val, cnt] : m_value_counts)
    {
        if (!merged.empty())
        {
            MergedGroup &last = merged.back();
            // Max band span: 2% of starting value (or absolute 0.005 for very small values)
            const float max_span = std::max(last.low * 0.02f, 0.005f);
            if (val - last.low <= max_span)
            {
                last.high = val;
                last.count += cnt;
                continue;
            }
        }
        merged.push_back({val, val, cnt});
    }

    const size_t n_groups = merged.size();
    const size_t max_bands = 10; // Target 10 legend bands; merge down if data exceeds this
    m_count = n_groups;

    if (n_groups <= max_bands)
    {
        // Each merged group becomes its own band
        for (const auto &group : merged)
        {
            m_bands.push_back({group.low, group.high, group.count});
        }
    }
    else
    {
        // Frequency-weighted quantile grouping into max_bands bands.
        // Each band covers roughly equal total vertex count.
        size_t total_count = 0;
        for (const auto &group : merged)
        {
            total_count += group.count;
        }

        const size_t num_bands = max_bands;
        size_t accumulated = 0;
        size_t band_idx = 0;

        ColorBand current_band;
        current_band.low = merged[0].low;
        current_band.high = merged[0].high;
        current_band.count = 0;

        for (size_t i = 0; i < n_groups; ++i)
        {
            current_band.high = merged[i].high;
            current_band.count += merged[i].count;
            accumulated += merged[i].count;

            // Check if we should split here
            const size_t remaining_bands = num_bands - band_idx - 1;
            const size_t target = (band_idx + 1) * total_count / num_bands;
            const bool has_more = (i + 1 < n_groups);

            if (accumulated >= target && remaining_bands > 0 && has_more)
            {
                m_bands.push_back(current_band);
                band_idx++;

                // Start next band
                current_band.low = merged[i + 1].low;
                current_band.high = merged[i + 1].high;
                current_band.count = 0;
            }
        }

        // Push the last band
        if (current_band.count > 0)
        {
            m_bands.push_back(current_band);
        }
    }

    m_finalized = true;
}

// Create fixed evenly-spaced bands regardless of the actual data distribution.
// Used for fan speed (0-10%, 11-20%, ..., 91-100%) where consistent legend display matters.
void ColorRange::finalize_fixed_bands(int num_bands, float min, float max)
{
    m_bands.clear();

    const float band_width = (max - min) / static_cast<float>(num_bands);

    for (int i = 0; i < num_bands; ++i)
    {
        ColorBand band;
        band.low = (i == 0) ? min : min + i * band_width + 1.0f;
        band.high = min + (i + 1) * band_width;
        band.count = 0;

        // Count vertices that fall in this band
        for (const auto &[val, cnt] : m_value_counts)
        {
            if (val >= band.low && val <= band.high)
                band.count += cnt;
        }

        m_bands.push_back(band);
    }

    m_finalized = true;
}

} // namespace libvgcode
