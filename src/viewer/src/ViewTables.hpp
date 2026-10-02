///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "../include/ColorPrint.hpp"
#include "../include/ColorRange.hpp"
#include "../include/PathVertex.hpp"
#include "../include/PreparedLoad.hpp"
#include "Bitset.hpp"
#include "ExtrusionRoles.hpp"
#include "Layers.hpp"
#include "SealedBeads.hpp"
#include "Settings.hpp"
#include "ViewChunks.hpp"
#include "ViewRange.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

// The tables the viewer builds from its vertices and view settings: the view ranges, the enabled lists with their
// chunks, the color ranges and the per-vertex colors. Pure functions of their arguments, so a load's preparation builds
// them on any thread and the viewer's own updates build them the same way.
namespace libvgcode
{

// How much previous (non-active) layers are darkened during G-code scrubbing: 0 full brightness, 1 black
static constexpr float PREVIOUS_LAYER_DARKEN_FACTOR = 0.60f;

// The encoded color a vertex uploads (0xRRGGBB as a float), and the same darkened by the given factor
float encode_color(const Color &color);
float encode_color_darkened(const Color &color, float factor);

// The ten color ranges, one per view type of COLOR_RANGE_VIEW_TYPES. A friend of ColorRange: it builds them.
struct ColorRanges
{
    ColorRange height;
    ColorRange width;
    ColorRange speed;
    ColorRange actual_speed;
    ColorRange fan_speed;
    ColorRange temperature;
    ColorRange volumetric_rate;
    ColorRange actual_volumetric_rate;
    std::array<ColorRange, COLOR_RANGE_TYPES_COUNT> layer_time{ColorRange(EColorRangeType::Linear),
                                                               ColorRange(EColorRangeType::Logarithmic)};

    // The range a view type reads; null for a view type that reads none
    ColorRange *of(EViewType type);
    const ColorRange *of(EViewType type) const;
    // Resets every range (the palettes stay) and fills them from the vertices, the settings' visibilities and the
    // layer times of its time mode, then computes their bands. `poll`, when set, is called every 65536 vertices and may
    // throw to stop.
    void build(const std::vector<PathVertex> &vertices, const Layers &layers, const Settings &settings,
               const std::function<void()> &poll = {});
};

// What a vertex's color reads besides the vertex
struct ColorInputs
{
    const Settings &settings;
    const std::array<Color, GCODE_EXTRUSION_ROLES_COUNT> &extrusion_roles_colors;
    const std::array<Color, OPTION_TYPES_COUNT> &options_colors;
    const ColorRanges &ranges;
    const Layers &layers;
    const Palette &tool_colors;
    const Palette &color_print_colors;
};

// The color a vertex is drawn with for the inputs' view type
Color vertex_color(const PathVertex &v, const ColorInputs &inputs);

// Pads the tool palette to the highest used extruder id plus one with DUMMY_COLOR
void pad_tool_colors(Palette &tool_colors, const std::map<uint8_t, std::vector<ColorPrint>> &used_extruders);

// The full and enabled ranges for the displayed layers and the settings (see ViewRange); the visible range is clamped
// into the enabled one
void compute_view_full_range(const std::vector<PathVertex> &vertices, const Interval &layers_range,
                             const Settings &settings, ViewRange &view_range);

// A vertex's visibility key: bit r for an extrusion of role r, bit 32 + t for a vertex of option type t (travels and
// wipes included), 0 for a vertex no setting shows. A vertex is shown when its key meets visible_vertex_keys().
static_assert(GCODE_EXTRUSION_ROLES_COUNT <= 32 && OPTION_TYPES_COUNT <= 32, "every visibility key has a bit");
uint64_t vertex_visibility_key(const PathVertex &v);
uint64_t visible_vertex_keys(const Settings &settings);

// The vertices sought by layer and by option type, built once per load. The layer part (valid when the layer ids never
// decrease along the vertices and the largest one is below the vertex count): per layer id l, the first vertex whose
// layer id is l or more (first[l], up to the largest id plus one, whose entry is the vertex count) and the union of
// the visibility keys of the vertices of layer l (keys[l]). The option part (valid when every vertex index fits 32
// bits): per option type, the vertices of that type that are options (is_option()), ascending.
struct ViewIndex
{
    bool layers_valid{false};
    bool options_valid{false};
    size_t vertices{0};
    std::vector<uint32_t> first;
    std::vector<uint64_t> keys;
    std::array<std::vector<uint32_t>, OPTION_TYPES_COUNT> options;

