///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 - 2013 Mike Sheldrake @mesheldrake
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ClipperUtils.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/surface/Surface.hpp"
#include "luminary/core/Prelude.hpp"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_reduce.h>

// Uncomment to enable detailed Clipper performance metrics
// #define CLIPPER_UTILS_TIMING

// Uncomment to enable Clipper2 verification logging (prints which version is being used)
#define CLIPPER2_VERIFY_USAGE

#ifdef CLIPPER2_VERIFY_USAGE
#include <cstdio>
#include <atomic>

namespace
{
std::atomic<bool> clipper_version_logged{false};

inline void log_clipper_version() {}
} // namespace
#endif

#ifdef CLIPPER_UTILS_TIMING
// time limit for one ClipperLib operation (union / diff / offset), in ms
#define CLIPPER_UTILS_TIME_LIMIT_DEFAULT 50
#include <boost/current_function.hpp>

#include "luminary/core/diagnostics/Timer.hpp"

#define CLIPPER_UTILS_TIME_LIMIT_SECONDS(limit) \
    Timing::TimeLimitAlarm time_limit_alarm(uint64_t(limit) * 1000000000l, BOOST_CURRENT_FUNCTION)
#define CLIPPER_UTILS_TIME_LIMIT_MILLIS(limit) \
    Timing::TimeLimitAlarm time_limit_alarm(uint64_t(limit) * 1000000l, BOOST_CURRENT_FUNCTION)

// Enhanced timing with operation counters
namespace
{
std::atomic<uint64_t> clipper_operation_count{0};
std::atomic<uint64_t> clipper_total_time_ns{0};
} // namespace

#define CLIPPER_METRICS_START()                                      \
    auto _clipper_start = std::chrono::high_resolution_clock::now(); \
    clipper_operation_count++;

#define CLIPPER_METRICS_END(op_name)                                                                     \
    do                                                                                                   \
    {                                                                                                    \
        auto _clipper_end = std::chrono::high_resolution_clock::now();                                   \
        auto _duration_ns =                                                                              \
            std::chrono::duration_cast<std::chrono::nanoseconds>(_clipper_end - _clipper_start).count(); \
        clipper_total_time_ns += _duration_ns;                                                           \
        if (_duration_ns > 1000000)                                                                      \
        { /* > 1ms */                                                                                    \
            printf("[CLIPPER PERF] %s: %.2f ms (Total ops: %llu, Cumulative: %.2f sec)\n", op_name,      \
                   _duration_ns / 1000000.0, (unsigned long long) clipper_operation_count.load(),        \
                   clipper_total_time_ns.load() / 1000000000.0);                                         \
        }                                                                                                \
    } while (false)
#else
#define CLIPPER_UTILS_TIME_LIMIT_SECONDS(limit) \
    do                                          \
    {                                           \
    } while (false)
#define CLIPPER_UTILS_TIME_LIMIT_MILLIS(limit) \
    do                                         \
    {                                          \
    } while (false)
#define CLIPPER_METRICS_START() \
    do                          \
    {                           \
    } while (false)
#define CLIPPER_METRICS_END(op_name) \
    do                               \
    {                                \
    } while (false)
#endif // CLIPPER_UTILS_TIMING

// #define CLIPPER_UTILS_DEBUG

#ifdef CLIPPER_UTILS_DEBUG
#include "luminary/geometry/diagnostics/SVG.hpp"
#endif /* CLIPPER_UTILS_DEBUG */

namespace Luminary
{

// All conversion functions now in ClipperUtils.hpp

// These functions provide compatibility with old Clipper1 API

// Wrapper for Clipper2's IsPositive (replaces Orientation)
inline bool Orientation(const Clipper2Lib::Path64 &path)
{
    return Clipper2Lib::IsPositive(path);
}

// Wrapper for Clipper2's Area function
inline double Area(const Clipper2Lib::Path64 &path)
{
    return Clipper2Lib::Area(path);
}

// Wrapper for Clipper2's SimplifyPaths (uses epsilon parameter, not fill rule)
inline Clipper2Lib::Paths64 SimplifyPolygons(const Clipper2Lib::Paths64 &paths, double epsilon = 2.0)
{
    return Clipper2Lib::SimplifyPaths(paths, epsilon);
}

// Wrapper for Clipper2's PointInPolygon
inline Clipper2Lib::PointInPolygonResult PointInPolygon(const Clipper2Lib::Point64 &pt, const Clipper2Lib::Path64 &path)
{
    return Clipper2Lib::PointInPolygon(pt, path);
}

// Note: CleanPolygons doesn't have direct equivalent in Clipper2
// SimplifyPaths can be used for similar purpose
inline void CleanPolygons(Clipper2Lib::Paths64 &paths, double epsilon = 2.0)
{
    // Clipper2 doesn't have CleanPolygons - SimplifyPaths is the replacement
    paths = Clipper2Lib::SimplifyPaths(paths, epsilon);
}

#ifdef CLIPPER_UTILS_DEBUG
// For debugging the Clipper library, for providing bug reports to the Clipper author.
bool export_clipper_input_polygons_bin(const char *path, const Paths &input_subject, const Paths &input_clip)
{
    FILE *pfile = fopen(path, "wb");
    if (pfile == NULL)
        return false;

    uint32_t sz = uint32_t(input_subject.size());
    fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_subject.size(); ++i)
    {
        const Path &path = input_subject[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(Clipper2Lib::Point64), sz, pfile);
    }
    sz = uint32_t(input_clip.size());
    ::fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_clip.size(); ++i)
    {
        const Path &path = input_clip[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(Clipper2Lib::Point64), sz, pfile);
    }
    ::fclose(pfile);
    return true;

err:
    ::fclose(pfile);
    return false;
}
#endif /* CLIPPER_UTILS_DEBUG */

namespace ClipperUtils
{
Points EmptyPathsProvider::s_empty_points;
Points SinglePathProvider::s_end;

// Clip source polygon to be used as a clipping polygon with a bouding box around the source (to be clipped) polygon.
// Useful as an optimization for expensive ClipperLib operations, for example when clipping source polygons one by one
// with a set of polygons covering the whole layer below.
template<typename PointsType>
inline void clip_clipper_polygon_with_subject_bbox_templ(const PointsType &src, const BoundingBox &bbox,
                                                         PointsType &out)
{
    using PointType = typename PointsType::value_type;

    out.clear();
    const size_t cnt = src.size();
    if (cnt < 3)
        return;

    enum class Side
    {
        Left = 1,
        Right = 2,
        Top = 4,
        Bottom = 8
    };

    auto sides = [bbox](const PointType &p)
    {
        return int(p.x() < bbox.min.x()) * int(Side::Left) + int(p.x() > bbox.max.x()) * int(Side::Right) +
               int(p.y() < bbox.min.y()) * int(Side::Bottom) + int(p.y() > bbox.max.y()) * int(Side::Top);
    };

    int sides_prev = sides(src.back());
    int sides_this = sides(src.front());
    const size_t last = cnt - 1;
    for (size_t i = 0; i < last; ++i)
    {
        int sides_next = sides(src[i + 1]);
        if ( // This point is inside. Take it.
            sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            (sides_prev & sides_this & sides_next) == 0)
        {
            out.emplace_back(src[i]);
            sides_prev = sides_this;
        }
        else
        {
            // All the three points (this, prev, next) are outside at the same side.
            // Ignore this point.
        }
        sides_this = sides_next;
    }

    // Never produce just a single point output polygon.
    if (!out.empty())
        if (int sides_next = sides(out.front());
            // The last point is inside. Take it.
            sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            (sides_prev & sides_this & sides_next) == 0)
            out.emplace_back(src.back());
}

void clip_clipper_polygon_with_subject_bbox(const Points &src, const BoundingBox &bbox, Points &out)
{
    clip_clipper_polygon_with_subject_bbox_templ(src, bbox, out);
}
void clip_clipper_polygon_with_subject_bbox(const ZPoints &src, const BoundingBox &bbox, ZPoints &out)
{
    clip_clipper_polygon_with_subject_bbox_templ(src, bbox, out);
}

template<typename PointsType>
[[nodiscard]] PointsType clip_clipper_polygon_with_subject_bbox_templ(const PointsType &src, const BoundingBox &bbox)
{
    PointsType out;
    clip_clipper_polygon_with_subject_bbox(src, bbox, out);
    return out;
}

[[nodiscard]] Points clip_clipper_polygon_with_subject_bbox(const Points &src, const BoundingBox &bbox)
{
    return clip_clipper_polygon_with_subject_bbox_templ(src, bbox);
}
[[nodiscard]] ZPoints clip_clipper_polygon_with_subject_bbox(const ZPoints &src, const BoundingBox &bbox)
{
    return clip_clipper_polygon_with_subject_bbox_templ(src, bbox);
}

void clip_clipper_polygon_with_subject_bbox(const Polygon &src, const BoundingBox &bbox, Polygon &out)
{
    clip_clipper_polygon_with_subject_bbox(src.points, bbox, out.points);
}

[[nodiscard]] Polygon clip_clipper_polygon_with_subject_bbox(const Polygon &src, const BoundingBox &bbox)
{
    Polygon out;
    clip_clipper_polygon_with_subject_bbox(src.points, bbox, out.points);
    return out;
}

[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const Polygons &src, const BoundingBox &bbox)
{
    Polygons out;
    out.reserve(src.size());
    for (const Polygon &p : src)
        out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox));
    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
              out.end());
    return out;
}
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygon &src, const BoundingBox &bbox)
{
    Polygons out;
    out.reserve(src.num_contours());
    out.emplace_back(clip_clipper_polygon_with_subject_bbox(src.contour, bbox));
    for (const Polygon &p : src.holes)
        out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox));
    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
              out.end());
    return out;
}
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygons &src, const BoundingBox &bbox)
{
    Polygons out;
    out.reserve(number_polygons(src));
    for (const ExPolygon &p : src)
    {
        Polygons temp = clip_clipper_polygons_with_subject_bbox(p, bbox);
        out.insert(out.end(), temp.begin(), temp.end());
    }

    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
              out.end());
    return out;
}
} // namespace ClipperUtils

