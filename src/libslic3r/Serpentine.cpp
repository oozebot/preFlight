///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "Serpentine.hpp"

#include "ClipperUtils.hpp"
#include "DebugOutput.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExtrusionRole.hpp"
#include "Geometry/MedialAxis.hpp"
#include "MultiPoint.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// Debug reporting: enabled at runtime with --debug serpentine, captured by
// redirecting the console binary's stdout to a file. Buffered per island (see
// DbgReport) so layer-parallel reports stay atomic under TBB.

// One atomic multi-line report per island: layers slice concurrently, so
// per-line printing would interleave.
class DbgReport
{
public:
    explicit DbgReport(double z) : m_z(z) {}
    ~DbgReport() { flush(); }

    void line(const char *fmt, ...)
    {
        if (!Slic3r::debug_enabled(Slic3r::DBG_SERPENTINE))
            return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "[%.3f][SRP] ", m_z);
        m_text += prefix;
        m_text += buf;
        m_text += '\n';
    }

    void flush()
    {
        if (!Slic3r::debug_enabled(Slic3r::DBG_SERPENTINE) || m_text.empty())
            return;
        // Write the whole island block in one fwrite (atomic under TBB); the
        // background flusher (g_dbg_flusher) pushes stdout on its cadence.
        fwrite(m_text.data(), 1, m_text.size(), stdout);
        m_text.clear();
    }

private:
    double m_z;
    std::string m_text;
};

namespace Slic3r::Serpentine
{

static constexpr double PITCH_FACTOR = 2.0; // slit pitch as a multiple of bead width
static constexpr int COARSE_STEPS = 32;     // ray marching resolution
static constexpr int BISECT_ITERS = 24;     // refinement iterations after a crossing

static Point to_point(const Vec2d &p)
{
    return Point(coord_t(std::llround(p.x())), coord_t(std::llround(p.y())));
}

static Vec2d nearest_on_segment(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    Vec2d v = b - a;
    double vv = v.squaredNorm();
    if (vv <= 0.)
        return a;
    double t = std::clamp((p - a).dot(v) / vv, 0., 1.);
    return a + v * t;
}

static double dist_point_segment(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    return (p - nearest_on_segment(p, a, b)).norm();
}

// Largest t in [0, t_max] for which ok() held at every sampled point up to t;
// the first crossing is refined by bisection.
template<typename OkFn>
static double advance_while(double t_max, OkFn ok)
{
    if (t_max <= 0.)
        return 0.;
    if (!ok(0.))
        return 0.;
    double prev = 0.;
    for (int s = 1; s <= COARSE_STEPS; ++s)
    {
        double t = t_max * (double) s / (double) COARSE_STEPS;
        if (!ok(t))
        {
            double lo = prev, hi = t;
            for (int it = 0; it < BISECT_ITERS; ++it)
            {
                double mid = 0.5 * (lo + hi);
                if (ok(mid))
                    lo = mid;
                else
                    hi = mid;
            }
            return lo;
        }
        prev = t;
    }
    return t_max;
}

// Projection target: the pruned medial axis (spine) for elongated shapes, a
// point at the centroid for compact ones.
struct Anchor
{
    Polylines spine;
    Vec2d center{0., 0.};
    bool is_point{true};
    bool ring_mode{false}; // a hole swallows the centroid: the contour fan crosses the band, bores carry no bead
    double max_width{0.};  // largest inscribed diameter (max medial width): how thick the material gets anywhere

    double distance(const Vec2d &p) const
    {
        if (is_point)
            return (p - center).norm();
        double best = std::numeric_limits<double>::max();
        for (const Polyline &pl : spine)
            for (size_t i = 0; i + 1 < pl.points.size(); ++i)
                best = std::min(best,
                                dist_point_segment(p, pl.points[i].cast<double>(), pl.points[i + 1].cast<double>()));
        return best;
    }

