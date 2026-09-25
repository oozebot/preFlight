///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ColorDithering.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace Luminary
{

int resolve_layer_filament(const std::vector<int> &pattern, int layer_index, const DitherConfig &config)
{
    if (pattern.empty())
        return 0;
    if (pattern.size() == 1)
        return pattern[0];

    int cycle_len = (int) pattern.size();

    if (config.method == DitherMethod::Ordered || !config.phase_rotation)
    {
        // Simple modulo into the pattern
        int idx = ((layer_index % cycle_len) + cycle_len) % cycle_len;
        return pattern[idx];
    }

    // Bresenham with phase rotation: shift the pattern start by a coprime step each cycle
    int safe_index = ((layer_index % cycle_len) + cycle_len) % cycle_len; // handle negative indices
    int cycle_num = layer_index / cycle_len;
    int pos_in_cycle = safe_index;

    if (config.phase_rotation)
    {
        // Find a coprime step for the cycle length
        int step = 0;
        for (int candidate = cycle_len / 3 + 1; candidate < cycle_len; ++candidate)
        {
            if (std::gcd(candidate, cycle_len) == 1)
            {
                step = candidate;
                break;
            }
        }
        if (step == 0)
            step = 1;

        int offset = (cycle_num * step) % cycle_len;
        pos_in_cycle = (pos_in_cycle + offset) % cycle_len;
    }

    return pattern[pos_in_cycle];
}

std::vector<int> build_bresenham_pattern(const std::vector<int> &filament_indices, const std::vector<float> &weights,
                                         int cycle_length)
{
    assert(filament_indices.size() == weights.size());
    if (filament_indices.empty() || cycle_length <= 0)
        return {};

    if (filament_indices.size() == 1)
        return std::vector<int>(cycle_length, filament_indices[0]);

    // Compute target counts per filament
    std::vector<int> counts(filament_indices.size());
    float weight_sum = 0.0f;
    for (float w : weights)
        weight_sum += w;
    if (weight_sum < 1e-6f)
        weight_sum = 1.0f;

    int remaining = cycle_length;
    for (size_t i = 0; i < counts.size() - 1; ++i)
    {
        counts[i] = std::max(0, (int) std::round((weights[i] / weight_sum) * cycle_length));
        remaining -= counts[i];
    }
    counts.back() = std::max(0, remaining);

    // Bresenham multi-filament distribution:
    // For each position, pick the filament that's most "behind schedule"
    std::vector<int> result(cycle_length);
    std::vector<float> error(filament_indices.size(), 0.0f);

    for (int pos = 0; pos < cycle_length; ++pos)
    {
        // Find which filament is most behind
        int best = 0;
        float best_deficit = -1e10f;
        for (size_t f = 0; f < filament_indices.size(); ++f)
        {
            float target_so_far = (float) counts[f] * (float) (pos + 1) / (float) cycle_length;
            float deficit = target_so_far - error[f];
            if (deficit > best_deficit)
            {
                best_deficit = deficit;
                best = (int) f;
            }
        }

        result[pos] = filament_indices[best];
        error[best] += 1.0f;
    }

    return result;
}

std::vector<int> build_ordered_pattern(const std::vector<int> &filament_indices, const std::vector<float> &weights,
                                       int cycle_length)
{
    assert(filament_indices.size() == weights.size());
    if (filament_indices.empty() || cycle_length <= 0)
        return {};

    float weight_sum = 0.0f;
    for (float w : weights)
        weight_sum += w;
    if (weight_sum < 1e-6f)
        weight_sum = 1.0f;

    std::vector<int> result;
    result.reserve(cycle_length);

    int remaining = cycle_length;
    for (size_t i = 0; i < filament_indices.size(); ++i)
    {
        int count;
        if (i == filament_indices.size() - 1)
        {
            count = remaining;
        }
        else
        {
            count = std::max(0, (int) std::round((weights[i] / weight_sum) * cycle_length));
            count = std::min(count, remaining);
        }
        for (int j = 0; j < count; ++j)
            result.push_back(filament_indices[i]);
        remaining -= count;
    }

    // Pad if rounding left us short
    while ((int) result.size() < cycle_length)
        result.push_back(filament_indices.back());

    return result;
}

} // namespace Luminary
