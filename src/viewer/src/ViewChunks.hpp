///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PrefilterNeighbours.hpp" // PrefilterVertexView

namespace libvgcode
{

// Chunk size of the per-frame culling: XY cell (mm) and consecutive layers
static constexpr float VIEW_CHUNK_CELL_MM = 8.0f;
static constexpr uint32_t VIEW_CHUNK_LAYERS = 8;
// Sub-cells of a chunk: its XY cell split this many times per axis, and its layers in this many bands (bands of
// layers_per_chunk / 4 layers, at least one layer)
static constexpr uint32_t VIEW_CHUNK_SUBDIVISIONS = 4;

struct ViewChunk
{
    float min[3], max[3];        // world bounds of the chunk's segments (both ends, widened by half the widest bead)
    uint32_t first{0}, count{0}; // its segments in ViewChunkSet::order
    uint32_t first_subcell{0}, subcell_count{0}; // its sub-cells in ViewChunkSet::subcell_first
};

struct ViewChunkSet
{
    // The enabled segment indices sorted by chunk, then by sub-cell within a chunk, original order within a sub-cell
    std::vector<uint32_t> order;
    std::vector<ViewChunk> chunks;
    // The order position where sub-cell s starts, for every non-empty sub-cell in order, then order.size(): sub-cell s
    // is order[subcell_first[s], subcell_first[s + 1])
    std::vector<uint32_t> subcell_first;
};

// Sorts `enabled` (segment indices k, vertex k -> k+1) into chunks of cell_mm x cell_mm in XY by the segment's
// midpoint and layers_per_chunk consecutive layer ids (the end vertex's), building each chunk's box.
// Details: the grid starts at the smallest midpoint; the layers are the distinct layer ids of the enabled segments in
// ascending order, chunked by ordinal, so gaps in the ids leave no empty chunks. Chunks are ordered by layer chunk,
// then row, then column; empty ones are skipped. A grid of more cells than max(enabled segments, 65536) doubles its
// cell size until it fits. The box pad is the chunk's largest max(half width, height) of the end vertices on every
// axis. Segments with an index past the vertices or a non-finite midpoint share one last chunk, which is unbounded
// (+/- FLT_MAX), as is a chunk with any non-finite end, width or height.
// Sub-cells: within a chunk the segments are sorted (stably) by sub-cell: the XY sub-cell of cell / 4 (the cell
// actually used, after coarsening) by the midpoint, clamped into the chunk's cell, then the band of the layer ordinal
// within the chunk (bands per chunk: layers_per_chunk over the layers of a band, rounded up); key
// (band * 4 + sub-row) * 4 + sub-column ascending. The segments without a midpoint form one sub-cell.
ViewChunkSet build_view_chunks(const PrefilterVertexView &vertices, const std::vector<uint32_t> &enabled,
                               float cell_mm = VIEW_CHUNK_CELL_MM, uint32_t layers_per_chunk = VIEW_CHUNK_LAYERS);

struct ViewCullParams
{
    float view_proj[16]; // column-major clip = view_proj * world, for the 6 frustum planes
};

// Appends to `out` the segments of every chunk that is not outside the frustum (box vs the 6 planes, conservative).
// Returns the number of chunks kept. Deterministic; no allocation beyond `out`. The frustum planes are the clip-space
// -w <= x, y, z <= w planes (a projection to 0 <= z <= w only widens the near plane).
size_t select_view_chunks(const ViewChunkSet &set, const ViewCullParams &params, std::vector<uint32_t> &out);

// Per position of set.order, the index of the sub-cell holding it (from set.subcell_first); empty when the set has no
// sub-cells
std::vector<uint32_t> subcell_of_order(const ViewChunkSet &set);

} // namespace libvgcode
