///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 - 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ClipperZUtils_hpp_
#define slic3r_ClipperZUtils_hpp_

#include <numeric>
#include <vector>

#include <clipper2/clipper.h>

#include <libslic3r/Point.hpp>
#include <libslic3r/ExPolygon.hpp>

namespace Slic3r
{

namespace ClipperZUtils
{

struct ZPoint
{
    int64_t x, y, z;
    ZPoint() : x(0), y(0), z(0) {}
    ZPoint(int64_t x_, int64_t y_, int64_t z_) : x(x_), y(y_), z(z_) {}
    // Construct from Clipper2 Point64 (which now has Z with USINGZ enabled)
    ZPoint(const Clipper2Lib::Point64 &pt) : x(pt.x), y(pt.y), z(pt.z) {}

    // Comparison operators needed by various algorithms
    bool operator==(const ZPoint &other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const ZPoint &other) const { return !(*this == other); }

    // Convert to Clipper2 Point64
    Clipper2Lib::Point64 to_point64() const { return Clipper2Lib::Point64(x, y, z); }
};
using ZPoints = std::vector<ZPoint>;
using ZPath = std::vector<ZPoint>;
using ZPaths = std::vector<ZPath>;

// Convert ZPath to Clipper2 Path64 (preserves Z coordinates)
inline Clipper2Lib::Path64 zpath_to_path64(const ZPath &zpath)
{
    Clipper2Lib::Path64 out;
    out.reserve(zpath.size());
    for (const ZPoint &zpt : zpath)
        out.emplace_back(zpt.x, zpt.y, zpt.z);
    return out;
}

// Convert ZPaths to Clipper2 Paths64 (preserves Z coordinates)
inline Clipper2Lib::Paths64 zpaths_to_paths64(const ZPaths &zpaths)
{
    Clipper2Lib::Paths64 out;
    out.reserve(zpaths.size());
    for (const ZPath &zpath : zpaths)
        out.emplace_back(zpath_to_path64(zpath));
    return out;
}

// Convert Clipper2 Path64 to ZPath (preserves Z coordinates)
inline ZPath path64_to_zpath(const Clipper2Lib::Path64 &path)
{
    ZPath out;
    out.reserve(path.size());
    for (const Clipper2Lib::Point64 &pt : path)
        out.emplace_back(pt.x, pt.y, pt.z);
    return out;
}

// Convert Clipper2 Paths64 to ZPaths (preserves Z coordinates)
inline ZPaths paths64_to_zpaths(const Clipper2Lib::Paths64 &paths)
{
    ZPaths out;
    out.reserve(paths.size());
    for (const Clipper2Lib::Path64 &path : paths)
        out.emplace_back(path64_to_zpath(path));
    return out;
}

inline bool zpoint_lower(const ZPoint &l, const ZPoint &r)
{
    // Clipper2: Point64 uses direct members .x, .y, .z (not methods)
    return l.x < r.x || (l.x == r.x && (l.y < r.y || (l.y == r.y && l.z < r.z)));
}

// Convert a single path to path with a given Z coordinate.
// If Open, then duplicate the first point at the end.
template<bool Open = false>
inline ZPath to_zpath(const Points &path, coord_t z)
{
    ZPath out;
    if (!path.empty())
    {
        out.reserve((path.size() + Open) ? 1 : 0);
        for (const Point &p : path)
            out.emplace_back(p.x(), p.y(), z); // Our ZPoint constructor
        if (Open)
            out.emplace_back(out.front());
    }
    return out;
}

// Convert multiple paths to paths with a given Z coordinate.
// If Open, then duplicate the first point of each path at its end.
template<bool Open = false>
inline ZPaths to_zpaths(const VecOfPoints &paths, coord_t z)
{
    ZPaths out;
    out.reserve(paths.size());
    for (const Points &path : paths)
        out.emplace_back(to_zpath<Open>(path, z));
    return out;
}

template<bool Open = false>
inline ZPaths to_zpaths(const Polygons &polygons, coord_t z)
{
    ZPaths out;
    out.reserve(polygons.size());
    for (const Polygon &poly : polygons)
        out.emplace_back(to_zpath<Open>(poly.points, z));
    return out;
}

// Convert multiple expolygons into z-paths with Z specified by an index of the source expolygon
// offsetted by base_index.
// If Open, then duplicate the first point of each path at its end.
template<bool Open = false>
inline ZPaths expolygons_to_zpaths(const ExPolygons &src, coord_t &base_idx)
{
    ZPaths out;
    out.reserve(std::accumulate(src.begin(), src.end(), size_t(0),
                                [](const size_t acc, const ExPolygon &expoly) { return acc + expoly.num_contours(); }));
    for (const ExPolygon &expoly : src)
    {
        out.emplace_back(to_zpath<Open>(expoly.contour.points, base_idx));
        for (const Polygon &hole : expoly.holes)
            out.emplace_back(to_zpath<Open>(hole.points, base_idx));
        ++base_idx;
    }
    return out;
}

// Convert multiple expolygons into z-paths with a given Z coordinate.
// If Open, then duplicate the first point of each path at its end.
template<bool Open>
inline ZPaths expolygons_to_zpaths_with_same_z(const ExPolygons &src, const coord_t z)
{
    ZPaths out;
    out.reserve(std::accumulate(src.begin(), src.end(), size_t(0),
                                [](const size_t acc, const ExPolygon &expoly) { return acc + expoly.num_contours(); }));
    for (const ExPolygon &expoly : src)
    {
        out.emplace_back(to_zpath<Open>(expoly.contour.points, z));
        for (const Polygon &hole : expoly.holes)
        {
            out.emplace_back(to_zpath<Open>(hole.points, z));
        }
    }

    return out;
}

// Convert a single path to path with a given Z coordinate.
// If Open, then duplicate the first point at the end.
template<bool Open = false>
inline Points from_zpath(const ZPoints &path)
{
    Points out;
    if (!path.empty())
    {
        out.reserve((path.size() + Open) ? 1 : 0);
        for (const ZPoint &p : path)
            out.emplace_back(p.x, p.y); // Clipper2: direct members
        if (Open)
            out.emplace_back(out.front());
    }
    return out;
}

// Convert multiple paths to paths with a given Z coordinate.
// If Open, then duplicate the first point of each path at its end.
template<bool Open = false>
inline void from_zpaths(const ZPaths &paths, VecOfPoints &out)
{
    out.reserve(out.size() + paths.size());
    for (const ZPoints &path : paths)
        out.emplace_back(from_zpath<Open>(path));
}
template<bool Open = false>
inline VecOfPoints from_zpaths(const ZPaths &paths)
{
    VecOfPoints out;
    from_zpaths(paths, out);
    return out;
}

class ClipperZIntersectionVisitor
{
public:
    using Intersection = std::pair<coord_t, coord_t>;
    using Intersections = std::vector<Intersection>;
    ClipperZIntersectionVisitor(Intersections &intersections) : m_intersections(intersections) {}
    void reset() { m_intersections.clear(); }

