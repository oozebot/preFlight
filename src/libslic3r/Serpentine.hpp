///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_Serpentine_hpp_
#define slic3r_Serpentine_hpp_

#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"
#include "Polyline.hpp"

namespace Slic3r::Serpentine
{

struct Params
{
    coord_t bead_width;
    coord_t overlap;        // allowed bead centerline overlap distance (scaled); negative spreads the beads apart
    coord_t max_bead;       // bead widening cap (scaled width)
    coord_t band_clamp{0};  // >0: band mode (perpendicular aim, corner miters, dual-face wall meeting)
    coord_t depth_clamp{0}; // >0: run the full pattern but stop every tooth at this depth; interior fills
                            // normally.
    ExPolygons fill_core;   // depth mode: the island offset inward by the depth (the region teeth must not enter).
                            // Teeth conform to this boundary so their tips fuse with the inner Athena bead,
                            // regardless of contour shape. Empty = fall back to the scalar depth_clamp.
    coord_t rib_pitch{0};   // >0: relaxed mode - sparse ribs at this center-to-center pitch along the
                            // contour (spokes on a wheel). The cascade, braking and anchor machinery run
                            // unchanged; stacking is forced aligned and holes print plain bead loops. Also
                            // sets the band pitch on solid-surface face layers; depth mode ignores it.
    bool outer_loop{false}; // full mode, any pitch: wrap one continuous perimeter around the island and move
                            // the rib-carrying wall a bonded spacing inside it; the tour snakes out through
                            // the widened seam rib, laps the object, and snakes back in (a single seam).
    bool prefer_clockwise{false}; // outer loop only: the lap prints clockwise when set, matching how
                                  // emission orients perimeter loops. Honored by building on the mirrored
                                  // island, so the tour keeps its order while every bead reverses.
    bool outer_first{false};      // outer loop only: print the lap first, then the interior (the woven tour
                                  // reverses; the mirror decision compensates so the lap direction still
                                  // follows prefer_clockwise).
    bool mirror_build{false};     // internal: this call is the mirrored build (set by the wrapper only)
    int phase_mode{1};            // 0 = aligned ridges, 1 = half-tooth stagger (brickwork), 2 = randomized (hash)
    int aim{0}; // 0 = Convergent (aim at anchor), 1 = Perpendicular (aim at inner boundary); depth mode only
    Flow flow;
    int layer_id;
    double print_z{0.0}; // layer z-height, for [<z>][SRP] debug line prefixes
};

// Generate a serpentine fill for a single island: one continuous extrusion
// replacing both perimeters and infill. Slits project inward from evenly
// spaced boundary mouths and brake against their neighbors at the configured
// overlap, producing a cascading depth pattern.
//
// Returns false if the island cannot host the pattern (caller falls back to
// normal perimeter generation).
bool generate(const ExPolygon &island, const Params &params, ExtrusionEntityCollection &out_loops,
              ExPolygons *out_interior = nullptr);

// A continuous closed band centred on the wall centreline, 3*bead_width - 2*overlap wide, clipped to
// the island (empty for a solid island). Depth mode unions it into Params::fill_core so the anchor bead
// the teeth bond to stays continuous on a thin or eccentric wall, where the per-boundary depth region
// pinches out and fragments it. One ring per hole: sample the hole boundary, project each sample to the
// nearest other boundary (the contour or another hole), take the midpoint as the wall centreline,
// radius-smooth it about the hole centroid, and offset by half the band width. Per-hole so a wall between
// two holes gets a band without bridging the solid between them. Best on thin or annular walls; on a
// strongly eccentric bore the thick side may fall to the per-boundary depth region instead. Out-counters
// report the sample count, loop count, rejected holes, and hole-to-hole projections skipped by the
// bounding-box prune.
ExPolygons wall_anchor_band(const ExPolygon &island, coord_t bead_width, coord_t overlap, size_t *out_samples = nullptr,
                            size_t *out_loops = nullptr, size_t *out_rejected = nullptr, size_t *out_pruned = nullptr);

} // namespace Slic3r::Serpentine

#endif // slic3r_Serpentine_hpp_