    // The first vertex whose layer id is `layer` or more (the vertex count when none); layer part only
    size_t layer_start(uint64_t layer) const { return layer < first.size() ? size_t(first[layer]) : vertices; }
    size_t size_in_bytes() const;
};

// Builds the index on up to `threads` threads (0: the hardware concurrency); the result does not depend on the thread
// count. An allocation failure leaves both parts invalid.
ViewIndex build_view_index(const std::vector<PathVertex> &vertices, unsigned threads = 0);

// compute_view_full_range() with the scans over the vertices replaced by lookups in the index and scans within the
// layers at the range ends; the same result. Without a valid layer part for these vertices, the scans.
void compute_view_full_range(const std::vector<PathVertex> &vertices, const ViewIndex &index,
                             const Interval &layers_range, const Settings &settings, ViewRange &view_range);

// The option list compute_enabled_lists() builds, from the index's option part: the options of enabled_lists_range()
// whose type is visible, ascending. Requires a valid option part for these vertices.
void enabled_options_from_index(const ViewIndex &index, const std::vector<PathVertex> &vertices,
                                const ViewRange &view_range, const Interval &layers_range, const Settings &settings,
                                std::vector<uint32_t> &options);

// What an upload of the vertex colors holds: each vertex's color, darkened when its layer id is below top_layer and it
// is not vertex `keep` (top_layer 0 darkens none, and keep is NO_VERTEX then)
static constexpr size_t NO_VERTEX = std::numeric_limits<size_t>::max();
struct ColorDarkening
{
    size_t top_layer{0};
    size_t keep{NO_VERTEX};

    bool operator==(const ColorDarkening &other) const { return top_layer == other.top_layer && keep == other.keep; }
    bool operator!=(const ColorDarkening &other) const { return !(*this == other); }
};

// The darkening of the displayed range: the layers below the top displayed one with the top layer only range, unless
// every layer and every move of the top one are shown; in spiral vase mode the first enabled vertex keeps its color
ColorDarkening color_darkening(const Layers &layers, const ViewRange &view_range, const Settings &settings);

inline bool vertex_darkened(const ColorDarkening &darkening, const PathVertex &v, size_t id)
{
    return v.layer_id < darkening.top_layer && id != darkening.keep;
}

// The vertex ranges [first, last), ascending and disjoint, outside of which every vertex is darkened alike under `from`
// and `to` (none when they are equal). Requires a valid layer part of the index.
std::vector<std::array<size_t, 2>> darkening_change_spans(const ViewIndex &index, const ColorDarkening &from,
                                                          const ColorDarkening &to);

// Whether the enabled segment list leaves out sealed beads: enabled, the pass data valid, no clipping plane and every
// extrusion role of the toolpaths visible
bool sealed_culling_applies(bool enabled, bool clipping_plane, size_t vertices_count, const SealedBeadData &sealed,
                            const ExtrusionRoles &roles, const Settings &settings);

// Segment type masks: bit r for an extrusion of role r, and these two bits for travels and wipes
static constexpr uint32_t SEGMENT_TYPE_TRAVEL = uint32_t(1) << 30;
static constexpr uint32_t SEGMENT_TYPE_WIPE = uint32_t(1) << 31;
static_assert(GCODE_EXTRUSION_ROLES_COUNT <= 30, "every extrusion role has a bit below the travel and wipe bits");

// The type bit of the line from vertex v to the next: its role's bit for an extrusion, SEGMENT_TYPE_TRAVEL for a
// travel, SEGMENT_TYPE_WIPE for a wipe; 0 for an option, any other move and a role outside the role count, which are
// never drawn segments
inline uint32_t segment_type_bit(const PathVertex &v)
{
    switch (v.type)
    {
    case EMoveType::Extrude:
        return size_t(v.role) < GCODE_EXTRUSION_ROLES_COUNT ? uint32_t(1) << unsigned(v.role) : 0;
    case EMoveType::Travel:
        return SEGMENT_TYPE_TRAVEL;
    case EMoveType::Wipe:
        return SEGMENT_TYPE_WIPE;
    default:
        return 0;
    }
}

// The segment types the settings show: the bits of the visible extrusion roles, and the travel and wipe bits when
// those options are visible
uint32_t visible_segment_types(const Settings &settings);

// Whether the line from vertex v to the next is a segment of the enabled lists: a drawn line (its valid line bit) of a
// type in `visible_types`. The vertex ids the lists take it from are enabled_lists_range()'s.
inline bool segment_enabled(const PathVertex &v, bool valid_line, uint32_t visible_types)
{
    return valid_line && (segment_type_bit(v) & visible_types) != 0;
}

// The vertex ids [first, last) whose lines and options the enabled lists take: the visible range, from the full
// range's start with the top layer only range, one vertex past its end when that end is an option (the tool marker
// stands there), and one vertex before its start in spiral vase mode with a single layer shown other than the first.
// Requires at least one vertex.
Interval enabled_lists_range(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                             const Interval &layers_range, const Settings &settings);

// The enabled lists of the visible range (extended to the full range's start with the top layer only range): the
// drawn segments (without the sealed beads when `cull`) and the options. Returns the segments enabled before culling.
// `poll`, when set, is called every 65536 vertices and may throw to stop.
size_t compute_enabled_lists(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                             const Interval &layers_range, const Settings &settings, const BitSet<> &valid_lines,
                             const SealedBeadData &sealed, bool cull, std::vector<uint32_t> &segments,
                             std::vector<uint32_t> &options, const std::function<void()> &poll = {});

// The chunk set of an enabled segment list and how its build went: `built` when chunk culling applied to the list (a
// failed build leaves an empty, invalid set), the build time
struct ListChunks
{
    bool built{false};
    ViewChunkSet chunks;
    bool valid{false};
    float build_ms{0.0f};
};

// Builds the chunk set of an enabled segment list
ListChunks build_list_chunks(const std::vector<PathVertex> &vertices, const std::vector<uint32_t> &segments);

} // namespace libvgcode
