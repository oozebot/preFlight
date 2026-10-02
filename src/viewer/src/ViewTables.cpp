///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ViewTables.hpp"
#include "PreparedLoadData.hpp"

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <thread>
#include <type_traits>

namespace libvgcode
{

// The vertex loops call their poll every 65536 vertices
static constexpr size_t VIEW_POLL_MASK = 0xFFFF;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

// Rounding up.
// 1.5 is rounded to 2
// 1.49 is rounded to 1
// 0.5 is rounded to 1,
// 0.49 is rounded to 0
// -0.5 is rounded to 0,
// -0.51 is rounded to -1,
// -1.5 is rounded to -1.
// -1.51 is rounded to -2.
// If input is not a valid float (it is infinity NaN or if it does not fit)
// the float to int conversion produces a max int on Intel and +-max int on ARM.
template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(std::floor(a + 0.5));
}

// Round to a bin with minimum two digits resolution.
// Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
static float round_to_bin(const float value)
{
    //    assert(value >= 0);
    constexpr float const scale[5] = {100.f, 1000.f, 10000.f, 100000.f, 1000000.f};
    constexpr float const invscale[5] = {0.01f, 0.001f, 0.0001f, 0.00001f, 0.000001f};
    constexpr float const threshold[5] = {0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f};
    // Scaling factor, pointer to the tables above.
    int i = 0;
    // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
    for (; value < threshold[i] && i < 4; ++i)
        ;
    // At least on MSVC std::round() calls a complex function, which is pretty expensive.
    // our fast_round_up is much cheaper and it could be inlined.
    //    return std::round(value * scale[i]) * invscale[i];
    double a = value * scale[i];
    assert(std::abs(a) < double(std::numeric_limits<int64_t>::max()));
    return fast_round_up<int64_t>(a) * invscale[i];
}

float encode_color(const Color &color)
{
    const int r = static_cast<int>(color[0]);
    const int g = static_cast<int>(color[1]);
    const int b = static_cast<int>(color[2]);
    const int i_color = r << 16 | g << 8 | b;
    return static_cast<float>(i_color);
}

float encode_color_darkened(const Color &color, float factor)
{
    const float keep = 1.0f - factor;
    const int r = static_cast<int>(color[0] * keep);
    const int g = static_cast<int>(color[1] * keep);
    const int b = static_cast<int>(color[2] * keep);
    const int i_color = r << 16 | g << 8 | b;
    return static_cast<float>(i_color);
}

ColorRange *ColorRanges::of(EViewType type)
{
    return const_cast<ColorRange *>(static_cast<const ColorRanges *>(this)->of(type));
}

const ColorRange *ColorRanges::of(EViewType type) const
{
    switch (type)
    {
    case EViewType::Height:
        return &height;
    case EViewType::Width:
        return &width;
    case EViewType::Speed:
        return &speed;
    case EViewType::ActualSpeed:
        return &actual_speed;
    case EViewType::FanSpeed:
        return &fan_speed;
    case EViewType::Temperature:
        return &temperature;
    case EViewType::VolumetricFlowRate:
        return &volumetric_rate;
    case EViewType::ActualVolumetricFlowRate:
        return &actual_volumetric_rate;
    case EViewType::LayerTimeLinear:
        return &layer_time[0];
    case EViewType::LayerTimeLogarithmic:
        return &layer_time[1];
    default:
        return nullptr;
    }
}

