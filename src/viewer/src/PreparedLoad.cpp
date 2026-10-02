///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "PreparedLoadData.hpp"
#include "../include/GCodeInputData.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <string>
#include <thread>

namespace libvgcode
{

// The share of the preparation's progress each part ends at, from the phase times measured on large prints: the
// vertex scan, the neighbour search, the sealed bead pass, the buffers, the chunk structure of the whole print (the
// last two scaled to end at 1 without a view snapshot); with one, the view ranges and enabled lists, their chunks, the
// color ranges, and the per-vertex colors
static constexpr float PROGRESS_SCAN_END = 0.08f;
static constexpr float PROGRESS_SEARCH_END = 0.13f;
static constexpr float PROGRESS_SEALED_END = 0.70f;
static constexpr float PROGRESS_BUFFERS_END = 0.74f;
static constexpr float PROGRESS_PRINT_CHUNKS_END = 0.80f;
static constexpr float PROGRESS_LISTS_END = 0.84f;
static constexpr float PROGRESS_CHUNKS_END = 0.89f;
static constexpr float PROGRESS_RANGES_END = 0.96f;
// The vertex loops report their progress (and poll the cancel test) every 65536 vertices
static constexpr size_t VERTEX_REPORT_MASK = 0xFFFF;

PreparedLoad::PreparedLoad() : m_data(std::make_unique<PreparedLoadData>()) {}

PreparedLoad::~PreparedLoad() = default;

PreparedLoad::PreparedLoad(PreparedLoad &&other) noexcept = default;

PreparedLoad &PreparedLoad::operator=(PreparedLoad &&other) noexcept = default;

const std::vector<PathVertex> &PreparedLoad::get_vertices() const
{
    static const std::vector<PathVertex> no_vertices;
    if (m_data == nullptr)
        return no_vertices;
    return m_data->vertices;
}

void PreparedLoad::set_palettes(Palette tools_colors, Palette color_print_colors)
{
    if (m_data == nullptr)
        return;
    m_data->tools_colors = std::move(tools_colors);
    m_data->color_print_colors = std::move(color_print_colors);
}

float PreparedLoad::get_prepare_ms() const
{
    return m_data != nullptr ? m_data->prepare_ms : 0.0f;
}

std::unique_ptr<PreparedLoadData> PreparedLoad::take_data()
{
    return std::move(m_data);
}

bool is_line_valid(const std::vector<PathVertex> &vertices, size_t i)
{
    return i + 1 < vertices.size() && vertices[i + 1].position != vertices[i].position &&
           vertices[i + 1].type == vertices[i].type && vertices[i].type != EMoveType::Seam;
}

// The extrusion roles the toolpath prefilter shades: the perimeters
static bool is_wall_extrusion(const PathVertex &v)
{
    return v.type == EMoveType::Extrude &&
           (v.role == EGCodeExtrusionRole::Perimeter || v.role == EGCodeExtrusionRole::ExternalPerimeter ||
            v.role == EGCodeExtrusionRole::OverhangPerimeter || v.role == EGCodeExtrusionRole::InterlockingPerimeter);
}

PrefilterVertexView path_vertex_view(const std::vector<PathVertex> &vertices)
{
    const PathVertex &first = vertices.front();
    const unsigned char *base = reinterpret_cast<const unsigned char *>(&first);
    const auto offset_of = [base](const void *field)
    {
        return static_cast<size_t>(static_cast<const unsigned char *>(field) - base);
    };
    PrefilterVertexView view;
    view.base = base;
    view.count = vertices.size();
    view.stride = sizeof(PathVertex);
    view.x_offset = offset_of(&first.position[0]);
    view.y_offset = offset_of(&first.position[1]);
    view.z_offset = offset_of(&first.position[2]);
    view.height_offset = offset_of(&first.height);
    view.width_offset = offset_of(&first.width);
    view.layer_id_offset = offset_of(&first.layer_id);
    return view;
}

void extract_pos_and_or_hwa(const std::vector<PathVertex> &vertices, float travels_radius, float wipes_radius,
                            BitSet<> &valid_lines_bitset, std::vector<Vec4> *positions,
                            std::vector<Vec4> *heights_widths_angles, bool update_bitset,
                            const PrefilterNeighbourData *prefilter, const std::function<void(float)> &progress)
{
    static constexpr const Vec3 ZERO = {0.0f, 0.0f, 0.0f};
    if (positions == nullptr && heights_widths_angles == nullptr)
        return;
    if (vertices.empty())
        return;
    if (travels_radius <= 0.0f || wipes_radius <= 0.0f)
        return;

    // The prefilter's neighbour data is encoded only when it holds one entry per vertex; without it the w components
    // stay 0 and the prefilter leaves every bead unfiltered
    const bool has_prefilter = prefilter != nullptr && prefilter->offset_x.size() == vertices.size() &&
                               prefilter->offset_y.size() == vertices.size() &&
                               prefilter->flags.size() == vertices.size();

    if (positions != nullptr)
        positions->reserve(vertices.size());
    if (heights_widths_angles != nullptr)
        heights_widths_angles->reserve(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (progress && (i & VERTEX_REPORT_MASK) == VERTEX_REPORT_MASK)
            progress(static_cast<float>(i) / static_cast<float>(vertices.size()));

        const PathVertex &v = vertices[i];
        const EMoveType move_type = v.type;
        const bool prev_line_valid = i > 0 && valid_lines_bitset[i - 1];
        const Vec3 prev_line = prev_line_valid ? v.position - vertices[i - 1].position : ZERO;
        const bool this_line_valid = is_line_valid(vertices, i);
        const Vec3 this_line = this_line_valid ? vertices[i + 1].position - v.position : ZERO;

        if (this_line_valid)
        {
            // there is a valid path between point i and i+1.
        }
        else
        {
            // the connection is invalid, there should be no line rendered, ever
            if (update_bitset)
                valid_lines_bitset.reset(i);
        }

        if (positions != nullptr)
        {
            // The last component, which pads the texel to the GL_RGBA32F format, is the prefilter's x offset from the
            // vertex to the bead above
            Vec4 position = {v.position[0], v.position[1], v.position[2],
                             has_prefilter ? prefilter->offset_x[i] : 0.0f};
            if (move_type == EMoveType::Extrude)
                // push down extrusion vertices by half height to render them at the right z
                position[2] -= 0.5f * v.height;
            positions->emplace_back(position);
        }

        if (heights_widths_angles != nullptr)
        {
            float height = 0.0f;
            float width = 0.0f;
            if (v.is_travel())
            {
                height = travels_radius;
                width = travels_radius;
            }
            else if (v.is_wipe())
            {
                height = wipes_radius;
                width = wipes_radius;
            }
            else
            {
                height = v.height;
                width = v.width;
            }
            // The last component, which pads the texel to the GL_RGBA32F format, is the prefilter's 256 times the flags
            // plus the y offset from the vertex to the bead above; the offset is clamped to +-100 mm so the shader
            // decodes the flags exactly
            const float prefilter_w = has_prefilter ? 256.0f * static_cast<float>(prefilter->flags[i]) +
                                                          std::clamp(prefilter->offset_y[i], -100.0f, 100.0f)
                                                    : 0.0f;
            heights_widths_angles->push_back(
                {height, width,
                 std::atan2(prev_line[0] * this_line[1] - prev_line[1] * this_line[0], dot(prev_line, this_line)),
                 prefilter_w});
        }
    }
}

// The threads a load-time pass works on: one hardware thread is left to the rest of the application, a slicing job
// included
static unsigned pass_threads()
{
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware - 1 : 1;
}

// The prefilter's wall neighbour search over the vertices, timed, reporting its progress in [0, 1]. A failed search
// leaves no data (the prefilter then leaves every bead unfiltered) and the statistics say why; a cancel is passed on.
static void run_prefilter_neighbours(const std::vector<PathVertex> &vertices,
                                     const std::function<void(float)> &progress, PrefilterNeighbourData &data,
                                     PrefilterNeighbourStats &stats)
{
    data = PrefilterNeighbourData();
    stats = PrefilterNeighbourStats();
    if (vertices.empty())
        return;

    // The time covers the whole step, the wall segment marks included
    const auto start = std::chrono::steady_clock::now();
    try
    {
        // Which lines are wall segments. A drawn line's end vertex carries its move's attributes, so the line is a
        // wall segment when that vertex extrudes a wall role.
        const size_t count = vertices.size();
        std::vector<uint8_t> wall_segment(count, 0);
        for (size_t i = 0; i < count; ++i)
            if (is_line_valid(vertices, i) && is_wall_extrusion(vertices[i + 1]))
                wall_segment[i] = 1;

        // The search reads the vertices in place: the position (z is the bead top), height, width and layer
        data = compute_prefilter_neighbours(path_vertex_view(vertices), wall_segment, pass_threads(), progress);
    }
    catch (const PrepareCanceled &)
    {
        throw;
    }
    catch (const std::exception &e)
    {
        data = PrefilterNeighbourData();
        stats.error = std::string("neighbour search failed: ") + e.what();
    }
    catch (...)
    {
        data = PrefilterNeighbourData();
        stats.error = "neighbour search failed";
    }
    const auto end = std::chrono::steady_clock::now();
    stats.wall_vertices = data.wall_vertices;
    stats.found_vertices = data.found_vertices;
    stats.excluded_segments = data.excluded_segments;
    stats.unmatched_segments = data.unmatched_segments;
    stats.search_threads = data.search_threads;
    stats.search_ms = std::chrono::duration<float, std::milli>(end - start).count();
}

// The sealed bead pass over the vertices, timed, reporting its progress in [0, 1]. A failed pass leaves no data
// (culling is then off) and the statistics say why; a cancel is passed on.
static void run_sealed_beads(const std::vector<PathVertex> &vertices, const std::function<void(float)> &progress,
                             SealedBeadData &data, SealedBeadStats &stats)
{
    data = SealedBeadData();
    stats = SealedBeadStats();
    if (vertices.empty())
        return;

    // The time covers the whole step, the extrusion segment marks included
    const auto start = std::chrono::steady_clock::now();
    try
    {
        // The extrusion segments the enabled list can draw: a drawn line (both ends extrude) starting at an
        // extrusion vertex, the vertex the enabled list tests
        const size_t count = vertices.size();
        std::vector<uint8_t> extrusion_segment(count, 0);
        for (size_t i = 0; i < count; ++i)
            if (is_line_valid(vertices, i) && vertices[i].is_extrusion())
                extrusion_segment[i] = 1;

        // The pass reads the vertices in place
        data = compute_sealed_beads(path_vertex_view(vertices), extrusion_segment, pass_threads(), progress);
    }
    catch (const PrepareCanceled &)
    {
        throw;
    }
    catch (const std::exception &e)
    {
        data = SealedBeadData();
        stats.error = std::string("sealed bead pass failed: ") + e.what();
    }
    catch (...)
    {
        data = SealedBeadData();
        stats.error = "sealed bead pass failed";
    }
    const auto end = std::chrono::steady_clock::now();
    stats.segments = data.segments;
    stats.sealed_full_view = data.sealed_full_view;
    stats.threads = data.threads;
    stats.resolution_mm = data.resolution_mm;
    stats.ms = std::chrono::duration<float, std::milli>(end - start).count();
}

// The chunk structure of the whole print over the drawn lines, timed, reporting its progress in [0, 1]. A failed build
// leaves an invalid structure (the viewer then has none); a cancel is passed on.
static void run_print_chunks(const std::vector<PathVertex> &vertices, const BitSet<> &valid_lines, float travels_radius,
                             float wipes_radius, const std::function<void(float)> &progress, PrintChunks &chunks,
                             float &ms)
{
    const auto start = std::chrono::steady_clock::now();
    try
    {
        chunks = build_print_chunks(vertices, valid_lines, travels_radius, wipes_radius, progress);
    }
    catch (const PrepareCanceled &)
    {
        throw;
    }
    catch (...)
    {
        chunks = PrintChunks();
    }
    ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Drops what the view stage made, with the reason the load reports
static void clear_prepared_view(PreparedLoadData &data, const std::string &error)
{
    data.lists_prepared = false;
    data.colors_prepared = false;
    data.enabled_segments = std::vector<uint32_t>();
    data.enabled_options = std::vector<uint32_t>();
    data.chunks = ListChunks();
    data.ranges = ColorRanges();
    data.vertices_colors = std::vector<float>();
    data.view_error = error;
}

// The view stage of a preparation: what a load's view ranges, enabled lists (with their chunks) and colors come out as
// with the snapshot's settings, built by the functions the viewer builds them with. The snapshot's time mode is set to
// the load's fallback. `report(fraction)` polls the cancel test and reports the progress.
static void prepare_view(PreparedLoadData &data, ViewSettings &view, const std::function<void(float)> &report)
{
    const auto start = std::chrono::steady_clock::now();
    const std::vector<PathVertex> &vertices = data.vertices;
    const auto poll_at = [&report](float fraction)
    {
        return std::function<void()>([&report, fraction]() { report(fraction); });
    };

    // The load falls back to the normal time mode when the snapshot's has no time
    if (view.time_mode != ETimeMode::Normal && data.total_time[static_cast<size_t>(view.time_mode)] == 0.0f)
        view.time_mode = ETimeMode::Normal;

    Settings settings;
    settings.view_type = view.view_type;
    settings.time_mode = view.time_mode;
    settings.top_layer_only_view_range = view.top_layer_only_view_range;
    settings.spiral_vase_mode = data.spiral_vase_mode;
    settings.options_visibility = view.options_visibility;
    settings.extrusion_roles_visibility = view.extrusion_roles_visibility;

    // The view ranges of the full layer range, the visible range the enabled one, and the enabled lists
    const Interval &layers_range = data.layers.get_view_range();
    compute_view_full_range(vertices, layers_range, settings, data.view_range);
    data.view_range.set_visible(data.view_range.get_enabled());
    data.cull = sealed_culling_applies(view.sealed_bead_culling, view.clipping_plane, vertices.size(), data.sealed,
                                       data.extrusion_roles, settings);
    data.segments_total = compute_enabled_lists(vertices, data.view_range, layers_range, settings,
                                                data.valid_lines_bitset, data.sealed, data.cull, data.enabled_segments,
                                                data.enabled_options, poll_at(PROGRESS_PRINT_CHUNKS_END));
    report(PROGRESS_LISTS_END);

    // The chunks of the segment list when chunk culling applies to it
    if (view.chunk_culling && !data.enabled_segments.empty())
        data.chunks = build_list_chunks(vertices, data.enabled_segments);
    report(PROGRESS_CHUNKS_END);
    data.lists_prepared = true;

    // The colors, unless the color print view has no palette to color with
    if (!(view.view_type == EViewType::ColorPrint && view.color_print_colors.empty()))
    {
        for (size_t k = 0; k < COLOR_RANGE_VIEW_TYPES.size(); ++k)
            data.ranges.of(COLOR_RANGE_VIEW_TYPES[k])->set_palette(view.color_range_palettes[k]);
        data.ranges.build(vertices, data.layers, settings, poll_at(PROGRESS_CHUNKS_END));
        report(PROGRESS_RANGES_END);

        data.padded_tool_colors = view.tool_colors;
        pad_tool_colors(data.padded_tool_colors, data.used_extruders);
        const ColorInputs inputs{settings,    view.extrusion_roles_colors, view.options_colors,    data.ranges,
                                 data.layers, data.padded_tool_colors,     view.color_print_colors};
        data.vertices_colors.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            if ((i & VERTEX_REPORT_MASK) == VERTEX_REPORT_MASK)
                report(PROGRESS_RANGES_END +
                       (1.0f - PROGRESS_RANGES_END) * static_cast<float>(i) / static_cast<float>(vertices.size()));
            data.vertices_colors[i] = encode_color(vertex_color(vertices[i], inputs));
        }
        data.colors_prepared = true;
    }
    data.view_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

PreparedLoad prepare_load(GCodeInputData &&gcode_data, const PrepareSettings &settings,
                          const std::function<void(float)> &progress, const std::function<bool()> &canceled)
{
    const auto start = std::chrono::steady_clock::now();

    PreparedLoad prepared;
    PreparedLoadData &data = *prepared.data();
    data.settings = settings;
    data.vertices = std::move(gcode_data.vertices);
    data.tools_colors = std::move(gcode_data.tools_colors);
    data.color_print_colors = std::move(gcode_data.color_print_colors);
    data.spiral_vase_mode = gcode_data.spiral_vase_mode;

    // Polls the cancel test, which ends the preparation, then reports the fraction done
    const auto report = [&progress, &canceled](float fraction)
    {
        if (canceled && canceled())
            throw PrepareCanceled();
        if (progress)
            progress(fraction);
    };
    // The same for a part of the preparation, its own fraction mapped to [from, to]
    const auto part = [&report](float from, float to)
    {
        return std::function<void(float)>([&report, from, to](float fraction)
                                          { report(from + (to - from) * std::clamp(fraction, 0.0f, 1.0f)); });
    };

    report(0.0f);

    const std::vector<PathVertex> &vertices = data.vertices;
    const size_t count = vertices.size();
    if (count > 0)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if ((i & VERTEX_REPORT_MASK) == VERTEX_REPORT_MASK)
                report(PROGRESS_SCAN_END * static_cast<float>(i) / static_cast<float>(count));

            const PathVertex &v = vertices[i];

            data.layers.update(v, static_cast<uint32_t>(i));

            // Extent of the layer for the per-sample shading bound
            if (v.layer_id >= data.layers_extent.size())
                data.layers_extent.resize(size_t(v.layer_id) + 1);
            LayerExtent &extent = data.layers_extent[v.layer_id];
            extent.min_z = std::min(extent.min_z, v.position[2]);
            extent.max_z = std::max(extent.max_z, v.position[2]);
            if (v.type == EMoveType::Extrude && v.height > 0.0f && v.width > 0.0f)
            {
                extent.min_size = std::min(extent.min_size, std::min(v.height, v.width));
                extent.max_size = std::max(extent.max_size, std::max(v.height, v.width));
            }
            // Bead tops of the layer for the prefilter's displayed layers
            if (v.type == EMoveType::Extrude && v.role != EGCodeExtrusionRole::Custom)
            {
                extent.min_bead_z = std::min(extent.min_bead_z, v.position[2]);
                extent.max_bead_z = std::max(extent.max_bead_z, v.position[2]);
            }
            data.toolpaths_xy_range[0] = std::min(data.toolpaths_xy_range[0], v.position[0]);
            data.toolpaths_xy_range[1] = std::min(data.toolpaths_xy_range[1], v.position[1]);
            data.toolpaths_xy_range[2] = std::max(data.toolpaths_xy_range[2], v.position[0]);
            data.toolpaths_xy_range[3] = std::max(data.toolpaths_xy_range[3], v.position[1]);

            for (size_t j = 0; j < TIME_MODES_COUNT; ++j)
            {
                data.total_time[j] += v.times[j];
                if (v.type == EMoveType::Travel)
                    data.travels_time[j] += v.times[j];
            }

            const EOptionType option_type = move_type_to_option(v.type);
            if (option_type != EOptionType::COUNT)
                data.options.emplace_back(option_type);

            if (v.type == EMoveType::Extrude)
            {
                data.extrusion_roles.add(v.role, v.times);

                auto extruder_it = data.used_extruders.find(v.extruder_id);
                if (extruder_it == data.used_extruders.end())
                    extruder_it = data.used_extruders.insert({v.extruder_id, std::vector<ColorPrint>()}).first;
                if (extruder_it->second.empty() || extruder_it->second.back().color_id != v.color_id)
                {
                    const ColorPrint cp = {v.extruder_id, v.color_id, v.layer_id, data.total_time};
                    extruder_it->second.emplace_back(cp);
                }
            }
        }

        if (!data.layers.empty())
            data.layers.set_view_range(0, static_cast<uint32_t>(data.layers.count()) - 1);

        std::sort(data.options.begin(), data.options.end());
        data.options.erase(std::unique(data.options.begin(), data.options.end()), data.options.end());
        data.options.shrink_to_fit();

        // Every line is drawn until the buffers below find it is not
        data.valid_lines_bitset = BitSet<>(count);
        data.valid_lines_bitset.setAll();

        // Toolpath prefilter: every wall vertex's offset to the bead above, encoded into the buffers below
        run_prefilter_neighbours(vertices, part(PROGRESS_SCAN_END, PROGRESS_SEARCH_END), data.prefilter_neighbours,
                                 data.prefilter_stats);

        // Sealed bead culling: which extrusion beads can be seen from outside the print, read by every enabled list
        // rebuild
        run_sealed_beads(vertices, part(PROGRESS_SEARCH_END, PROGRESS_SEALED_END), data.sealed, data.sealed_stats);

        // The contents of the buffers to send to the GPU (both carry the toolpath prefilter's neighbour data)
        data.positions.reserve(count);
        data.heights_widths_angles.reserve(count);
        const bool has_view = data.settings.view.has_value();
        const float buffers_end = has_view ? PROGRESS_BUFFERS_END : PROGRESS_BUFFERS_END / PROGRESS_PRINT_CHUNKS_END;
        const float print_chunks_end = has_view ? PROGRESS_PRINT_CHUNKS_END : 1.0f;
        extract_pos_and_or_hwa(vertices, settings.travels_radius, settings.wipes_radius, data.valid_lines_bitset,
                               &data.positions, &data.heights_widths_angles, true, &data.prefilter_neighbours,
                               part(PROGRESS_SEALED_END, buffers_end));

        // The chunk structure of the whole print, over the lines the buffers left drawn, which the view settings
        // filter without sorting it again
        run_print_chunks(vertices, data.valid_lines_bitset, settings.travels_radius, settings.wipes_radius,
                         part(buffers_end, print_chunks_end), data.print_chunks, data.print_chunks_ms);

        // The buffers now hold the neighbour data; the per-vertex copy is kept only when asked for (the statistics
        // stay)
        if (!settings.keep_prefilter_neighbours)
            data.prefilter_neighbours = PrefilterNeighbourData();

        // With a view snapshot, the enabled lists and colors the load would build with it (they read the line bits
        // the buffers cleared). A failure there leaves them to the load, which builds them as it always did.
        if (data.settings.view.has_value())
        {
            try
            {
                prepare_view(data, *data.settings.view, report);
            }
            catch (const PrepareCanceled &)
            {
                throw;
            }
            catch (const std::exception &e)
            {
                clear_prepared_view(data, std::string("the view preparation failed: ") + e.what());
            }
            catch (...)
            {
                clear_prepared_view(data, "the view preparation failed");
            }
        }
    }

    report(1.0f);
    data.prepare_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return prepared;
}

AABox get_vertices_bounding_box(const std::vector<PathVertex> &vertices, const std::vector<EMoveType> &types)
{
    Vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const PathVertex &v : vertices)
    {
        if (std::find(types.begin(), types.end(), v.type) != types.end())
        {
            for (int j = 0; j < 3; ++j)
            {
                min[j] = std::min(min[j], v.position[j]);
                max[j] = std::max(max[j], v.position[j]);
            }
        }
    }
    return {min, max};
}

AABox get_vertices_extrusion_bounding_box(const std::vector<PathVertex> &vertices,
                                          const std::vector<EGCodeExtrusionRole> &roles)
{
    Vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const PathVertex &v : vertices)
    {
        if (v.is_extrusion() && std::find(roles.begin(), roles.end(), v.role) != roles.end())
        {
            for (int j = 0; j < 3; ++j)
            {
                min[j] = std::min(min[j], v.position[j]);
                max[j] = std::max(max[j], v.position[j]);
            }
        }
    }
    return {min, max};
}

} // namespace libvgcode