static ExPolygons PolyTree64ToExPolygons(PolyTree64 &&polytree)
{
    struct Inner
    {
        static void PolyTree64ToExPolygonsRecursive(const Clipper2Lib::PolyPath64 &polypath, ExPolygons *expolygons)
        {
            size_t cnt = expolygons->size();
            expolygons->resize(cnt + 1);

            // Trust PolyTree64 hierarchy, don't reverse based on winding.
            // The PolyTree64 already has IsHole() flags - we should trust that, not area/winding.
            auto contour_path = polypath.Polygon();
            double area = Clipper2Lib::Area(contour_path);

            // DON'T reverse - trust the PolyTree64 structure!
            (*expolygons)[cnt].contour = ClipperPath_to_Polygon(contour_path);

            // Collect holes - iterate using indexed access
            size_t hole_idx = 0;
            for (size_t i = 0; i < polypath.Count(); ++i)
            {
                const Clipper2Lib::PolyPath64 *child = polypath[i];
                if (child->IsHole())
                {
                    // Trust PolyTree64 IsHole() flag, don't reverse holes.
                    auto hole_path = child->Polygon();

                    // DON'T reverse - trust PolyTree64 IsHole() flag!
                    (*expolygons)[cnt].holes.resize(hole_idx + 1);
                    (*expolygons)[cnt].holes[hole_idx] = ClipperPath_to_Polygon(hole_path);
                    hole_idx++;

                    // Recurse for nested outer polygons within holes
                    for (size_t j = 0; j < child->Count(); ++j)
                    {
                        const Clipper2Lib::PolyPath64 *nested = (*child)[j];
                        if (!nested->IsHole())
                            PolyTree64ToExPolygonsRecursive(*nested, expolygons);
                    }
                }
            }
        }

        static size_t PolyTree64CountExPolygons(const Clipper2Lib::PolyPath64 &polypath)
        {
            size_t cnt = 1;
            for (size_t i = 0; i < polypath.Count(); ++i)
            {
                const Clipper2Lib::PolyPath64 *child = polypath[i];
                if (child->IsHole())
                {
                    for (size_t j = 0; j < child->Count(); ++j)
                    {
                        const Clipper2Lib::PolyPath64 *nested = (*child)[j];
                        if (!nested->IsHole())
                            cnt += PolyTree64CountExPolygons(*nested);
                    }
                }
            }
            return cnt;
        }
    };

    ExPolygons retval;

    // shrink_paths creates no bounding boxes, so every case takes the same plain conversion.

    // Count top-level contours
    size_t cnt = 0;
    for (size_t i = 0; i < polytree.Count(); ++i)
    {
        const Clipper2Lib::PolyPath64 *child = polytree[i];
        if (!child->IsHole())
            cnt += Inner::PolyTree64CountExPolygons(*child);
    }

    retval.reserve(cnt);

    // Process top-level contours
    for (size_t i = 0; i < polytree.Count(); ++i)
    {
        const Clipper2Lib::PolyPath64 *child = polytree[i];
        if (!child->IsHole())
        {
            Inner::PolyTree64ToExPolygonsRecursive(*child, &retval);
        }
    }

    return retval;
}

Polylines PolyTree64ToPolylines(PolyTree64 &&polytree)
{
    struct Inner
    {
        static void AddPolyPath64ToPaths(Clipper2Lib::PolyPath64 &polynode, Polylines &out)
        {
            const auto &polygon = polynode.Polygon();
            if (!polygon.empty())
                out.emplace_back(ClipperPath_to_Points(polygon));
            // Iterate children using Count() and operator[]
            for (size_t i = 0; i < polynode.Count(); ++i)
                AddPolyPath64ToPaths(*polynode[i], out);
        }

        static size_t CountTotal(const Clipper2Lib::PolyPath64 &polynode)
        {
            size_t count = polynode.Polygon().empty() ? 0 : 1;
            for (size_t i = 0; i < polynode.Count(); ++i)
                count += CountTotal(*polynode[i]);
            return count;
        }
    };

    Polylines out;
    // Clipper2: PolyTree64 doesn't have Total(), compute it recursively
    size_t total = 0;
    for (size_t i = 0; i < polytree.Count(); ++i)
        total += Inner::CountTotal(*polytree[i]);
    out.reserve(total);

    for (size_t i = 0; i < polytree.Count(); ++i)
        Inner::AddPolyPath64ToPaths(*polytree[i], out);
    return out;
}

// Local test inside each of the contours.
bool has_duplicate_points(const PolyTree64 &polytree)
{
    struct Helper
    {
        static bool has_duplicate_points_recursive(const Clipper2Lib::PolyPath64 &polynode)
        {
            const auto &contour = polynode.Polygon();
            if (has_duplicate_points(ClipperPath_to_Points(contour)))
                return true;
            for (size_t i = 0; i < polynode.Count(); ++i)
                if (has_duplicate_points_recursive(*polynode[i]))
                    return true;
            return false;
        }
    };
    for (size_t i = 0; i < polytree.Count(); ++i)
        if (Helper::has_duplicate_points_recursive(*polytree[i]))
            return true;
    return false;
}

// Offset CCW contours outside, CW contours (holes) inside.
// Don't calculate union of the output paths.
template<typename PathsProvider>
static Paths raw_offset(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit,
                        EndType endType = EndType::Polygon)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Clipper2Lib::ClipperOffset co;
    Clipper2Lib::Paths64 out;
    out.reserve(paths.size());
    Clipper2Lib::Paths64 out_this;

    // Clipper2 spells these as methods rather than properties
    if (joinType == JoinType::Round)
        co.ArcTolerance(miterLimit); // Method call, not property
    else
        co.MiterLimit(miterLimit); // Method call, not property

    // Note: ShortestEdgeLength not in Clipper2 - handled internally
    for (auto &&item : paths)
    {
        // Convert to Path64 - handles both Points and Path64 input
        Clipper2Lib::Path64 path;
        if constexpr (std::is_same_v<std::decay_t<decltype(item)>, Points>)
        {
            path = Points_to_ClipperPath(item);
        }
        else
        {
            path = item; // Already Path64
        }
        co.Clear();
        co.AddPath(path, joinType, endType);
        bool ccw = endType == Clipper2Lib::EndType::Polygon ? Clipper2Lib::IsPositive(path) : true;
        // Execute takes the delta first and the result second
        co.Execute(ccw ? offset : -offset, out_this);
        // Clipper2's winding is already right, so no reversal here.
        append(out, std::move(out_this));
    }
    return out;
}

// For move-only types like PolyTree64, always use std::move

// Offset outside by 10um, one by one.
template<typename PathsProvider>
static Paths safety_offset(PathsProvider &&paths)
{
    return raw_offset(std::forward<PathsProvider>(paths), ClipperSafetyOffset, DefaultJoinType, DefaultMiterLimit);
}

// Generic template for copyable return types (Paths, etc.)
template<class TResult, class TSubj, class TClip>
typename std::enable_if<!std::is_same<TResult, PolyTree64>::value, TResult>::type clipper_do(const ClipType clipType,
                                                                                             TSubj &&subject,
                                                                                             TClip &&clip,
                                                                                             const FillRule fillType)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);
    CLIPPER_METRICS_START();

#ifdef CLIPPER2_VERIFY_USAGE
    log_clipper_version();
#endif

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(PathsProvider_to_Paths64(std::forward<TSubj>(subject)));
    clipper.AddClip(PathsProvider_to_Paths64(std::forward<TClip>(clip)));
    TResult retval;
    clipper.Execute(clipType, fillType, retval);
    CLIPPER_METRICS_END("clipper_do [Clipper2]");
    return retval; // Works for copyable types
}

// Helper for move-only PolyTree64 type - uses output parameter
template<class TSubj, class TClip>
static void clipper_do_polytree_direct(const ClipType clipType, TSubj &&subject, TClip &&clip, const FillRule fillType,
                                       PolyTree64 &out_result)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);
    CLIPPER_METRICS_START();

#ifdef CLIPPER2_VERIFY_USAGE
    log_clipper_version();
#endif

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(PathsProvider_to_Paths64(std::forward<TSubj>(subject)));
    clipper.AddClip(PathsProvider_to_Paths64(std::forward<TClip>(clip)));
    clipper.Execute(clipType, fillType, out_result);
    CLIPPER_METRICS_END("clipper_do [Clipper2]");
}

template<class TResult, class TSubj, class TClip>
TResult clipper_do(const ClipType clipType, TSubj &&subject, TClip &&clip, const FillRule fillType,
                   const ApplySafetyOffset do_safety_offset)
{
    // Safety offset only allowed on intersection and difference.
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipType::Union);
    return do_safety_offset == ApplySafetyOffset::Yes
               ? clipper_do<TResult>(clipType, std::forward<TSubj>(subject), safety_offset(std::forward<TClip>(clip)),
                                     fillType)
               : clipper_do<TResult>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip), fillType);
}

// Generic template for copyable return types (Paths, etc.)
// Do not use with TResult=PolyTree64, use clipper_union_polytree() instead
template<class TResult, class TSubj>
typename std::enable_if<!std::is_same<TResult, PolyTree64>::value, TResult>::type clipper_union(
    TSubj &&subject,
    // fillType FillRule::NonZero and FillRule::Positive "should" produce the same result for "normalized with implicit union" set of polygons
    const FillRule fillType = FillRule::NonZero)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(PathsProvider_to_Paths64(std::forward<TSubj>(subject))); // Convert provider to Paths64
    TResult retval;
    clipper.Execute(Clipper2Lib::ClipType::Union, fillType, retval);
    return retval; // Works for copyable types
}

// PolyPath64 has deleted copy constructor (contains std::vector<std::unique_ptr<>>)
// MSVC's template validation rejects return-by-value even with move semantics
// Solution: Use output parameter pattern instead of return-by-value