void ColorRanges::build(const std::vector<PathVertex> &vertices, const Layers &layers, const Settings &settings,
                        const std::function<void()> &poll)
{
    width.reset();
    height.reset();
    speed.reset();
    actual_speed.reset();
    fan_speed.reset();
    temperature.reset();
    volumetric_rate.reset();
    actual_volumetric_rate.reset();
    layer_time[0].reset(); // ColorRange::EType::Linear
    layer_time[1].reset(); // ColorRange::EType::Logarithmic

    for (size_t i = 0; i < vertices.size(); i++)
    {
        if (poll && (i & VIEW_POLL_MASK) == VIEW_POLL_MASK)
            poll();
        const PathVertex &v = vertices[i];
        if (v.is_extrusion())
        {
            height.update(round_to_bin(v.height));
            if (!v.is_custom_gcode() || settings.extrusion_roles_visibility[size_t(EGCodeExtrusionRole::Custom)])
            {
                width.update(round_to_bin(v.width));
                volumetric_rate.update(round_to_bin(v.volumetric_rate()));
                actual_volumetric_rate.update(round_to_bin(v.actual_volumetric_rate()));
            }
            fan_speed.update(round_to_bin(v.fan_speed));
            temperature.update(round_to_bin(v.temperature));
        }
        if ((v.is_travel() && settings.options_visibility[size_t(EOptionType::Travels)]) ||
            (v.is_wipe() && settings.options_visibility[size_t(EOptionType::Wipes)]) || v.is_extrusion())
        {
            speed.update(round_to_bin(v.feedrate));
            actual_speed.update(round_to_bin(v.actual_feedrate));
        }
    }

    const std::vector<float> times = layers.get_times(settings.time_mode);
    // Bin layer times to integer seconds so sub-second variation
    // doesn't create false distinctions in the legend (e.g. 10.39s and
    // 10.61s both belong in the "10s" bucket).
    for (size_t i = 0; i < layer_time.size(); ++i)
    {
        for (float t : times)
        {
            layer_time[i].update(std::floor(t));
        }
    }

    // Frequency-based bands, but equal-width bands over the data for speed and fixed bands for fan speed
    height.finalize();
    width.finalize();
    speed.finalize_linear_bands(10);
    actual_speed.finalize_linear_bands(10);
    fan_speed.finalize_fixed_bands(10, 0.0f, 100.0f);
    temperature.finalize();
    volumetric_rate.finalize();
    actual_volumetric_rate.finalize();
    layer_time[0].finalize();
    layer_time[1].finalize();
}

Color vertex_color(const PathVertex &v, const ColorInputs &inputs)
{
    const Settings &settings = inputs.settings;
    const auto option_color = [&inputs](EOptionType type) -> const Color &
    {
        return inputs.options_colors[size_t(type)];
    };

    if (v.type == EMoveType::Noop)
        return DUMMY_COLOR;

    if ((v.is_wipe() && (settings.view_type != EViewType::Speed && settings.view_type != EViewType::ActualSpeed)) ||
        v.is_option())
        return option_color(move_type_to_option(v.type));

    const ColorRanges &ranges = inputs.ranges;
    switch (settings.view_type)
    {
    case EViewType::FeatureType:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : inputs.extrusion_roles_colors[size_t(v.role)];
    }
    case EViewType::Height:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : ranges.height.get_color_at(v.height);
    }
    case EViewType::Width:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : ranges.width.get_color_at(v.width);
    }
    case EViewType::Speed:
    {
        return ranges.speed.get_color_at(v.feedrate);
    }
    case EViewType::ActualSpeed:
    {
        return ranges.actual_speed.get_color_at(v.actual_feedrate);
    }
    case EViewType::FanSpeed:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : ranges.fan_speed.get_color_at(v.fan_speed);
    }
    case EViewType::Temperature:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : ranges.temperature.get_color_at(v.temperature);
    }
    case EViewType::VolumetricFlowRate:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : ranges.volumetric_rate.get_color_at(v.volumetric_rate());
    }
    case EViewType::ActualVolumetricFlowRate:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : ranges.actual_volumetric_rate.get_color_at(v.actual_volumetric_rate());
    }
    case EViewType::LayerTimeLinear:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : ranges.layer_time[0].get_color_at(
                                   inputs.layers.get_layer_time(settings.time_mode, static_cast<size_t>(v.layer_id)));
    }
    case EViewType::LayerTimeLogarithmic:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type))
                             : ranges.layer_time[1].get_color_at(
                                   inputs.layers.get_layer_time(settings.time_mode, static_cast<size_t>(v.layer_id)));
    }
    case EViewType::Tool:
    {
        assert(static_cast<size_t>(v.extruder_id) < inputs.tool_colors.size());
        return inputs.tool_colors[v.extruder_id];
    }
    case EViewType::ColorPrint:
    {
        return inputs.layers.layer_contains_colorprint_options(static_cast<size_t>(v.layer_id))
                   ? DUMMY_COLOR
                   : inputs.color_print_colors[static_cast<size_t>(v.color_id) % inputs.color_print_colors.size()];
    }
    default:
    {
        break;
    }
    }

    return DUMMY_COLOR;
}

