///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_BaobabSupport_hpp_
#define slic3r_BaobabSupport_hpp_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r
{

class PrintObject;
class PrintObjectConfig;

namespace FFFTreeSupport
{
struct TreeSupportMeshGroupSettings;
}

// Per-layer union of the canopy rings the builder emitted, keyed by the layer's print z in
// microns. This is what separates a canopy from a trunk after the slices fuse: the fill may
// only grow where a canopy exists, so trunk bores stay empty.
using BaobabCanopyFootprints = std::unordered_map<int64_t, Polygons>;

// Baobab is the Organic tree engine parameterized and post-processed into a baobab:
// an interface-shaped canopy, an elongated trunk, a flared foot. It routes through
// FFFTreeSupport, so with every Baobab phase disabled it produces stock Organic output.
//
// Trigger: painted Baobab enforcers, or the Baobab support style for auto supports. On a
// multi-type object (baobab sharing the object with snug/grid/organic paint) the dispatcher
// runs one engine pass per painted type, Baobab last with every earlier support as
// collision, and pins the style to the pass being run - so this predicate answers "is the
// CURRENT pass baobab", which is what the shared tree and toolpath code needs. The tree
// pipeline treats the Baobab style as organic-like wherever it branches on style.
bool is_baobab_object(const PrintObject &print_object);

// The internal Baobab constants. The user-facing knobs live in the support_baobab_* config
// options and reach the engine through baobab_apply_tree_settings(); these two never
// graduate.
namespace Baobab
{
// Development override for the canopy's contraction slope, from vertical. Zero means "use the
// Maximum canopy angle setting" (`support_baobab_max_canopy_angle`); a positive value overrides
// it for a sweep. Capped by the bead rule below, never by this value alone.
inline constexpr double taper_angle_deg = 0.;

// Concentric wall loops around any section wide enough to carry them. Unconditional, unlike the
// stock organic double wall, which is skipped whenever an offset probe happens to split the
// island - and a wide concave canopy mouth splits it constantly.
inline constexpr int wall_count = 2;
} // namespace Baobab

// Effective taper override: PREFLIGHT_BAOBAB_TAPER_DEG sweeps the canopy slope without a
// rebuild; zero or unset defers to the Maximum canopy angle setting. Read once per process.
double baobab_taper_angle_deg();

// How far a printed outline may grow outward from the layer below, before the new bead
// hangs more than halfway off the one under it. This, not layer_height * tan(angle), is
// what actually limits a widening wall, and it does not vary with layer height.
coord_t baobab_max_growth_per_layer(coord_t support_line_width);

// The canopy's slope for this object, in radians from vertical: the requested taper, capped
// so that one layer of contraction never exceeds baobab_max_growth_per_layer(). At thin
// layers the requested angle binds; from about 0.25 mm upward the bead rule does.
double baobab_taper_angle_rad(const FFFTreeSupport::TreeSupportMeshGroupSettings &settings);

// Where seeded tips actually came from. The tree engine silently discards a sparse sampling
// when a region yields too few points and re-samples that region's OUTLINE at connect_length
// instead, which re-densifies exactly the forest the gather spacing exists to thin. Counting
// both sources is the only way to tell a gather spacing that did not apply from one that
// applied and was then overridden. Summed and reported once, so the report is deterministic.
struct BaobabSeedStats
{
    std::atomic<size_t> regions_roof{0};
    std::atomic<size_t> regions_regular{0};
    std::atomic<size_t> points_gathered{0};
    std::atomic<size_t> outline_fallbacks{0};
    std::atomic<size_t> points_from_fallback{0};
    // Regions whose outline had a point farther than the gather spacing from every gathered
    // seed, and the seeds added there. The canopy's rim envelope reaches one gather spacing
    // from its trunk, so an interface point beyond that distance from every seed is outside
    // every canopy's clip and CANNOT be covered - the rim must cover the whole interface
    // outline, so such a point gets a seed of its own on the outline.
    std::atomic<size_t> regions_supplemented{0};
    std::atomic<size_t> points_supplemental{0};
};

// Why a tip did not get a canopy. Every rejection falls back to the stock hemisphere tip, so a
// rejected canopy costs shape, never support: the worst case stays an Organic tree.
struct BaobabCanopyStats
{
    std::atomic<size_t> canopies_built{0};
    std::atomic<size_t> deepest_canopy_layers{0};
    std::atomic<size_t> reject_no_interface{0};         // no contact layer found above the tip
    std::atomic<size_t> reject_trunk_outside_region{0}; // never closes on its trunk within a sane depth
    std::atomic<size_t> regions_from_overhang{0};       // canopies clipped to the overhang (no contact layer exists)
    // A ring is disk ∩ region, so a ring hole can ONLY be a region hole.
    std::atomic<size_t> region_holes{0}; // holes in the contact region the canopy was clipped to
    std::atomic<size_t> ring_holes{0};   // holes in the emitted rings themselves
    std::atomic<size_t> rings_holed{0};  // rings emitted with at least one hole
    // Canopy depth range of the holed rings, 0 = the rim: whether holes sit at the top of the
    // canopy or only appear deep in it.
    std::atomic<size_t> ring_holed_d_min{std::numeric_limits<size_t>::max()};
    std::atomic<size_t> ring_holed_d_max{0};
    // What the collision clip does to the sliced branch outlines: layers where it removed any
    // area, layers where it created a hole, and the absolute layer ranges of both.
    std::atomic<size_t> clip_layers_cut{0};
    std::atomic<int32_t> clip_cut_layer_min{std::numeric_limits<int32_t>::max()};
    std::atomic<int32_t> clip_cut_layer_max{-1};
    std::atomic<size_t> clip_pre_holes{0};
    std::atomic<size_t> clip_post_holes{0};
    std::atomic<size_t> clip_layers_holed{0};
    std::atomic<int32_t> clip_holed_layer_min{std::numeric_limits<int32_t>::max()};
    std::atomic<int32_t> clip_holed_layer_max{-1};
    // Where the per-layer assembly makes holes: after the union+smooth of all branch slices
    // (union_holes), and in the final stored base polygons (final_holes), with the absolute
    // layer range of the latter.
    std::atomic<size_t> asm_union_holes{0};
    std::atomic<size_t> asm_final_holes{0};
    std::atomic<size_t> asm_layers_holed{0};
    std::atomic<int32_t> asm_holed_layer_min{std::numeric_limits<int32_t>::max()};
    std::atomic<int32_t> asm_holed_layer_max{-1};
    // Canopies that emitted rings but stopped before the reach met the trunk: the deepest ring
    // is then wider than the tube below it, and its overhang is unsupported.
    std::atomic<size_t> canopies_truncated{0};
    // Range of the per-seed sized initial reach, centi-mm. Each canopy's envelope starts at what
    // its seed must actually cover, not at one gather spacing for every seed.
    std::atomic<int64_t> reach_sized_min_cmm{std::numeric_limits<int64_t>::max()};
    std::atomic<int64_t> reach_sized_max_cmm{-1};
};

// Outcome of one tip's canopy build, for the per-tip debug line. built == 0 is a full
// rejection (the tip keeps its stock hemisphere); truncated means rings were emitted but
// the descent ended before the reach met the trunk.
struct BaobabCanopyOutcome
{
    size_t built = 0;
    bool truncated = false; // the envelope was still open where the canopy ended (partial mouth)
    const char *reason = "converged";
    size_t depth = 0;         // layers walked when the descent ended
    double reach_left = 0.;   // reach still open at the end, mm
    double reach0 = 0.;       // the sized initial reach this canopy was built with, mm
    double trunk_radius = 0.; // trunk radius where it ended, mm
    double travel = 0.;       // accumulated horizontal step of the trunk over the walk, mm
    double net = 0.;          // straight-line displacement over the walk, mm; travel >> net is
                              // merge jitter, travel ~ net is genuine lean
};

// WKT (mm) for debug dumps: paste into shapely/QGIS to inspect geometry.
inline std::string baobab_wkt(const ExPolygons &eps)
{
    auto pt = [](const Point &p)
    {
        return std::to_string(unscaled<double>(p.x())) + " " + std::to_string(unscaled<double>(p.y()));
    };
    auto ring = [&](const Polygon &poly)
    {
        std::string r = "(";
        for (size_t k = 0; k < poly.points.size(); ++k)
            r += (k ? "," : "") + pt(poly.points[k]);
        if (!poly.points.empty())
            r += "," + pt(poly.points.front()); // close the ring
        return r + ")";
    };
    std::string s = "MULTIPOLYGON(";
    for (size_t i = 0; i < eps.size(); ++i)
    {
        s += (i ? ",(" : "(") + ring(eps[i].contour);
        for (const Polygon &h : eps[i].holes)
            s += "," + ring(h);
        s += ")";
    }
    return s + ")";
}

// Holes in a Polygons soup are its clockwise contours.
inline size_t baobab_count_holes(const Polygons &polys)
{
    size_t n = 0;
    for (const Polygon &p : polys)
        if (p.is_clockwise())
            ++n;
    return n;
}

template<typename T>
inline void baobab_atomic_min(std::atomic<T> &a, T v)
{
    T cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
        ;
}

template<typename T>
inline void baobab_atomic_max(std::atomic<T> &a, T v)
{
    T cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
        ;
}

// A disk of `radius` about `center`, its vertex count set by a chord tolerance.
Polygon baobab_disk(const Point &center, coord_t radius);

// Overwrite the routing settings from the support_baobab_* options: angles, trunk diameter
// and its widening angle, trunk distance (the gather spacing), the derived tip diameter
// (two beads wide, capped at the trunk), and the plant-on-model permission - which replaces
// buildplate-only for baobab objects entirely. Must run at the end of the settings
// constructor so no Organic-derived value survives it.
void baobab_apply_tree_settings(FFFTreeSupport::TreeSupportMeshGroupSettings &settings,
                                const PrintObjectConfig &config);

} // namespace Slic3r

#endif // slic3r_BaobabSupport_hpp_
