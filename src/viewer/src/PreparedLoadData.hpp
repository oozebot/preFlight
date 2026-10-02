///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "../include/ColorPrint.hpp"
#include "../include/PathVertex.hpp"
#include "../include/PreparedLoad.hpp"
#include "../include/Viewer.hpp"
#include "Bitset.hpp"
#include "ExtrusionRoles.hpp"
#include "Layers.hpp"
#include "PrefilterNeighbours.hpp"
#include "PrintChunks.hpp"
#include "SealedBeads.hpp"
#include "ViewRange.hpp"
#include "ViewTables.hpp"

#include <array>
#include <cfloat>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace libvgcode
{

// One texel of the positions and heights/widths/angles texture buffers. Some graphic cards do not render texture
// buffers of the GL_RGB32F format, so both use GL_RGBA32F; the fourth component carries the toolpath prefilter's
// neighbour data.
using Vec4 = std::array<float, 4>;

//
// Per layer: the smallest and largest extrusion size (the lesser and the greater of height and width) and the z range
// of the vertex positions; with the xy range of all positions, the extent the per-sample shading bound works from.
// Also the z range of its extrusion bead tops (custom G-code excluded), which travels and lifts do not reach, for the
// prefilter's top and bottom displayed layers.
//
struct LayerExtent
{
    float min_size{FLT_MAX};
    float max_size{0.0f};
    float min_z{FLT_MAX};
    float max_z{-FLT_MAX};
    float min_bead_z{FLT_MAX};
    float max_bead_z{-FLT_MAX};
};

//
// What prepare_load() computes, in the form the viewer keeps it. Owned by a PreparedLoad until the viewer's install
// moves every member into its own.
//
struct PreparedLoadData
{
    std::vector<PathVertex> vertices;
    Palette tools_colors;
    Palette color_print_colors;
    bool spiral_vase_mode{false};
    // The vertex scan
    Layers layers;
    std::vector<LayerExtent> layers_extent;
    std::array<float, 4> toolpaths_xy_range{{FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX}};
    std::array<float, TIME_MODES_COUNT> total_time{0.0f, 0.0f};
    std::array<float, TIME_MODES_COUNT> travels_time{0.0f, 0.0f};
    std::vector<EOptionType> options;
    ExtrusionRoles extrusion_roles;
    std::map<uint8_t, std::vector<ColorPrint>> used_extruders;
    // Line i (vertex i to i + 1) is drawn when its bit is set
    BitSet<> valid_lines_bitset;
    // The load-time passes; the per-vertex neighbour data only when the settings keep it (the buffers hold it)
    PrefilterNeighbourData prefilter_neighbours;
    PrefilterNeighbourStats prefilter_stats;
    SealedBeadData sealed;
    SealedBeadStats sealed_stats;
    // The contents of the positions and heights/widths/angles buffers, one texel per vertex
    std::vector<Vec4> positions;
    std::vector<Vec4> heights_widths_angles;
    // The chunk structure of the whole print with the settings' radii (invalid when it could not be made), and its
    // build time in ms
    PrintChunks print_chunks;
    float print_chunks_ms{0.0f};
    // The settings the buffers were built with, and the wall time of the preparation in ms
    PrepareSettings settings;
    float prepare_ms{0.0f};

    // With a view snapshot (settings.view, its time mode after the load's fallback to Normal): the view ranges of
    // the full layer range, the enabled lists with their chunks, and, unless the snapshot's palettes cannot color the
    // view type, the color ranges (with the snapshot's palettes), the padded tool palette and the per-vertex colors
    bool lists_prepared{false};
    ViewRange view_range;
    bool cull{false};
    size_t segments_total{0};
    std::vector<uint32_t> enabled_segments;
    std::vector<uint32_t> enabled_options;
    ListChunks chunks;
    bool colors_prepared{false};
    ColorRanges ranges;
    Palette padded_tool_colors;
    std::vector<float> vertices_colors;
    // The wall time of that view stage in ms, and why it made nothing (empty when it ran)
    float view_ms{0.0f};
    std::string view_error;
};

// Whether the line from vertex i to vertex i + 1 is drawn: one move type at both ends, two distinct positions, not a
// seam
bool is_line_valid(const std::vector<PathVertex> &vertices, size_t i);

// The vertices seen in place by the load-time passes and the chunk builder: position (z is the bead top), height,
// width and layer. Requires at least one vertex.
PrefilterVertexView path_vertex_view(const std::vector<PathVertex> &vertices);

// Fills the positions and/or the heights/widths/angles buffers of the given vertices (travels and wipes get the given
// radii as height and width), clearing the bit of every line that is not drawn when `update_bitset` is set. The
// prefilter's neighbour data is encoded in the fourth components when it holds one entry per vertex. `progress`, when
// set, is called with the fraction done every 65536 vertices and may throw to stop.
void extract_pos_and_or_hwa(const std::vector<PathVertex> &vertices, float travels_radius, float wipes_radius,
                            BitSet<> &valid_lines_bitset, std::vector<Vec4> *positions = nullptr,
                            std::vector<Vec4> *heights_widths_angles = nullptr, bool update_bitset = false,
                            const PrefilterNeighbourData *prefilter = nullptr,
                            const std::function<void(float)> &progress = {});

} // namespace libvgcode