void pad_tool_colors(Palette &tool_colors, const std::map<uint8_t, std::vector<ColorPrint>> &used_extruders)
{
    if (used_extruders.empty())
        return;
    // ensure that the number of defined tool colors matches the max id of the used extruders
    const size_t max_used_extruder_id = 1 + static_cast<size_t>(used_extruders.rbegin()->first);
    const size_t tool_colors_size = tool_colors.size();
    if (tool_colors.size() < max_used_extruder_id)
    {
        for (size_t i = 0; i < max_used_extruder_id - tool_colors_size; ++i)
        {
            tool_colors.emplace_back(DUMMY_COLOR);
        }
    }
}

static bool is_visible(const PathVertex &v, const Settings &settings)
{
    const EOptionType option_type = move_type_to_option(v.type);
    try
    {
        return (option_type == EOptionType::COUNT)
                   ? (v.type == EMoveType::Extrude) ? settings.extrusion_roles_visibility[size_t(v.role)] : false
                   : settings.options_visibility[size_t(option_type)];
    }
    catch (...)
    {
        return false;
    }
}

void compute_view_full_range(const std::vector<PathVertex> &vertices, const Interval &layers_range,
                             const Settings &settings, ViewRange &view_range)
{
    const bool travels_visible = settings.options_visibility[size_t(EOptionType::Travels)];
    const bool wipes_visible = settings.options_visibility[size_t(EOptionType::Wipes)];

    auto first_it = vertices.begin();
    while (first_it != vertices.end() && (first_it->layer_id < layers_range[0] || !is_visible(*first_it, settings)))
    {
        ++first_it;
    }

    // If the first vertex is an extrusion, add an extra step to properly detect the first segment
    if (first_it != vertices.begin() && first_it != vertices.end() && first_it->type == EMoveType::Extrude)
        --first_it;

    if (first_it == vertices.end())
        view_range.set_full(Range());
    else
    {
        if (travels_visible || wipes_visible)
        {
            // if the global range starts with a travel/wipe move, extend it to the travel/wipe start
            while (first_it != vertices.begin() &&
                   ((travels_visible && first_it->is_travel()) || (wipes_visible && first_it->is_wipe())))
            {
                --first_it;
            }
        }

        auto last_it = first_it;
        while (last_it != vertices.end() && last_it->layer_id <= layers_range[1])
        {
            ++last_it;
        }
        if (last_it != first_it)
            --last_it;

        // remove disabled trailing options, if any
        auto rev_first_it = std::make_reverse_iterator(first_it);
        if (rev_first_it != vertices.rbegin())
            --rev_first_it;
        auto rev_last_it = std::make_reverse_iterator(last_it);
        if (rev_last_it != vertices.rbegin())
            --rev_last_it;

        bool reduced = false;
        while (rev_last_it != rev_first_it && !is_visible(*rev_last_it, settings))
        {
            ++rev_last_it;
            reduced = true;
        }

        if (reduced && rev_last_it != vertices.rend())
            last_it = rev_last_it.base() - 1;

        if (travels_visible || wipes_visible)
        {
            // if the global range ends with a travel/wipe move, extend it to the travel/wipe end
            while (last_it != vertices.end() && last_it + 1 != vertices.end() &&
                   ((travels_visible && last_it->is_travel() && (last_it + 1)->is_travel()) ||
                    (wipes_visible && last_it->is_wipe() && (last_it + 1)->is_wipe())))
            {
                ++last_it;
            }
        }

        if (first_it != last_it)
            view_range.set_full(std::distance(vertices.begin(), first_it), std::distance(vertices.begin(), last_it));
        else
            view_range.set_full(Range());

        if (settings.top_layer_only_view_range)
        {
            const Interval &full_range = view_range.get_full();
            auto top_first_it = vertices.begin() + full_range[0];
            bool shortened = false;
            while (top_first_it != vertices.end() &&
                   (top_first_it->layer_id < layers_range[1] || !is_visible(*top_first_it, settings)))
            {
                ++top_first_it;
                shortened = true;
            }
            if (shortened)
                --top_first_it;

            // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
            if (settings.spiral_vase_mode && layers_range[0] > 0 && layers_range[0] == layers_range[1])
                --top_first_it;
            view_range.set_enabled(std::distance(vertices.begin(), top_first_it), full_range[1]);
        }
        else
            view_range.set_enabled(view_range.get_full());
    }
}