// Helper that performs union and stores result in output parameter
template<class TSubj>
static void clipper_union_polytree(TSubj &&subject, const FillRule fillType, PolyTree64 &out_result)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Clipper2Lib::Clipper64 clipper;
    auto input_paths = PathsProvider_to_Paths64(std::forward<TSubj>(subject));
    clipper.AddSubject(input_paths);
    clipper.Execute(Clipper2Lib::ClipType::Union, fillType, out_result);
}

// Perform union of input polygons using the positive rule, convert to ExPolygons.
// Removed inline to export function
ExPolygons ClipperPaths_to_ExPolygons(const Paths &input, bool do_union)
{
    PolyTree64 polytree;
    clipper_union_polytree(input, do_union ? FillRule::NonZero : FillRule::EvenOdd, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

// Reconstruct ExPolygons from paths whose winding is trusted (contours CCW, holes CW),
// e.g. the output of the per-path offset helpers. The Positive fill rule discards
// regions of non-positive winding, so a hole grown past its contour by a negative
// offset annihilates the contour instead of surviving as a phantom solid.
static ExPolygons WindingTrustedPaths_to_ExPolygons(const Paths &input)
{
    PolyTree64 polytree;
    clipper_union_polytree(input, FillRule::Positive, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

// Overload for Luminary::Polygons - converts to Paths first
ExPolygons ClipperPaths_to_ExPolygons(const Polygons &input, bool do_union)
{
    // Convert Luminary::Polygons to Clipper2Lib::Paths64, keeping the winding
    Paths paths;
    paths.reserve(input.size());
    for (const Polygon &poly : input)
    {
        auto path = Points_to_ClipperPath(poly.points);
        paths.emplace_back(std::move(path));
    }
    // Call the Paths version
    return ClipperPaths_to_ExPolygons(paths, do_union);
}

template<typename PathsProvider>
static Paths raw_offset_polyline(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit,
                                 EndType end_type = EndType::Butt)
{
    assert(offset > 0);
    return raw_offset<PathsProvider>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit, end_type);
}

template<class TResult, typename PathsProvider>
static TResult expand_paths(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit)
{
    assert(offset > 0);
    return clipper_union<TResult>(raw_offset(std::forward<PathsProvider>(paths), offset, joinType, miterLimit));
}

// Internal implementation that uses output parameter to avoid return-by-value issues
template<class TResult, typename PathsProvider>
static void shrink_paths_impl(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit, TResult &out)
{
    assert(offset > 0);
    // raw_offset preserves winding: contours stay CCW and shrink, holes stay CW and grow.
    // The regularizing union must use the Positive fill rule: a hole grown past its contour
    // leaves regions of winding <= 0 which must vanish. NonZero would fill the winding -1
    // region left by an overgrown hole, fabricating a phantom polygon where no material exists.
    if (auto raw = raw_offset(std::forward<PathsProvider>(paths), -offset, joinType, miterLimit); !raw.empty())
    {
        Clipper2Lib::Clipper64 clipper;
        clipper.AddSubject(raw);
        clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::Positive, out);
    }
}

// Public interface for copyable types (excludes PolyTree64)
template<class TResult, typename PathsProvider>
static typename std::enable_if<!std::is_same<TResult, PolyTree64>::value, TResult>::type shrink_paths(
    PathsProvider &&paths, float offset, JoinType joinType, double miterLimit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    TResult out;
    shrink_paths_impl(std::forward<PathsProvider>(paths), offset, joinType, miterLimit, out);
    return out; // Works for copyable types
}

// Helper for move-only PolyTree64 type - uses output parameter
template<typename PathsProvider>
static void shrink_paths_polytree(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit,
                                  PolyTree64 &out_result)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);
    shrink_paths_impl(std::forward<PathsProvider>(paths), offset, joinType, miterLimit, out_result);
}

// Generic template for copyable types (excludes PolyTree64)
template<class TResult, typename PathsProvider>
static typename std::enable_if<!std::is_same<TResult, PolyTree64>::value, TResult>::type offset_paths(
    PathsProvider &&paths, float offset, JoinType joinType, double miterLimit)
{
    assert(offset != 0);
    if (offset > 0)
        return expand_paths<TResult>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit);
    else
        return shrink_paths<TResult>(std::forward<PathsProvider>(paths), -offset, joinType, miterLimit);
}

// Helper for move-only PolyTree64 type - uses output parameter
template<typename PathsProvider>
static void offset_paths_polytree(PathsProvider &&paths, float offset, JoinType joinType, double miterLimit,
                                  PolyTree64 &out_result)
{
    assert(offset != 0);
    if (offset > 0)
    {
        auto expanded = expand_paths<Paths>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit);
        clipper_union_polytree(std::move(expanded), FillRule::NonZero, out_result);
    }
    else
    {
        shrink_paths_polytree(std::forward<PathsProvider>(paths), -offset, joinType, miterLimit, out_result);
    }
}

Luminary::Polygons offset(const Luminary::Polygon &polygon, const float delta, JoinType joinType, double miterLimit)
{
    return ClipperPaths_to_Polygons(
        raw_offset(ClipperUtils::SinglePathProvider(polygon.points), delta, joinType, miterLimit));
}

Luminary::Polygons offset(const Luminary::Polygons &polygons, const float delta, JoinType joinType, double miterLimit)
{
    return ClipperPaths_to_Polygons(
        offset_paths<Paths>(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit));
}
Luminary::ExPolygons offset_ex(const Luminary::Polygons &polygons, const float delta, JoinType joinType,
                               double miterLimit)
{
    PolyTree64 polytree;
    offset_paths_polytree(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

Luminary::Polygons offset(const Luminary::Polyline &polyline, const float delta, JoinType joinType, double miterLimit,
                          EndType end_type)
{
    assert(delta > 0);
    return ClipperPaths_to_Polygons(clipper_union<Paths>(
        raw_offset_polyline(ClipperUtils::SinglePathProvider(polyline.points), delta, joinType, miterLimit, end_type)));
}
Luminary::Polygons offset(const Luminary::Polylines &polylines, const float delta, JoinType joinType, double miterLimit,
                          EndType end_type)
{
    assert(delta > 0);
    return ClipperPaths_to_Polygons(clipper_union<Paths>(
        raw_offset_polyline(ClipperUtils::PolylinesProvider(polylines), delta, joinType, miterLimit, end_type)));
}

Polygons contour_to_polygons(const Polygon &polygon, const float line_width, JoinType join_type, double miter_limit)
{
    assert(line_width > 1.f);
    return ClipperPaths_to_Polygons(
        clipper_union<Paths>(raw_offset(ClipperUtils::SinglePathProvider(polygon.points), line_width / 2, join_type,
                                        miter_limit, EndType::Joined)));
}
Polygons contour_to_polygons(const Polygons &polygons, const float line_width, JoinType join_type, double miter_limit)
{
    assert(line_width > 1.f);
    return ClipperPaths_to_Polygons(clipper_union<Paths>(
        raw_offset(ClipperUtils::PolygonsProvider(polygons), line_width / 2, join_type, miter_limit, EndType::Joined)));
}

// returns number of expolygons collected (0 or 1).
static int offset_expolygon_inner(const Luminary::ExPolygon &expoly, const float delta, JoinType joinType,
                                  double miterLimit, Paths &out)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // 1) Offset the outer contour.
    Paths contours;
    {
        Clipper2Lib::ClipperOffset co;
        if (joinType == JoinType::Round)
            co.ArcTolerance(miterLimit);
        else
            co.MiterLimit(miterLimit);
        // Note: ShortestEdgeLength removed in Clipper2
        co.AddPath(Points_to_ClipperPath(expoly.contour.points), joinType, EndType::Polygon);
        co.Execute(delta, contours); // Parameter swap
    }
    if (contours.empty())
        // No need to try to offset the holes.
        return 0;

    if (expoly.holes.empty())
    {
        // No need to subtract holes from the offsetted expolygon, we are done.
        append(out, std::move(contours));
    }
    else
    {
        // 2) Offset the holes one by one, collect the offsetted holes.
        Paths holes;
        {
            for (const Polygon &hole : expoly.holes)
            {
                Clipper2Lib::ClipperOffset co;
                if (joinType == JoinType::Round)
                    co.ArcTolerance(miterLimit);
                else
                    co.MiterLimit(miterLimit);
                // Note: ShortestEdgeLength removed in Clipper2
                co.AddPath(Points_to_ClipperPath(hole.points), joinType, EndType::Polygon);
                Paths out2;
                // ClipperOffset retains the input orientation (ExecuteInternal reverses the
                // solution for reversed groups), so these CW holes come back CW. The delta is
                // applied relative to the path's winding and holes wind opposite to contours,
                // so the ExPolygon's delta is negated here. Downstream relies on the CW output:
                // the PolyTree64 hierarchy reconstruction below and the Positive-fill unions in
                // the offset legs both classify holes by their negative area.
                co.Execute(-delta, out2);

                append(holes, std::move(out2));
            }
        }

        // 3) Combine contours and holes - let PolyTree64 reconstruct hierarchy
        if (holes.empty())
        {
            // No hole remaining after an offset. Just copy the outer contour.
            append(out, std::move(contours));
        }
        else
        {
            // For both a positive and a negative offset, append contours (CCW) and holes (CW) to the
            // output and let ClipperPaths_to_ExPolygons() rebuild the hierarchy from the winding.
            // A ClipType::Difference here would return flat Paths with no hierarchy, losing the holes.
            //
            // Why this works:
            // - Contours are CCW (positive area)
            // - Holes are CW (negative area): ClipperOffset retains the input orientation
            // - ClipperPaths_to_ExPolygons() uses PolyTree64 which respects winding order
            // - If holes grow so large they intersect/exceed the contour, PolyTree64 handles it correctly

            out.reserve(contours.size() + holes.size());
            append(out, std::move(contours));
            // Holes stay CW so PolyTree64 recognizes them as holes
            append(out, std::move(holes));
        }
    }

    return 1;
}

static int offset_expolygon_inner(const Luminary::Surface &surface, const float delta, JoinType joinType,
                                  double miterLimit, Paths &out)
{
    return offset_expolygon_inner(surface.expolygon, delta, joinType, miterLimit, out);
}
static int offset_expolygon_inner(const Luminary::Surface *surface, const float delta, JoinType joinType,
                                  double miterLimit, Paths &out)
{
    return offset_expolygon_inner(surface->expolygon, delta, joinType, miterLimit, out);
}

Paths expolygon_offset(const Luminary::ExPolygon &expolygon, const float delta, JoinType joinType, double miterLimit)
{
    Paths out;
    offset_expolygon_inner(expolygon, delta, joinType, miterLimit, out);
    return out;
}

// This is a safe variant of the polygons offset, tailored for multiple ExPolygons.
// It is required, that the input expolygons do not overlap and that the holes of each ExPolygon don't intersect with their respective outer contours.
// Each ExPolygon is offsetted separately. For outer offset, the the offsetted ExPolygons shall be united outside of this function.
template<typename ExPolygonVector>
static std::pair<Paths, size_t> expolygons_offset_raw(const ExPolygonVector &expolygons, const float delta,
                                                      JoinType joinType, double miterLimit)
{
    // Offsetted ExPolygons before they are united.
    Paths output;
    output.reserve(expolygons.size());
    // How many non-empty offsetted expolygons were actually collected into output?
    // If only one, then there is no need to do a final union.
    size_t expolygons_collected = 0;
    for (const auto &expoly : expolygons)
        expolygons_collected += offset_expolygon_inner(expoly, delta, joinType, miterLimit, output);
    return std::make_pair(std::move(output), expolygons_collected);
}

// See comment on expolygon_offsets_raw. In addition, for positive offset the contours are united.
template<typename ExPolygonVector>
static Paths expolygons_offset(const ExPolygonVector &expolygons, const float delta, JoinType joinType,
                               double miterLimit)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // Unite the offsetted expolygons.
    if (expolygons_collected > 1 && delta > 0)
        // There is a chance that the outwards offsetted expolygons may intersect. Perform a union.
        return clipper_union<Paths>(output);
    if (delta < 0)
    {
        // The shrunk expolygons cannot mutually intersect, but a hole grown past its own
        // contour would escape as a stray CW path and become phantom area downstream.
        // Holes are the CW (negative area) paths; regularize with a Positive union so an
        // overgrown hole annihilates its contour. Hole-free output needs no union.
        for (const auto &path : output)
            if (Clipper2Lib::Area(path) < 0.)
                return clipper_union<Paths>(output, FillRule::Positive);
    }
    return output;
}