    Vec2d nearest(const Vec2d &p) const
    {
        if (is_point)
            return center;
        Vec2d best_pt = center;
        double best = std::numeric_limits<double>::max();
        for (const Polyline &pl : spine)
            for (size_t i = 0; i + 1 < pl.points.size(); ++i)
            {
                Vec2d q = nearest_on_segment(p, pl.points[i].cast<double>(), pl.points[i + 1].cast<double>());
                double d = (p - q).norm();
                if (d < best)
                {
                    best = d;
                    best_pt = q;
                }
            }
        return best_pt;
    }
};

// Distance to the nearest boundary crossing along a ray.
static double raycast_depth(const ExPolygon &island, const Point &from, const Vec2d &dir, double min_dist)
{
    Point far_pt = from + (dir * 1e9).cast<coord_t>();
    Line ray(from, far_pt);

    Points hits;
    island.contour.intersections(ray, &hits);
    for (const Polygon &hole : island.holes)
        hole.intersections(ray, &hits);

    double best = std::numeric_limits<double>::max();
    for (const Point &h : hits)
    {
        double d = (h - from).cast<double>().norm();
        if (d > min_dist && d < best)
            best = d;
    }
    return (best < std::numeric_limits<double>::max()) ? best : 0.0;
}

// Like raycast_depth, but against an explicit region set (the depth-limit fill
// core). Returns the nearest crossing of any region boundary along the ray:
// where the tooth enters the core and should weld into the bead laid there.
static double raycast_to_regions(const ExPolygons &regions, const Point &from, const Vec2d &dir, double min_dist)
{
    Point far_pt = from + (dir * 1e9).cast<coord_t>();
    Line ray(from, far_pt);

    Points hits;
    for (const ExPolygon &ep : regions)
    {
        ep.contour.intersections(ray, &hits);
        for (const Polygon &hole : ep.holes)
            hole.intersections(ray, &hits);
    }

    double best = std::numeric_limits<double>::max();
    for (const Point &h : hits)
    {
        double d = (h - from).cast<double>().norm();
        if (d > min_dist && d < best)
            best = d;
    }
    return (best < std::numeric_limits<double>::max()) ? best : 0.0;
}

static Anchor build_anchor(const ExPolygon &island, double bead_width, bool allow_ring_mode, DbgReport &dbg)
{
    Anchor anchor;
    anchor.center = island.contour.centroid().cast<double>();

    // Denoise first: micro-wiggles in high-vertex slices spawn spurious
    // medial axis chains.
    ExPolygon coarse;
    douglas_peucker(island.contour.points.begin(), island.contour.points.end(),
                    std::back_inserter(coarse.contour.points), bead_width * 0.25);
    if (coarse.contour.points.size() < 3)
        coarse.contour = island.contour;
    for (const Polygon &h : island.holes)
    {
        Polygon hc;
        douglas_peucker(h.points.begin(), h.points.end(), std::back_inserter(hc.points), bead_width * 0.25);
        coarse.holes.push_back(hc.points.size() >= 3 ? std::move(hc) : h);
    }

    ThickPolylines tp;
    Geometry::MedialAxis ma(0., 1e18, coarse);
    ma.build(&tp);

    // A genuine spine branch is longer than the local shape is thick; compact
    // regions self-reject and collapse the anchor to a point.
    const double min_len = 4.0 * bead_width;
    double total = 0.;
    double max_w = 0.;
    for (const ThickPolyline &t : tp)
        for (double w : t.width)
            max_w = std::max(max_w, w);
    anchor.max_width = max_w;
    for (const ThickPolyline &t : tp)
    {
        if (t.points.size() < 2)
            continue;
        double len = t.length();
        if (len < min_len)
            continue;
        double mean_w = 0.;
        if (!t.width.empty())
        {
            for (double w : t.width)
                mean_w += w;
            mean_w /= (double) t.width.size();
        }
        if (len < 1.2 * mean_w)
            continue;
        Polyline pl;
        douglas_peucker(t.points.begin(), t.points.end(), std::back_inserter(pl.points), bead_width * 0.1);
        if (pl.points.size() >= 2)
        {
            total += pl.length();
            anchor.spine.push_back(std::move(pl));
        }
    }
    auto joint_inside = [&](const Point &a, const Point &b) -> bool
    {
        for (double t : {0.25, 0.5, 0.75})
        {
            Vec2d m = a.cast<double>() * (1.0 - t) + b.cast<double>() * t;
            if (!island.contains(to_point(m)))
                return false;
        }
        return true;
    };
    // T-bridge fragment ends to the nearest point of another fragment so the
    // anchor field has no hole at junctions; mid-branch hits represent Y
    // junctions, which endpoint merging cannot.
    const size_t n_frag = anchor.spine.size();
    const double bridge_tol = std::max(0.5 * max_w, 8.0 * bead_width);
    std::vector<std::array<bool, 2>> end_bridged(n_frag, {false, false});
    Polylines bridges;
    for (size_t i = 0; i < n_frag; ++i)
        for (int e = 0; e < 2; ++e)
        {
            const Point &ep = e ? anchor.spine[i].last_point() : anchor.spine[i].first_point();
            double best_d = bridge_tol;
            Vec2d best_q(0., 0.);
            for (size_t j = 0; j < n_frag; ++j)
            {
                if (j == i)
                    continue;
                const Polyline &pj = anchor.spine[j];
                for (size_t sg = 0; sg + 1 < pj.points.size(); ++sg)
                {
                    Vec2d q = nearest_on_segment(ep.cast<double>(), pj.points[sg].cast<double>(),
                                                 pj.points[sg + 1].cast<double>());
                    double d = (ep.cast<double>() - q).norm();
                    if (d < best_d)
                    {
                        best_d = d;
                        best_q = q;
                    }
                }
            }
            if (best_d < bridge_tol && joint_inside(ep, to_point(best_q)))
            {
                end_bridged[i][e] = true;
                if (best_d > 10.0 * SCALED_EPSILON)
                {
                    Polyline br;
                    br.append(ep);
                    br.append(to_point(best_q));
                    bridges.push_back(std::move(br));
                }
            }
        }

    // Stretch free ends along their tangent to within two beads of the
    // boundary so the comb continues to the shape's tips.
    double grew = 0.;
    for (size_t i = 0; i < n_frag; ++i)
    {
        Polyline &sp = anchor.spine[i];
        if (sp.points.size() < 2)
            continue;
        auto extended_end = [&](const Point &end_pt, const Point &prev_pt, Point &out) -> bool
        {
            Vec2d d = (end_pt - prev_pt).cast<double>();
            double len = d.norm();
            if (len < SCALED_EPSILON)
                return false;
            d /= len;
            double rc = raycast_depth(island, end_pt, d, 10.0 * SCALED_EPSILON);
            // a curved end tangent can graze far along the body; cap regions
            // are never deeper than the local thickness
            double ext = std::min(rc - 2.0 * bead_width, max_w);
            if (ext <= SCALED_EPSILON)
                return false;
            out = end_pt + (d * ext).cast<coord_t>();
            return true;
        };
        Point ext_pt;
        if (!end_bridged[i][1] && extended_end(sp.points.back(), sp.points[sp.points.size() - 2], ext_pt))
        {
            grew += (ext_pt - sp.points.back()).cast<double>().norm();
            sp.points.push_back(ext_pt);
        }
        if (!end_bridged[i][0] && extended_end(sp.points.front(), sp.points[1], ext_pt))
        {
            grew += (ext_pt - sp.points.front()).cast<double>().norm();
            sp.points.insert(sp.points.begin(), ext_pt);
        }
    }
    for (Polyline &br : bridges)
        anchor.spine.push_back(std::move(br));
    if (grew > 0. || !bridges.empty())
        dbg.line("spine: %zu junction bridges, ends extended by %.2fmm total", bridges.size(), unscale<double>(grew));

    // The finished spine must span a fair share of the island; a spurious spine
    // is fatal, a needless point anchor is only suboptimal.
    BoundingBox bb = get_extents(island);
    double total_final = 0.;
    for (const Polyline &pl : anchor.spine)
        total_final += pl.length();
    if (total_final < 0.30 * (double) std::max(bb.size().x(), bb.size().y()))
        anchor.spine.clear();
    // A compact solid blob has no central spine: a knurled or serrated rim fragments
    // the medial axis into many short rim branches that sum past the span gate above and
    // read as a spine, so teeth project to the rim and stop shallow, leaving the interior
    // unfilled. The largest inscribed circle (max_w) cannot exceed the short bounding-box
    // side, so max_w >= 0.6 of the long side means the shape is a blob (disc, knurled
    // head); anchor it to the centroid so the contour fan converges across the whole body.
    if (max_w >= 0.6 * (double) std::max(bb.size().x(), bb.size().y()))
        anchor.spine.clear();
    // Ring topology (nut, washer): slits projecting outward from a bore
    // toward a mid-band spine diverge and open wedge gaps, so compact rings
    // use a point anchor and the contour fan crosses the whole band. Band
    // mode keeps the spine instead: hole mouths need it for their direction
    // and the shallow clamp never lets a fan diverge.
    double ring_circ = -1.;
    if (allow_ring_mode)
        for (size_t hi = 0; hi < island.holes.size(); ++hi)
            if (island.holes[hi].contains(to_point(anchor.center)))
            {
                double aspect = (double) std::min(bb.size().x(), bb.size().y()) /
                                (double) std::max(bb.size().x(), bb.size().y());
                // A compact part with a central bore uses the point-anchor fan (the
                // contour fan converges across the band, tips stop flush with the
                // bore) - the solid-blob fan with the bore punched out. This fills
                // the wall solidly, so a cornered bore takes it too, in any mode: the
                // teeth converge and clip at the bore, and the per-tooth caps follow
                // the bore boundary around its corners. Circularity 4*pi*A/P^2: circle
                // ~0.99, square 0.785, hex ~0.91, triangle ~0.60; the floor rules out
                // degenerate / slot-like bores, which keep the spine cascade.
                const Polygon &hd = hi < coarse.holes.size() ? coarse.holes[hi] : island.holes[hi];
                const double per = (double) hd.length();
                ring_circ = per > 0. ? 4.0 * M_PI * std::abs(hd.area()) / (per * per) : 0.;
                const double ring_floor = 0.70;
                if (aspect > 0.5 && ring_circ >= ring_floor)
                {
                    anchor.spine.clear();
                    anchor.ring_mode = true;
                }
                break;
            }
    anchor.is_point = anchor.spine.empty();
    dbg.line("anchor: %s branches=%zu accepted_len=%.2fmm final_len=%.2fmm max_w=%.2fmm bore_circ=%.3f "
             "center=(%.2f,%.2f)mm",
             anchor.ring_mode ? "point(ring)" : (anchor.is_point ? "point" : "spine"), anchor.spine.size(),
             unscale<double>(total), unscale<double>(total_final), unscale<double>(max_w), ring_circ,
             unscale<double>(anchor.center.x()), unscale<double>(anchor.center.y()));
    return anchor;
}

ExPolygons wall_anchor_band(const ExPolygon &island, coord_t bead_width, coord_t overlap, size_t *out_samples,
                            size_t *out_loops, size_t *out_rejected, size_t *out_pruned)
{
    if (out_samples)
        *out_samples = 0;
    if (out_loops)
        *out_loops = 0;
    if (out_rejected)
        *out_rejected = 0;
    if (out_pruned)
        *out_pruned = 0;
    if (island.holes.empty())
        return {};
    const double bw = (double) bead_width;
    const float band_w = 3.0f * (float) bead_width - 2.0f * (float) overlap;
    const double tol = bw * 0.25;

    // Denoise the contour once: strip tessellation noise but keep the gross wall shape. It is every
    // hole's projection target; the band is clipped against the true island below, so detail is not lost.
    Polygon contour_dp;
    douglas_peucker(island.contour.points.begin(), island.contour.points.end(), std::back_inserter(contour_dp.points),
                    tol);
    if (contour_dp.points.size() < 3)
        contour_dp = island.contour;
    std::vector<Polygon> holes_dp;
    holes_dp.reserve(island.holes.size());
    for (const Polygon &h : island.holes)
    {
        if (h.points.size() < 3)
        {
            if (out_rejected)
                ++*out_rejected;
            continue; // degenerate hole (sliver): cannot seat a band
        }
        Polygon hd;
        douglas_peucker(h.points.begin(), h.points.end(), std::back_inserter(hd.points), tol);
        if (hd.points.size() >= 3)
            holes_dp.push_back(std::move(hd));
        else
            holes_dp.push_back(h);
    }
    if (holes_dp.empty())
        return {};

    const Polygons island_polys = to_polygons(island);
    // Bounding box of every projection-target hole, precomputed once for the nearest-boundary prune
    // below. Indexed by position in holes_dp, so it stays valid for holes skipped as an outer ring.
    std::vector<BoundingBox> hole_bb;
    hole_bb.reserve(holes_dp.size());
    for (const Polygon &h : holes_dp)
        hole_bb.push_back(h.bounding_box());
    ExPolygons band_all;
    size_t samples_total = 0, pruned_total = 0;
    // One mid-wall ring per hole: sample the hole boundary and project each sample to the nearest other
    // boundary (the contour or another hole); the midpoint is the wall centreline. Per-hole also covers
    // the wall between two holes, and no centreline crosses solid material; a single contour-sampled loop
    // would bridge the solid between two holes into a fill_core bar.
    for (size_t hi = 0; hi < holes_dp.size(); ++hi)
    {
        const Polygon &H = holes_dp[hi];
        // Reject a near-zero-area sliver: a non-manifold or boolean slice can leave a >=3-point but
        // nearly collinear loop that clears the point-count guard, and its centroid (a divide by the
        // area) would blow up to infinite coordinates that throw inside Clipper.
        if (std::abs(H.area()) < bw * bw)
        {
            if (out_rejected)
                ++*out_rejected;
            continue;
        }
        const double L = H.length();
        // sample at ~bw, but floor the count off the perimeter so a small bore still gets enough points
        const double step = std::max(std::min(bw, L / 24.0), 4.0 * (double) SCALED_EPSILON);
        Points samples = H.equally_spaced_points(step);
        if (samples.size() < 8)
        {
            if (out_rejected)
                ++*out_rejected;
            continue;
        }
        const int n = (int) samples.size();
        samples_total += (size_t) n;
        const Point cen = H.centroid();
        const Vec2d c = cen.cast<double>();
        Points centre_raw(n);
        std::vector<double> ang(n), rad(n);
        for (int i = 0; i < n; ++i)
        {
            const Point &p = samples[i];
            Point best = contour_dp.point_projection(p);
            double bd = (best - p).cast<double>().squaredNorm();
            for (size_t hj = 0; hj < holes_dp.size(); ++hj)
            {
                if (hj == hi)
                    continue;
                // Skip the projection when no point on hole hj's boundary can be nearer to p than p's
                // distance to hj's bounding box already is: a lower bound at or above the current best
                // rules the hole out. The result matches testing every hole; only the far holes of a
                // many-bore part are cut, turning the per-sample cost from O(holes) projections into
                // O(holes) bounding-box tests.
                const BoundingBox &bb = hole_bb[hj];
                const double lbx = p.x() < bb.min.x()   ? double(bb.min.x() - p.x())
                                   : p.x() > bb.max.x() ? double(p.x() - bb.max.x())
                                                        : 0.0;
                const double lby = p.y() < bb.min.y()   ? double(bb.min.y() - p.y())
                                   : p.y() > bb.max.y() ? double(p.y() - bb.max.y())
                                                        : 0.0;
                if (lbx * lbx + lby * lby >= bd)
                {
                    ++pruned_total;
                    continue;
                }
                const Point cand = holes_dp[hj].point_projection(p);
                const double d = (cand - p).cast<double>().squaredNorm();
                if (d < bd)
                {
                    bd = d;
                    best = cand;
                }
            }
            const Point mid((p.x() + best.x()) / 2, (p.y() + best.y()) / 2);
            centre_raw[i] = mid;
            const Vec2d v = mid.cast<double>() - c;
            ang[i] = std::atan2(v.y(), v.x());
            rad[i] = v.norm();
        }
        // Radius-smooth about the hole centroid: average the radial signal r(theta), not the x,y points,
        // so the knurl ripple is shed without the radial contraction that averaging points would cause on
        // a small ring. Valid only when the ring winds once about the centroid; a strongly non-convex hole
        // (crescent, C, L) whose centroid falls outside it is not star-shaped there, so the angle sequence
        // backtracks and the polar rebuild self-crosses. Use the raw midpoints for such a hole. Smooths
        // over +/-5 samples of arc.
        Polyline centreline;
        centreline.points.reserve(n);
        if (H.contains(cen))
        {
            const int W = std::min(5, (n - 1) / 2);
            for (int i = 0; i < n; ++i)
            {
                double rr = 0.0;
                for (int k = -W; k <= W; ++k)
                {
                    int idx = (i + k) % n;
                    if (idx < 0)
                        idx += n;
                    rr += rad[idx];
                }
                rr /= (double) (2 * W + 1);
                centreline.points.push_back(
                    Point((coord_t) (c.x() + rr * std::cos(ang[i])), (coord_t) (c.y() + rr * std::sin(ang[i]))));
            }
        }
        else
            centreline.points = std::move(centre_raw);
        Polylines pls;
        pls.push_back(std::move(centreline));
        ExPolygons ring = intersection_ex(offset(pls, 0.5f * band_w, DefaultLineJoinType, DefaultLineMiterLimit,
                                                 etClosedLine),
                                          island_polys);
        for (ExPolygon &e : ring)
            band_all.push_back(std::move(e));
    }
    if (out_samples)
        *out_samples = samples_total;
    if (out_pruned)
        *out_pruned = pruned_total;
    // Union so per-hole rings that overlap (close holes) merge into single loops.
    band_all = union_ex(band_all);
    if (out_loops)
        *out_loops = band_all.size();
    return band_all;
}

// Largest advance along a ray before it reaches the anchor standoff or passes
// its closest approach to the anchor.
static double ray_anchor_limit(const Anchor &anchor, const Vec2d &from, const Vec2d &dir, double standoff, double t_hi)
{
    if (t_hi <= 0.)
        return 0.;
    double best_t = 0.;
    double best_d = anchor.distance(from);
    double prev_t = 0.;
    for (int s = 1; s <= COARSE_STEPS; ++s)
    {
        double t = t_hi * (double) s / (double) COARSE_STEPS;
        double d = anchor.distance(from + dir * t);
        if (d <= standoff)
        {
            double lo = prev_t, hi = t;
            for (int it = 0; it < BISECT_ITERS; ++it)
            {
                double mid = 0.5 * (lo + hi);
                if (anchor.distance(from + dir * mid) > standoff)
                    lo = mid;
                else
                    hi = mid;
            }
            return lo;
        }
        if (d < best_d)
        {
            best_d = d;
            best_t = t;
        }
        prev_t = t;
    }
    return best_t;
}

// Boundary loop with cumulative arc lengths for arc-position walking.
struct Loop
{
    std::vector<Vec2d> pts;
    std::vector<double> cum; // cum[i] = arc length up to vertex i; cum[pts.size()] = total
    double total{0.};
};

static Loop make_loop(const Polygon &poly)
{
    Loop lp;
    lp.pts.reserve(poly.points.size());
    lp.cum.reserve(poly.points.size() + 1);
    double acc = 0.;
    for (size_t i = 0; i < poly.points.size(); ++i)
    {
        lp.pts.push_back(poly.points[i].cast<double>());
        lp.cum.push_back(acc);
        const Point &p0 = poly.points[i];
        const Point &p1 = poly.points[(i + 1) % poly.points.size()];
        acc += (p1 - p0).cast<double>().norm();
    }
    lp.cum.push_back(acc);
    lp.total = acc;
    return lp;
}

static double wrap_arc(const Loop &lp, double arc)
{
    arc = std::fmod(arc, lp.total);
    if (arc < 0.)
        arc += lp.total;
    return arc;
}

static size_t loop_segment_index(const Loop &lp, double arc)
{
    auto it = std::upper_bound(lp.cum.begin(), lp.cum.end(), arc);
    size_t i = (it == lp.cum.begin()) ? 0 : (size_t) (it - lp.cum.begin()) - 1;
    return std::min(i, lp.pts.size() - 1);
}

static Vec2d loop_point_at(const Loop &lp, double arc)
{
    arc = wrap_arc(lp, arc);
    size_t i = loop_segment_index(lp, arc);
    Vec2d a = lp.pts[i];
    Vec2d b = lp.pts[(i + 1) % lp.pts.size()];
    double sl = lp.cum[i + 1] - lp.cum[i];
    double t = (sl > 0.) ? (arc - lp.cum[i]) / sl : 0.;
    return a + (b - a) * t;
}

static Vec2d loop_tangent_at(const Loop &lp, double arc)
{
    arc = wrap_arc(lp, arc);
    size_t i = loop_segment_index(lp, arc);
    Vec2d v = lp.pts[(i + 1) % lp.pts.size()] - lp.pts[i];
    double n = v.norm();
    return (n > 0.) ? Vec2d(v / n) : Vec2d(1., 0.);
}

// Append the loop vertices strictly between arc_a and arc_b, walking forward.
static void loop_walk_between(const Loop &lp, double arc_a, double arc_b, Polyline &out)
{
    arc_a = wrap_arc(lp, arc_a);
    arc_b = wrap_arc(lp, arc_b);
    double span = arc_b - arc_a;
    if (span < 0.)
        span += lp.total;
    size_t i = loop_segment_index(lp, arc_a);
    size_t v = (i + 1) % lp.pts.size();
    double walked = lp.cum[i + 1] - arc_a;
    size_t guard = lp.pts.size() + 2;
    while (walked < span && guard-- > 0)
    {
        out.append(to_point(lp.pts[v]));
        walked += lp.cum[v + 1] - lp.cum[v];
        v = (v + 1) % lp.pts.size();
    }
}

// Arc coordinate of the nearest point on the loop to p (brute force over edges).
static double loop_nearest_arc(const Loop &lp, const Vec2d &p)
{
    double best = std::numeric_limits<double>::max(), best_arc = 0.;
    for (size_t i = 0; i < lp.pts.size(); ++i)
    {
        const Vec2d &a = lp.pts[i];
        const Vec2d &b = lp.pts[(i + 1) % lp.pts.size()];
        Vec2d ab = b - a;
        double l2 = ab.squaredNorm();
        double t = l2 > 0. ? std::clamp((p - a).dot(ab) / l2, 0., 1.) : 0.;
        double d = (p - (a + ab * t)).squaredNorm();
        if (d < best)
        {
            best = d;
            best_arc = lp.cum[i] + t * (lp.cum[i + 1] - lp.cum[i]);
        }
    }
    return best_arc;
}

// Append the loop vertices along the shorter arc from arc_a to arc_b, in order
// from arc_a toward arc_b. Routes a U-turn cap along the bead curve.
static void loop_walk_short(const Loop &lp, double arc_a, double arc_b, Polyline &out)
{
    double fwd = wrap_arc(lp, arc_b - arc_a);
    if (fwd <= lp.total - fwd)
        loop_walk_between(lp, arc_a, arc_b, out);
    else
    {
        Polyline tmp;
        loop_walk_between(lp, arc_b, arc_a, tmp);
        for (auto it = tmp.points.rbegin(); it != tmp.points.rend(); ++it)
            out.points.push_back(*it);
    }
}

// Signed arc difference wrapped into (-total/2, total/2]: negative means the
// first arc precedes the second along the loop.
static double wrapped_signed_arc(const Loop &lp, double d)
{
    d = std::fmod(d, lp.total);
    if (d < 0.)
        d += lp.total;
    if (d > 0.5 * lp.total)
        d -= lp.total;
    return d;
}

// A genuine sharp corner of a band loop: the divide where two edges' teeth would
// otherwise overlap. Detected on the denoised contour so slice micro-wiggle does
// not read as a corner; a smooth curve yields none.
struct BandCorner
{
    Vec2d apex;             // the corner vertex
    double arc{0.};         // its arc coordinate on the raw loop
    Vec2d n_in, n_out;      // inward (into-material) normals of the two edges
    Vec2d bisector{0., 0.}; // into the material, between n_in and n_out
};

// Find the loop's genuine corners. Douglas-Peucker removes sub-bead slice noise
// first (the same tolerance build_anchor uses to keep noise out of the medial
// axis). A corner is a concentrated convex turn flanked by straight runs, not a
// single DP vertex exceeding a fixed angle: the turn in a narrow arc window must
// clear a real-corner threshold and make up most of the turn in a wider window.
// On a smooth arc the windowed turn scales with the window length, so the
// narrow/wide ratio is ~1/wide_mult at every radius and the arc is rejected at
// every scale; at a corner the wide window adds only the straight flanks, so the
// ratio approaches 1. This tells a filleted corner apart from a small smooth arc.
static std::vector<BandCorner> detect_band_corners(const Polygon &raw, const Loop &lp, double bw)
{
    std::vector<BandCorner> corners;
    if (raw.points.size() < 3)
        return corners;
    Points dp;
    douglas_peucker(raw.points.begin(), raw.points.end(), std::back_inserter(dp), bw * 0.25);
    const size_t m = dp.size();
    if (m < 3)
        return corners;
    // Map each denoised vertex back to its raw arc coordinate. DP keeps a subset
    // of the input vertices in order, so one forward scan suffices.
    std::vector<double> dp_arc(m, 0.);
    {
        size_t ri = 0;
        for (size_t j = 0; j < m; ++j)
        {
            while (ri < raw.points.size() && !(raw.points[ri].x() == dp[j].x() && raw.points[ri].y() == dp[j].y()))
                ++ri;
            dp_arc[j] = (ri < lp.cum.size()) ? lp.cum[ri] : 0.;
            if (ri < raw.points.size())
                ++ri;
        }
    }

    // Per-vertex signed exterior turn (positive = convex into material, i.e. a
    // left turn on the consistently-oriented boundary: CCW contour and CW hole),
    // and each DP edge's length (edge j -> j+1).
    std::vector<double> turn(m, 0.), seg(m, 0.);
    for (size_t j = 0; j < m; ++j)
    {
        const Vec2d prev = dp[(j + m - 1) % m].cast<double>();
        const Vec2d apex = dp[j].cast<double>();
        const Vec2d next = dp[(j + 1) % m].cast<double>();
        Vec2d ti = apex - prev, to = next - apex;
        const double li = ti.norm(), lo = to.norm();
        seg[j] = lo;
        if (li < SCALED_EPSILON || lo < SCALED_EPSILON)
            continue;
        ti /= li;
        to /= lo;
        const double dot = std::clamp(ti.dot(to), -1.0, 1.0);
        const double cross = ti.x() * to.y() - ti.y() * to.x();
        turn[j] = (cross > 0.) ? std::acos(dot) : -std::acos(dot);
    }

    // Cumulative arc of each DP vertex and the total, for window/cluster math.
    std::vector<double> arc(m, 0.);
    for (size_t j = 1; j < m; ++j)
        arc[j] = arc[j - 1] + seg[j - 1];
    const double total = arc[m - 1] + seg[m - 1];
    if (total < 1e-6)
        return corners;
    auto arc_gap = [&](size_t a, size_t b)
    {
        const double d = std::abs(arc[a] - arc[b]);
        return std::min(d, total - d);
    };

    // Accumulated signed turn within +/- W arc-distance of vertex j.
    auto windowed = [&](size_t j, double W)
    {
        double acc = turn[j], d = 0.;
        for (size_t i = j;;)
        {
            d += seg[i];
            i = (i + 1) % m;
            if (d > W || i == j)
                break;
            acc += turn[i];
        }
        d = 0.;
        for (size_t i = j;;)
        {
            i = (i + m - 1) % m;
            d += seg[i];
            if (d > W || i == j)
                break;
            acc += turn[i];
        }
        return acc;
    };

    const double W = 1.5 * bw;
    const double Wwide = 2.0 * W;
    const double theta = 40.0 * M_PI / 180.0; // a real corner: a >= 40 deg turn
    const double ratio = 0.7;                 // concentrated: most of the wide turn fits in W
    std::vector<char> qual(m, 0);
    std::vector<double> Tn(m, 0.);
    for (size_t j = 0; j < m; ++j)
    {
        Tn[j] = windowed(j, W);
        const double tw = windowed(j, Wwide);
        if (Tn[j] >= theta && Tn[j] >= ratio * std::max(tw, 1e-9))
            qual[j] = 1;
    }

    // Cluster qualifying vertices by arc distance (the DP indices of two distinct
    // corners are adjacent because the straight edge between them is one segment),
    // then take each cluster's max-turn vertex as the apex. The vertices are already
    // in arc order, so one linear pass suffices: a run of consecutive qualifiers whose
    // arc gap to the previous stays within W, with the seam run merged into the first.
    std::vector<size_t> q;
    q.reserve(m);
    for (size_t j = 0; j < m; ++j)
        if (qual[j])
            q.push_back(j);
    std::vector<std::vector<size_t>> clusters;
    for (size_t i = 0; i < q.size(); ++i)
    {
        if (i > 0 && arc_gap(q[i], q[i - 1]) <= W)
            clusters.back().push_back(q[i]);
        else
            clusters.push_back({q[i]});
    }
    if (clusters.size() > 1 && arc_gap(q.back(), q.front()) <= W)
    {
        clusters.front().insert(clusters.front().begin(), clusters.back().begin(), clusters.back().end());
        clusters.pop_back();
    }

    for (const std::vector<size_t> &cluster : clusters)
    {
        size_t apex_i = cluster.front();
        for (size_t c : cluster)
            if (Tn[c] > Tn[apex_i])
                apex_i = c;

        // n_in / n_out: inward normals of the straight runs ~W on each side of the
        // apex (beyond any fillet). The step guard stops a tiny loop from spinning.
        auto edge_back = [&](size_t a) -> Vec2d
        {
            size_t i = a;
            for (size_t step = 0; step < m && arc_gap(i, a) < W; ++step)
                i = (i + m - 1) % m;
            const Vec2d d = (dp[(i + 1) % m] - dp[i]).cast<double>();
            const double n = d.norm();
            return n > 1e-9 ? Vec2d(d / n) : Vec2d(0., 0.);
        };
        auto edge_fwd = [&](size_t a) -> Vec2d
        {
            size_t i = a;
            for (size_t step = 0; step < m && arc_gap(i, a) < W; ++step)
                i = (i + 1) % m;
            const Vec2d d = (dp[i] - dp[(i + m - 1) % m]).cast<double>();
            const double n = d.norm();
            return n > 1e-9 ? Vec2d(d / n) : Vec2d(0., 0.);
        };
        const Vec2d ti = edge_back(apex_i), to = edge_fwd(apex_i);
        BandCorner c;
        c.apex = dp[apex_i].cast<double>();
        c.arc = dp_arc[apex_i];
        c.n_in = Vec2d(-ti.y(), ti.x());
        c.n_out = Vec2d(-to.y(), to.x());
        const Vec2d b = c.n_in + c.n_out;
        const double bn = b.norm();
        c.bisector = bn > 1e-9 ? Vec2d(b / bn) : c.n_in;
        corners.push_back(c);
    }
    return corners;
}

struct Slit
{
    Vec2d pos;                                         // mouth center on the contour
    double arc{0.};                                    // arc position along the loop
    Vec2d tangent;                                     // contour tangent at the mouth
    Vec2d dir;                                         // projection direction (unit), zero when degenerate
    double max_t{0.};                                  // depth clamp from anchor standoff and raycast
    int level{1};                                      // ruler level
    double depth{0.};                                  // resolved depth
    double m_off{0.};                                  // mouth half-width along the contour
    double off{0.};                                    // lateral offset of each pair bead from the ray
    size_t comp_l{std::numeric_limits<size_t>::max()}; // braking competitor on each side
    size_t comp_r{std::numeric_limits<size_t>::max()};
};

// Slit count of the form q * 2^k (q odd, up to 15) with pitch closest to
// nominal: the binary depth cascade nests only for such counts.
static size_t pick_slit_count(double loop_total, double pitch_nominal)
{
    const double pitch_min = pitch_nominal * 0.965;
    const double pitch_max = pitch_nominal * 1.10;
    size_t best = 0;
    double best_err = std::numeric_limits<double>::max();
    for (int k = 1; k < 40; ++k)
        for (size_t q : {size_t(1), size_t(3), size_t(5), size_t(7), size_t(9), size_t(11), size_t(13), size_t(15)})
        {
            size_t cand = q << k;
            if (cand < 8)
                continue;
            double pitch = loop_total / (double) cand;
            if (pitch < pitch_min || pitch > pitch_max)
                continue;
            double err = std::abs(pitch - pitch_nominal);
            if (err < best_err)
            {
                best_err = err;
                best = cand;
            }
        }
    return best;
}

// Ruler levels: q top-level walls every 2^k mouths, each sector nesting as a
// binary cascade. Other counts take their level from the index's trailing
// zeros.
// force_ruler: always use the arbitrary-count trailing-zeros ruler, even when n
// factors as q*2^k. Relaxed counts follow the loop length, so a coincidentally
// clean factorization would flip the level structure between the sector and
// ruler regimes whenever taper moves n by one, lurching the depth pattern and
// the outer-loop host with it. The ruler keeps levels a pure function of the
// rib index.
static void assign_ring_levels(std::vector<Slit> &slits, bool force_ruler = false)
{
    const size_t n = slits.size();
    if (n == 0)
        return;
    int k = 0;
    while (((n >> k) & 1) == 0)
        ++k;
    if (!force_ruler && (n >> k) <= 15)
    {
        const size_t sector = size_t(1) << k;
        for (size_t v = 0; v < n; ++v)
        {
            int level;
            if (v % sector == 0)
                level = k + 1;
            else
            {
                level = 1;
                size_t vv = v;
                while ((vv & 1) == 0)
                {
                    ++level;
                    vv >>= 1;
                }
            }
            slits[v].level = level;
        }
        return;
    }
    // Arbitrary counts: the ruler level comes straight from the index's trailing
    // zeros, index 0 the single top, so short and long ribs alternate evenly all
    // the way around.
    int m = 0;
    while ((size_t(1) << (m + 1)) <= n)
        ++m;
    for (size_t v = 0; v < n; ++v)
    {
        int level;
        if (v == 0)
            level = m + 1;
        else
        {
            level = 1;
            size_t vv = v;
            while ((vv & 1) == 0)
            {
                ++level;
                vv >>= 1;
            }
            if (level > m)
                level = m;
        }
        slits[v].level = level;
    }
}

bool generate(const ExPolygon &island, const Params &params, ExtrusionEntityCollection &out_loops,
              ExPolygons *out_interior)
{
    // Loop-direction preference, outer loop only. The woven lap runs opposite
    // the ring walk (that is what keeps the crossover crossing-free), so to
    // flip the lap while keeping the inside-first order, the whole pattern is
    // built on the Y-mirrored island and the output mirrored back: same
    // structure and order, opposite handedness. The lap prints
    // counterclockwise like every perimeter loop unless prefer_clockwise.
    // external_perimeters_first reverses the finished tour at emit (lap
    // first); the reversal flips every bead too, so the mirror decision
    // inverts to keep the lap on the preference. Full mode only. A mirrored
    // build's debug lines log negated Y coordinates.
    const bool dir_full_mode = params.band_clamp == 0 && params.depth_clamp == 0.;
    const bool want_mirror = dir_full_mode && params.outer_loop &&
                             (params.outer_first ? params.prefer_clockwise : !params.prefer_clockwise);
    if (want_mirror && !params.mirror_build)
    {
        auto mirror_poly = [](Polygon &p)
        {
            for (Point &pt : p.points)
                pt.y() = -pt.y();
            std::reverse(p.points.begin(), p.points.end()); // restore orientation
        };
        ExPolygon mirrored = island;
        mirror_poly(mirrored.contour);
        for (Polygon &h : mirrored.holes)
            mirror_poly(h);
        Params p2 = params;
        p2.mirror_build = true;
        ExtrusionEntityCollection tmp;
        tmp.no_sort = true;
        if (!generate(mirrored, p2, tmp, out_interior))
            return false;
        // Mirror the geometry back; the traversal order stays as built, which
        // in the real frame is the reversed ring walk. Entities are one island
        // collection of multipaths (never nested deeper by contract).
        for (ExtrusionEntity *ee : tmp.entities)
            if (auto *coll = dynamic_cast<ExtrusionEntityCollection *>(ee))
                for (ExtrusionEntity *ce : coll->entities)
                    if (auto *mp = dynamic_cast<ExtrusionMultiPath *>(ce))
                        for (ExtrusionPath &path : mp->paths)
                            for (Point &pt : path.polyline.points)
                                pt.y() = -pt.y();
        if (out_interior != nullptr)
            for (ExPolygon &ep : *out_interior)
            {
                mirror_poly(ep.contour);
                for (Polygon &h : ep.holes)
                    mirror_poly(h);
            }
        // splice, never nest: move the island collection(s) over
        for (ExtrusionEntity *ee : tmp.entities)
            out_loops.entities.push_back(ee);
        tmp.entities.clear();
        return true;
    }

    BoundingBox bb = get_extents(island);
    coord_t min_dim = std::min(bb.size().x(), bb.size().y());

    DbgReport dbg(params.print_z);
    dbg.line("island: bw=%.4fmm ov=%.4fmm maxbead=%.4fmm band=%.2fmm depthclamp=%.2fmm min_dim=%.2fmm "
             "contour_pts=%zu holes=%zu",
             unscale<double>(params.bead_width), unscale<double>(params.overlap), unscale<double>(params.max_bead),
             unscale<double>(params.band_clamp), unscale<double>(params.depth_clamp), unscale<double>(min_dim),
             island.contour.points.size(), island.holes.size());

    if (min_dim < params.bead_width * 3)
    {
        dbg.line("REJECT island: min_dim below 3 bead widths");
        return false;
    }

    // A near-zero-area sliver gives a NaN centroid, which compares false in
    // every guard downstream.
    const double bw_d = (double) params.bead_width;
    if (std::abs(island.contour.area()) < 9.0 * bw_d * bw_d)
    {
        dbg.line("REJECT island: degenerate area sliver");
        return false;
    }

    const double bw = (double) params.bead_width;
    // How close to its conform limit a depth-mode tooth must run to count as a
    // "boundary" tooth (full-run perpendicular cap, contoured onto the bead).
    // Shared by the cap setup, the apex gate, and the cap-walk emit so the three
    // sites cannot drift apart on a retune.
    const double boundary_margin = 0.5 * bw;
    // Negative overlap spreads the units apart instead of bonding them: the
    // braking threshold (bw - ov) becomes bw + gap, so every unit stops a
    // uniform gap short of its neighbors. The pitch grows by the gap to give
    // the units their room, and the tip margins widen with it.
    const double ov = std::clamp((double) params.overlap, -3.0 * bw, bw * 0.45);
    const double gap = std::max(0., -ov);
    // Tip-to-side bonds have almost no contact area at the flank overlap
    // alone, so end caps engage deeper.
    const double tip_bond = ov + 0.15 * bw;
    // A unit has two bead contacts (pair internal and inter-unit), so the
    // pitch carries one gap for each.
    const double pitch_t = PITCH_FACTOR * bw + 2.0 * gap;

    // Relaxed layout: sparse fixed-pitch ribs at a user pitch instead of the dense
    // cascade - the bead follows the boundary clean between ribs; each rib is a
    // bonded pair. The layout also serves BAND mode (solid-surface face layers run
    // the band at the relaxed spacing so the visible side pattern matches the body).
    // The full-mode machinery on top of it - hub, racetrack, plain-ring holes, the
    // outer-loop excursion - is full mode only.
    const bool relaxed_layout = params.rib_pitch > 0 && params.depth_clamp == 0.;
    const bool relaxed = relaxed_layout && params.band_clamp == 0;
    // The user pitch can never drop below the dense pitch; the dense cascade is the
    // right structure there.
    const double pitch_rx = std::max((double) params.rib_pitch, pitch_t);

    // Depth-limited parts keep the full anchor selection, ring mode included: a
    // washer must stay point(ring) so its fan stays convergent and the bore keeps
    // its cap-chain treatment. The clamp only shortens the teeth.
    // Relaxed mode disables ring mode: the bore surface in ring mode is the chain of
    // densely packed caps, which disintegrates at sparse spacing. Bores get a plain
    // bead loop instead and the spokes weld into it.
    Anchor anchor = build_anchor(island, bw, params.band_clamp == 0 && !relaxed, dbg);
    if (relaxed && !island.holes.empty() && !anchor.is_point)
    {
        // Holed island: the wall-cycle medial axis would stop every rib mid-wall,
        // leaving the hole beads laterally unbonded. A point anchor sends each rib
        // across the wall instead; the boundary raycast welds it into the far bead.
        anchor.spine.clear();
        anchor.is_point = true;
        dbg.line("relaxed: wall-cycle spine dropped on a holed island, point anchor");
    }
    if (!std::isfinite(anchor.center.x()) || !std::isfinite(anchor.center.y()))
    {
        dbg.line("REJECT island: anchor center not finite");
        return false;
    }

    // Relaxed spine racetrack: the hub-ring analog for an elongated island. A tight
    // capsule loop traced around the spine (out one side, U-turn at each free end,
    // back the other), woven into the tour at the seam exactly like the center ring;
    // every rib tip stops one tip-bond short of it and welds in. The offset radius is
    // half the pair separation, so the two long sides are a bonded pair - one solid
    // center spar. The capsule is kept a half-bead inside the island: where the wall
    // thins (a trailing edge) the raw capsule would poke through the side walls and
    // fail its clip; hugging the wall inset there bonds it edge to edge with the
    // boundary bead instead. An empty result falls back to the bare spine standoff
    // (tips meet tip-to-tip mid-wall as before).
    const double r_track = 0.5 * (bw - ov);
    Polygons track_polys;
    if (!anchor.is_point && params.band_clamp == 0 && params.depth_clamp == 0.)
    {
        // The track centerline keeps a half-bead clearance beyond the bonded
        // spacing: running the pair all the way to wall contact merges it with
        // the walls along a shallow wedge (spine noise eats the zero slack), so
        // the pair stops early and the V-tip below points it; the small sliver
        // beyond stays open. Clipping the capsule against the island instead
        // makes the track hug the wall inset at half-bead overlap with doubled
        // sides.
        const ExPolygons track_fit_region = offset_ex(island, -(float) (r_track + (bw - ov) + 0.5 * bw));
        Polylines spine_fit = intersection_pl(anchor.spine, track_fit_region);
        // Stabilize the free ends: the raw spine's tip extension rides a raycast
        // along the end tangent, which is hypersensitive to tessellation in a
        // thin wedge - some layers it reaches past the clip region (the clip then
        // defines the end, stable), some layers it falls short (the raw medial
        // end defines it, jumping millimeters between layers). March every free
        // end forward along its tangent until it exits the clip region, so the
        // track end always lands on the same geometric boundary regardless of
        // which regime the raw spine arrived in. Junction ends stay put.
        size_t n_stab = 0;
        double stab_len = 0.;
        auto in_fit_region = [&](const Vec2d &p) -> bool
        {
            const Point q = to_point(p);
            for (const ExPolygon &ep : track_fit_region)
                if (ep.contains(q))
                    return true;
            return false;
        };
        // End direction from a >= 3-bead baseline back along the polyline, not
        // the last segment: the end POINT is pinned to stable geometry, but the
        // final segment is a short Voronoi stub whose direction jitters with the
        // layer's tessellation - and everything built along this direction (the
        // stabilization march, the V-tip 1.3mm out) swings with it.
        auto end_dir = [&](const Polyline &pl, int end_i) -> Vec2d
        {
            const size_t np2 = pl.points.size();
            const Vec2d ee = (end_i ? pl.points.back() : pl.points.front()).cast<double>();
            Vec2d back = (end_i ? pl.points[np2 - 2] : pl.points[1]).cast<double>();
            double acc = 0.;
            if (end_i)
                for (size_t j = np2 - 1; j > 0; --j)
                {
                    acc += (pl.points[j].cast<double>() - pl.points[j - 1].cast<double>()).norm();
                    back = pl.points[j - 1].cast<double>();
                    if (acc >= 3.0 * bw)
                        break;
                }
            else
                for (size_t j = 0; j + 1 < np2; ++j)
                {
                    acc += (pl.points[j + 1].cast<double>() - pl.points[j].cast<double>()).norm();
                    back = pl.points[j + 1].cast<double>();
                    if (acc >= 3.0 * bw)
                        break;
                }
            Vec2d d = ee - back;
            const double dn = d.norm();
            return dn > SCALED_EPSILON ? Vec2d(d / dn) : Vec2d(0., 0.);
        };
        for (Polyline &sf : spine_fit)
        {
            if (sf.points.size() < 2)
                continue;
            for (int end = 0; end < 2; ++end)
            {
                const Vec2d e = (end ? sf.points.back() : sf.points.front()).cast<double>();
                const Vec2d t = end_dir(sf, end);
                if (t.squaredNorm() < 0.5)
                    continue;
                double d_other = std::numeric_limits<double>::max();
                for (const Polyline &po : spine_fit)
                {
                    if (&po == &sf)
                        continue;
                    for (size_t si = 0; si + 1 < po.points.size(); ++si)
                        d_other = std::min(d_other, dist_point_segment(e, po.points[si].cast<double>(),
                                                                       po.points[si + 1].cast<double>()));
                }
                if (d_other < 2.0 * r_track)
                    continue;
                const double adv = advance_while(40.0 * bw, [&](double tt) { return in_fit_region(e + t * tt); });
                if (adv > 0.5 * bw && adv < 40.0 * bw - SCALED_EPSILON)
                {
                    const Point np = to_point(e + t * adv);
                    if (end)
                        sf.points.push_back(np);
                    else
                        sf.points.insert(sf.points.begin(), np);
                    ++n_stab;
                    stab_len += adv;
                }
            }
        }
        if (n_stab > 0)
            dbg.line("track: stabilized %zu free end(s), ext total %.2fmm", n_stab, unscale<double>(stab_len));
        ExPolygons capsule = union_ex(offset(spine_fit, (float) r_track, jtRound, DefaultLineMiterLimit, etOpenRound));
        size_t n_track_holes = 0;
        for (ExPolygon &ep : intersection_ex(capsule, offset_ex(island, -(float) (bw - ov))))
            if (ep.contour.points.size() >= 3 && std::abs(ep.contour.area()) >= bw * bw)
            {
                // An annular capsule (a spine cycling a hole) keeps only its
                // outer lane; the inner boundary is dropped and counted.
                n_track_holes += ep.holes.size();
                track_polys.push_back(std::move(ep.contour));
            }
        if (n_track_holes > 0)
            dbg.line("track: %zu inner capsule boundary(ies) dropped", n_track_holes);

        // Free spine ends taper the capsule to a point: the round end cap (a
        // U-turn at the pair spacing, the tightest a fixed-width bead can fold)
        // is swapped for a V whose legs converge at a fixed steepness, confining
        // the over-bond to that short stretch. The V reaches only as far as the
        // island gives the bead room: a continuing wedge grows a full tip, a
        // blunt end stays nearly round. Junction ends keep their cap.
        if (!track_polys.empty())
        {
            const double v_len = r_track / std::tan(15.0 * M_PI / 180.0);
            const ExPolygons tip_room = offset_ex(island, -(float) (0.55 * bw));
            for (const Polyline &sf : spine_fit)
            {
                if (sf.points.size() < 2)
                    continue;
                for (int end = 0; end < 2; ++end)
                {
                    const Vec2d e = (end ? sf.points.back() : sf.points.front()).cast<double>();
                    const Vec2d t = end_dir(sf, end);
                    if (t.squaredNorm() < 0.5)
                        continue;
                    double d_other = std::numeric_limits<double>::max();
                    for (const Polyline &po : spine_fit)
                    {
                        if (&po == &sf)
                            continue;
                        for (size_t si = 0; si + 1 < po.points.size(); ++si)
                            d_other = std::min(d_other, dist_point_segment(e, po.points[si].cast<double>(),
                                                                           po.points[si + 1].cast<double>()));
                    }
                    if (d_other < 2.0 * r_track)
                        continue;
                    // longest V that still has bead room, at the fixed steepness
                    Vec2d tip(0., 0.);
                    bool have_tip = false;
                    double tip_f = 0.;
                    for (double f : {1.0, 0.75, 0.5})
                    {
                        const Vec2d cand = e + t * (v_len * f);
                        const Point cp = to_point(cand);
                        for (const ExPolygon &room : tip_room)
                            if (room.contains(cp))
                            {
                                tip = cand;
                                have_tip = true;
                                tip_f = f;
                                break;
                            }
                        if (have_tip)
                            break;
                    }
                    if (!have_tip)
                    {
                        dbg.line("track tip: end=(%.2f,%.2f)mm f=%.2f (no room - cap kept)", unscale<double>(e.x()),
                                 unscale<double>(e.y()), tip_f);
                        continue;
                    }
                    // Swap the end cap for the single tip vertex. Cap vertices are
                    // selected geometrically - ahead of the end's base plane and
                    // within two cap radii - NOT by distance to the cap circle:
                    // Clipper tessellates a sub-bead-radius cap coarsely and its
                    // vertices (a miter-ish corner sits at r*sqrt2) land inside or
                    // outside any tight radius test depending on the layer's
                    // tessellation phase, which flips the tip between V and blunt
                    // across layers of unchanged geometry.
                    bool spliced = false;
                    size_t n_near = 0;
                    for (Polygon &tp : track_polys)
                    {
                        const size_t m = tp.points.size();
                        std::vector<char> near_cap(m, 0);
                        n_near = 0;
                        for (size_t vi = 0; vi < m; ++vi)
                        {
                            const Vec2d dv = tp.points[vi].cast<double>() - e;
                            if (dv.dot(t) > -0.25 * r_track && dv.norm() <= 2.0 * r_track)
                            {
                                near_cap[vi] = 1;
                                ++n_near;
                            }
                        }
                        if (n_near == 0 || n_near >= m)
                            continue;
                        size_t start = 0;
                        while (start < m && (near_cap[start] || !near_cap[(start + 1) % m]))
                            ++start;
                        if (start == m)
                            continue;
                        Points np;
                        np.reserve(m + 2);
                        bool replaced = false;
                        for (size_t k2 = 0; k2 < m; ++k2)
                        {
                            const size_t vi = (start + 1 + k2) % m;
                            if (near_cap[vi])
                            {
                                if (!replaced)
                                {
                                    // Exact base corners flank the tip so the V's
                                    // legs are a pure function of (end, dir, r) -
                                    // anchoring them on surviving polygon vertices
                                    // instead leaves the leg shape to wherever
                                    // Clipper happened to place a vertex, which
                                    // varies with the layer's tessellation.
                                    const Vec2d perp(-t.y(), t.x());
                                    const Vec2d b1 = e + perp * r_track;
                                    const Vec2d b2 = e - perp * r_track;
                                    const Vec2d prev_kept = tp.points[start].cast<double>();
                                    const bool b1_first = (b1 - prev_kept).squaredNorm() <=
                                                          (b2 - prev_kept).squaredNorm();
                                    np.push_back(to_point(b1_first ? b1 : b2));
                                    np.push_back(to_point(tip));
                                    np.push_back(to_point(b1_first ? b2 : b1));
                                    replaced = true;
                                }
                            }
                            else
                                np.push_back(tp.points[vi]);
                        }
                        if (replaced && np.size() >= 3)
                        {
                            tp.points = std::move(np);
                            spliced = true;
                            break;
                        }
                    }
                    // Across layers of unchanged geometry end=, dir= must hold
                    // still and spliced= must stay 1; whichever flaps is the
                    // visible tip flip.
                    dbg.line("track tip: end=(%.2f,%.2f)mm dir=%.1fdeg f=%.2f spliced=%d nn=%zu",
                             unscale<double>(e.x()), unscale<double>(e.y()), std::atan2(t.y(), t.x()) * 180.0 / M_PI,
                             tip_f, spliced ? 1 : 0, n_near);
                }
            }
        }
    }
    const bool spine_track = !track_polys.empty();
    if (!anchor.is_point && params.band_clamp == 0 && params.depth_clamp == 0.)
        dbg.line("track: loops=%zu r=%.2fmm%s", track_polys.size(), unscale<double>(r_track),
                 spine_track ? "" : " (EMPTY - bare spine standoff)");

    // Beaded holes (every hole but a full-mode ring bore) trace a centerline
    // boundary bead; grow each a half-bead so the bead's outer edge lands on the
    // true hole, like a normal perimeter, instead of straddling the edge and
    // printing the hole a bead undersize. The full-mode ring bore keeps the true
    // boundary and carries no bead (its caps stop at the edge). The tour still
    // clips to the true island, so growing the bead loop never bridges teeth.
    // Band and depth modes skip this; their interior fills clip the true holes.
    ExPolygon work_island = island;
    if (params.band_clamp == 0 && params.depth_clamp == 0. && !island.holes.empty())
    {
        work_island.holes.clear();
        for (const Polygon &h : island.holes)
        {
            if (anchor.ring_mode && h.contains(to_point(anchor.center)))
            {
                work_island.holes.push_back(h); // ring bore: true edge, no bead
                continue;
            }
            // Grow the beaded hole a half-bead with one offset, no diff, so the bead
            // lands as exactly as the contour's half-bead inset; a diff would snap
            // intersection vertices and shift the bead a few microns. Miter keeps a
            // cornered hole's corners sharp.
            Polygon v = h;
            v.make_counter_clockwise();
            Polygons grown = offset(v, (float) (0.5 * bw), jtMiter, 2.0);
            if (grown.size() == 1)
            {
                grown.front().make_clockwise();
                work_island.holes.push_back(std::move(grown.front()));
            }
            else
            {
                work_island.holes.push_back(h);
                dbg.line("hole-grow: half-bead grow split a hole into %zu loops; keeping true edge", grown.size());
            }
        }
    }

    // Relaxed outer loop: one continuous perimeter wraps the island and the
    // rib-carrying wall moves one bonded spacing inside it, so the rib mouths
    // interrupt only the hidden inner wall. The tour reaches the outer loop
    // through a single crossover (emitted in the contour walk below); its two
    // diagonals occupy the bare rectangle both walls leave there, so material
    // stays nominal and the surface shows one seam-like chevron. The inner wall
    // replaces the working contour, so ribs, raycasts and the count all follow
    // it; the outer loop keeps the original contour and the part's true size.
    bool have_outer = false;
    Loop outer_lp;
    if (params.outer_loop && params.band_clamp == 0 && params.depth_clamp == 0.)
    {
        Polygons inner = offset(work_island.contour, -(float) (bw - ov), jtMiter, 2.0);
        if (inner.size() == 1 && inner.front().points.size() >= 3)
        {
            inner.front().make_counter_clockwise();
            outer_lp = make_loop(work_island.contour);
            work_island.contour = std::move(inner.front());
            have_outer = true;
        }
        else
            dbg.line("outer loop: inner inset failed (%zu pieces); no outer loop for this island", inner.size());
    }

    // Band mode is an explicit user setting only. A hole that declines the ring
    // gate takes the convergent point fan (it fills the wall) or, failing that, the
    // spine cascade. There is no automatic dual-face band: it leaves a hollow core
    // (each face reaches only half the wall and the two never meet).
    double band = (double) params.band_clamp;

    // Point anchors get a single-bead ring at the center for the deep spokes
    // to bond to, woven into the tour as the seam slit's turnaround. Band
    // mode has no deep spokes, so no ring. Relaxed mode uses the same ring:
    // its cascade fills the convergence exactly like classic, so the deepest
    // ribs reach this ring and the rest reverse where they touch a neighbor.
    const double r_loop = bw;
    // No center ring under a depth clamp: the teeth stop at the depth and never
    // reach the centroid, so a ring there would float in the interior fill.
    const bool center_loop = band == 0. && params.depth_clamp == 0 && anchor.is_point && min_dim > coord_t(10.0 * bw) &&
                             island.contains(to_point(anchor.center));

    ExtrusionEntityCollection collection;
    collection.no_sort = true;

    // One role for the whole tour keeps speeds, cooling and visualization
    // uniform across boundary and slit segments. Serpentine carries the External
    // and Perimeter modifiers, so it prints with external-perimeter speeds, fans
    // and flow; the distinct role only gives it its own G-code type and color.
    ExtrusionAttributes base_attrs(ExtrusionRole::Serpentine, params.flow);

    std::vector<Polygon> loops_src;
    loops_src.push_back(work_island.contour);
    for (const Polygon &h : work_island.holes)
        loops_src.push_back(h);

    // The bead curve(s) the clipped teeth join, for contouring the U-turn caps
    // along the boundary instead of capping them square. Depth mode uses the fill
    // core; ring mode uses the bore tip line (the hole set half a bead into the
    // material, where the converging teeth stop) so a cornered bore's caps follow
    // its corners instead of a square chord that crosses the bore and is dropped.
    std::vector<Loop> core_loops;
    if (params.depth_clamp > 0.)
        for (const ExPolygon &ep : params.fill_core)
        {
            core_loops.push_back(make_loop(ep.contour));
            for (const Polygon &h : ep.holes)
                core_loops.push_back(make_loop(h));
        }
    else if (anchor.ring_mode && !island.holes.empty())
        for (const ExPolygon &ep : offset_ex(island, -(float) (0.5 * bw)))
            for (const Polygon &h : ep.holes)
                core_loops.push_back(make_loop(h));

    // Depth hand-off: the smooth tooth-tip line per loop, built from each tooth's s.max_t (the
    // rc/2 target depth, not the braked s.depth, which dips into the shallow-tooth voids and
    // re-admits the inter-tooth gaps). process_athena diffs these to hand Athena only the
    // interior beyond the tips, never the gaps. Built only when the caller passes out_interior.
    Polygon contour_tip_ring;
    Polygons bore_tip_rings;

    for (size_t loop_idx = 0; loop_idx < loops_src.size(); ++loop_idx)
    {
        const bool is_contour = (loop_idx == 0);
        // Depth mode: a loop aims perpendicular (at the nearest inner-boundary point)
        // when it's a bore (the convergent anchor is the wrong side of a hole) OR the
        // user chose Perpendicular aim. Otherwise (Convergent, outer contour) it aims
        // at the anchor. Full mode is unaffected.
        const bool depth_perp = params.depth_clamp > 0. && (!is_contour || params.aim != 0);
        Loop lp = make_loop(loops_src[loop_idx]);

        // Band mode only: the loop's genuine corners (from the denoised contour)
        // drive both the projection aim near a corner and the depth clamp at its
        // bisector. A smooth curve yields none, so the band keeps curve behavior.
        // Depth mode does not use these: every tooth conforms to the fill line, so
        // perpendicular aim already fans teeth into a corner and the conform caps
        // them; a bisector clamp would only pull teeth back from a corner the
        // conform already fills.
        std::vector<BandCorner> band_corners;
        if (band > 0.)
            band_corners = detect_band_corners(loops_src[loop_idx], lp, bw);

        auto emit_plain_ring = [&]()
        {
            if (lp.pts.size() < 3)
                return;
            Polyline ring;
            for (const Vec2d &p : lp.pts)
                ring.append(to_point(p));
            ring.append(ring.first_point());
            ExtrusionMultiPath ring_mp;
            ExtrusionPath path(base_attrs);
            path.polyline = std::move(ring);
            ring_mp.paths.push_back(std::move(path));
            collection.append(std::move(ring_mp));
        };

        // Ring topology in full mode: bores carry no slits (a fan projecting
        // outward from a hole diverges to the far wall) and no boundary bead
        // either; the bore surface is the chain of the contour fan's U-turn caps.
        // In depth mode the contour fan is clipped short of the bore, so the bore
        // grows its own shallow outward band (the threads): divergence over the
        // limited depth is bounded, and the conform caps it at the depth line.
        if (anchor.ring_mode && !is_contour && params.depth_clamp == 0.)
        {
            dbg.line("bore loop=%zu: no bead (cap chain forms the bore)", loop_idx);
            continue;
        }

        // Relaxed mode: holes carry a plain single-bead loop only; ribs come from
        // the contour and weld into it. Sparse hole ribs would leave the bead with
        // long unbonded spans, and the aligned stacking keeps the plain loop a
        // continuous vertical wall. Face-layer band mode matches (the fill bonds
        // to the plain ring).
        if (relaxed_layout && !is_contour)
        {
            dbg.line("relaxed loop=%zu: hole prints as plain ring", loop_idx);
            emit_plain_ring();
            continue;
        }

        if (lp.total < 8.0 * pitch_t)
        {
            if (is_contour)
            {
                dbg.line("REJECT island: contour too short for slits (%.2fmm)", unscale<double>(lp.total));
                return false;
            }
            dbg.line("FALLBACK loop=%zu: hole too short for slits, plain ring", loop_idx);
            emit_plain_ring();
            continue;
        }

        // The grid anchors to a stable geometric reference (the max-X extreme,
        // ties broken by Y), not to the slice polygon's arbitrary start
        // vertex: layers whose outlines or vertex orders differ slightly
        // (first layer, solid shells, chamfers) still place their mouths in
        // the same physical spots, so the side pattern stays coherent.
        size_t ref_i = 0;
        for (size_t i = 1; i < lp.pts.size(); ++i)
            if (lp.pts[i].x() > lp.pts[ref_i].x() + 1.0 ||
                (std::abs(lp.pts[i].x() - lp.pts[ref_i].x()) <= 1.0 && lp.pts[i].y() < lp.pts[ref_i].y()))
                ref_i = i;
        // Smooth the extreme: the single argmax vertex rides the layer's
        // tessellation and wanders a vertex spacing or more per layer on a
        // smooth or inset-rounded extreme, and every rib column, the seam, and
        // the outer-loop host anchor to it. Anchor instead to the arc midpoint
        // of the boundary band within half a bead of max X, with the band ends
        // interpolated on their crossing edges so they move only with the
        // geometry. On a flat max-X edge (a square) the midpoint lands
        // deterministically mid-edge.
        const double ref_thr = lp.pts[ref_i].x() - 0.5 * bw;
        const size_t np_ref = lp.pts.size();
        auto band_cross = [&](int dir) -> double
        {
            size_t i = ref_i;
            for (size_t step = 0; step + 1 < np_ref; ++step)
            {
                const size_t j = (i + (dir > 0 ? 1 : np_ref - 1)) % np_ref;
                if (lp.pts[j].x() < ref_thr)
                {
                    const double xi = lp.pts[i].x();
                    const double f = (xi - ref_thr) / (xi - lp.pts[j].x());
                    const double seg = dir > 0 ? wrap_arc(lp, lp.cum[j] - lp.cum[i])
                                               : wrap_arc(lp, lp.cum[i] - lp.cum[j]);
                    return wrap_arc(lp, lp.cum[i] + (double) dir * seg * f);
                }
                i = j;
            }
            return lp.cum[ref_i]; // whole loop inside the band: a sliver, anchor at the argmax
        };
        const double cr_a = band_cross(-1);
        const double cr_b = band_cross(+1);
        const double ref_arc = wrap_arc(lp, cr_a + 0.5 * wrap_arc(lp, cr_b - cr_a));

        // Always aim straight at the anchor: the collision braking relies on
        // a convergent direction field, skewed projections evade it.
        auto dir_at = [&](const Vec2d &pos, const Vec2d &tangent, double arc, double &d_anchor) -> Vec2d
        {
            Vec2d a = anchor.nearest(pos);
            d_anchor = (a - pos).norm();
            // Perpendicular aim only: aim at the nearest point on the smooth inner
            // boundary (the offset-inward curve), so the aim follows the overall shape,
            // not the local outer serrations. Each tooth points square to the wall at
            // its corresponding inner point, best on curved/irregular parts. Into-material
            // falls out automatically (the nearest point is inward for the outer band,
            // outward for a bore). An empty core (thin wall, offset collapsed) falls
            // through to the smoothed normal. Convergent is excluded here so its bore
            // keeps the smoothed-normal aim.
            if (depth_perp && params.aim != 0 && !core_loops.empty())
            {
                Vec2d target(0., 0.);
                double bestd = std::numeric_limits<double>::max();
                for (const Loop &cl : core_loops)
                {
                    double aarc = loop_nearest_arc(cl, pos);
                    Vec2d p = loop_point_at(cl, aarc);
                    double d2 = (p - pos).squaredNorm();
                    if (d2 < bestd)
                    {
                        bestd = d2;
                        target = p;
                    }
                }
                Vec2d v = target - pos;
                double n = v.norm();
                if (n > 1e-6)
                    return Vec2d(v / n);
            }
            // Band mode: window-smoothed inward normal. The band never
            // resolves depth by braking, so it does not need the convergent
            // anchor field; the smoothed normal is exactly radial on arcs,
            // rotates gradually through corners, and is immune to spine
            // fragment noise on ring islands. Spine proximity must not drop
            // band mouths either (a missing tooth breaks the wall pattern):
            // the raycast and the clamp are the band's real depth bounds.
            // Perpendicular depth loops use this same into-material aim: each tooth aims
            // straight at its corresponding point on the inner boundary, so teeth stay
            // square to the surface on non-round shapes instead of skewing toward the
            // centroid. (The into-material normal points inward on a CCW contour and
            // outward on a CW hole, so it serves the outer band and the bore alike.)
            if (band > 0. || depth_perp)
            {
                const Vec2d local(-tangent.y(), tangent.x());
                // Smoothed inward normal over +/-2 beads: denoises tessellation
                // and slice wiggle on arcs and straight walls alike.
                Vec2d nsum(0., 0.);
                for (int k = -2; k <= 2; ++k)
                {
                    Vec2d tk = loop_tangent_at(lp, arc + (double) k * bw);
                    nsum += Vec2d(-tk.y(), tk.x());
                }
                const double nn = nsum.norm();
                const Vec2d smoothed = nn > 0.1 ? Vec2d(nsum / nn) : local;

                // Within 2.5 beads of a genuine (denoised) corner the smoothing
                // window straddles the vertex and the smoothed normal leans
                // toward the bisector, which would make the slit wander
                // diagonally into the part and overlap its neighbors. Aim along
                // that corner's own-side edge normal instead, so the slit
                // projects straight in and the bisector clamp can cut it cleanly.
                // Beyond 2.5 beads the window no longer straddles the vertex, so
                // the smoothed normal already equals the edge perpendicular and
                // the aim is continuous across the boundary. Arcs have no corners,
                // so they keep the smoothed radial aim.
                const BandCorner *nearest = nullptr;
                double nearest_da = 0.;
                for (const BandCorner &c : band_corners)
                {
                    const double da = wrapped_signed_arc(lp, arc - c.arc);
                    if (std::abs(da) <= 2.5 * bw && (nearest == nullptr || std::abs(da) < std::abs(nearest_da)))
                    {
                        nearest = &c;
                        nearest_da = da;
                    }
                }
                if (nearest != nullptr)
                    return nearest_da < 0. ? nearest->n_in : nearest->n_out;
                return smoothed;
            }
            if (d_anchor < 1.5 * bw)
                return Vec2d(0., 0.); // mouth sits essentially on the spine
            Vec2d dir0 = (a - pos) / d_anchor;
            Vec2d inward(-tangent.y(), tangent.x());
            if (dir0.dot(inward) < 0.05)
                return Vec2d(0., 0.); // the anchor is not inward from here
            return dir0;
        };

        // Relaxed mode: count = loop length over the user pitch, rounded, floored
        // at two ribs. Placement is fixed pitch in two arms from the reference,
        // with the whole residue in one 0.5x-1.5x cell where the arms meet,
        // opposite the reference. Every rib column sits at a constant distance
        // from the reference, so taper never moves a column and a count change
        // only starts or stops the single antipodal rib; dividing the loop
        // evenly instead re-spaces every column whenever the count changes.
        size_t n;
        bool count_fallback = false;
        bool count_floored = false;
        std::vector<double> rib_arcs;
        // Outer-loop host rib: the seam rib widened to carry the excursion to the
        // external loop (see the selection below).
        size_t wide_idx = std::numeric_limits<size_t>::max();
        if (relaxed_layout)
        {
            const long long n_ideal = std::llround(lp.total / pitch_rx);
            count_floored = n_ideal < 2;
            n = (size_t) std::max<long long>(2, n_ideal);
            rib_arcs.reserve(n);
            if (count_floored)
            {
                // Forced to two ribs on a loop under 1.5 pitches: fixed pitch
                // would cram them into one end; split the loop evenly.
                rib_arcs.push_back(wrap_arc(lp, ref_arc));
                rib_arcs.push_back(wrap_arc(lp, ref_arc + 0.5 * lp.total));
            }
            else
            {
                const size_t n_ccw = (n + 1) / 2; // rib 0 plus the counterclockwise arm
                for (size_t k = 0; k < n_ccw; ++k)
                    rib_arcs.push_back(wrap_arc(lp, ref_arc + (double) k * pitch_rx));
                for (size_t j = n - n_ccw; j >= 1; --j)
                    rib_arcs.push_back(wrap_arc(lp, ref_arc + lp.total - (double) j * pitch_rx));
            }
            // Do not sort: the array is already in cyclic ring order (ascending
            // from the reference, wrapping once), which is all the braking and
            // walk logic need. Sorting by arc value would re-index the ribs to
            // the slice polygon's arbitrary start vertex, anchoring the level
            // pattern and the host to a point that moves layer to layer.
            // Index 0 is the rib at the reference.
        }
        else
        {
            n = pick_slit_count(lp.total, pitch_t);
            count_fallback = (n == 0);
            if (count_fallback)
                n = std::max<size_t>(8, (size_t) std::llround(lp.total / pitch_t));
        }
        const double pitch = lp.total / (double) n;
        // Furthest a cap follows the bore tip line; the wedge clamps to it so a
        // widened cap never reaches past what the cap-walk can trace.
        const double walk_limit = 4.0 * pitch;

        // Per-layer phase of the whole pattern: aligned ridges stack the
        // mouths vertically; staggered shifts half a tooth each layer so
        // every cap is bridged by solid tooth above and below (brickwork);
        // random scatters them, mixed with the reference vertex so loops and
        // objects across the plate decorrelate while reslicing stays
        // reproducible.
        double phase_off;
        if (relaxed_layout || have_outer)
            // Relaxed: ribs must stack into continuous vertical webs - at
            // multi-mm pitch a staggered or random rib lands over the previous
            // layer's open cell with nothing beneath it. Outer loop: the whole
            // grid re-anchors to the host rib, so a moving phase moves the host
            // and the seam column with it - the butt seam must stack into one
            // vertical column and the ribbing align beneath the wrapped
            // surface. Aligned always in both.
            phase_off = 0.;
        else
            switch (params.phase_mode)
            {
            case 0:
                phase_off = 0.;
                break;
            case 2:
            {
                uint32_t h = (uint32_t) params.layer_id * 2654435761u;
                h ^= (uint32_t) (int64_t) std::llround(lp.pts[ref_i].x());
                h *= 2246822519u;
                h ^= (uint32_t) (int64_t) std::llround(lp.pts[ref_i].y());
                h *= 3266489917u;
                h ^= h >> 16;
                phase_off = (double) (h & 0xFFFFFF) / (double) 0x1000000 * lp.total;
                break;
            }
            default:
                phase_off = (params.layer_id & 1) ? 0.5 * pitch : 0.;
                break;
            }
        const double phase = wrap_arc(lp, ref_arc + phase_off);

        if (relaxed_layout)
        {
            const double res_cell = count_floored ? 0.5 * lp.total : lp.total - (double) (n - 1) * pitch_rx;
            dbg.line("loop=%zu %s len=%.2fmm relaxed n=%zu%s pitch=%.3fmm clear_run=%.3fmm res_cell=%.2fmm (%.2fx) "
                     "hub_r=%.2fmm",
                     loop_idx, is_contour ? "contour" : "hole", unscale<double>(lp.total), n,
                     count_floored ? " count-floored" : "", unscale<double>(pitch_rx),
                     unscale<double>(pitch_rx - (bw - ov)), unscale<double>(res_cell), res_cell / pitch_rx,
                     unscale<double>(center_loop ? r_loop : 0.));
        }
        else
        {
            int kq = 0;
            while (((n >> kq) & 1) == 0)
                ++kq;
            dbg.line("loop=%zu %s len=%.2fmm n=%zu (q=%zu k=%d)%s pitch=%.3fmm (%+.1f%% of nominal) phase=%.2fmm "
                     "corners=%zu",
                     loop_idx, is_contour ? "contour" : "hole", unscale<double>(lp.total), n, n >> kq, kq,
                     count_fallback ? " count-fallback" : "", unscale<double>(pitch), 100.0 * (pitch / pitch_t - 1.0),
                     unscale<double>(phase), band_corners.size());
        }
        std::vector<Slit> slits(n);
        // The full cascade always runs: it is what lets deep teeth penetrate (a
        // deep tooth brakes against its already-resolved shallower neighbors),
        // while a uniform comb collapses to the first convergence depth and
        // leaves a moat around the ring. Sparse ribs converge exactly like
        // classic: a rib reverses when it would touch its resolved deeper
        // neighbor, and the deepest reach the center ring.
        std::vector<double> arcs(n);
        for (size_t k = 0; k < n; ++k)
            arcs[k] = relaxed_layout ? rib_arcs[k] : wrap_arc(lp, phase + (double) k * pitch);
        size_t n_degenerate = 0, n_crowded = 0, n_floored = 0, n_truncated = 0;
        // The layout runs as a function of the arc grid so the outer-loop host
        // can re-space the WHOLE grid around itself and lay out again.
        auto run_layout = [&](const std::vector<double> &arc_in)
        {
            slits.assign(n, Slit());
            assign_ring_levels(slits, relaxed_layout);
            n_degenerate = n_crowded = n_floored = n_truncated = 0;
            for (size_t k = 0; k < n; ++k)
            {
                Slit &s = slits[k];
                s.arc = arc_in[k];
                s.pos = loop_point_at(lp, s.arc);
                s.tangent = loop_tangent_at(lp, s.arc);
                double d_anchor = 0.;
                s.dir = dir_at(s.pos, s.tangent, s.arc, d_anchor);
                if (s.dir.squaredNorm() < 0.5)
                {
                    ++n_degenerate;
                    continue;
                }

                double sin_psi = std::max(std::abs(s.tangent.x() * s.dir.y() - s.tangent.y() * s.dir.x()), 0.4);

                // An oblique mouth has only pitch*sin(psi) of lateral room: split
                // the deficit evenly between the pair and the neighbor contacts.
                // At extreme obliquity the level-1 slits yield and the survivors
                // use the doubled room.
                // Band mode keeps every mouth: its teeth are uniformly shallow,
                // so the obliquity yield would only hole the solid-surface wall.
                // Relaxed mode keeps every mouth too: nothing crowds at multi-mm
                // pitch, and a dropped rib leaves a spacing-sized hole in the fill.
                double local_pitch = pitch;
                if (band == 0. && !relaxed && sin_psi < 0.55)
                {
                    if (s.level == 1)
                    {
                        s.dir = Vec2d(0., 0.); // crowded out: the boundary walk bridges this mouth
                        ++n_crowded;
                        continue;
                    }
                    local_pitch = 2.0 * pitch;
                }
                // Units are born with one overlap of clearance and brake at one
                // overlap of contact, transitioning from clear to bonded as the
                // rays converge.
                // Bond mode: the pitch budget splits between the pair and the
                // neighbor contacts, born one overlap clear of the brake
                // distance. Spread mode: the pair separation cannot change after
                // birth (its beads are parallel), so it is born at its final
                // value, bw + gap between centerlines; the inter-unit contact
                // then also lands at bw + gap from the doubled pitch budget.
                // Relaxed mode: the pitch budget is meaningless (the pitch is the
                // user spacing), so the pair is always born at its final bonded
                // separation, bw - ov between centerlines - one solid rib.
                if (relaxed_layout || ov < 0.)
                    s.m_off = std::clamp((bw - ov) / (2.0 * sin_psi), 0.15 * bw, 0.6 * pitch);
                else
                    s.m_off = std::clamp(local_pitch * 0.25 - ov / (2.0 * sin_psi), 0.15 * bw, 0.6 * pitch);
                s.off = s.m_off * sin_psi;

                // The band's depth is bounded by the raycast and the clamp; the
                // anchor standoff would only inject spine noise into it. Perpendicular depth
                // teeth aim into material (not at the anchor), so the convergent standoff
                // limit is meaningless there too; the conform bounds the depth instead.
                if (band > 0. || depth_perp)
                    s.max_t = std::numeric_limits<double>::max();
                else
                {
                    const double tip_standoff = center_loop   ? r_loop + bw - tip_bond
                                                : spine_track ? r_track + bw - tip_bond
                                                              : 0.5 * (bw - tip_bond);
                    s.max_t = ray_anchor_limit(anchor, s.pos, s.dir, tip_standoff, d_anchor * 1.5 + 2.0 * bw);
                }

                double rc = raycast_depth(work_island, to_point(s.pos), s.dir, 10.0 * SCALED_EPSILON);
                if (rc > 0.)
                {
                    if (band > 0.)
                        // Band mode: stop at the wall's medial axis (half the
                        // raycast, less a half-bond) so the band on the far wall
                        // fills the other half and the two meet at the centerline
                        // instead of overlapping. On a thick part the far wall is
                        // beyond 2*band_clamp, so the band_clamp cap below governs and
                        // the interior hands off to fill.
                        s.max_t = std::min(s.max_t, 0.5 * (rc - (bw - tip_bond)));
                    else if (params.depth_clamp > 0.)
                        // Depth mode "as great as possible": never cross past the midplane of a
                        // two-sided wall (around a hole, or a thin section), and leave exactly the
                        // two overlapping Athena beads (2*bw - ov) that bond the two tooth sets in
                        // the center - any wider and the residual between them falls to solid infill.
                        // Both sides subtract this standoff, so it is one bead's room per side, not
                        // the full pair: tooth coverage reaches bw/2 past each cap, so a (1.5*bw - ov)
                        // standoff leaves an uncovered core of 2*bw - 2*ov between the facing caps,
                        // which the overlap-grown wall seats as two beads overlapping by ov. The
                        // conform's serp_depth (below) governs where the wall is thick enough; this
                        // binds only where it is too thin for full depth. On a solid/thick part the
                        // half-span exceeds serp_depth, so this never binds. Variable per tooth.
                        s.max_t = std::min(s.max_t, 0.5 * rc - (1.5 * bw - ov));
                    else
                    {
                        // Full ring mode: the caps are the bore surface. The tip rides a
                        // half bead short of the bore less the overlap, so the cap bead
                        // laps the bore by ov (the serpentine overlap) and seals it.
                        // Otherwise weld into the boundary bead on the far wall.
                        const double far_margin = anchor.ring_mode ? 0.5 * bw : (bw - tip_bond);
                        s.max_t = std::min(s.max_t, rc - far_margin);
                    }
                }
                // The clamp bounds every cap to band_clamp of ray depth, keeping it
                // within band_clamp of the boundary. On straight walls and arcs the
                // smoothed normal is perpendicular, so this is the exact
                // perpendicular band depth and a straight fill inset seals against
                // all of them.
                if (band > 0.)
                {
                    s.max_t = std::min(s.max_t, band);
                    // Shell islands (a frame, a hollow part) carry a real wall medial
                    // axis as their anchor spine. Clamp each tooth at it so opposing
                    // faces meet at the wall midpoint exactly as on the straight runs,
                    // the spine turning each corner (its diagonal spoke) the same way,
                    // instead of a convex miter that runs the outer teeth all the way
                    // to the inner corner. On a thick part the spine lies beyond
                    // band_clamp, so this never binds.
                    if (!anchor.is_point)
                        s.max_t = std::min(s.max_t, ray_anchor_limit(anchor, s.pos, s.dir, 0.5 * (bw - tip_bond),
                                                                     d_anchor * 1.5 + 2.0 * bw));
                    // At each genuine corner the band is divided by the corner's
                    // bisector: this slit owns its own side and must stop there so it
                    // does not overlap the adjacent edge's teeth. The perpendicular
                    // distance from the ray to the bisector line is linear in t, so
                    // the crossing is closed-form. Each side stops half a contact
                    // short (margin) so the facing caps meet at exactly the overlap.
                    // A slit whose mouth is already on the divide (the would-be
                    // diagonal spoke) keeps only a notch.
                    const double margin = 0.5 * (bw - ov);
                    for (const BandCorner &c : band_corners)
                    {
                        const double da = wrapped_signed_arc(lp, s.arc - c.arc);
                        if (std::abs(da) > 2.0 * band + 2.0 * bw)
                            continue; // out of this corner's reach
                        // Unit normal of the bisector line, oriented toward the
                        // mouth's own side (positive d0).
                        Vec2d nrm(-c.bisector.y(), c.bisector.x());
                        double d0 = (s.pos - c.apex).dot(nrm);
                        if (d0 < 0.)
                        {
                            nrm = -nrm;
                            d0 = -d0;
                        }
                        const double slope = s.dir.dot(nrm); // d(t) = d0 + t * slope
                        if (d0 <= margin)
                            s.max_t = std::min(s.max_t, 0.6 * bw);
                        else if (slope < -1e-9)
                            s.max_t = std::min(s.max_t, std::max(0.6 * bw, (d0 - margin) / -slope));
                    }
                    if (s.max_t < band - 1e-6)
                        ++n_truncated;
                }
                if (s.max_t < 0.)
                    s.max_t = 0.;
                // Depth-limit: conform each tooth to the depth line where Athena lays
                // the bead, so the tip welds into the bead regardless of contour shape.
                // A fixed scalar depth misses the smooth bead on a wavy contour (the
                // knurl gap); the raycast to the fill core gives the exact stop. The
                // scalar depth_clamp is the fallback for a ray that misses the core.
                if (params.depth_clamp > 0.)
                {
                    double cap_t = -1.;
                    if (!params.fill_core.empty())
                    {
                        double rc_core = raycast_to_regions(params.fill_core, to_point(s.pos), s.dir,
                                                            10.0 * SCALED_EPSILON);
                        if (rc_core > 0.)
                            cap_t = rc_core;
                    }
                    if (cap_t < 0.)
                        cap_t = (double) params.depth_clamp;
                    if (s.max_t > cap_t)
                        s.max_t = cap_t;
                }
            }
        };
        run_layout(arcs);

        // Outer-loop host: the excursion lives inside ONE widened rib (the seam
        // rib) - its legs part by two extra spacings and the two excursion beads
        // fill the slot, four bonded parallel beads, nothing crossing. Chosen
        // only now, with directions and depth budgets known; a braked-short host
        // apexes, an apexed rib cannot host, and the excursion strands in the
        // standalone fallback.
        if (have_outer && is_contour)
        {
            // Candidates are ordered by level, then forward arc distance from
            // the reference. Highest level first: the deepest structural ribs
            // resolve against only their distant peers, reach the anchor
            // without an apex, and form a small fixed set that keeps the
            // choice layer-stable. Forward distance, not absolute: two
            // candidates straddling the reference near-equidistant would
            // otherwise swap sides with any half-pitch grid shift.
            int best_level = -1;
            double best = std::numeric_limits<double>::max();
            size_t n_host_corner = 0;
            for (size_t k = 0; k < n; ++k)
            {
                const Slit &s = slits[k];
                if (s.level < best_level)
                    continue;
                if (s.dir.squaredNorm() < 0.5)
                    continue;
                // No depth requirement: the turnaround and lane separation come
                // from the slot's width, a full-run host's V flattens to a
                // lateral jog at the cap, and a truly degenerate slot fails its
                // seam-half clipping into the standalone fallback on its own.
                // The widened mouth must sit on a locally straight stretch: the
                // slot lattice is built on perpendicular offsets of the center
                // ray, and a mouth that wraps a corner anchors its legs on two
                // different edges, laying the lanes over the neighboring ribs
                // and leaving the corner unfilled. Compare tangents across the
                // widened span (ends and center, so a corner cannot hide
                // mid-span); a corner turns them far more than surface
                // curvature does at this scale.
                const Vec2d tan_l = loop_tangent_at(lp, wrap_arc(lp, s.arc - 3.0 * s.m_off - 0.5 * bw));
                const Vec2d tan_r = loop_tangent_at(lp, wrap_arc(lp, s.arc + 3.0 * s.m_off + 0.5 * bw));
                if (std::min({tan_l.dot(tan_r), tan_l.dot(s.tangent), s.tangent.dot(tan_r)}) < 0.75)
                {
                    ++n_host_corner;
                    continue;
                }
                const double d = wrap_arc(lp, s.arc - ref_arc);
                if (s.level > best_level || d < best)
                {
                    best_level = s.level;
                    best = d;
                    wide_idx = k;
                }
            }
            if (n_host_corner > 0)
                dbg.line("outer loop: %zu corner-spanning host candidate(s) skipped", n_host_corner);
            if (wide_idx != std::numeric_limits<size_t>::max())
            {
                // The widened mouth needs two extra mouth-widths of arc. At
                // CLASSIC pitch the whole grid re-spaces around it: the host
                // stays pinned at its arc (re-indexed to 0, a top level by
                // construction) and the other mouths distribute evenly over the
                // loop minus the extra, so every cell - including the two
                // beside the host - is exactly nominal. Dropping the neighbors
                // instead leaves double-wide cells; keeping them in place
                // births overlapping beads. At RELAXED pitch there is NO
                // re-spacing: the fixed-pitch columns must not move, and the
                // widened mouth just eats two mouth-widths from each flanking
                // clear run - negligible against a multi-mm cell.
                if (!relaxed_layout)
                {
                    const double host_arc = slits[wide_idx].arc;
                    const double host_extra = 4.0 * slits[wide_idx].m_off;
                    if (lp.total > host_extra + (double) n * 0.5 * pitch)
                    {
                        const double p2 = (lp.total - host_extra) / (double) n;
                        arcs[0] = wrap_arc(lp, host_arc);
                        for (size_t k2 = 1; k2 < n; ++k2)
                            arcs[k2] = wrap_arc(lp, host_arc + 0.5 * host_extra + p2 * (double) k2);
                        wide_idx = 0;
                        run_layout(arcs);
                    }
                }
                slits[wide_idx].m_off *= 3.0;
                slits[wide_idx].off *= 3.0;
                // Log the host POSITION, not just its arc: the arc coordinate is
                // measured from the slice polygon's arbitrary start vertex, so
                // it cannot distinguish a drifting host from a drifting
                // parametrization across layers - only XY can.
                dbg.line("outer loop: host at (%.2f,%.2f)mm arc=%.2fmm (%.2fmm from ref) sin_psi=%.2f",
                         unscale<double>(slits[wide_idx].pos.x()), unscale<double>(slits[wide_idx].pos.y()),
                         unscale<double>(slits[wide_idx].arc), unscale<double>(best),
                         std::abs(slits[wide_idx].tangent.x() * slits[wide_idx].dir.y() -
                                  slits[wide_idx].tangent.y() * slits[wide_idx].dir.x()));
                // Safety net for genuine pathology only: a mouth interpenetrating
                // the host's widened mouth (edge gap below half a bonded contact)
                // yields and the walk bridges it. The re-spaced grid guarantees
                // NOMINAL gaps by construction, and a nominal-strictness test
                // here mislabels the standard spacing itself - it yielded the
                // host's healthy neighbors and merged each flanking pair into
                // one wide tooth.
                const Slit &w = slits[wide_idx];
                for (size_t k2 = 0; k2 < n; ++k2)
                {
                    if (k2 == wide_idx)
                        continue;
                    Slit &s2 = slits[k2];
                    if (s2.dir.squaredNorm() < 0.5)
                        continue;
                    const double da = std::abs(wrapped_signed_arc(lp, s2.arc - w.arc));
                    if (da - w.m_off - s2.m_off < 0.5 * (bw - ov))
                    {
                        s2.dir = Vec2d(0., 0.);
                        ++n_crowded;
                    }
                }
            }
            else
                dbg.line("outer loop: no viable host rib on this loop (%zu corner-spanning, all others degenerate)",
                         n_host_corner);
        }

        // Depth-mode conform backstop. Each tooth's max_t was just capped by a raycast into fill_core;
        // where a convergent ray grazes fill_core's boundary at near-tangent incidence that distance is
        // unstable, so a tooth can overshoot far past its ring neighbours and lap into the anchor bead or
        // opposing fan. Pull such an outlier down to the median of its nearest live neighbours. The repair
        // only shortens, never lengthens: the raycast cap keeps a tooth out of fill_core, and raising
        // max_t back above it would let a tooth cross into the reserved core. A real depth gradient changes
        // far less than the threshold per tooth, so a smooth layer is untouched; the median over +/-2 live
        // neighbours absorbs a two-wide overshoot run.
        size_t n_conform_repaired = 0;
        if (params.depth_clamp > 0.)
        {
            std::vector<size_t> live;
            live.reserve(n);
            for (size_t k = 0; k < n; ++k)
                if (slits[k].dir.squaredNorm() > 0.5 && slits[k].max_t > 0.)
                    live.push_back(k);
            const size_t L = live.size();
            if (L >= 5)
            {
                const double repair_thr = 1.5 * bw; // the one knob: > any real per-tooth step, < a grazing spike
                std::vector<double> orig(L);
                for (size_t j = 0; j < L; ++j)
                    orig[j] = slits[live[j]].max_t;
                for (size_t j = 0; j < L; ++j)
                {
                    double win[5] = {orig[(j + L - 2) % L], orig[(j + L - 1) % L], orig[j], orig[(j + 1) % L],
                                     orig[(j + 2) % L]};
                    std::sort(win, win + 5);
                    if (orig[j] - win[2] > repair_thr) // only shorten an overshoot; never raise past the cap
                    {
                        slits[live[j]].max_t = win[2];
                        ++n_conform_repaired;
                    }
                }
            }
        }

        // Resolve depths deepest level first, so shallower slits brake
        // against the resolved extent of their deeper competitors.
        std::vector<size_t> order(n);
        for (size_t k = 0; k < n; ++k)
            order[k] = k;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
                  { return slits[a].level != slits[b].level ? slits[a].level > slits[b].level : a < b; });

        for (size_t idx : order)
        {
            Slit &s = slits[idx];
            double t = s.max_t;
            if (t <= 0. || s.dir.squaredNorm() < 0.5)
            {
                s.depth = 0.;
                continue;
            }
            for (int side = -1; side <= 1 && t > 0.; side += 2)
            {
                // Nearest strictly-deeper neighbor: brake against its resolved
                // segment. Nearest equal-level neighbor: its full ray
                // (unresolved, so symmetric and conservative).
                size_t f_deep = n, f_eq = n;
                size_t j = idx;
                for (size_t step = 1; step < n; ++step)
                {
                    j = (side > 0) ? (j + 1) % n : (j + n - 1) % n;
                    int lv_j = slits[j].level;
                    if (lv_j > s.level)
                    {
                        f_deep = j;
                        break;
                    }
                    if (lv_j == s.level && f_eq == n)
                        f_eq = j;
                }
                for (int which = 0; which < 2; ++which)
                {
                    size_t found = (which == 0) ? f_deep : f_eq;
                    if (found == n)
                        continue;
                    const Slit &c = slits[found];
                    if (c.dir.squaredNorm() < 0.5)
                        continue;
                    if (s.dir.dot(c.dir) < -0.5)
                        continue; // opposing projection: the anchor standoff governs
                    double ext = (which == 0) ? c.depth : c.max_t;
                    if (ext <= 0.)
                        continue;
                    if (which == 0)
                        (side < 0 ? s.comp_l : s.comp_r) = found;
                    Vec2d seg_a = c.pos;
                    Vec2d seg_b = c.pos + c.dir * ext;
                    // Brake where the near beads would exceed the configured
                    // overlap; units born tighter (oblique corners) keep their
                    // birth overlap but may not deepen it.
                    const double s_nom = (bw - ov) + s.off + c.off;
                    const double d0 = dist_point_segment(s.pos, seg_a, seg_b);
                    const double s_req = std::min(s_nom, 0.97 * d0);
                    t = advance_while(t, [&](double tt)
                                      { return dist_point_segment(s.pos + s.dir * tt, seg_a, seg_b) >= s_req; });
                }
            }
            // A crowded mouth keeps a short notch so the boundary band stays
            // continuous through oblique stretches.
            const double floor_d = std::min(0.6 * bw, s.max_t);
            if (t < floor_d)
                ++n_floored;
            s.depth = std::max(t, floor_d);
        }

        // Neighbors braking within a bead of each other snap to the shallower
        // depth so their caps bond end to end instead of leaving micro-wedges.
        // Snapping only reduces depth, so it cannot violate any constraint.
        size_t n_depthsnapped = 0;
        auto snap_pair = [&](Slit &a, Slit &b)
        {
            if (a.depth < 0.5 * bw || b.depth < 0.5 * bw)
                return;
            // Caps at their full run follow a surface (bore, far wall): their
            // depth is dictated by the geometry, not by braking, and snapping
            // would quantize a smooth profile (thread ramps) into steps.
            if (a.depth >= a.max_t - 0.1 * bw || b.depth >= b.max_t - 0.1 * bw)
                return;
            double lo = std::min(a.depth, b.depth);
            if (std::max(a.depth, b.depth) - lo < 0.8 * bw)
            {
                if (a.depth != b.depth)
                    ++n_depthsnapped;
                a.depth = b.depth = lo;
            }
        };
        // Band mode places caps at equal perpendicular depth, which means
        // unequal ray depths: snapping ray depths would drag oblique slits
        // off the uniform line, so the band skips it.
        if (band == 0.)
        {
            for (size_t k = 0; k < n; ++k)
                snap_pair(slits[k], slits[(k + 1) % n]);
            for (size_t k = n; k-- > 0;)
                snap_pair(slits[k], slits[(k + 1) % n]);
        }

        // Tip line for this loop: each tooth's cap target (s.max_t) in arc order. process_athena
        // uses it as a mask (a half-bead outside the real uncovered center) to clip the
        // inter-tooth fingers from the opened core, so the wall stays smooth and the fingers
        // (all shallower than the tip line) are excluded. Holed islands only: the solid depth
        // path uses the depth-band cut, never this ring, so building it for a solid island is
        // wasted work on an un-normalized ring.
        if (out_interior && !island.holes.empty())
        {
            Polygon ring;
            ring.points.reserve(n);
            for (size_t k = 0; k < n; ++k)
            {
                const Slit &s = slits[k];
                if (s.dir.squaredNorm() < 0.5 || s.max_t <= 0.)
                    continue;
                ring.points.push_back(to_point(s.pos + s.dir * s.max_t));
            }
            if (ring.points.size() >= 3)
            {
                if (is_contour)
                    contour_tip_ring = std::move(ring);
                else
                    bore_tip_rings.push_back(std::move(ring));
            }
        }

        if (Slic3r::debug_enabled(Slic3r::DBG_SERPENTINE))
        {
            double dmin = std::numeric_limits<double>::max(), dmax = 0., dsum = 0.;
            size_t live = 0;
            size_t level_hist[24] = {};
            for (const Slit &s : slits)
                if (s.depth > 0.5 * bw)
                {
                    dmin = std::min(dmin, s.depth);
                    dmax = std::max(dmax, s.depth);
                    dsum += s.depth;
                    ++live;
                    if (s.level >= 1 && s.level < 24)
                        ++level_hist[s.level];
                }
            dbg.line("slits: live=%zu/%zu degenerate=%zu crowded=%zu floored=%zu truncated=%zu snapped=%zu "
                     "conform_repaired=%zu depth[min/avg/max]=%.2f/%.2f/%.2fmm",
                     live, n, n_degenerate, n_crowded, n_floored, n_truncated, n_depthsnapped, n_conform_repaired,
                     live ? unscale<double>(dmin) : 0., live ? unscale<double>(dsum / (double) live) : 0.,
                     live ? unscale<double>(dmax) : 0.);
            std::string hist;
            char hb[32];
            for (int l = 1; l < 24; ++l)
                if (level_hist[l] > 0)
                {
                    snprintf(hb, sizeof(hb), " L%d x%zu", l, level_hist[l]);
                    hist += hb;
                }
            dbg.line("levels:%s", hist.c_str());
        }

        struct Mouth
        {
            size_t slit; // index into slits
            double arc_l, arc_r;
            Vec2d inner_l, inner_r;
            Vec2d apex;
            bool has_apex;
            double depth;
        };
        std::vector<Mouth> mouths;
        mouths.reserve(n);
        size_t n_apex = 0, n_hub = 0;
        const double hub_reach = (center_loop   ? r_loop + bw - tip_bond
                                  : spine_track ? r_track + bw - tip_bond
                                                : 0.5 * (bw - tip_bond)) +
                                 0.25 * bw;
        for (size_t self = 0; self < n; ++self)
        {
            const Slit &s = slits[self];
            if (s.depth < 0.5 * bw || s.dir.squaredNorm() < 0.5)
                continue;
            Mouth m;
            m.slit = self;
            m.arc_l = wrap_arc(lp, s.arc - s.m_off);
            m.arc_r = wrap_arc(lp, s.arc + s.m_off);
            // Depth mode brakes the deepest teeth a hair short of the conform
            // limit, so use a wider band to count them as boundary caps (and
            // contour them below) instead of letting them grow a pointed apex.
            const double frun_margin = params.depth_clamp > 0. ? boundary_margin : 0.1 * bw;
            if (s.depth >= s.max_t - frun_margin)
            {
                // Full-run caps follow a far surface (bore, far wall, hub
                // standoff): orient the cap perpendicular to the ray, which
                // is tangent to that surface, instead of parallel to the
                // mouth edge. Bore chains stay smooth regardless of the
                // outer shape.
                const Vec2d perp(-s.dir.y(), s.dir.x());
                const Vec2d cap_c = s.pos + s.dir * s.depth;
                const double sgn = (loop_point_at(lp, m.arc_l) - s.pos).dot(perp) >= 0. ? 1. : -1.;
                m.inner_l = cap_c + perp * (sgn * s.off);
                m.inner_r = cap_c - perp * (sgn * s.off);
            }
            else
            {
                m.inner_l = loop_point_at(lp, m.arc_l) + s.dir * s.depth;
                m.inner_r = loop_point_at(lp, m.arc_r) + s.dir * s.depth;
            }
            m.has_apex = false;
            m.depth = s.depth;

            // V apex: the pair stops where the pair no longer fits, but a
            // single tip bead can chase on until it welds into the first bead
            // it meets or arrives at the hub.
            // Interior teeth still apex; boundary teeth (within frun_margin of the
            // limit) are capped onto the boundary above and contoured below.
            // The excursion host never apexes: its U-turn needs a flat cap at
            // whatever depth it braked, and an apexed rib cannot host the seam -
            // in classic pitch nearly every rib apexes, which otherwise strands
            // the outer loop in the standalone fallback with the slot unfilled.
            // A slit that braked ON the host faces a leg one bonded spacing wider
            // than any normal competitor, so it stops both shallower and farther
            // out: the standard depth gate blunts the host's flanking ribs, and
            // the standard chase cap runs out before the apex reaches the leg,
            // leaving its tip floating beside the slot. Both bounds widen for
            // exactly those slits; the weld predicate still ends the chase.
            const bool by_host = wide_idx != std::numeric_limits<size_t>::max() &&
                                 (s.comp_l == wide_idx || s.comp_r == wide_idx);
            if (self != wide_idx && s.depth > (by_host ? 1.5 : 3.0) * bw && s.depth < s.max_t - frun_margin)
            {
                double t_apex = advance_while(s.max_t,
                                              [&](double tt)
                                              {
                                                  Vec2d p = s.pos + s.dir * tt;
                                                  for (size_t ci = 0; ci < n; ++ci)
                                                  {
                                                      if (ci == self)
                                                          continue;
                                                      const Slit &c = slits[ci];
                                                      if (c.dir.squaredNorm() < 0.5 || c.depth <= 0.)
                                                          continue;
                                                      if (dist_point_segment(p, c.pos, c.pos + c.dir * c.depth) <
                                                          bw + c.off - tip_bond)
                                                          return false;
                                                  }
                                                  return true;
                                              });
                // the V flanks double material over the extension: bound the chase.
                // Only DEEP host-braked slits get the extended chase (their weld
                // into the wide leg needs it); a shallow host-flanking rib keeps
                // the standard cap so its V matches its neighbors instead of
                // stretching into a needle along the slot.
                t_apex = std::min(t_apex, s.depth + ((by_host && s.depth > 3.0 * bw) ? 6.0 : 3.0) * bw);
                // a tip stalling within reach of the anchor finishes its run
                // and bonds at the hub to avoid an unfilled gap
                if (s.max_t - t_apex < 1.5 * bw && anchor.distance(s.pos + s.dir * s.max_t) < hub_reach)
                {
                    t_apex = s.max_t;
                    ++n_hub;
                }
                if (t_apex > s.depth + 0.35 * bw)
                {
                    m.apex = s.pos + s.dir * t_apex;
                    m.has_apex = true;
                    ++n_apex;
                }
            }
            mouths.push_back(m);
        }
        dbg.line("caps: apexes=%zu hub=%zu", n_apex, n_hub);

        if (mouths.empty())
        {
            if (is_contour)
            {
                dbg.line("REJECT island: no usable mouths on the contour");
                return false;
            }
            dbg.line("FALLBACK loop=%zu: no usable mouths, plain ring", loop_idx);
            emit_plain_ring();
            continue;
        }

        // Distance from a point to the nearest other unit's bead centerline
        // (ray segments at their lateral offset, V flanks where present).
        auto near_clearance = [&](const Vec2d &p, size_t exclude_mouth) -> double
        {
            double clearance = std::numeric_limits<double>::max();
            for (size_t o = 0; o < mouths.size(); ++o)
            {
                if (o == exclude_mouth)
                    continue;
                const Mouth &mc = mouths[o];
                const Slit &c = slits[mc.slit];
                clearance = std::min(clearance, dist_point_segment(p, c.pos, c.pos + c.dir * c.depth) - c.off);
                if (mc.has_apex)
                {
                    clearance = std::min(clearance, dist_point_segment(p, mc.inner_l, mc.apex));
                    clearance = std::min(clearance, dist_point_segment(p, mc.apex, mc.inner_r));
                }
            }
            return clearance;
        };

        // Tip splay: a tip ending in a widened slot tilts outward by half its
        // measured daylight, meeting the neighbor's half-splay at the
        // configured overlap. Tips braked at bonded contact measure no
        // daylight and stay parallel. Measured on pre-splay snapshots so the
        // result is order independent. Disabled at sparse rib pitch: a tip
        // there faces either an intentional open cell or an opposing fan's rib
        // that never tilts to meet it, so the splay would only part the pair
        // into a wedge. Sparse convergence welds are tip_bond's job.
        size_t n_splayed = 0, n_tipsnap = 0;
        double splay_peak = 0.;
        if (!relaxed_layout && mouths.size() > 1)
        {
            std::vector<Vec2d> tip_new(2 * mouths.size());
            for (size_t e = 0; e < mouths.size(); ++e)
            {
                const Mouth &m = mouths[e];
                const Slit &s = slits[m.slit];
                const Vec2d perp(-s.dir.y(), s.dir.x());
                for (int side = 0; side < 2; ++side)
                {
                    const Vec2d tip = (side == 0) ? m.inner_l : m.inner_r;
                    Vec2d out_dir = perp * ((side == 0) ? 1.0 : -1.0);
                    if ((m.inner_l - m.inner_r).dot(perp) < 0.)
                        out_dir = -out_dir;
                    double clearance = near_clearance(tip, e);
                    const double excess = clearance - (bw - ov);
                    // A tilt closes daylight only when the facing structure is
                    // near enough for the two half-splays (plus the width
                    // expansion behind them) to meet - cascade slack, bounded
                    // by the pruning rule at about a pair's width. At sparse
                    // rib pitch the measured "daylight" is the intentional
                    // open cell, and tilting into it cannot close anything: it
                    // just parts the pair into a visible wedge. Those tips
                    // stay parallel.
                    double splay = excess > 4.0 * bw ? 0. : 0.5 * excess;
                    // depth bound keeps short notches upright; width bound
                    // keeps junction voids from dragging beads sideways
                    splay = std::clamp(splay, 0., std::min(0.5 * bw, 0.25 * m.depth));
                    if (splay > 0.05 * bw)
                    {
                        ++n_splayed;
                        splay_peak = std::max(splay_peak, splay);
                    }
                    tip_new[2 * e + side] = tip + out_dir * splay;
                }
            }
            for (size_t e = 0; e < mouths.size(); ++e)
            {
                Mouth &mm = mouths[e];
                mm.inner_l = tip_new[2 * e];
                mm.inner_r = tip_new[2 * e + 1];
                // Ring mode: a full-run cap's splayed tips can flare past a cornered
                // bore into the void, which drops the whole tooth (the bridge). Snap
                // them back onto the bore tip line so the tip rides the bore around
                // its corners. A no-op on flats, where the splay runs tangent to it.
                if (anchor.ring_mode && !core_loops.empty() && mm.depth >= slits[mm.slit].max_t - 0.1 * bw)
                    for (Vec2d *tip : {&mm.inner_l, &mm.inner_r})
                    {
                        double bestd = std::numeric_limits<double>::max();
                        Vec2d snapped = *tip;
                        for (const Loop &cl : core_loops)
                        {
                            const Vec2d q = loop_point_at(cl, loop_nearest_arc(cl, *tip));
                            const double d = (q - *tip).norm();
                            if (d < bestd)
                            {
                                bestd = d;
                                snapped = q;
                            }
                        }
                        if (bestd < 1.5 * bw)
                        {
                            if ((snapped - *tip).norm() > 0.05 * bw)
                                ++n_tipsnap;
                            *tip = snapped;
                        }
                    }
            }
            dbg.line("splay: tips=%zu peak=%.3fmm tipsnap=%zu", n_splayed, unscale<double>(splay_peak), n_tipsnap);
        }

        // Ring-mode bore seal: on a thin wall only the deepest teeth reach the bore,
        // spaced apart, so the inside leaks. Widen each full-run cap along the bore tip
        // line toward the midpoint of the gap to its neighbours, capped at the cap-walk
        // reach. The short teeth keep their floored depth and fill the outer notches; a
        // gap wider than the walk seals as far as it reaches and leaves the rest open.
        // Ring mode only: in depth mode the Athena wall fills the core, so the bore needs
        // no cap-chain seal, and widening the deep caps along the thin core line eats the
        // midplane-reserved anchor-bead core; on an eccentric wall that erases the anchor
        // bead on the thin side and the teeth lose their bond. The midplane standoff
        // reserves the core for the Athena wall there instead.
        size_t n_wedge = 0;
        if (anchor.ring_mode && !core_loops.empty() && params.depth_clamp == 0.)
        {
            std::vector<size_t> deep;
            for (size_t e = 0; e < mouths.size(); ++e)
                if (!mouths[e].has_apex && mouths[e].depth >= slits[mouths[e].slit].max_t - 0.1 * bw)
                    deep.push_back(e);
            if (deep.size() >= 3)
            {
                const Loop *bore = &core_loops.front();
                double bd = std::numeric_limits<double>::max();
                const Vec2d t0 = mouths[deep.front()].inner_l;
                for (const Loop &cl : core_loops)
                {
                    const double d = (loop_point_at(cl, loop_nearest_arc(cl, t0)) - t0).norm();
                    if (d < bd)
                    {
                        bd = d;
                        bore = &cl;
                    }
                }
                std::vector<double> ac(deep.size());
                for (size_t i = 0; i < deep.size(); ++i)
                {
                    const Slit &s = slits[mouths[deep[i]].slit];
                    ac[i] = loop_nearest_arc(*bore, s.pos + s.dir * s.depth);
                }
                for (size_t i = 0; i < deep.size(); ++i)
                {
                    const double sp = wrapped_signed_arc(*bore, ac[(i + deep.size() - 1) % deep.size()] - ac[i]);
                    const double sn = wrapped_signed_arc(*bore, ac[(i + 1) % deep.size()] - ac[i]);
                    Mouth &m = mouths[deep[i]];
                    // Stop a half-overlap short of the midpoint so consecutive caps
                    // land side by side and overlap by ov, like the outer teeth,
                    // instead of all converging to one point.
                    const double back = 0.5 * (bw - ov);
                    double ol = 0.5 * sp - std::copysign(std::min(back, std::abs(0.5 * sp)), sp);
                    double orr = 0.5 * sn - std::copysign(std::min(back, std::abs(0.5 * sn)), sn);
                    // Reach toward the midpoint only as far as the cap-walk can trace,
                    // so a sparse bore seals what the walk follows instead of chording
                    // across the void.
                    const double reach = 0.5 * walk_limit - 0.05 * bw;
                    ol = std::copysign(std::min(std::abs(ol), reach), ol);
                    orr = std::copysign(std::min(std::abs(orr), reach), orr);
                    const Vec2d cp = loop_point_at(*bore, wrap_arc(*bore, ac[i] + ol));
                    const Vec2d cn = loop_point_at(*bore, wrap_arc(*bore, ac[i] + orr));
                    // Thick walls already seal (their deep caps overlap): widen only
                    // when the midpoint reaches further than the current cap, so dense
                    // bores stay as they are.
                    const Vec2d cc = loop_point_at(*bore, ac[i]);
                    if (0.5 * ((cp - cc).norm() + (cn - cc).norm()) <=
                        0.5 * ((m.inner_l - cc).norm() + (m.inner_r - cc).norm()) + 0.05 * bw)
                        continue;
                    const Vec2d ml = loop_point_at(lp, m.arc_l), mr = loop_point_at(lp, m.arc_r);
                    // assign so the legs (mouth -> tip) do not cross
                    if ((cp - ml).norm() + (cn - mr).norm() <= (cn - ml).norm() + (cp - mr).norm())
                    {
                        m.inner_l = cp;
                        m.inner_r = cn;
                    }
                    else
                    {
                        m.inner_l = cn;
                        m.inner_r = cp;
                    }
                    ++n_wedge;
                }
            }
            dbg.line("wedge: deep=%zu sealed=%zu", deep.size(), n_wedge);
        }

        ExtrusionMultiPath multi;

        // Downstream G-code emission assumes the multipath chains end to
        // start. Clipping can fragment, trim or drop a path, so accept a
        // clipped path only when it comes back as one piece with its
        // endpoints intact.
        const double cont_tol = scale_(0.05);
        auto try_add_path = [&](Polyline &&pl, bool clip, const ExtrusionAttributes *attrs = nullptr) -> bool
        {
            if (pl.size() < 2)
                return true;
            if (!clip)
            {
                ExtrusionPath path(attrs ? *attrs : base_attrs);
                path.polyline = std::move(pl);
                multi.paths.push_back(std::move(path));
                return true;
            }
            const Point want_front = pl.first_point();
            const Point want_back = pl.last_point();
            Polylines clipped = intersection_pl(pl, island);
            if (clipped.size() != 1 || clipped.front().size() < 2)
                return false;
            Polyline &cp = clipped.front();
            if ((cp.first_point() - want_front).cast<double>().norm() > cont_tol ||
                (cp.last_point() - want_back).cast<double>().norm() > cont_tol)
                return false;
            ExtrusionPath path(attrs ? *attrs : base_attrs);
            path.polyline = std::move(cp);
            multi.paths.push_back(std::move(path));
            return true;
        };

        // The tour starts and ends inside the island so layer changes never
        // mark the boundary. The seam host is the deepest mouth whose two
        // halves survive clipping; in ring mode it hides mid-band instead
        // (the deepest caps are the bore surface).
        const size_t ne = mouths.size();
        auto clip_intact = [&](const Polyline &pl) -> bool
        {
            Polylines clipped = intersection_pl(pl, island);
            return clipped.size() == 1 && clipped.front().size() >= 2 &&
                   (clipped.front().first_point() - pl.first_point()).cast<double>().norm() <= cont_tol &&
                   (clipped.front().last_point() - pl.last_point()).cast<double>().norm() <= cont_tol;
        };
        std::vector<size_t> seam_cand(ne);
        for (size_t e = 0; e < ne; ++e)
            seam_cand[e] = e;
        if (anchor.ring_mode)
        {
            auto mid_err = [&](size_t e)
            {
                return std::abs(mouths[e].depth - 0.5 * slits[mouths[e].slit].max_t);
            };
            std::sort(seam_cand.begin(), seam_cand.end(), [&](size_t a, size_t b) { return mid_err(a) < mid_err(b); });
        }
        else
            std::sort(seam_cand.begin(), seam_cand.end(),
                      [&](size_t a, size_t b) { return mouths[a].depth > mouths[b].depth; });
        // The outer-loop host rib must be the seam host (the excursion is the
        // tour's tail, riding the seam rib's widened slot): try it first. If it
        // is unviable the outer loop falls back to a standalone ring below.
        if (wide_idx != std::numeric_limits<size_t>::max())
            for (size_t e = 0; e < ne; ++e)
                if (mouths[e].slit == wide_idx && !mouths[e].has_apex)
                {
                    seam_cand.erase(std::remove(seam_cand.begin(), seam_cand.end(), e), seam_cand.end());
                    seam_cand.insert(seam_cand.begin(), e);
                    break;
                }
        size_t e0 = ne;
        for (size_t tries = 0; tries < seam_cand.size() && tries < 16; ++tries)
        {
            const Mouth &mc = mouths[seam_cand[tries]];
            Vec2d sp = mc.has_apex ? mc.apex : Vec2d((mc.inner_l + mc.inner_r) * 0.5);
            Polyline out_test;
            out_test.append(to_point(sp));
            out_test.append(to_point(mc.inner_r));
            out_test.append(to_point(loop_point_at(lp, mc.arc_r)));
            Polyline in_test;
            in_test.append(to_point(loop_point_at(lp, mc.arc_l)));
            in_test.append(to_point(mc.inner_l));
            in_test.append(to_point(sp));
            if (clip_intact(out_test) && clip_intact(in_test))
            {
                e0 = seam_cand[tries];
                if (tries > 0)
                    dbg.line("seam: host moved off the deepest mouth after %zu rejects", tries);
                break;
            }
        }
        if (e0 == ne)
        {
            dbg.line(is_contour ? "REJECT island: no viable seam host" : "FALLBACK loop=%zu: no viable seam host",
                     loop_idx);
            if (is_contour)
                return false;
            emit_plain_ring();
            continue;
        }
        const Mouth &m0 = mouths[e0];
        Vec2d seam_pt = m0.has_apex ? m0.apex : Vec2d((m0.inner_l + m0.inner_r) * 0.5);
        // With the outer-loop excursion armed, the tour leaves the hub up the host
        // rib's RIGHT leg, not from the cap center: aim the ring/track entry at that
        // leg's cap corner so the bead leaves the hub and simply turns outward,
        // instead of skipping sideways along the cap to reach the leg.
        const bool weave_pending = have_outer && m0.slit == wide_idx && !m0.has_apex;
        const Vec2d hub_target = weave_pending ? m0.inner_r : seam_pt;

        if (center_loop && is_contour)
        {
            Vec2d u = hub_target - anchor.center;
            double un = u.norm();
            if (un > 1.)
            {
                u /= un;
                // Trace the center ring once, entering and leaving at the
                // point facing the seam slit: its two beads become the ring's
                // entry and exit, keeping the tour one continuous line.
                constexpr int LOOP_SEGS = 32;
                double a0 = std::atan2(u.y(), u.x());
                Polyline ring;
                for (int i = 0; i <= LOOP_SEGS; ++i)
                {
                    double a = a0 + 2.0 * M_PI * (double) i / (double) LOOP_SEGS;
                    ring.append(to_point(anchor.center + Vec2d(std::cos(a), std::sin(a)) * r_loop));
                }
                if (try_add_path(std::move(ring), true))
                    seam_pt = anchor.center + u * r_loop;
                else
                    dbg.line("emit: center ring failed clipping, seam stays at the cap");
            }
        }
        else if (spine_track && is_contour)
        {
            // Weave the racetrack into the tour like the center ring, entering and
            // leaving at the INTERPOLATED point nearest the hub target - a straight
            // capsule side has vertices millimeters apart, so snapping to the
            // nearest vertex can drag the entry far along the track and the seam
            // connection prints as a long diagonal. A disconnected spine yields
            // extra loops; those emit standalone (their ribs still need a track).
            size_t best_loop = 0;
            double best_d = std::numeric_limits<double>::max(), best_arc = 0.;
            std::vector<Loop> track_loops;
            track_loops.reserve(track_polys.size());
            for (size_t li = 0; li < track_polys.size(); ++li)
            {
                track_loops.push_back(make_loop(track_polys[li]));
                const double na = loop_nearest_arc(track_loops.back(), hub_target);
                const double d = (loop_point_at(track_loops.back(), na) - hub_target).squaredNorm();
                if (d < best_d)
                {
                    best_d = d;
                    best_loop = li;
                    best_arc = na;
                }
            }
            size_t n_track_extra = 0, n_track_drop = 0;
            for (size_t li = 0; li < track_polys.size(); ++li)
            {
                const Loop &tl = track_loops[li];
                Polyline ring;
                ring.points.reserve(tl.pts.size() + 2);
                if (li == best_loop)
                {
                    const Vec2d entry = loop_point_at(tl, best_arc);
                    const size_t i0 = loop_segment_index(tl, best_arc);
                    ring.append(to_point(entry));
                    for (size_t j = 1; j <= tl.pts.size(); ++j)
                        ring.append(to_point(tl.pts[(i0 + j) % tl.pts.size()]));
                    ring.append(to_point(entry));
                    if (try_add_path(std::move(ring), true))
                        seam_pt = entry;
                    else
                        dbg.line("emit: racetrack failed clipping, seam stays at the cap");
                    continue;
                }
                for (const Vec2d &p : tl.pts)
                    ring.append(to_point(p));
                ring.append(to_point(tl.pts.front()));
                if (clip_intact(ring))
                {
                    ExtrusionMultiPath ring_mp;
                    ExtrusionPath path(base_attrs);
                    path.polyline = std::move(ring);
                    ring_mp.paths.push_back(std::move(path));
                    collection.append(std::move(ring_mp));
                    ++n_track_extra;
                }
                else
                    ++n_track_drop;
            }
            if (track_polys.size() > 1)
                dbg.line("track: extra standalone loops=%zu dropped=%zu", n_track_extra, n_track_drop);
        }

        bool tour_ok = true;
        bool outer_woven = false;
        size_t n_bridged = 0;
        size_t n_capwalk = 0;
        size_t n_capdrop = 0;
        size_t n_widened = 0;
        double w_peak = 0.;

        // Contour a full-run cap along the bore tip line between its two (snapped)
        // tips, so at the ov-overlap standoff the cap follows a cornered bore around
        // its corner instead of a chord that cuts across it into the void.
        auto append_cap = [&](Polyline &cap, const Mouth &m)
        {
            if (!anchor.ring_mode || core_loops.empty() || m.depth < slits[m.slit].max_t - 0.1 * bw)
                return;
            const Loop *best = nullptr;
            double bestd = std::numeric_limits<double>::max(), a_l = 0., a_r = 0.;
            for (const Loop &cl : core_loops)
            {
                const double al = loop_nearest_arc(cl, m.inner_l);
                const double ar = loop_nearest_arc(cl, m.inner_r);
                const double d = (loop_point_at(cl, al) - m.inner_l).norm() +
                                 (loop_point_at(cl, ar) - m.inner_r).norm();
                if (d < bestd)
                {
                    bestd = d;
                    best = &cl;
                    a_l = al;
                    a_r = ar;
                }
            }
            if (best != nullptr && bestd < 1.5 * bw)
            {
                double span = wrap_arc(*best, a_r - a_l);
                span = std::min(span, best->total - span);
                // The wedge clamps a cap to walk_limit, so a longer span means the
                // projection jumped a concavity; keep the straight cap in that case.
                if (span < walk_limit)
                {
                    loop_walk_short(*best, a_l, a_r, cap);
                    ++n_capwalk;
                    return;
                }
            }
            ++n_capdrop;
        };
        {
            Polyline out_half;
            out_half.append(to_point(seam_pt));
            out_half.append(to_point(m0.inner_r));
            out_half.append(to_point(loop_point_at(lp, m0.arc_r)));
            tour_ok = try_add_path(std::move(out_half), true);
        }

        for (size_t k = 1; tour_ok && k <= ne; ++k)
        {
            const size_t e = (e0 + k) % ne;
            const Mouth &m = mouths[e];
            const Mouth &prev = mouths[(e + ne - 1) % ne];

            Polyline walk;
            walk.append(to_point(loop_point_at(lp, prev.arc_r)));
            loop_walk_between(lp, prev.arc_r, m.arc_l, walk);
            walk.append(to_point(loop_point_at(lp, m.arc_l)));
            try_add_path(std::move(walk), false);

            if (e == e0)
            {
                // Outer-loop excursion: the tour rides the seam rib's widened slot.
                // The in-half returns along the left leg but stops on the cap half a
                // spacing past center; the tail U-turns there, runs back out through
                // the slot, exits the mouth to the external loop, laps the object
                // AWAY from its return lane, comes back in the slot's other lane,
                // and ends on the already-traced cap at the hub. Four parallel
                // bonded beads, no crossings; the external loop's ends butt one
                // spacing apart - the single visible seam.
                bool weave_done = false;
                if (weave_pending)
                {
                    const Slit &s0 = slits[m.slit];
                    const Vec2d perp(-s0.dir.y(), s0.dir.x());
                    const double sgn = (loop_point_at(lp, m.arc_l) - s0.pos).dot(perp) >= 0. ? 1. : -1.;
                    const double s_sp = bw - ov;
                    const Vec2d cap_c = s0.pos + s0.dir * s0.depth;
                    const Vec2d c1 = cap_c + perp * (sgn * 0.5 * s_sp);
                    const Vec2d c2 = cap_c - perp * (sgn * 0.5 * s_sp);
                    const Vec2d f1_top = s0.pos + perp * (sgn * 0.5 * s_sp);
                    const Vec2d f2_top = s0.pos - perp * (sgn * 0.5 * s_sp);
                    const double oa1 = loop_nearest_arc(outer_lp, f1_top);
                    const double oa2 = loop_nearest_arc(outer_lp, f2_top);
                    const Vec2d p1 = loop_point_at(outer_lp, oa1);
                    const Vec2d p2 = loop_point_at(outer_lp, oa2);
                    // The lap must genuinely span the loop: its extent rests on two
                    // nearest-point projections a fraction of a bead apart, and a
                    // concave stretch at the reference can invert them, collapsing
                    // the "full lap" to a stub - the outer surface then prints one
                    // spacing undersize with a mouth gap at every rib and nothing
                    // else would catch it. Degenerate span falls back to the plain
                    // seam + standalone outer loop.
                    const double lap_span = wrap_arc(outer_lp, oa1 - oa2);
                    if (lap_span >= 0.5 * outer_lp.total)
                    {
                        // The seam-half's turnaround (host leg to lane 1) comes to
                        // a point like the neighboring ribs: leg down to an apex
                        // midway between leg and lane, then up to the lane foot -
                        // a V instead of a squared cap chord. The apex depth is
                        // not fixed: like every other tip it chases along its own
                        // line until it welds into the first neighboring bead at
                        // the tip engagement, capped at the classic three-bead
                        // extension and the anchor standoff.
                        const Vec2d apex_off = perp * (sgn * 1.0 * s_sp);
                        const double extra = advance_while(std::max(0., s0.max_t - s0.depth),
                                                           [&](double dt)
                                                           {
                                                               const Vec2d ap = s0.pos + s0.dir * (s0.depth + dt) +
                                                                                apex_off;
                                                               for (size_t ci = 0; ci < n; ++ci)
                                                               {
                                                                   if (ci == m.slit)
                                                                       continue;
                                                                   const Slit &cs = slits[ci];
                                                                   if (cs.dir.squaredNorm() < 0.5 || cs.depth <= 0.)
                                                                       continue;
                                                                   if (dist_point_segment(ap, cs.pos,
                                                                                          cs.pos + cs.dir * cs.depth) <
                                                                       bw + cs.off - tip_bond)
                                                                       return false;
                                                               }
                                                               return true;
                                                           });
                        const double t_p = s0.depth + std::clamp(extra, 0.35 * s_sp, 3.0 * bw);
                        const Vec2d turn_apex = s0.pos + s0.dir * std::min(t_p, s0.max_t) + apex_off;
                        Polyline in_half;
                        in_half.append(to_point(loop_point_at(lp, m.arc_l)));
                        in_half.append(to_point(m.inner_l));
                        in_half.append(to_point(turn_apex));
                        in_half.append(to_point(c1));
                        tour_ok = try_add_path(std::move(in_half), true);
                        if (tour_ok)
                        {
                            // The external lap travels from p1 the long way around
                            // to p2: walk forward p2 -> p1 and reverse, so the lap
                            // heads away from its own return lane and nothing
                            // crosses.
                            Polyline lap;
                            loop_walk_between(outer_lp, oa2, oa1, lap);
                            Polyline tail;
                            tail.append(to_point(c1));
                            tail.append(to_point(p1));
                            for (auto it = lap.points.rbegin(); it != lap.points.rend(); ++it)
                                tail.points.push_back(*it);
                            tail.append(to_point(p2));
                            tail.append(to_point(c2));
                            tour_ok = try_add_path(std::move(tail), false);
                            outer_woven = tour_ok;
                        }
                        weave_done = true;
                    }
                    else
                        dbg.line("outer loop: lap span degenerate (%.1f of %.1fmm); standalone fallback",
                                 unscale<double>(lap_span), unscale<double>(outer_lp.total));
                }
                if (!weave_done)
                {
                    Polyline in_half;
                    in_half.append(to_point(loop_point_at(lp, m.arc_l)));
                    in_half.append(to_point(m.inner_l));
                    in_half.append(to_point(seam_pt));
                    tour_ok = try_add_path(std::move(in_half), true);
                }
                break;
            }

            // Deep width expansion: past the depth where the neighboring unit
            // ends, the flank bead grows (up to 150% of the nozzle) to fill
            // the slot it faces, measured against both the neighbor and its
            // own pair partner so neither face bonds beyond the configured
            // overlap. The last bead length and the cap stay nominal so the
            // widened bead never pushes toward the bore.
            const Vec2d mouth_l_pt = loop_point_at(lp, m.arc_l);
            const Vec2d mouth_r_pt = loop_point_at(lp, m.arc_r);
            const double w_hi = std::max(bw, (double) params.max_bead);
            auto flank_zone = [&](const Mouth &nb, const Vec2d &mpt, const Vec2d &inner, const Vec2d &pair_mpt,
                                  const Vec2d &pair_inner, double &t0, double &t1, double &w_e) -> bool
            {
                const Slit &cn = slits[nb.slit];
                if (ov < 0.)
                    return false; // spread mode: the slot daylight IS the requested gap
                if (relaxed_layout)
                    return false; // sparse cells are intentional voids, not slots to fatten into
                t0 = nb.has_apex ? (nb.apex - cn.pos).norm() : nb.depth;
                t1 = m.depth - 1.0 * bw;
                if (t1 - t0 < 0.75 * bw)
                    return false;
                double frac = 0.5 * (t0 + t1) / m.depth;
                Vec2d probe = mpt + (inner - mpt) * frac;
                double cl = std::min(near_clearance(probe, e), dist_point_segment(probe, pair_mpt, pair_inner));
                w_e = std::clamp(cl + ov, bw, w_hi);
                // engage only on genuine voids: hairline cascade wedges print
                // fine at nominal width
                return w_e > bw + ov;
            };
            double t0_l = 0., t1_l = 0., w_l = 0., t0_r = 0., t1_r = 0., w_r = 0.;
            const bool zone_l = flank_zone(mouths[(e + ne - 1) % ne], mouth_l_pt, m.inner_l, mouth_r_pt, m.inner_r,
                                           t0_l, t1_l, w_l);
            const bool zone_r = flank_zone(mouths[(e + 1) % ne], mouth_r_pt, m.inner_r, mouth_l_pt, m.inner_l, t0_r,
                                           t1_r, w_r);
            const size_t rollback = multi.paths.size();
            bool slit_ok;
            if (!zone_l && !zone_r)
            {
                Polyline slit;
                slit.append(to_point(mouth_l_pt));
                slit.append(to_point(m.inner_l));
                if (m.has_apex)
                    slit.append(to_point(m.apex));
                else
                    append_cap(slit, m);
                slit.append(to_point(m.inner_r));
                slit.append(to_point(mouth_r_pt));
                slit_ok = try_add_path(std::move(slit), true);
            }
            else
            {
                auto emit_flank = [&](const Vec2d &mpt, const Vec2d &inner, bool zone, double t0, double t1, double w_e,
                                      bool inward) -> bool
                {
                    if (!zone)
                    {
                        Polyline fl;
                        fl.append(to_point(inward ? mpt : inner));
                        fl.append(to_point(inward ? inner : mpt));
                        return try_add_path(std::move(fl), true);
                    }
                    ++n_widened;
                    w_peak = std::max(w_peak, w_e);
                    ExtrusionAttributes wide(ExtrusionRole::Serpentine,
                                             params.flow.with_width((float) unscale<double>(w_e)));
                    // Widened flanks carry their volumetric excess so export can hold the
                    // nominal rate when no max volumetric flow is configured.
                    if (params.flow.mm3_per_mm() > 0.)
                        wide.flow_ratio = float(std::max(1., wide.mm3_per_mm / params.flow.mm3_per_mm()));
                    std::array<Vec2d, 4> pts{mpt, mpt + (inner - mpt) * (t0 / m.depth),
                                             mpt + (inner - mpt) * (t1 / m.depth), inner};
                    if (!inward)
                        std::reverse(pts.begin(), pts.end());
                    bool ok = true;
                    for (int seg = 0; seg < 3 && ok; ++seg)
                    {
                        Polyline fl;
                        fl.append(to_point(pts[seg]));
                        fl.append(to_point(pts[seg + 1]));
                        ok = try_add_path(std::move(fl), true, seg == 1 ? &wide : nullptr);
                    }
                    return ok;
                };
                slit_ok = emit_flank(mouth_l_pt, m.inner_l, zone_l, t0_l, t1_l, w_l, true);
                if (slit_ok)
                {
                    Polyline cap;
                    cap.append(to_point(m.inner_l));
                    if (m.has_apex)
                        cap.append(to_point(m.apex));
                    else if (!core_loops.empty() && (params.depth_clamp > 0. || anchor.ring_mode) &&
                             slits[m.slit].depth >= slits[m.slit].max_t - boundary_margin)
                    {
                        // Boundary tooth: join its two beads with a cap contoured along
                        // the boundary curve instead of a square chord, so the inner
                        // edge follows the smooth contour and adheres across the cap.
                        const Loop *best = nullptr;
                        double bestd = std::numeric_limits<double>::max(), a_l = 0., a_r = 0.;
                        for (const Loop &cl : core_loops)
                        {
                            double al = loop_nearest_arc(cl, m.inner_l);
                            double ar = loop_nearest_arc(cl, m.inner_r);
                            double d = (loop_point_at(cl, al) - m.inner_l).norm() +
                                       (loop_point_at(cl, ar) - m.inner_r).norm();
                            if (d < bestd)
                            {
                                bestd = d;
                                best = &cl;
                                a_l = al;
                                a_r = ar;
                            }
                        }
                        bool walked = false;
                        // Both tips must project onto the same loop close by. On a
                        // multi-bore part a tooth straddling two loops would otherwise
                        // pick one by least summed distance and walk a spurious arc through
                        // empty space (the cap then self-intersects and the slit drops).
                        if (best != nullptr && bestd < 1.5 * bw)
                        {
                            double span = wrap_arc(*best, a_r - a_l);
                            span = std::min(span, best->total - span);
                            if (span < walk_limit) // skip if the projection crossed a concavity
                            {
                                loop_walk_short(*best, a_l, a_r, cap);
                                ++n_capwalk;
                                walked = true;
                            }
                        }
                        if (!walked)
                            ++n_capdrop; // kept the straight chord for this boundary cap
                    }
                    cap.append(to_point(m.inner_r));
                    slit_ok = try_add_path(std::move(cap), true);
                }
                if (slit_ok)
                    slit_ok = emit_flank(mouth_r_pt, m.inner_r, zone_r, t0_r, t1_r, w_r, false);
            }
            if (!slit_ok)
            {
                // A slit that does not survive clipping intact is dropped and
                // its mouth bridged along the contour: continuity above fill.
                multi.paths.erase(multi.paths.begin() + rollback, multi.paths.end());
                ++n_bridged;
                Polyline bridge;
                bridge.append(to_point(loop_point_at(lp, m.arc_l)));
                loop_walk_between(lp, m.arc_l, m.arc_r, bridge);
                bridge.append(to_point(loop_point_at(lp, m.arc_r)));
                try_add_path(std::move(bridge), false);
            }
        }

        if (outer_woven)
            dbg.line("outer loop: woven through the seam rib slot (outer len=%.2fmm)", unscale<double>(outer_lp.total));

        size_t chain_break = 0;
        for (size_t i = 1; tour_ok && i < multi.paths.size(); ++i)
            if ((multi.paths[i].polyline.first_point() - multi.paths[i - 1].polyline.last_point()).cast<double>().norm() >
                cont_tol)
            {
                tour_ok = false;
                chain_break = i;
            }

        dbg.line("emit: paths=%zu mouths=%zu bridged=%zu capwalk=%zu capdrop=%zu widened=%zu wpeak=%.3fmm seam_e0=%zu "
                 "tour_ok=%d%s",
                 multi.paths.size(), ne, n_bridged, n_capwalk, n_capdrop, n_widened, unscale<double>(w_peak), e0,
                 tour_ok ? 1 : 0, chain_break ? " CHAIN-BREAK" : "");
        // external_perimeters_first: the woven tour reverses as one unit, so
        // the outer lap prints first and the interior after, matching
        // outside-in perimeter ordering. The reversal also flips every bead's
        // direction; the mirror decision at the top compensates, keeping the
        // lap on the loop-direction preference. Chain and interior seam point
        // survive reversal.
        if (tour_ok && is_contour && outer_woven && params.outer_first)
            multi.reverse();
        if (tour_ok && !multi.paths.empty())
        {
            collection.append(std::move(multi));
            // The excursion needs the host rib as the seam; with the host
            // degenerate or unviable, a bare closed outer loop with a travel
            // move still beats a part printed one spacing undersize. The
            // host's widened slot stays unfilled in that case. The ring
            // follows the tour (or leads the island under
            // external_perimeters_first) and obeys the loop-direction
            // preference; in a mirrored build the frame flips handedness, so
            // the direction test flips with it.
            if (have_outer && is_contour && !outer_woven)
            {
                dbg.line("outer loop: host rib unviable; standalone outer loop, slot unfilled");
                Polyline ring;
                ring.points.reserve(outer_lp.pts.size() + 1);
                for (const Vec2d &p : outer_lp.pts)
                    ring.append(to_point(p));
                ring.append(ring.first_point());
                if (params.prefer_clockwise != params.mirror_build)
                    ring.reverse();
                ExtrusionMultiPath ring_mp;
                ExtrusionPath path(base_attrs);
                path.polyline = std::move(ring);
                ring_mp.paths.push_back(std::move(path));
                if (params.outer_first)
                    collection.entities.insert(collection.entities.begin(), new ExtrusionMultiPath(std::move(ring_mp)));
                else
                    collection.append(std::move(ring_mp));
            }
        }
        else if (is_contour)
        {
            dbg.line("REJECT island: tour failed (seam half or chain break at path %zu)", chain_break);
            return false;
        }
        else
        {
            dbg.line("FALLBACK loop=%zu: tour failed, plain ring", loop_idx);
            emit_plain_ring();
        }
    }

    if (collection.empty())
    {
        dbg.line("REJECT island: nothing emitted");
        return false;
    }
    // Interior region for the depth hand-off (holed islands only): inside the contour tip line,
    // minus each bore's tip-line disc = the central band the teeth bound, with the inter-tooth
    // gaps excluded by construction (they fall on the boundary side of the tip line). The rings
    // are raw polygons threaded through each tooth tip; on a concave-holed island with convergent
    // aim the convergent rays can cross, so a ring can self-intersect. Normalize through Clipper
    // (union_ex resolves the crossing into valid simple polygons) before the diff, so a
    // self-intersecting ring is not collapsed by the diff's NonZero fill, which would drop a lobe
    // and hand off an empty interior, a structural void inside the wall.
    if (out_interior && !island.holes.empty() && contour_tip_ring.points.size() >= 3)
    {
        ExPolygons interior = union_ex(Polygons{std::move(contour_tip_ring)});
        if (!bore_tip_rings.empty())
            interior = diff_ex(interior, union_ex(bore_tip_rings));
        *out_interior = std::move(interior);
    }
    out_loops.append(std::move(collection));
    return true;
}

} // namespace Slic3r::Serpentine