// The visibility key of each move type that is not an extrusion (an extrusion's is its role's bit)
static std::array<uint64_t, MOVE_TYPES_COUNT> move_type_keys()
{
    std::array<uint64_t, MOVE_TYPES_COUNT> keys{};
    for (size_t t = 0; t < MOVE_TYPES_COUNT; ++t)
    {
        const EOptionType option = move_type_to_option(static_cast<EMoveType>(t));
        keys[t] = option != EOptionType::COUNT ? uint64_t(1) << (32 + unsigned(option)) : 0;
    }
    return keys;
}

static uint64_t visibility_key(const PathVertex &v, const std::array<uint64_t, MOVE_TYPES_COUNT> &type_keys)
{
    if (v.type == EMoveType::Extrude)
        return size_t(v.role) < GCODE_EXTRUSION_ROLES_COUNT ? uint64_t(1) << unsigned(v.role) : 0;
    return size_t(v.type) < MOVE_TYPES_COUNT ? type_keys[size_t(v.type)] : 0;
}

uint64_t vertex_visibility_key(const PathVertex &v)
{
    return visibility_key(v, move_type_keys());
}

uint64_t visible_vertex_keys(const Settings &settings)
{
    uint64_t keys = 0;
    for (size_t role = 0; role < GCODE_EXTRUSION_ROLES_COUNT; ++role)
        if (settings.extrusion_roles_visibility[role])
            keys |= uint64_t(1) << role;
    for (size_t option = 0; option < OPTION_TYPES_COUNT; ++option)
        if (settings.options_visibility[option])
            keys |= uint64_t(1) << (32 + option);
    return keys;
}

size_t ViewIndex::size_in_bytes() const
{
    size_t bytes = first.capacity() * sizeof(uint32_t) + keys.capacity() * sizeof(uint64_t);
    for (const std::vector<uint32_t> &ids : options)
        bytes += ids.capacity() * sizeof(uint32_t);
    return bytes;
}