// See comment on expolygons_offset_raw. In addition, the polygons are always united to conver to polytree.
template<typename ExPolygonVector>
static void expolygons_offset_pt(const ExPolygonVector &expolygons, const float delta, JoinType joinType,
                                 double miterLimit, PolyTree64 &out_result)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // The offset output has trusted winding (contours CCW, holes CW). Positive keeps
    // holes intact and lets a hole grown past its contour annihilate the contour,
    // where NonZero would fill the overflow region with phantom area.
    clipper_union_polytree(output, FillRule::Positive, out_result);
}

Luminary::Polygons offset(const Luminary::ExPolygon &expolygon, const float delta, JoinType joinType, double miterLimit)
{
    return ClipperPaths_to_Polygons(expolygon_offset(expolygon, delta, joinType, miterLimit));
}
Luminary::Polygons offset(const Luminary::ExPolygons &expolygons, const float delta, JoinType joinType,
                          double miterLimit)
{
    return ClipperPaths_to_Polygons(expolygons_offset(expolygons, delta, joinType, miterLimit));
}
Luminary::Polygons offset(const Luminary::Surfaces &surfaces, const float delta, JoinType joinType, double miterLimit)
{
    return ClipperPaths_to_Polygons(expolygons_offset(surfaces, delta, joinType, miterLimit));
}
Luminary::Polygons offset(const Luminary::SurfacesPtr &surfaces, const float delta, JoinType joinType,
                          double miterLimit)
{
    return ClipperPaths_to_Polygons(expolygons_offset(surfaces, delta, joinType, miterLimit));
}
Luminary::ExPolygons offset_ex(const Luminary::ExPolygon &expolygon, const float delta, JoinType joinType,
                               double miterLimit)
{
    // expolygon_offset preserves winding, so reconstruct with the Positive rule.
    return WindingTrustedPaths_to_ExPolygons(expolygon_offset(expolygon, delta, joinType, miterLimit));
}
Luminary::ExPolygons offset_ex(const Luminary::ExPolygons &expolygons, const float delta, JoinType joinType,
                               double miterLimit)
{
    PolyTree64 polytree;
    expolygons_offset_pt(expolygons, delta, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons offset_ex(const Luminary::Surfaces &surfaces, const float delta, JoinType joinType,
                               double miterLimit)
{
    PolyTree64 polytree;
    expolygons_offset_pt(surfaces, delta, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons offset_ex(const Luminary::SurfacesPtr &surfaces, const float delta, JoinType joinType,
                               double miterLimit)
{
    PolyTree64 polytree;
    expolygons_offset_pt(surfaces, delta, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

// This function offsets ExPolygons such that:
// - Outer contours shrink inward (for negative delta) - creates outer perimeter band
// - Holes SHRINK (get smaller) instead of expanding - creates inner perimeter band around holes
//
// Standard offset_ex with negative delta causes holes to EXPAND (due to using -delta for holes),
// which "eats into" painted regions. For fuzzy skin, we want holes to shrink so that:
// 1. Painted outer surfaces near holes are preserved
// 2. Painted hole interiors have a perimeter band to be "inside" of
Luminary::ExPolygons offset_ex_contour_only(const Luminary::ExPolygons &expolygons, const float delta,
                                            JoinType joinType, double miterLimit)
{
    ExPolygons result;
    result.reserve(expolygons.size());

    for (const ExPolygon &expoly : expolygons)
    {
        // 1) Offset the outer contour (as a simple polygon without holes)
        ExPolygons contour_offset = offset_ex(ExPolygon(expoly.contour), delta, joinType, miterLimit);

        if (contour_offset.empty())
            continue;

        // For shrinking (negative delta), we may get multiple disjoint contours
        // if the original shape gets split. Process each resulting contour.
        for (ExPolygon &new_expoly : contour_offset)
        {
            // 2) For each original hole, SHRINK it (not expand) by applying negative offset
            // Standard offset_ex uses -delta for holes which EXPANDS them.
            // We want to SHRINK holes, so we offset the hole boundary with the same delta sign.
            // For CW hole with negative offset: boundary moves inward (hole shrinks)
            for (const Polygon &hole : expoly.holes)
            {
                // Convert hole to CCW ExPolygon for offset operation
                Polygon hole_as_contour = hole;
                hole_as_contour.make_counter_clockwise();
                ExPolygon hole_expoly(hole_as_contour);

                // Offset the hole with NEGATIVE delta (same as outer contour)
                // This shrinks the hole (makes it smaller) instead of expanding it
                ExPolygons hole_shrunk = offset_ex(hole_expoly, delta, joinType, miterLimit);

                // Clip shrunk hole to be within the new contour
                // (handles case where shrunk hole would extend outside)
                for (const ExPolygon &shrunk : hole_shrunk)
                {
                    ExPolygons hole_clipped = intersection_ex(ExPolygons{shrunk}, ExPolygons{new_expoly});

                    // If hole survives intersection, add it back
                    for (const ExPolygon &clipped : hole_clipped)
                    {
                        // Convert back to hole (ensure CW orientation)
                        Polygon new_hole = clipped.contour;
                        new_hole.make_clockwise();
                        new_expoly.holes.push_back(std::move(new_hole));
                    }
                }
            }
            result.push_back(std::move(new_expoly));
        }
    }
    return result;
}

Polygons offset2(const ExPolygons &expolygons, const float delta1, const float delta2, JoinType joinType,
                 double miterLimit)
{
    return ClipperPaths_to_Polygons(
        offset_paths<Paths>(expolygons_offset(expolygons, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
// Convert back to ExPolygons after the first offset and offset those, so the holes survive:
// offset_paths_polytree would treat every path as a contour and lose them.
ExPolygons offset2_ex(const ExPolygons &expolygons, const float delta1, const float delta2, JoinType joinType,
                      double miterLimit)
{
    // First offset: expand (or shrink) - this returns Paths with CW holes
    Paths paths1 = expolygons_offset(expolygons, delta1, joinType, miterLimit);

    // The intermediate paths have trusted winding (contours CCW, holes CW), so a
    // Positive-rule reconstruction keeps the holes and drops any hole overflow.
    ExPolygons expolygons1 = WindingTrustedPaths_to_ExPolygons(paths1);

    // Second offset: on ExPolygons, which offsets holes as holes.
    ExPolygons result = offset_ex(expolygons1, delta2, joinType, miterLimit);

    return result;
}
ExPolygons offset2_ex(const Surfaces &surfaces, const float delta1, const float delta2, JoinType joinType,
                      double miterLimit)
{
    PolyTree64 polytree;
    offset_paths_polytree(expolygons_offset(surfaces, delta1, joinType, miterLimit), delta2, joinType, miterLimit,
                          polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

// Offset outside, then inside produces morphological closing. All deltas should be positive.
Luminary::Polygons closing(const Luminary::Polygons &polygons, const float delta1, const float delta2,
                           JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return ClipperPaths_to_Polygons(
        shrink_paths<Paths>(expand_paths<Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit),
                            delta2, joinType, miterLimit));
}
Luminary::ExPolygons closing_ex(const Luminary::Polygons &polygons, const float delta1, const float delta2,
                                JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    PolyTree64 polytree;
    shrink_paths_polytree(expand_paths<Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit),
                          delta2, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons closing_ex(const Luminary::Surfaces &surfaces, const float delta1, const float delta2,
                                JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    PolyTree64 polytree;
    shrink_paths_polytree(expand_paths<Paths>(ClipperUtils::SurfacesProvider(surfaces), delta1, joinType, miterLimit),
                          delta2, joinType, miterLimit, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

// Offset inside, then outside produces morphological opening. All deltas should be positive.
Luminary::Polygons opening(const Luminary::Polygons &polygons, const float delta1, const float delta2,
                           JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return ClipperPaths_to_Polygons(
        expand_paths<Paths>(shrink_paths<Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit),
                            delta2, joinType, miterLimit));
}
Luminary::Polygons opening(const Luminary::ExPolygons &expolygons, const float delta1, const float delta2,
                           JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return ClipperPaths_to_Polygons(
        expand_paths<Paths>(shrink_paths<Paths>(ClipperUtils::ExPolygonsProvider(expolygons), delta1, joinType,
                                                miterLimit),
                            delta2, joinType, miterLimit));
}
Luminary::Polygons opening(const Luminary::Surfaces &surfaces, const float delta1, const float delta2,
                           JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return ClipperPaths_to_Polygons(
        expand_paths<Paths>(shrink_paths<Paths>(ClipperUtils::SurfacesProvider(surfaces), delta1, joinType, miterLimit),
                            delta2, joinType, miterLimit));
}

// Fix of #117: A large fractal pyramid takes ages to slice
// The Clipper library has difficulties processing overlapping polygons.
// Namely, the function ClipperLib::JoinCommonEdges() has potentially a terrible time complexity if the output
// of the operation is of the PolyTree64 type.
// This function implemenets a following workaround:
// 1) Peform the Clipper operation with the output to Paths. This method handles overlaps in a reasonable time.
// 2) Run Clipper Union once again to extract the PolyTree64 from the result of 1).
// Changed to output parameter to handle move-only PolyTree64 type
template<typename PathProvider1, typename PathProvider2>
inline void clipper_do_polytree(const ClipType clipType, PathProvider1 &&subject, PathProvider2 &&clip,
                                const FillRule fillType, PolyTree64 &out_result)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // Perform the operation with the output to Paths first.
    // This pass does not generate a PolyTree64, which is a very expensive operation with the current Clipper library
    // if there are overlapping edges.
    auto output = clipper_do<Paths>(clipType, subject, clip, fillType);
    if (!output.empty())
    {
        // Perform an additional Union operation to generate the PolyTree64 ordering.
        clipper_union_polytree(std::move(output), fillType, out_result);
    }
    // else: out_result remains empty (default constructed)
}

template<typename PathProvider1, typename PathProvider2>
inline void clipper_do_polytree(const ClipType clipType, PathProvider1 &&subject, PathProvider2 &&clip,
                                const FillRule fillType, const ApplySafetyOffset do_safety_offset,
                                PolyTree64 &out_result)
{
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipType::Union);
    if (do_safety_offset == ApplySafetyOffset::Yes)
        clipper_do_polytree(clipType, std::forward<PathProvider1>(subject),
                            safety_offset(std::forward<PathProvider2>(clip)), fillType, out_result);
    else
        clipper_do_polytree(clipType, std::forward<PathProvider1>(subject), std::forward<PathProvider2>(clip), fillType,
                            out_result);
}

template<class TSubj, class TClip>
static inline Polygons _clipper(ClipType clipType, TSubj &&subject, TClip &&clip, ApplySafetyOffset do_safety_offset)
{
    return ClipperPaths_to_Polygons(clipper_do<Paths>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip),
                                                      FillRule::NonZero, do_safety_offset));
}

Luminary::Polygons diff(const Luminary::Polygon &subject, const Luminary::Polygon &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::SinglePathProvider(subject.points),
                    ClipperUtils::SinglePathProvider(clip.points), do_safety_offset);
}
Luminary::Polygons diff(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Luminary::Polygons diff_clipped(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return diff(subject,
                ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip,
                                                                      get_extents(subject).inflated(SCALED_EPSILON)),
                do_safety_offset);
}
Luminary::Polygons diff(const Luminary::Polygons &subject, const Luminary::ExPolygons &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::PolygonsProvider(subject),
                    ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons diff(const Luminary::ExPolygons &subject, const Luminary::Polygons &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::ExPolygonsProvider(subject),
                    ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons diff(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::ExPolygonsProvider(subject),
                    ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons diff(const Luminary::Surfaces &subject, const Luminary::Polygons &clip,
                        ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Difference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Polygon &subject, const Luminary::Polygon &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::SinglePathProvider(subject.points),
                    ClipperUtils::SinglePathProvider(clip.points), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Polygon &subject, const Luminary::ExPolygon &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::SinglePathProvider(subject.points),
                    ClipperUtils::ExPolygonProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection_clipped(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                                        ApplySafetyOffset do_safety_offset)
{
    return intersection(subject,
                        ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(
                                                                                        SCALED_EPSILON)),
                        do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Polygons &subject, const Luminary::ExPolygon &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::PolygonsProvider(subject),
                    ClipperUtils::ExPolygonProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::PolygonsProvider(subject),
                    ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::ExPolygon &subject, const Luminary::ExPolygon &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::ExPolygonProvider(subject),
                    ClipperUtils::ExPolygonProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::ExPolygons &subject, const Luminary::Polygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::ExPolygonsProvider(subject),
                    ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::ExPolygonsProvider(subject),
                    ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Surfaces &subject, const Luminary::Polygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::SurfacesProvider(subject),
                    ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons intersection(const Luminary::Surfaces &subject, const Luminary::ExPolygons &clip,
                                ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipType::Intersection, ClipperUtils::SurfacesProvider(subject),
                    ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::Polygons union_(const Luminary::Polygons &subject)
{
    return _clipper(ClipType::Union, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                    ApplySafetyOffset::No);
}
Luminary::Polygons union_(const Luminary::Polygons &subject, const FillRule fillType)
{
    return ClipperPaths_to_Polygons(clipper_do<Paths>(ClipType::Union, ClipperUtils::PolygonsProvider(subject),
                                                      ClipperUtils::EmptyPathsProvider(), fillType,
                                                      ApplySafetyOffset::No));
}
Luminary::Polygons union_unknown_winding(const Luminary::Polygons &loops, UnknownWindingStats *stats)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // Canonicalize each loop (CCW winding, rotated to its lexicographically smallest vertex)
    // so exact duplicates compare equal regardless of direction or start point. Bounds and
    // area per kept loop feed the near-duplicate test here and the containment prefilter
    // below. A loop matching a kept loop's bounds within near_dup_tol and its area within a
    // perimeter's worth of that drift is the same physical wall re-emitted with coordinate
    // drift (the solver drifts ~1nm; 100nm sits far below any printable feature). It must be
    // dropped like an exact duplicate BEFORE containment counting: counted as containment it
    // would flip one copy to a hole and the pair would annihilate under the Positive union,
    // and kept as a sibling it would inflate the depth of every loop inside it. A genuine
    // hole hugging its contour within 100nm is sub-printable; keeping material is the safe
    // reading.
    constexpr int64_t near_dup_tol = 100;
    auto near_edge = [](int64_t a, int64_t b)
    {
        const int64_t d = a > b ? a - b : b - a;
        return d <= near_dup_tol;
    };
    Paths canonical;
    std::vector<Clipper2Lib::Rect64> bounds;
    std::vector<double> areas;
    canonical.reserve(loops.size());
    bounds.reserve(loops.size());
    areas.reserve(loops.size());
    for (const Polygon &loop : loops)
    {
        if (loop.points.size() < 3)
            continue;
        Clipper2Lib::Path64 path = Points_to_ClipperPath(loop.points);
        if (!Clipper2Lib::IsPositive(path))
            std::reverse(path.begin(), path.end());
        auto mn = std::min_element(path.begin(), path.end(),
                                   [](const Clipper2Lib::Point64 &a, const Clipper2Lib::Point64 &b)
                                   { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        std::rotate(path.begin(), mn, path.end());
        const Clipper2Lib::Rect64 path_bounds = Clipper2Lib::GetBounds(path);
        const double path_area = Clipper2Lib::Area(path);
        const double max_area_drift = 4. *
                                      double((path_bounds.right - path_bounds.left) +
                                             (path_bounds.bottom - path_bounds.top)) *
                                      double(near_dup_tol);
        bool duplicate = false, near_duplicate = false;
        for (size_t k = 0; k < canonical.size() && !duplicate && !near_duplicate; ++k)
        {
            if (canonical[k] == path)
                duplicate = true;
            else if (near_edge(path_bounds.left, bounds[k].left) && near_edge(path_bounds.right, bounds[k].right) &&
                     near_edge(path_bounds.top, bounds[k].top) && near_edge(path_bounds.bottom, bounds[k].bottom) &&
                     (path_area > areas[k] ? path_area - areas[k] : areas[k] - path_area) <= max_area_drift)
                near_duplicate = true;
        }
        if (duplicate)
        {
            if (stats)
                ++stats->duplicates_dropped;
        }
        else if (near_duplicate)
        {
            if (stats)
                ++stats->near_duplicates_dropped;
        }
        else
        {
            canonical.emplace_back(std::move(path));
            bounds.emplace_back(path_bounds);
            areas.emplace_back(path_area);
        }
    }

    // Containment decided by the first vertex that is strictly inside or outside; vertices
    // lying on the other loop's boundary are skipped. A loop whose vertices ALL lie on the
    // other loop (a boundary-coincident near-duplicate) counts as not contained, so it
    // behaves like a duplicate under the Positive union instead of cancelling as a hole.
    auto contains_loop = [](const Clipper2Lib::Path64 &outer, const Clipper2Lib::Path64 &inner)
    {
        for (const Clipper2Lib::Point64 &v : inner)
        {
            Clipper2Lib::PointInPolygonResult r = Clipper2Lib::PointInPolygon(v, outer);
            if (r == Clipper2Lib::PointInPolygonResult::IsOn)
                continue;
            return r == Clipper2Lib::PointInPolygonResult::IsInside;
        }
        return false;
    };
    Paths oriented;
    oriented.reserve(canonical.size());
    for (size_t i = 0; i < canonical.size(); ++i)
    {
        int depth = 0;
        for (size_t j = 0; j < canonical.size(); ++j)
        {
            if (i == j)
                continue;
            // A contained loop's bounds lie within its container's; skip the vertex
            // walk when they cannot.
            if (!bounds[j].Contains(bounds[i]))
                continue;
            if (contains_loop(canonical[j], canonical[i]))
                ++depth;
        }
        Clipper2Lib::Path64 path = canonical[i];
        if (depth & 1)
        {
            // Odd containment depth: this loop is a hole; orient CW.
            std::reverse(path.begin(), path.end());
            if (stats)
                ++stats->hole_loops;
        }
        oriented.emplace_back(std::move(path));
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(oriented);
    Paths out;
    clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::Positive, out);
    return ClipperPaths_to_Polygons(out);
}
Luminary::Polygons union_(const Luminary::ExPolygons &subject)
{
    return _clipper(ClipType::Union, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                    ApplySafetyOffset::No);
}
Luminary::Polygons union_(const Luminary::Polygons &subject, const Luminary::Polygon &subject2)
{
    return _clipper(ClipType::Union, ClipperUtils::PolygonsProvider(subject),
                    ClipperUtils::SinglePathProvider(subject2.points), ApplySafetyOffset::No);
}
Luminary::Polygons union_(const Luminary::Polygons &subject, const Luminary::Polygons &subject2)
{
    return _clipper(ClipType::Union, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(subject2),
                    ApplySafetyOffset::No);
}
Luminary::Polygons union_(Luminary::Polygons &&subject, const Luminary::Polygons &subject2)
{
    if (subject.empty())
        return subject2;
    if (subject2.empty())
        return subject;
    return union_(subject, subject2);
}
Luminary::Polygons union_(const Luminary::Polygons &subject, const Luminary::ExPolygon &subject2)
{
    return _clipper(ClipType::Union, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonProvider(subject2),
                    ApplySafetyOffset::No);
}

template<typename TSubject, typename TClip>
static ExPolygons _clipper_ex(ClipType clipType, TSubject &&subject, TClip &&clip, ApplySafetyOffset do_safety_offset,
                              FillRule fill_type = FillRule::NonZero)
{
    PolyTree64 polytree;
    clipper_do_polytree(clipType, std::forward<TSubject>(subject), std::forward<TClip>(clip), fill_type,
                        do_safety_offset, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

Luminary::ExPolygons diff_ex(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Polygons &subject, const Luminary::Surfaces &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::SurfacesProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Polygon &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SinglePathProvider(subject.points),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Polygons &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygon &subject, const Luminary::Polygon &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonProvider(subject),
                       ClipperUtils::SinglePathProvider(clip.points), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygon &subject, const Luminary::Polygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygon &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygons &subject, const Luminary::Polygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonsProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonsProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Surfaces &subject, const Luminary::Polygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Surfaces &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::ExPolygons &subject, const Luminary::Surfaces &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::ExPolygonsProvider(subject),
                       ClipperUtils::SurfacesProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::Surfaces &subject, const Luminary::Surfaces &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::SurfacesProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::SurfacesPtr &subject, const Luminary::Polygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SurfacesPtrProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons diff_ex(const Luminary::SurfacesPtr &subject, const Luminary::ExPolygons &clip,
                             ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Difference, ClipperUtils::SurfacesPtrProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}

Luminary::ExPolygons intersection_ex(const Luminary::Polygons &subject, const Luminary::Polygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::ExPolygon &subject, const Luminary::Polygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::ExPolygonProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::Polygons &subject, const Luminary::ExPolygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::ExPolygons &subject, const Luminary::Polygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::ExPolygonsProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::ExPolygonsProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::Surfaces &subject, const Luminary::Polygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::PolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::Surfaces &subject, const Luminary::ExPolygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::Surfaces &subject, const Luminary::Surfaces &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::SurfacesProvider(subject),
                       ClipperUtils::SurfacesProvider(clip), do_safety_offset);
}
Luminary::ExPolygons intersection_ex(const Luminary::SurfacesPtr &subject, const Luminary::ExPolygons &clip,
                                     ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Intersection, ClipperUtils::SurfacesPtrProvider(subject),
                       ClipperUtils::ExPolygonsProvider(clip), do_safety_offset);
}
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (FillRule::EvenOdd, FillRule::NonZero, FillRule::Positive, FillRule::Negative).
Luminary::ExPolygons union_ex(const Luminary::Polygons &subject, FillRule fill_type)
{
    return _clipper_ex(ClipType::Union, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                       ApplySafetyOffset::No, fill_type);
}
Luminary::ExPolygons union_ex(const Luminary::Polygons &subject, const Luminary::Polygons &subject2, FillRule fill_type)
{
    return _clipper_ex(ClipType::Union, ClipperUtils::PolygonsProvider(subject),
                       ClipperUtils::PolygonsProvider(subject2), ApplySafetyOffset::No, fill_type);
}
Luminary::ExPolygons union_ex(const Luminary::ExPolygons &subject)
{
    PolyTree64 polytree;
    clipper_do_polytree(ClipType::Union, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                        FillRule::NonZero, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons union_ex(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &subject2)
{
    PolyTree64 polytree;
    clipper_do_polytree(ClipType::Union, ClipperUtils::ExPolygonsProvider(subject),
                        ClipperUtils::ExPolygonsProvider(subject2), FillRule::NonZero, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons union_ex(const Luminary::Polygons &subject, const Luminary::ExPolygons &subject2)
{
    PolyTree64 polytree;
    clipper_do_polytree(ClipType::Union, ClipperUtils::PolygonsProvider(subject),
                        ClipperUtils::ExPolygonsProvider(subject2), FillRule::NonZero, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons union_ex(const Luminary::ExPolygons &subject, const Luminary::Polygons &subject2)
{
    PolyTree64 polytree;
    clipper_do_polytree(ClipType::Union, ClipperUtils::ExPolygonsProvider(subject),
                        ClipperUtils::PolygonsProvider(subject2), FillRule::NonZero, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}
Luminary::ExPolygons union_ex(const Luminary::Surfaces &subject)
{
    PolyTree64 polytree;
    clipper_do_polytree(ClipType::Union, ClipperUtils::SurfacesProvider(subject), ClipperUtils::EmptyPathsProvider(),
                        FillRule::NonZero, polytree);
    return PolyTree64ToExPolygons(std::move(polytree));
}

Luminary::ExPolygons xor_ex(const Luminary::ExPolygons &subject, const Luminary::ExPolygon &clip,
                            ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Xor, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                       do_safety_offset);
}
Luminary::ExPolygons xor_ex(const Luminary::ExPolygons &subject, const Luminary::ExPolygons &clip,
                            ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipType::Xor, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}

template<typename PathsProvider1, typename PathsProvider2>
Polylines _clipper_pl_open(ClipType clipType, PathsProvider1 &&subject, PathsProvider2 &&clip)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Clipper2Lib::Clipper64 clipper;

    Clipper2Lib::Paths64 subject_paths = PathsProvider_to_Paths64(std::forward<PathsProvider1>(subject));
    Clipper2Lib::Paths64 clip_paths = PathsProvider_to_Paths64(std::forward<PathsProvider2>(clip));

    clipper.AddOpenSubject(subject_paths);
    clipper.AddClip(clip_paths);

    // When using AddOpenSubject, Execute needs a second Paths64 parameter for open path results
    PolyTree64 retval;
    Clipper2Lib::Paths64 open_paths;
    bool success = clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, retval, open_paths);

    // Convert open paths to Polylines
    Polylines result;
    result.reserve(open_paths.size());
    for (const auto &path : open_paths)
    {
        result.emplace_back(ClipperPath_to_Points(path));
    }

    return result;
}

// If the split_at_first_point() call above happens to split the polygon inside the clipping area
// we would get two consecutive polylines instead of a single one, so we go through them in order
// to recombine continuous polylines.
// Merged polylines are marked and erased once at the end rather than erased inside the loop, which
// keeps the pass O(n^2) instead of O(n^3): every erase inside the loop shifts the whole tail.
// Clipper operations often fragment a continuous path into segments that share endpoints, which is
// what this recombines.
static void _clipper_pl_recombine(Polylines &polylines)
{
    if (polylines.size() <= 1)
        return; // Nothing to recombine

    // Track which polylines have been merged into others
    std::vector<bool> merged(polylines.size(), false);

    // Flag to track if any merges happened (for potential multiple passes)
    bool any_merged = true;

    // Keep trying to merge until no more merges are possible
    // This handles chains: A-B, B-C, C-D should merge into A-B-C-D
    while (any_merged)
    {
        any_merged = false;

        for (size_t i = 0; i < polylines.size(); ++i)
        {
            if (merged[i])
                continue; // Skip already-merged polylines

            for (size_t j = i + 1; j < polylines.size(); ++j)
            {
                if (merged[j])
                    continue; // Skip already-merged polylines

                bool did_merge = false;

                // Case 1: end of i connects to start of j
                if (polylines[i].points.back() == polylines[j].points.front())
                {
                    /* If last point of i coincides with first point of j,
                       append points of j to i and mark j as merged */
                    polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin() + 1,
                                               polylines[j].points.end());
                    merged[j] = true;
                    did_merge = true;
                }
                // Case 2: start of i connects to end of j
                else if (polylines[i].points.front() == polylines[j].points.back())
                {
                    /* If first point of i coincides with last point of j,
                       prepend points of j to i and mark j as merged */
                    polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(),
                                               polylines[j].points.end() - 1);
                    merged[j] = true;
                    did_merge = true;
                }
                // Case 3: start of i connects to start of j (need to reverse j)
                else if (polylines[i].points.front() == polylines[j].points.front())
                {
                    /* Since Clipper does not preserve orientation of polylines,
                       also check the case when first point of i coincides with first point of j. */
                    polylines[j].reverse();
                    polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(),
                                               polylines[j].points.end() - 1);
                    merged[j] = true;
                    did_merge = true;
                }
                // Case 4: end of i connects to end of j (need to reverse j)
                else if (polylines[i].points.back() == polylines[j].points.back())
                {
                    /* Since Clipper does not preserve orientation of polylines,
                       also check the case when last point of i coincides with last point of j. */
                    polylines[j].reverse();
                    polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin() + 1,
                                               polylines[j].points.end());
                    merged[j] = true;
                    did_merge = true;
                }

                if (did_merge)
                {
                    any_merged = true;
                    // Don't break - continue checking if this extended polyline
                    // can merge with more polylines in this pass
                }
            }
        }
    }

    // Rebuild the polylines vector with only non-merged entries
    if (std::any_of(merged.begin(), merged.end(), [](bool m) { return m; }))
    {
        Polylines result;
        result.reserve(polylines.size()); // Reserve maximum possible size

        for (size_t i = 0; i < polylines.size(); ++i)
        {
            if (!merged[i])
            {
                result.emplace_back(std::move(polylines[i]));
            }
        }

        polylines = std::move(result);
    }
}

template<typename PathProvider1, typename PathProvider2>
Polylines _clipper_pl_closed(ClipType clipType, PathProvider1 &&subject, PathProvider2 &&clip)
{
    // For clipping closed polygons (to get polylines), we need to use AddSubject (closed)
    // not AddOpenSubject, and retrieve results from the PolyTree64, not open_paths

    Clipper2Lib::Paths64 subject_paths = PathsProvider_to_Paths64(std::forward<PathProvider1>(subject));
    Clipper2Lib::Paths64 clip_paths = PathsProvider_to_Paths64(std::forward<PathProvider2>(clip));

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(subject_paths); // Use AddSubject for closed polygons
    clipper.AddClip(clip_paths);

    // Execute and get results as Paths64 (closed polygons)
    Clipper2Lib::Paths64 solution;
    bool success = clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, solution);

    // Convert closed polygon results to Polylines by splitting at the first point
    Polylines retval;
    retval.reserve(solution.size());
    for (const Clipper2Lib::Path64 &path : solution)
    {
        if (path.size() >= 2)
        {
            // Convert to engine points and create the polyline
            Points points = ClipperPath_to_Points(path);

            // Clipper2 returns closed polygons with implicit closure [P0,P1,P2,P3], P3 back to P0
            // To convert to an open polyline that represents the same closed path,
            // we must duplicate the first point at the end: [P0,P1,P2,P3,P0]
            if (!points.empty() && points.front() != points.back())
            {
                points.push_back(points.front());
            }

            retval.emplace_back(std::move(points));
        }
    }

    _clipper_pl_recombine(retval);
    return retval;
}

Luminary::Polylines diff_pl(const Luminary::Polyline &subject, const Luminary::Polygons &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::PolygonsProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polylines &subject, const Luminary::Polygons &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::PolygonsProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polyline &subject, const Luminary::ExPolygon &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polyline &subject, const Luminary::ExPolygons &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonsProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polylines &subject, const Luminary::ExPolygon &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::ExPolygonProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polylines &subject, const Luminary::ExPolygons &clip)
{
    return _clipper_pl_open(ClipType::Difference, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::ExPolygonsProvider(clip));
}
Luminary::Polylines diff_pl(const Luminary::Polygons &subject, const Luminary::Polygons &clip)
{
    // Deprecated contract: with a POLYGONS subject this performs an AREA boolean and returns
    // the result's boundary split into polylines, NOT a clip of the subject's boundary curves.
    // No live callers. Do not add one without reworking the semantics.
    assert(false && "diff_pl(Polygons, Polygons): deprecated area-boolean semantics");
    return _clipper_pl_closed(ClipType::Difference, ClipperUtils::PolygonsProvider(subject),
                              ClipperUtils::PolygonsProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polylines &subject, const Luminary::Polygon &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::SinglePathProvider(clip.points));
}
Luminary::Polylines intersection_pl(const Luminary::Polyline &subject, const Luminary::ExPolygon &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polylines &subject, const Luminary::ExPolygon &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::ExPolygonProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polyline &subject, const Luminary::Polygons &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::PolygonsProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polyline &subject, const Luminary::ExPolygons &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonsProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polylines &subject, const Luminary::Polygons &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::PolygonsProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polylines &subject, const Luminary::ExPolygons &clip)
{
    return _clipper_pl_open(ClipType::Intersection, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::ExPolygonsProvider(clip));
}
Luminary::Polylines intersection_pl(const Luminary::Polygons &subject, const Luminary::Polygons &clip)
{
    // Deprecated contract: with a POLYGONS subject this performs an AREA boolean and returns
    // the result's boundary split into polylines, NOT a clip of the subject's boundary curves.
    // No live callers. Do not add one without reworking the semantics.
    assert(false && "intersection_pl(Polygons, Polygons): deprecated area-boolean semantics");
    return _clipper_pl_closed(ClipType::Intersection, ClipperUtils::PolygonsProvider(subject),
                              ClipperUtils::PolygonsProvider(clip));
}

Lines _clipper_ln(ClipType clipType, const Lines &subject, const Polygons &clip)
{
    // convert Lines to Polylines
    Polylines polylines;
    polylines.reserve(subject.size());
    for (const Line &line : subject)
        polylines.emplace_back(Polyline(line.a, line.b));

    // perform operation
    polylines = _clipper_pl_open(clipType, ClipperUtils::PolylinesProvider(polylines),
                                 ClipperUtils::PolygonsProvider(clip));

    // convert Polylines to Lines
    Lines retval;
    for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline)
        if (polyline->size() >= 2)
            // Clipper may leave collinear interior vertices; the endpoints still bound the clipped segment.
            retval.push_back({polyline->front(), polyline->back()});
    return retval;
}

// Convert polygons / expolygons into PolyTree64 using FillRule::EvenOdd, thus union will NOT be performed.
// If the contours are not intersecting, their orientation shall not be modified by union_pt().
void union_pt(const Polygons &subject, PolyTree64 &out_result)
{
    clipper_do_polytree_direct(ClipType::Union, ClipperUtils::PolygonsProvider(subject),
                               ClipperUtils::EmptyPathsProvider(), FillRule::EvenOdd, out_result);
}
void union_pt(const ExPolygons &subject, PolyTree64 &out_result)
{
    clipper_do_polytree_direct(ClipType::Union, ClipperUtils::ExPolygonsProvider(subject),
                               ClipperUtils::EmptyPathsProvider(), FillRule::EvenOdd, out_result);
}

static void traverse_pt_noholes(const PolyPaths &nodes, Polygons *out)
{
    foreach_node<e_ordering::ON>(nodes,
                                 [&out](const PolyPath64 *node)
                                 {
                                     // Build PolyPaths vector from children
                                     PolyPaths childs;
                                     for (size_t i = 0; i < node->Count(); ++i)
                                         childs.push_back((*node)[i]);
                                     traverse_pt_noholes(childs, out);

                                     const auto &polygon = node->Polygon();
                                     out->emplace_back(ClipperPath_to_Polygon(polygon));
                                     if (node->IsHole())
                                         out->back().reverse();
                                 });
}

Polygons union_parallel_reduce(const Polygons &subject)
{
    // Clipper rounds intersection vertices as it goes, so the union of many overlapping
    // polygons depends on the order they are combined in. A plain parallel_reduce groups the
    // input by whatever the scheduler decides, which made the slab surfaces of painted models
    // differ from run to run. The deterministic variant reduces in a fixed tree.
    return tbb::parallel_deterministic_reduce(
        tbb::blocked_range<size_t>(0, subject.size()), Polygons(),
        [&subject](tbb::blocked_range<size_t> range, Polygons partial_union)
        {
            for (size_t subject_idx = range.begin(); subject_idx < range.end(); ++subject_idx)
            {
                partial_union = union_(partial_union, subject[subject_idx]);
            }
            return partial_union;
        },
        [](const Polygons &a, const Polygons &b) { return union_(a, b); });
}

Polygons simplify_polygons(const Polygons &subject)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths output;
    Clipper2Lib::Clipper64 c;
    // Clipper2 output is always strictly simple, so a union pass is the whole simplification.
    c.AddSubject(Polygons_to_ClipperPaths(subject));
    c.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, output);
    return ClipperPaths_to_Polygons(output);
}

// Outer offset shall not split the input contour into multiples. It is expected, that the solution will be non empty and it will contain just a single polygon.
Paths fix_after_outer_offset(
    const Path &input,
    // combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution, double delta)
    // to produce a CCW output contour from CCW input contour for a positive offset.
    FillRule filltype,   // = FillRule::Positive
    bool reverse_result) // = false
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths solution;
    if (!input.empty())
    {
        Clipper2Lib::Clipper64 clipper;
        Clipper2Lib::Paths64 subject_paths = {input};
        clipper.AddSubject(subject_paths);
        clipper.ReverseSolution(reverse_result);
        clipper.Execute(Clipper2Lib::ClipType::Union, filltype, solution);
    }
    return solution;
}

// Inner offset may split the source contour into multiple contours, but one resulting contour shall not lie inside the other.
Paths fix_after_inner_offset(
    const Path &input,
    // combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution, double delta)
    // to produce a CCW output contour from CCW input contour for a negative offset.
    FillRule filltype,   // = FillRule::Negative
    bool reverse_result) // = true
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths solution;
    if (!input.empty())
    {
        Clipper2Lib::Clipper64 clipper;
        Clipper2Lib::Paths64 subject_input = {input};
        clipper.AddSubject(subject_input);
        Clipper2Lib::Rect64 r = Clipper2Lib::GetBounds(subject_input);
        r.left -= 10;
        r.top -= 10;
        r.right += 10;
        r.bottom += 10;
        Clipper2Lib::Paths64 bounding_box_pos = {
            Clipper2Lib::Path64{{r.left, r.bottom}, {r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}}};
        Clipper2Lib::Paths64 bounding_box_neg = {
            Clipper2Lib::Path64{{r.left, r.bottom}, {r.right, r.bottom}, {r.right, r.top}, {r.left, r.top}}};
        if (filltype == FillRule::Positive)
            clipper.AddSubject(bounding_box_pos);
        else
            clipper.AddSubject(bounding_box_neg);
        clipper.ReverseSolution(reverse_result);
        clipper.Execute(Clipper2Lib::ClipType::Union, filltype, solution);
        if (!solution.empty())
            solution.erase(solution.begin());
    }
    return solution;
}

Path mittered_offset_path_scaled(const Points &contour, const std::vector<float> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    assert(contour.size() == deltas.size());

#ifndef NDEBUG
    // Verify that the deltas are either all positive, or all negative.
    bool positive = false;
    bool negative = false;
    for (float delta : deltas)
        if (delta < 0.f)
            negative = true;
        else if (delta > 0.f)
            positive = true;
    assert(!(negative && positive));
#endif /* NDEBUG */

    Path out;

    if (deltas.size() > 2)
    {
        out.reserve(contour.size() * 2);

        // Clamp miter limit to 2.
        miter_limit = (miter_limit > 2.) ? 2. / (miter_limit * miter_limit) : 0.5;

        // perpenduclar vector
        auto perp = [](const Vec2d &v) -> Vec2d
        {
            return Vec2d(v.y(), -v.x());
        };

        // Add a new point to the output, scale by CLIPPER_OFFSET_SCALE and round to int64_t.
        auto add_offset_point = [&out](Vec2d pt)
        {
            pt += Vec2d(0.5 - (pt.x() < 0), 0.5 - (pt.y() < 0));
            out.emplace_back(int64_t(pt.x()), int64_t(pt.y()));
        };

        // Minimum edge length, squared.
        double lmin = *std::max_element(deltas.begin(), deltas.end()) * ClipperOffsetShortestEdgeFactor;
        double l2min = lmin * lmin;
        // Minimum angle to consider two edges to be parallel.
        // Vojtech's estimate.
        //		const double sin_min_parallel = EPSILON + 1. / double(CLIPPER_OFFSET_SCALE);
        // Implementation equal to Clipper.
        const double sin_min_parallel = 1.;

        // Find the last point further from pt by l2min.
        Vec2d pt = contour.front().cast<double>();
        size_t iprev = contour.size() - 1;
        Vec2d ptprev;
        for (; iprev > 0; --iprev)
        {
            ptprev = contour[iprev].cast<double>();
            if ((ptprev - pt).squaredNorm() > l2min)
                break;
        }

        if (iprev != 0)
        {
            size_t ilast = iprev;
            // Normal to the (pt - ptprev) segment.
            Vec2d nprev = perp(pt - ptprev).normalized();
            for (size_t i = 0;;)
            {
                // Find the next point further from pt by l2min.
                size_t j = i + 1;
                Vec2d ptnext;
                for (; j <= ilast; ++j)
                {
                    ptnext = contour[j].cast<double>();
                    double l2 = (ptnext - pt).squaredNorm();
                    if (l2 > l2min)
                        break;
                }
                if (j > ilast)
                {
                    assert(i <= ilast);
                    // If the last edge is too short, merge it with the previous edge.
                    i = ilast;
                    ptnext = contour.front().cast<double>();
                }

                // Normal to the (ptnext - pt) segment.
                Vec2d nnext = perp(ptnext - pt).normalized();

                double delta = deltas[i];
                double sin_a = std::clamp(cross2(nprev, nnext), -1., 1.);
                double convex = sin_a * delta;
                if (convex <= -sin_min_parallel)
                {
                    // Concave corner.
                    add_offset_point(pt + nprev * delta);
                    add_offset_point(pt);
                    add_offset_point(pt + nnext * delta);
                }
                else
                {
                    double dot = nprev.dot(nnext);
                    if (convex < sin_min_parallel && dot > 0.)
                    {
                        // Nearly parallel.
                        add_offset_point((nprev.dot(nnext) > 0.) ? (pt + nprev * delta) : pt);
                    }
                    else
                    {
                        // Convex corner, possibly extremely sharp if convex < sin_min_parallel.
                        double r = 1. + dot;
                        if (r >= miter_limit)
                            add_offset_point(pt + (nprev + nnext) * (delta / r));
                        else
                        {
                            double dx = std::tan(std::atan2(sin_a, dot) / 4.);
                            Vec2d newpt1 = pt + (nprev - perp(nprev) * dx) * delta;
                            Vec2d newpt2 = pt + (nnext + perp(nnext) * dx) * delta;
#ifndef NDEBUG
                            Vec2d vedge = 0.5 * (newpt1 + newpt2) - pt;
                            double dist_norm = vedge.norm();
                            assert(std::abs(dist_norm - std::abs(delta)) < SCALED_EPSILON);
#endif /* NDEBUG */
                            add_offset_point(newpt1);
                            add_offset_point(newpt2);
                        }
                    }
                }

                if (i == ilast)
                    break;

                ptprev = pt;
                nprev = nnext;
                pt = ptnext;
                i = j;
            }
        }
    }

    return out;
}

static void variable_offset_inner_raw(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                                      double miter_limit, Paths &contours, Paths &holes)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float> &ds : deltas)
        for (float delta : ds)
            assert(delta <= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
    assert(Clipper2Lib::Area(Points_to_ClipperPath(expoly.contour.points)) > 0.);
    for (auto &h : expoly.holes)
        assert(Clipper2Lib::Area(Points_to_ClipperPath(h.points)) < 0.);
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    contours = fix_after_inner_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                      FillRule::Negative, true);
#ifndef NDEBUG
    // Shrinking a contour may split it into pieces, but never create a new hole inside the contour.
    for (auto &c : contours)
        assert(Clipper2Lib::Area(c) > 0.);
#endif /* NDEBUG */

    // 2) Offset the holes one by one, collect the results.
    holes.reserve(expoly.holes.size());
    for (const Polygon &hole : expoly.holes)
        append(holes,
               fix_after_outer_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()],
                                                                  miter_limit),
                                      FillRule::Negative, false));
}

Polygons variable_offset_inner(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                               double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths contours, holes;
    variable_offset_inner_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
    Paths output;
    if (holes.empty())
        output = std::move(contours);
    else
    {
        Clipper2Lib::Clipper64 clipper;
        clipper.Clear();
        clipper.AddSubject(contours);
        // Holes may contain holes in holes produced by expanding a C hole shape.
        // The situation is processed correctly by Clipper diff operation.
        clipper.AddClip(holes);
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, output);
    }

    return ClipperPaths_to_Polygons(output);
}

ExPolygons variable_offset_inner_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                                    double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths contours, holes;
    variable_offset_inner_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
    ExPolygons output;
    if (holes.empty())
    {
        output.reserve(contours.size());
        // Shrinking a CCW contour may only produce more CCW contours, but never new holes.
        for (Path &path : contours)
            output.emplace_back(ClipperPath_to_Points(path));
    }
    else
    {
        Clipper2Lib::Clipper64 clipper;
        clipper.AddSubject(contours);
        // Holes may contain holes in holes produced by expanding a C hole shape.
        // The situation is processed correctly by Clipper diff operation, producing concentric expolygons.
        clipper.AddClip(holes);
        PolyTree64 polytree;
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, polytree);
        output = PolyTree64ToExPolygons(std::move(polytree));
    }

    return output;
}

static void variable_offset_outer_raw(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                                      double miter_limit, Paths &contours, Paths &holes)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float> &ds : deltas)
        for (float delta : ds)
            assert(delta >= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
    assert(Clipper2Lib::Area(Points_to_ClipperPath(expoly.contour.points)) > 0.);
    for (auto &h : expoly.holes)
        assert(Clipper2Lib::Area(Points_to_ClipperPath(h.points)) < 0.);
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    contours = fix_after_outer_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                      FillRule::Positive, false);
    // Inflating a contour must not remove it.
    assert(contours.size() >= 1);

    // 2) Offset the holes one by one, collect the results.
    holes.reserve(expoly.holes.size());
    for (const Polygon &hole : expoly.holes)
        append(holes,
               fix_after_inner_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()],
                                                                  miter_limit),
                                      FillRule::Positive, true));
