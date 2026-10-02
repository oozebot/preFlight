///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "PrefilterNeighbours.hpp" // PrefilterVertexView (x, y, z, height, width, layer_id per vertex)

namespace libvgcode
{

// Per extrusion segment k (vertex k -> k+1), indexed like the vertices (size == vertex count, last entry unused)
struct SealedBeadData
{
    std::vector<uint8_t> touches_outside; // 1: some probe of the segment reached outside air (always drawn)
    std::vector<uint32_t> cavity_lo;      // layer span of the cavities its probes reached; lo > hi: none
    std::vector<uint32_t> cavity_hi;
    size_t segments{0};         // extrusion segments classified
    size_t sealed_full_view{0}; // of those, culled in the full layer range
    size_t ring2{0};            // of those, drawn because they touch a segment that touches outside air
    unsigned threads{0};        // threads that worked, the calling thread included
    float resolution_mm{0.0f};  // raster resolution used
};

// Raster resolution (mm): the classified segments' 10th percentile width over 4, clamped to
// [SEALED_BEADS_MIN_RESOLUTION, SEALED_BEADS_RESOLUTION]. The side probes step 1..3 px past a bead edge; a resolution
// coarser than about a third of the bead width merges the air between neighbouring beads, a finer one costs time with
// the square of it. The percentile keeps a few thin beads from setting it.
static constexpr float SEALED_BEADS_RESOLUTION = 0.1f;
static constexpr float SEALED_BEADS_MIN_RESOLUTION = 0.05f;
static constexpr float SEALED_BEADS_WIDTH_PERCENTILE = 0.1f;
static constexpr float SEALED_BEADS_WIDTH_DIVISOR = 4.0f;
// Largest raster side (pixels); a larger print coarsens the resolution to fit
static constexpr int SEALED_BEADS_MAX_RASTER = 8192;

// Which extrusion segments can be seen from outside the print. Each layer's extrusion segments are rasterized as
// capsules of radius max(half their width, half a pixel diagonal) plus half a pixel (so touching beads leave no seam),
// and the air left is labelled 4-connected. Air regions of adjacent layers are joined where both layers' air survives
// a 3 x 3 erosion (a pixel and its 8 neighbours air), so an opening narrower than about three pixels, such as a slit
// between beads, stays a region of its own layer that opens only when a range end reaches it, while wider openings
// and cavities connect through the layers. A region touching the raster
// border (the extrusions' XY bounds plus 2 mm and half the widest bead, the origin a further 0.37 pixel out so input
// on a round grid keeps probes off pixel edges) or lying in the first or the last layer is outside air, every other
// joined region a cavity with the layer span it covers. Layers are the distinct layer ids of the extrusion segments
// in ascending order; two consecutive ones are adjacent even when ids in between have no extrusion. A cavity's span
// reaches from one past the id of the layer below its lowest layer to one before the id of the layer above its
// highest, so a range end in an id gap opens it like the layer it exposes.
// A segment is probed at samples every max(0.25 mm, resolution) along it (at least one, at the midpoint): in its own
// layer at 0.5 w + k * resolution (k = 1..3) along its XY normal on both sides, in the next and the previous layer at
// -0.4 w, 0 and 0.4 w across it (no layer there counts as outside air). It touches outside air when a probe hits
// outside air (ring 1); otherwise its cavity span is the union of the spans of the cavities hit.
// Then one ring inward, from those results only, so nothing chains further: the preview's bead profile leaves grooves
// between beads through which the next bead inward shows, so a segment whose same probes land on a pixel of a ring 1
// segment (ring 1 rasterized alone the same way, in its own, the next or the previous layer; none beyond the first
// and the last layer) touches outside air too (counted in ring2). Every other segment adds to its cavity span the
// spans found at its probes' pixels, each pixel carrying the union of the spans of the segments whose footprint
// covers it, so a bead touching a bead that reaches an opened cavity is drawn.
// Memory per worker thread: two layers of solid and eroded air bits with their air runs plus a scratch bit layer, or
// three solid bit layers with their air runs, or three ring 1 bit layers with their span intervals; a bit layer is
// rows * ceil(cols / 64) * 8 bytes (0.5 MB for 2100 x 1950 pixels, 8 MB at the 8192 cap). Globally 12 bytes per air
// region until the third pass and about 21 bytes per classified segment.
// extrusion_segment[k] != 0: vertex k -> k+1 is a drawn extrusion (size == vertices.count, last 0). A segment's layer
// is its end vertex's (k + 1) layer_id; its width that vertex's width. A segment of zero length, with a non-finite or
// non-positive width, a non-finite end or an end farther than 1e6 mm from the origin is not classified and not
// rasterized (touches_outside = 1); so is every vertex that starts no extrusion segment. The layers
// are worked on `threads` threads (0: the hardware concurrency; a thread that fails to start leaves its layers to the
// others); the result does not depend on the thread count. `progress`, when set, is called on the calling thread with
// the fraction done (never decreasing, 1 at the end) while the other threads keep working. Throws std::bad_alloc when
// memory runs out (or the air regions outnumber 32-bit ids) and what `progress` throws.
SealedBeadData compute_sealed_beads(const PrefilterVertexView &vertices, const std::vector<uint8_t> &extrusion_segment,
                                    unsigned threads = 0, const std::function<void(float)> &progress = {});

// The same over an array of PrefilterVertex
SealedBeadData compute_sealed_beads(const std::vector<PrefilterVertex> &vertices,
                                    const std::vector<uint8_t> &extrusion_segment, unsigned threads = 0,
                                    const std::function<void(float)> &progress = {});

// Whether segment k is drawn for the displayed layer range [first_layer, last_layer] (layer ids as in the vertices):
// touches outside air, or its layer is within 2 of either end of the range (the exposed layer is ring 1, the one
// under it ring 2, and beads are taller than their layer), or a cavity it reaches spans either end.
bool sealed_bead_drawn(const SealedBeadData &data, size_t k, uint32_t layer, uint32_t first_layer, uint32_t last_layer);

} // namespace libvgcode