ViewIndex build_view_index(const std::vector<PathVertex> &vertices, unsigned threads)
{
    ViewIndex index;
    const size_t count = vertices.size();
    index.vertices = count;
    if (count == 0 || count >= size_t(std::numeric_limits<uint32_t>::max()))
        return index;
    try
    {
        // Per block of vertices: whether its layer ids never decrease, its runs of one layer id (the layer, its first
        // vertex in the block, the union of their keys) and its option vertices per type
        struct Run
        {
            uint32_t layer;
            uint32_t first;
            uint64_t keys;
        };
        struct Block
        {
            bool ascending{true};
            std::vector<Run> runs;
            std::array<std::vector<uint32_t>, OPTION_TYPES_COUNT> options;
        };
        constexpr size_t BLOCK = size_t(1) << 20;
        const size_t blocks = (count + BLOCK - 1) / BLOCK;
        std::vector<Block> parts(blocks);
        const std::array<uint64_t, MOVE_TYPES_COUNT> type_keys = move_type_keys();
        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        const auto worker = [&]()
        {
            for (size_t b = next.fetch_add(1); b < blocks && !failed.load(); b = next.fetch_add(1))
            {
                try
                {
                    Block &part = parts[b];
                    const size_t end = std::min(count, (b + 1) * BLOCK);
                    for (size_t i = b * BLOCK; i < end; ++i)
                    {
                        const PathVertex &v = vertices[i];
                        if (part.runs.empty() || part.runs.back().layer != v.layer_id)
                        {
                            if (!part.runs.empty() && v.layer_id < part.runs.back().layer)
                                part.ascending = false;
                            part.runs.push_back({v.layer_id, uint32_t(i), 0});
                        }
                        part.runs.back().keys |= visibility_key(v, type_keys);
                        if (v.is_option())
                            part.options[size_t(move_type_to_option(v.type))].push_back(uint32_t(i));
                    }
                }
                catch (...)
                {
                    failed = true;
                }
            }
        };
        if (threads == 0)
            threads = std::max(1u, std::thread::hardware_concurrency());
        const size_t helpers = blocks > 1 ? std::min<size_t>(threads, blocks) - 1 : 0;
        std::vector<std::thread> pool;
        try
        {
            pool.reserve(helpers);
            for (size_t t = 0; t < helpers; ++t)
                pool.emplace_back(worker);
        }
        catch (...)
        {
            // The threads started share the blocks with the calling thread
        }
        worker();
        for (std::thread &thread : pool)
            thread.join();
        if (failed.load())
            return index;

        // The option part: the blocks' lists in block order, ascending
        for (size_t t = 0; t < OPTION_TYPES_COUNT; ++t)
        {
            size_t total = 0;
            for (const Block &part : parts)
                total += part.options[t].size();
            index.options[t].reserve(total);
            for (Block &part : parts)
            {
                index.options[t].insert(index.options[t].end(), part.options[t].begin(), part.options[t].end());
                std::vector<uint32_t>().swap(part.options[t]);
            }
        }
        index.options_valid = true;

        // The layer part: every block ascending and each block's first layer not below the one before's last
        bool ascending = true;
        for (size_t b = 0; b < blocks && ascending; ++b)
            ascending = parts[b].ascending && !parts[b].runs.empty() &&
                        (b == 0 || parts[b - 1].runs.back().layer <= parts[b].runs.front().layer);
        const uint64_t max_layer = ascending ? uint64_t(parts.back().runs.back().layer) : 0;
        if (!ascending || max_layer >= count)
            return index;
        index.first.assign(size_t(max_layer) + 2, uint32_t(count));
        index.keys.assign(size_t(max_layer) + 1, 0);
        std::vector<uint8_t> present(size_t(max_layer) + 1, 0);
        for (const Block &part : parts)
            for (const Run &run : part.runs)
            {
                // A layer's first run is its first vertex: the ids never decrease
                if (present[run.layer] == 0)
                {
                    present[run.layer] = 1;
                    index.first[run.layer] = run.first;
                }
                index.keys[run.layer] |= run.keys;
            }
        // A layer id no vertex has starts where the next present one does
        for (size_t l = size_t(max_layer) + 1; l-- > 0;)
            if (present[l] == 0)
                index.first[l] = index.first[l + 1];
        index.layers_valid = true;
    }
    catch (...)
    {
        ViewIndex none;
        none.vertices = count;
        return none;
    }
    return index;
}

