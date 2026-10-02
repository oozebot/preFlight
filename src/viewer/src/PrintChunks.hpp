///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "../include/PathVertex.hpp"
#include "../include/Types.hpp"
#include "Bitset.hpp"
#include "ViewChunks.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace libvgcode
{

class ViewRange;
struct Settings;
struct SealedBeadData;

// The chunk structure of the whole print: every segment the Preview can draw, built once per load and filtered by
// the view settings, so a change of the layer range, the visible types or the moves range sorts nothing.
//
// The segments are every k (the line from vertex k to k + 1, k + 1 below the vertex count) whose valid line bit is
// set and whose vertex k is an extrusion, a travel or a wipe (segment_type_bit() not 0), sorted into chunks and
// sub-cells by build_view_chunks(): `set` holds the order, the chunks and subcell_first as that function builds
// them, chunk boxes included.
//
// Box rule, from the segments vertex shader: end a of segment k reads vertex k, end b vertex k + 1. An end's position
// is the position buffer's: an extrusion vertex half its height below its top (the bead centre, computed in float as
// the buffer fill does), any other vertex where it is.
// Its sizes are the heights/widths buffer's: the travel radius as height and width for a travel vertex, the wipe
// radius for a wipe vertex, the vertex's own height and width otherwise; hh and hw are half of them. Every vertex the
// shader places for an end is that end's position plus one of: +-hw * right, +-hh * up, 0 (each of the shader's sign
// pairs holds one non-zero sign at most), or for the joint and cap vertices (eff_id 2 and 7) hw * (s * dir + c *
// right) with s^2 + c^2 = 1 (+-hw * dir for a line with no turn). dir is the unit line from a to b ((1, 0, 0) under
// 1e-4 mm); right and up are unit and perpendicular to dir and to each other.
// - With |dir.z| <= 0.9 the shader takes right = normalize(cross(dir, Z)), which is horizontal, and up =
//   normalize(cross(right, dir)), whose vertical part is at most 1 and horizontal part |dir.z|. So the reach of an end
//   is max(hw, hh |dir.z|) on x and on y, and max(hh, hw |dir.z|) on z: hw and hh for a horizontal line.
// - Otherwise (a near-vertical line, or one whose direction the shader's float rounding could see either way: |dir.z|
//   above PRINT_CHUNK_LEVEL_DIR_Z, or a length under PRINT_CHUNK_SHORT_LINE_MM) the reach is max(hw, hh) on every
//   axis, which holds every offset above whatever the right and up directions are.
// A segment's bounds are, per end, its position plus and minus its reach; a sub-cell's box is the bounds of its
// segments, rounded outward to float. The triangles lie in the convex hull of their vertices, so in the box. Not
// covered: the clipping plane's cap vertices (vertex_id 8 to 15 with a plane set), which slide along the line onto the
// plane by up to 4 (h + w) of the interpolated sizes, and the shader's own float rounding of each vertex (about 1e-5
// mm on a 200 mm print). A sub-cell with a non-finite position, height or width read is unbounded (+/- FLT_MAX on every
// axis), as chunks are; a chunk's box is the union of its sub-cells' boxes.
// The largest |dir.z| and the shortest length that take the per-axis reach: the shader's 0.9 and 1e-4 mm limits with a
// margin over the float rounding of its direction
static constexpr float PRINT_CHUNK_LEVEL_DIR_Z = 0.899f;
static constexpr float PRINT_CHUNK_SHORT_LINE_MM = 2.0e-4f;

// What compute_enabled_lists() reads to decide a segment: segment k is enabled when first <= k < last (the ids of
// enabled_lists_range()) and segment_enabled(vertex k, its valid line bit, types) holds. valid_lines must be the bits
// the structure was built with; without it no segment is enabled. With `sealed` set, an extrusion segment k is enabled
// only when sealed_bead_drawn(*sealed, k, vertex k + 1's layer, sealed_first_layer, sealed_last_layer) holds as well:
// compute_enabled_lists() with cull true. print_chunk_flags() does not read it.
struct PrintChunkFilter
{
    size_t first{0};
    size_t last{0};
    uint32_t types{0};
    const BitSet<> *valid_lines{nullptr};
    const SealedBeadData *sealed{nullptr};
    uint32_t sealed_first_layer{0};
    uint32_t sealed_last_layer{0};
};

struct PrintChunks
{
    // False when the structure could not be made (too many vertices or segments, out of memory) or was not built
    bool valid{false};
    ViewChunkSet set;
    // Per sub-cell s, parallel to set.subcell_first without its closing entry:
    // - box: 6 floats from 6 s, min x, y, z then max x, y, z (the box rule above);
    // - types: the type bits of its segments (segment_type_bit());
    // - first_id, last_id: its smallest and largest segment index k;
    // - first_layer, last_layer: the smallest and largest layer id of both ends of its segments;
    // - chunk: the index in set.chunks of the chunk that holds it.
    std::vector<float> box;
    std::vector<uint32_t> types;
    std::vector<uint32_t> first_id;
    std::vector<uint32_t> last_id;
    std::vector<uint32_t> first_layer;
    std::vector<uint32_t> last_layer;
    std::vector<uint32_t> chunk;
    // Per chunk, 6 floats as box: the union of its sub-cells' boxes (set.chunks' own boxes keep their build rule)
    std::vector<float> chunk_box;
    // The travel and wipe radii the boxes were built with
    float travels_radius{0.0f};
    float wipes_radius{0.0f};
    // Under the filter last applied (apply_print_chunk_filter(); `applied` false before the first application and after
    // one failed): per sub-cell the count of its enabled segments, which its slot of set.order holds first in ascending
    // segment index, its other segments following in ascending index; and the sum of those counts
    bool applied{false};
    PrintChunkFilter applied_filter;
    std::vector<uint32_t> enabled;
    size_t enabled_total{0};
    // Per sub-cell, over its extrusion segments that do not touch outside air and reach a cavity (cavity_lo <= cavity_hi
    // in the sealed data): the smallest and largest first layer of those cavity spans, and of their last layer
    // (cavity_lo_min above cavity_lo_max for none). Made by an application with the sealed test that partitions every
    // sub-cell, from the sealed data `cavities_of` points to, which must stay alive and unchanged while it is set.
    const SealedBeadData *cavities_of{nullptr};
    std::vector<uint32_t> cavity_lo_min;
    std::vector<uint32_t> cavity_lo_max;
    std::vector<uint32_t> cavity_hi_min;
    std::vector<uint32_t> cavity_hi_max;

    size_t subcells() const { return set.subcell_first.empty() ? 0 : set.subcell_first.size() - 1; }
    size_t segments() const { return set.order.size(); }
    // The bytes the structure's arrays hold (their capacity)
    size_t bytes() const;
};

// Builds the structure of the vertices with the given valid line bits and radii. Returns an invalid structure for a
// vertex count above what a uint32_t segment index holds or a segment count build_view_chunks() refuses; an empty
// valid one when no segment qualifies. `progress`, when set, is called with the fraction done (0 to 1) every 65536
// segments of its own loops and may throw to stop; an allocation failure throws.
PrintChunks build_print_chunks(const std::vector<PathVertex> &vertices, const BitSet<> &valid_lines,
                               float travels_radius, float wipes_radius,
                               const std::function<void(float)> &progress = {});

// Rebuilds the boxes of the sub-cells that hold a travel or a wipe, and of their chunks, with the given radii. Does
// nothing for an invalid structure or radii equal to its own.
void set_print_chunk_radii(PrintChunks &chunks, const std::vector<PathVertex> &vertices, float travels_radius,
                           float wipes_radius);

// The filter of the viewer's view range, layer range, settings and valid line bits (no segment for no vertices)
PrintChunkFilter make_print_chunk_filter(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                                         const Interval &layers_range, const Settings &settings,
                                         const BitSet<> &valid_lines);

// The filter with the sealed bead test compute_enabled_lists() applies with cull true: the pass data, and the displayed
// layer range as its first and last layer
PrintChunkFilter with_sealed_beads(PrintChunkFilter filter, const SealedBeadData &sealed, const Interval &layers_range);

// One byte per sub-cell into `flags` (resized to the sub-cell count): 1 when the sub-cell can hold an enabled segment,
// its types intersecting the filter's and its [first_id, last_id] intersecting [first, last); 0 when it holds none.
// A 1 does not say every segment of it is enabled. Returns the count of 1s.
size_t print_chunk_flags(const PrintChunks &chunks, const PrintChunkFilter &filter, std::vector<uint8_t> &flags);

// Appends to `out`, in the order of set.order, the enabled segments of the sub-cells whose byte in visible_subcells
// is not 0 (a sub-cell past its size is not visible); returns their count. With every byte 1 it appends exactly the
// segments compute_enabled_lists() lists for the same settings (with cull true when the filter has the sealed test),
// in chunk order. Tests every segment; it reads no applied filter.
size_t emit_print_chunks(const PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                         const PrintChunkFilter &filter, const std::vector<uint8_t> &visible_subcells,
                         std::vector<uint32_t> &out);
// The count emit_print_chunks() returns, without writing
size_t count_print_chunks(const PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                          const PrintChunkFilter &filter, const std::vector<uint8_t> &visible_subcells);

// What apply_print_chunk_filter() did: the sub-cells it partitioned again, and whether it took them all
struct PrintChunkApplication
{
    size_t subcells{0};
    bool full{false};
};

// Applies a filter to the structure: every sub-cell's slot of set.order holds its enabled segments first and its other
// segments after them, each part in ascending segment index, and `enabled` their count; the result depends on the
// structure and the filter alone, not on the filters applied before. With `incremental` and a filter applied before
// that differs only in its range [first, last), its types and the sealed test's layers, only the sub-cells the
// difference can change are partitioned again: those whose [first_id, last_id] meets an id in one range and not the
// other, those whose types meet a type in one mask and not the other, and for moved sealed layers (with the cavity
// spans known for that sealed data) those with a layer or a cavity span end the move can change sealed_bead_drawn()
// for. Any other change (valid lines, the sealed test on, off or on other data) partitions every sub-cell.
// Works on up to `threads` threads, the calling thread included (0: the hardware concurrency); the result does not
// depend on the thread count. An allocation failure throws and leaves the structure not applied, its slots still
// permutations of their segments.
PrintChunkApplication apply_print_chunk_filter(PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                                               const PrintChunkFilter &filter, unsigned threads = 0,
                                               bool incremental = true);

// Appends to `out` the enabled segments of the sub-cells whose byte in visible_subcells is not 0, under the filter
// applied: each one's slot prefix, in sub-cell order; returns their count (0 for a structure not applied)
size_t emit_applied_print_chunks(const PrintChunks &chunks, const std::vector<uint8_t> &visible_subcells,
                                 std::vector<uint32_t> &out);

// The enabled segments under the filter applied of the sub-cells whose byte in visible_subcells is not 0 (a sub-cell
// past its size is not visible), chunk by chunk in the order chunk_order lists them (indices in set.chunks; one past
// their count is skipped), each chunk's sub-cells in sub-cell order, each one's slot prefix. Writes the first `limit`
// of them to `out` when it is not null, and returns how many that is (the count up to `limit` with no `out`). 0 for
// a structure not applied.
size_t write_applied_print_chunks(const PrintChunks &chunks, const std::vector<uint32_t> &chunk_order,
                                  const std::vector<uint8_t> &visible_subcells, size_t limit, uint32_t *out);

} // namespace libvgcode