#ifndef NDEBUG
    // Shrinking a hole may split it into pieces, but never create a new hole inside a hole.
    for (auto &c : holes)
        assert(Clipper2Lib::Area(c) > 0.);
#endif /* NDEBUG */
}

Polygons variable_offset_outer(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                               double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths contours, holes;
    variable_offset_outer_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
    Paths output;
    if (holes.empty())
        output = std::move(contours);
    else
    {
        Clipper2Lib::Clipper64 clipper;
        clipper.Clear();
        clipper.AddSubject(contours);
        clipper.AddClip(holes);
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, output);
    }

    return ClipperPaths_to_Polygons(output);
}

ExPolygons variable_offset_outer_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas,
                                    double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    Paths contours, holes;
    variable_offset_outer_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
    ExPolygons output;
    if (holes.empty())
    {
        output.reserve(1);
        if (contours.size() > 1)
        {
            // One expolygon with holes created by closing a C shape. Which is which?
            output.push_back({});
            ExPolygon &out = output.back();
            out.holes.reserve(contours.size() - 1);
            for (Path &path : contours)
            {
                if (Clipper2Lib::Area(path) > 0)
                {
                    // Only one contour with positive area is expected to be created by an outer offset of an ExPolygon.
                    assert(out.contour.empty());
                    out.contour.points = ClipperPath_to_Points(path);
                }
                else
                    out.holes.push_back(Polygon{ClipperPath_to_Points(path)});
            }
        }
        else
        {
            // Single contour must be CCW.
            assert(contours.size() == 1);
            assert(Clipper2Lib::Area(contours.front()) > 0);
            output.push_back(ExPolygon{Polygon{ClipperPath_to_Points(contours.front())}});
        }
    }
    else
    {
        Clipper2Lib::Clipper64 clipper;
        // Contours may have holes if they were created by closing a C shape.
        clipper.AddSubject(contours);
        clipper.AddClip(holes);
        PolyTree64 polytree;
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, polytree);
        output = PolyTree64ToExPolygons(std::move(polytree));
    }

    assert(output.size() == 1);
    return output;
}

} // namespace Luminary