void compute_view_full_range(const std::vector<PathVertex> &vertices, const ViewIndex &index,
                             const Interval &layers_range, const Settings &settings, ViewRange &view_range)
{
    if (!index.layers_valid || index.vertices != vertices.size() || vertices.empty())
    {
        compute_view_full_range(vertices, layers_range, settings, view_range);
        return;
    }
    const size_t count = vertices.size();
    const std::array<uint64_t, MOVE_TYPES_COUNT> type_keys = move_type_keys();
    const uint64_t shown = visible_vertex_keys(settings);
    const auto visible = [&](size_t i)
    {
        return (visibility_key(vertices[i], type_keys) & shown) != 0;
    };
    // The first visible vertex at or after p (the count when none), a layer without a visible key skipped whole
    const auto next_visible = [&](size_t p)
    {
        while (p < count)
        {
            const uint32_t layer = vertices[p].layer_id;
            const size_t end = index.layer_start(uint64_t(layer) + 1);
            if ((index.keys[layer] & shown) != 0)
                for (; p < end; ++p)
                    if (visible(p))
                        return p;
            p = end;
        }
        return count;
    };
    // The last visible vertex above `low` and at most `high`; `low` when none
    const auto previous_visible = [&](size_t low, size_t high)
    {
        size_t p = high;
        while (p > low)
        {
            const uint32_t layer = vertices[p].layer_id;
            const size_t start = std::max(index.layer_start(layer), low + 1);
            if ((index.keys[layer] & shown) != 0)
            {
                for (; p >= start; --p)
                    if (visible(p))
                        return p;
            }
            else
                p = start - 1;
        }
        return low;
    };

    const bool travels_visible = settings.options_visibility[size_t(EOptionType::Travels)];
    const bool wipes_visible = settings.options_visibility[size_t(EOptionType::Wipes)];

    // The first visible vertex of the displayed layers or after them
    size_t first = next_visible(index.layer_start(layers_range[0]));

    // If the first vertex is an extrusion, add an extra step to properly detect the first segment
    if (first != 0 && first != count && vertices[first].type == EMoveType::Extrude)
        --first;

    if (first == count)
    {
        view_range.set_full(Range());
        return;
    }
    if (travels_visible || wipes_visible)
    {
        // if the global range starts with a travel/wipe move, extend it to the travel/wipe start
        while (first != 0 &&
               ((travels_visible && vertices[first].is_travel()) || (wipes_visible && vertices[first].is_wipe())))
            --first;
    }

    // The first vertex past the displayed layers from the first one on, then the one before it
    size_t last = std::max(first, index.layer_start(uint64_t(layers_range[1]) + 1));
    if (last != first)
        --last;

    // remove disabled trailing options, if any
    last = previous_visible(first, last);

    if (travels_visible || wipes_visible)
    {
        // if the global range ends with a travel/wipe move, extend it to the travel/wipe end
        while (last + 1 < count && ((travels_visible && vertices[last].is_travel() && vertices[last + 1].is_travel()) ||
                                    (wipes_visible && vertices[last].is_wipe() && vertices[last + 1].is_wipe())))
            ++last;
    }

    if (first != last)
        view_range.set_full(first, last);
    else
        view_range.set_full(Range());

    if (settings.top_layer_only_view_range)
    {
        const Interval &full_range = view_range.get_full();
        const size_t from = full_range[0];
        // The first visible vertex of the top displayed layer or after it, from the full range's start on
        size_t top_first = next_visible(std::max(from, index.layer_start(layers_range[1])));
        if (top_first != from)
            --top_first;

        // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
        if (settings.spiral_vase_mode && layers_range[0] > 0 && layers_range[0] == layers_range[1])
            --top_first;
        view_range.set_enabled(top_first, full_range[1]);
    }
    else
        view_range.set_enabled(view_range.get_full());
}

void enabled_options_from_index(const ViewIndex &index, const std::vector<PathVertex> &vertices,
                                const ViewRange &view_range, const Interval &layers_range, const Settings &settings,
                                std::vector<uint32_t> &options)
{
    options.clear();
    if (vertices.empty() || !index.options_valid)
        return;
    const Interval range = enabled_lists_range(vertices, view_range, layers_range, settings);
    if (range[0] >= range[1])
        return;
    // Each visible type's options in the range, merged in vertex order
    for (size_t t = 0; t < OPTION_TYPES_COUNT; ++t)
    {
        if (!settings.options_visibility[t] || index.options[t].empty())
            continue;
        const std::vector<uint32_t> &ids = index.options[t];
        const auto lo = std::lower_bound(ids.begin(), ids.end(), range[0],
                                         [](uint32_t id, size_t bound) { return size_t(id) < bound; });
        const auto hi = std::lower_bound(lo, ids.end(), range[1],
                                         [](uint32_t id, size_t bound) { return size_t(id) < bound; });
        if (lo == hi)
            continue;
        const size_t middle = options.size();
        options.insert(options.end(), lo, hi);
        if (middle > 0)
            std::inplace_merge(options.begin(), options.begin() + std::ptrdiff_t(middle), options.end());
    }
}