    void operator()(const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top,
                    const Clipper2Lib::Point64 &e2bot, const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
    {
        coord_t srcs[4]{static_cast<coord_t>(e1bot.z), static_cast<coord_t>(e1top.z), static_cast<coord_t>(e2bot.z),
                        static_cast<coord_t>(e2top.z)};
        coord_t *begin = srcs;
        coord_t *end = srcs + 4;
        std::sort(begin, end);
        end = std::unique(begin, end);
        if (begin + 1 == end)
        {
            // Self intersection may happen on source contour. Just copy the Z value.
            pt.z = *begin;
        }
        else
        {
            assert(begin + 2 == end);
            if (begin + 2 <= end)
            {
                // store a -1 based negative index into the "intersections" vector here.
                m_intersections.emplace_back(srcs[0], srcs[1]);
                pt.z = -coord_t(m_intersections.size());
            }
        }
    }

    // Returns a ZCallback64 that can be passed to Clipper2's SetZCallback()
    // Usage: clipper.SetZCallback(visitor.clipper_callback());
    Clipper2Lib::ZCallback64 clipper_callback()
    {
        return [this](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top,
                      const Clipper2Lib::Point64 &e2bot, const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
        {
            (*this)(e1bot, e1top, e2bot, e2top, pt);
        };
    }

    const std::vector<std::pair<coord_t, coord_t>> &intersections() const { return m_intersections; }

private:
    std::vector<std::pair<coord_t, coord_t>> &m_intersections;
};

} // namespace ClipperZUtils
} // namespace Slic3r

#endif // slic3r_ClipperZUtils_hpp_
