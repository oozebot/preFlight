///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace libvgcode
{

// Bits of PrefilterNeighbourData::flags
static constexpr uint8_t PREFILTER_FLAG_WALL = 1;  // the vertex starts or ends a wall segment
static constexpr uint8_t PREFILTER_FLAG_FOUND = 2; // an adjacent wall segment found a bead above or below it

// Bits of compute_prefilter_open_sides' result: the sides of a wall segment that no other wall segment of its layer
// occupies
static constexpr uint8_t PREFILTER_OPEN_RIGHT = 1; // the right, (y, -x) of the segment's direction
static constexpr uint8_t PREFILTER_OPEN_LEFT = 2;  // the left, (-y, x)

// A toolpath vertex as the wall neighbour search reads it: z is the bead top, height and width the extrusion's
struct PrefilterVertex
{
    float x{0}, y{0}, z{0};
    float height{0}, width{0};
    uint32_t layer_id{0};
};

// The toolpath vertices read in place from the caller's own array, so the search needs no copy of them: vertex i's
// fields lie at base + i * stride plus each field's byte offset; the layer id is a uint32_t, the others floats
struct PrefilterVertexView
{
    const unsigned char *base{nullptr};
    size_t count{0};
    size_t stride{0};
    size_t x_offset{0}, y_offset{0}, z_offset{0};
    size_t height_offset{0}, width_offset{0};
    size_t layer_id_offset{0};

    float x(size_t i) const { return read<float>(i, x_offset); }
    float y(size_t i) const { return read<float>(i, y_offset); }
    float z(size_t i) const { return read<float>(i, z_offset); }
    float height(size_t i) const { return read<float>(i, height_offset); }
    float width(size_t i) const { return read<float>(i, width_offset); }
    uint32_t layer_id(size_t i) const { return read<uint32_t>(i, layer_id_offset); }

    template<class T>
    T read(size_t i, size_t offset) const
    {
        T value;
        std::memcpy(&value, base + i * stride + offset, sizeof(T));
        return value;
    }
};

// The view of an array of PrefilterVertex
PrefilterVertexView prefilter_vertex_view(const std::vector<PrefilterVertex> &vertices);

// Per vertex, indexed like the input vertices
struct PrefilterNeighbourData
{
    std::vector<float> offset_x; // mm, XY offset from the vertex to the bead above
    std::vector<float> offset_y;
    std::vector<uint8_t> flags; // PREFILTER_FLAG_WALL, PREFILTER_FLAG_FOUND
    size_t wall_vertices{0};
    size_t found_vertices{0};
    // Lines marked as wall segments that were left out for a non-finite or out-of-range attribute
    size_t excluded_segments{0};
    // Wall segments that took a bead that does not continue their surface, for want of one that does
    size_t unmatched_segments{0};
    // Threads that searched the layers, the calling thread included
    unsigned search_threads{0};
};

// For every vertex of a wall segment: the XY offset to the wall bead one layer above it that continues its surface.
// wall_segment[k] != 0: vertex k -> k+1 is a drawn line of a wall role; size == vertices.count (last 0).
// A segment's attributes are its end vertex's (k + 1): z (bead top), height, width, layer_id.
// Layer M is above layer L when M's bead bottom lies within a quarter of the smaller layer height of L's bead top
// (below: M's top near L's bottom), from each layer's highest wall segment (the smallest height among those at that
// top). A wall segment's candidates are the wall segments of a layer within R = clamp(1.5 w, 0.1, 5) mm of its
// midpoint, running within 30 degrees of it and no farther than w along it; a candidate's offset is the signed
// distance to its closest point along the segment's right (y, -x).
// Open sides (compute_prefilter_open_sides): a candidate of the segment's own layer occupies its right (offset above
// 0) or its left when it lies at least 0.25 w across (nearer, it is the bead itself) and no farther along than across
// (ahead lie the bead's own segments around a bend); a side not occupied is open. A candidate continues the segment's
// surface when neither has an open side, when the segment is open on both sides and the candidate on either, or when
// the segment is open on one side and the candidate has an open side whose outward normal has a positive dot product
// with that side's (a candidate may run either way).
// Each wall segment takes the offset of the nearest candidate above that continues its surface, else minus that of
// the nearest such candidate below, else the offset of the nearest candidate of any kind above, else minus that of the
// nearest below; the last two are counted in unmatched_segments, so a segment finds a bead exactly when it has any
// candidate. A vertex joins the offsets of its wall segments: the miter of two (the average when they turn by less
// than 20 degrees; at most twice the larger offset), the offset of one, 0 below 0.005 mm. A segment with a non-finite
// end attribute or an end farther than 1e6 mm from the origin is not a wall segment (counted in excluded_segments); a
// zero-length one is nobody's candidate, occupies no side and finds none. The open sides and the search run on
// `threads` threads (0: the hardware concurrency; a thread that fails to start leaves its layers to the others); the
// result does not depend on the thread count. `progress`, when set, is called on the calling thread between the
// layers it searches with the fraction of layers taken so far (never decreasing, 1 at the end of the search) while
// the other threads keep searching, so it must not change the vertices. Throws std::bad_alloc when memory runs out,
// and what `progress` throws.
PrefilterNeighbourData compute_prefilter_neighbours(const PrefilterVertexView &vertices,
                                                    const std::vector<uint8_t> &wall_segment, unsigned threads = 0,
                                                    const std::function<void(float)> &progress = {});

// The same over an array of PrefilterVertex
PrefilterNeighbourData compute_prefilter_neighbours(const std::vector<PrefilterVertex> &vertices,
                                                    const std::vector<uint8_t> &wall_segment, unsigned threads = 0,
                                                    const std::function<void(float)> &progress = {});

// The open sides of the wall segments as compute_prefilter_neighbours computes them: entry k holds
// PREFILTER_OPEN_RIGHT and PREFILTER_OPEN_LEFT of the wall segment k -> k+1 when the search keeps that segment, else
// 0; size == vertices.count. Computed on `threads` threads (0: the hardware concurrency); the result does not depend
// on the thread count. Throws std::bad_alloc when memory runs out.
std::vector<uint8_t> compute_prefilter_open_sides(const PrefilterVertexView &vertices,
                                                  const std::vector<uint8_t> &wall_segment, unsigned threads = 0);

// The same over an array of PrefilterVertex
std::vector<uint8_t> compute_prefilter_open_sides(const std::vector<PrefilterVertex> &vertices,
                                                  const std::vector<uint8_t> &wall_segment, unsigned threads = 0);

} // namespace libvgcode