ColorDarkening color_darkening(const Layers &layers, const ViewRange &view_range, const Settings &settings)
{
    const size_t top_layer_id = settings.top_layer_only_view_range ? layers.get_view_range()[1] : 0;

    // Full render = all layers shown and all commands of the top layer visible.
    // In that case, show everything at full brightness. Otherwise, darken previous layers.
    const bool full_render = (layers.get_view_range()[0] == 0) && (layers.get_view_range()[1] >= layers.count() - 1) &&
                             (view_range.get_visible()[1] == view_range.get_full()[1]);

    ColorDarkening darkening;
    if (!full_render && top_layer_id > 0)
    {
        darkening.top_layer = top_layer_id;
        if (settings.spiral_vase_mode)
            darkening.keep = view_range.get_enabled()[0];
    }
    return darkening;
}

std::vector<std::array<size_t, 2>> darkening_change_spans(const ViewIndex &index, const ColorDarkening &from,
                                                          const ColorDarkening &to)
{
    std::vector<std::array<size_t, 2>> spans;
    if (from == to)
        return spans;
    // A vertex other than the two kept ones changes when its layer lies between the two top layers
    size_t low = 0;
    size_t high = 0;
    if (from.top_layer != to.top_layer)
    {
        low = index.layer_start(std::min(from.top_layer, to.top_layer));
        high = index.layer_start(std::max(from.top_layer, to.top_layer));
    }
    if (low < high)
        spans.push_back({low, high});
    // A kept vertex changes only when the kept vertex does
    if (from.keep != to.keep)
        for (const size_t keep : {from.keep, to.keep})
            if (keep < index.vertices && !(low <= keep && keep < high))
                spans.push_back({keep, keep + 1});
    std::sort(spans.begin(), spans.end());
    spans.erase(std::unique(spans.begin(), spans.end()), spans.end());
    return spans;
}

bool sealed_culling_applies(bool enabled, bool clipping_plane, size_t vertices_count, const SealedBeadData &sealed,
                            const ExtrusionRoles &roles, const Settings &settings)
{
    if (!enabled || clipping_plane)
        return false;
    if (vertices_count == 0 || sealed.touches_outside.size() != vertices_count ||
        sealed.cavity_lo.size() != vertices_count || sealed.cavity_hi.size() != vertices_count)
        return false;
    // A hidden role would expose the beads it sealed
    for (const EGCodeExtrusionRole role : roles.get_roles())
        if (!settings.extrusion_roles_visibility[size_t(role)])
            return false;
    return true;
}

uint32_t visible_segment_types(const Settings &settings)
{
    uint32_t types = 0;
    for (size_t role = 0; role < GCODE_EXTRUSION_ROLES_COUNT; ++role)
        if (settings.extrusion_roles_visibility[role])
            types |= uint32_t(1) << role;
    if (settings.options_visibility[size_t(EOptionType::Travels)])
        types |= SEGMENT_TYPE_TRAVEL;
    if (settings.options_visibility[size_t(EOptionType::Wipes)])
        types |= SEGMENT_TYPE_WIPE;
    return types;
}

Interval enabled_lists_range(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                             const Interval &layers_range, const Settings &settings)
{
    Interval range = view_range.get_visible();

    // when top layer only visualization is enabled, we need to render
    // all the toolpaths in the other layers as grayed, so extend the range
    // to contain them
    if (settings.top_layer_only_view_range)
        range[0] = view_range.get_full()[0];

    // to show the options at the current tool marker position we need to extend the range by one extra step
    if (vertices[range[1]].is_option() && range[1] < static_cast<uint32_t>(vertices.size()) - 1)
        ++range[1];

    if (settings.spiral_vase_mode)
    {
        // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
        if (layers_range[0] > 0 && layers_range[0] == layers_range[1])
            --range[0];
    }
    return range;
}

size_t compute_enabled_lists(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                             const Interval &layers_range, const Settings &settings, const BitSet<> &valid_lines,
                             const SealedBeadData &sealed, bool cull, std::vector<uint32_t> &segments,
                             std::vector<uint32_t> &options, const std::function<void()> &poll)
{
    segments.clear();
    options.clear();
    if (vertices.empty())
        return 0;

    const Interval range = enabled_lists_range(vertices, view_range, layers_range, settings);
    const uint32_t visible_types = visible_segment_types(settings);

    // Sealed bead culling leaves out the extrusion beads that cannot be seen from outside the print in the displayed
    // layer range. Enabled entry i is the line from vertex i to vertex i + 1, whose layer is the end vertex's.
    const uint32_t cull_first_layer = static_cast<uint32_t>(layers_range[0]);
    const uint32_t cull_last_layer = static_cast<uint32_t>(layers_range[1]);
    size_t segments_total = 0;

    for (size_t i = range[0]; i < range[1]; ++i)
    {
        if (poll && (i & VIEW_POLL_MASK) == VIEW_POLL_MASK)
            poll();
        const PathVertex &v = vertices[i];

        // An option is listed whether or not its line is drawn
        if (v.is_option())
        {
            if (settings.options_visibility[size_t(move_type_to_option(v.type))])
                options.push_back(static_cast<uint32_t>(i));
            continue;
        }
        if (!segment_enabled(v, valid_lines[i], visible_types))
            continue;

        ++segments_total;
        if (cull && v.is_extrusion() && i + 1 < vertices.size() &&
            !sealed_bead_drawn(sealed, i, vertices[i + 1].layer_id, cull_first_layer, cull_last_layer))
            continue;
        segments.push_back(static_cast<uint32_t>(i));
    }
    return segments_total;
}

ListChunks build_list_chunks(const std::vector<PathVertex> &vertices, const std::vector<uint32_t> &segments)
{
    ListChunks result;
    result.built = true;
    if (vertices.empty())
        return result;

    // A failed build leaves chunk culling off for this list: the full list is drawn
    const auto start = std::chrono::steady_clock::now();
    try
    {
        result.chunks = build_view_chunks(path_vertex_view(vertices), segments);
        result.valid = true;
    }
    catch (...)
    {
        result.chunks = ViewChunkSet();
        result.valid = false;
    }
    const auto end = std::chrono::steady_clock::now();
    result.build_ms = std::chrono::duration<float, std::milli>(end - start).count();
    return result;
}

std::string view_settings_difference(const ViewSettings &a, const ViewSettings &b, bool &lists_differ,
                                     bool &colors_differ)
{
    lists_differ = false;
    colors_differ = false;
    std::string names;
    const auto differs = [&names](const char *name)
    {
        if (!names.empty())
            names += ", ";
        names += name;
    };
    // Read by the enabled lists and the colors
    if (a.options_visibility != b.options_visibility)
    {
        differs("option visibility");
        lists_differ = colors_differ = true;
    }
    if (a.extrusion_roles_visibility != b.extrusion_roles_visibility)
    {
        differs("extrusion role visibility");
        lists_differ = colors_differ = true;
    }
    // Read by the enabled lists
    if (a.top_layer_only_view_range != b.top_layer_only_view_range)
    {
        differs("top layer only");
        lists_differ = true;
    }
    if (a.sealed_bead_culling != b.sealed_bead_culling)
    {
        differs("sealed bead culling");
        lists_differ = true;
    }
    if (a.chunk_culling != b.chunk_culling)
    {
        differs("chunk culling");
        lists_differ = true;
    }
    if (a.clipping_plane != b.clipping_plane)
    {
        differs("clipping plane");
        lists_differ = true;
    }
    // Read by the colors
    if (a.view_type != b.view_type)
    {
        differs("view type");
        colors_differ = true;
    }
    if (a.time_mode != b.time_mode)
    {
        differs("time mode");
        colors_differ = true;
    }
    if (a.extrusion_roles_colors != b.extrusion_roles_colors)
    {
        differs("extrusion role colors");
        colors_differ = true;
    }
    if (a.options_colors != b.options_colors)
    {
        differs("option colors");
        colors_differ = true;
    }
    if (a.color_range_palettes != b.color_range_palettes)
    {
        differs("range palettes");
        colors_differ = true;
    }
    if (a.tool_colors != b.tool_colors)
    {
        differs("tool palette");
        colors_differ = true;
    }
    if (a.color_print_colors != b.color_print_colors)
    {
        differs("color print palette");
        colors_differ = true;
    }
    return names;
}

} // namespace libvgcode
