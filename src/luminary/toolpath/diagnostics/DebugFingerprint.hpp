///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
///|/ Provisional module: fingerprinting belongs with diagnostics by subject, and sits in toolpath because it reads the extrusion IR, which core cannot.
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntity.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntityCollection.hpp"
#include "luminary/core/diagnostics/DebugOutput.hpp"

namespace Luminary
{

// Run-to-run determinism fingerprint of one pipeline stage ([DETERM] lines). Order-insensitive:
// a polygon is hashed from its lexicographically smallest vertex, an open path from its smaller
// end, and the per-element hashes are summed, so neither element order nor a loop's start vertex
// moves the value; only the geometry does.
struct DetermFingerprint
{
    uint64_t hash{0};
    // Order-sensitive companion: elements in stored order, polygons from their stored start
    // vertex. Equal `hash` with a differing `ordered` is a run that only reordered or rotated
    // its output, which still matters when a later stage picks a start point from it.
    uint64_t ordered{1469598103934665603ull};
    size_t count{0};
    size_t points{0};
    double area{0.};   // scaled units squared, holes negative
    double length{0.}; // mm, extrusions only

    // Extrusion length per role family, mm. Coarser than ExtrusionRole on purpose: a role flip
    // between an overhang and an external perimeter is the event these buckets exist to show.
    enum RoleBucket
    {
        ExtPerim,
        Perim,
        Overhang,
        GapFill,
        SolidInfill,
        SparseInfill,
        BridgeInfill,
        Support,
        Skirt,
        Other,
        RoleBucketCount
    };
    double role_length[RoleBucketCount]{};

    static uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
    static uint64_t mix(uint64_t h, const Point &pt) { return mix(mix(h, uint64_t(pt.x())), uint64_t(pt.y())); }
    static uint64_t bits(float v)
    {
        uint32_t b;
        std::memcpy(&b, &v, sizeof(b));
        return b;
    }
    static uint64_t polygon(const Polygon &poly, uint64_t h = 1469598103934665603ull)
    {
        const size_t n = poly.points.size();
        size_t start = 0;
        for (size_t i = 1; i < n; ++i)
            if (poly.points[i] < poly.points[start])
                start = i;
        for (size_t i = 0; i < n; ++i)
            h = mix(h, poly.points[(start + i) % n]);
        return h;
    }
    static uint64_t polyline(const Points &pts, uint64_t h = 1469598103934665603ull)
    {
        const size_t n = pts.size();
        const bool reverse = n > 1 && pts.back() < pts.front();
        for (size_t i = 0; i < n; ++i)
            h = mix(h, pts[reverse ? n - 1 - i : i]);
        return h;
    }
    // Sum of the polygon hashes of a set, for a set that is one element of a larger fingerprint.
    static uint64_t polygons(const Polygons &polys)
    {
        uint64_t h = 0;
        for (const Polygon &poly : polys)
            h += polygon(poly);
        return h;
    }
    static int role_bucket(const ExtrusionRole role)
    {
        if (role.is_perimeter())
            return role.is_bridge() ? Overhang : role.is_external() ? ExtPerim : Perim;
        if (role.has(ExtrusionRoleModifier::Thin))
            return GapFill;
        if (role.is_infill())
            return role.is_bridge() ? BridgeInfill : role.is_solid_infill() ? SolidInfill : SparseInfill;
        if (role.is_support())
            return Support;
        if (role.is_skirt())
            return Skirt;
        return Other;
    }
    static const char *role_bucket_name(const int bucket)
    {
        static const char *names[RoleBucketCount] = {"ext", "per", "ovh", "gap", "sol",
                                                     "spa", "brg", "sup", "ski", "oth"};
        return names[bucket];
    }
    void add(uint64_t element_hash)
    {
        hash += element_hash;
        ordered = mix(ordered, element_hash);
        ++count;
    }
    void add(const Polygon &poly)
    {
        hash += polygon(poly);
        ++count;
        for (const Point &pt : poly.points)
            ordered = mix(ordered, pt);
        points += poly.points.size();
        area += poly.area();
    }
    void add(const Polygons &polys)
    {
        for (const Polygon &poly : polys)
            this->add(poly);
    }
    void add(const ExPolygon &expoly)
    {
        this->add(expoly.contour);
        this->add(expoly.holes);
    }
    void add(const ExPolygons &expolys)
    {
        for (const ExPolygon &expoly : expolys)
            this->add(expoly);
    }
    // One extrusion path: hashed from its smaller end with its role family folded in, so a
    // reversed path keeps `hash` while `ordered` follows the stored direction.
    void add_path(const Points &pts, const ExtrusionRole role)
    {
        const int bucket = role_bucket(role);
        hash += polyline(pts, mix(1469598103934665603ull, uint64_t(bucket)));
        ++count;
        ordered = mix(ordered, uint64_t(bucket));
        for (const Point &pt : pts)
            ordered = mix(ordered, pt);
        points += pts.size();
        double len = 0.;
        for (size_t i = 1; i < pts.size(); ++i)
            len += (pts[i] - pts[i - 1]).cast<double>().norm();
        len *= SCALING_FACTOR;
        length += len;
        role_length[bucket] += len;
    }
    // A loop is hashed as one polygon from its smallest vertex, so the seam (its start vertex)
    // and the role split points do not move `hash`; the role lengths still show a role flip.
    void add(const ExtrusionLoop &loop)
    {
        this->add(loop.polygon());
        for (const ExtrusionPath &path : loop.paths)
        {
            const int bucket = role_bucket(path.role());
            ordered = mix(ordered, uint64_t(bucket));
            const double len = unscaled<double>(path.length());
            length += len;
            role_length[bucket] += len;
        }
    }
    void add(const ExtrusionEntity &entity)
    {
        if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity))
        {
            for (const ExtrusionEntity *child : collection->entities)
                this->add(*child);
        }
        else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity))
            this->add(*loop);
        else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity))
        {
            for (const ExtrusionPath &path : multi->paths)
                this->add_path(path.polyline.points, path.role());
        }
        else if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity))
            this->add_path(path->polyline.points, path->role());
    }
    double area_mm2() const { return area * SCALING_FACTOR * SCALING_FACTOR; }
    // Non-zero role lengths as `ext:12.345,per:...` for the roles= field.
    std::string roles() const
    {
        std::string out;
        char buf[48];
        for (int b = 0; b < RoleBucketCount; ++b)
            if (role_length[b] > 0.)
            {
                std::snprintf(buf, sizeof(buf), "%s%s:%.3f", out.empty() ? "" : ",", role_bucket_name(b),
                              role_length[b]);
                out += buf;
            }
        return out.empty() ? "none" : out;
    }
};

// One fingerprint line of an extrusion collection, stamped with z: paths, points, length, the
// per-role lengths and both hashes.
inline void determ_fp_extrusions(uint32_t cat, double z, const char *tag, const char *kind, size_t index,
                                 const ExtrusionEntityCollection &collection)
{
    DetermFingerprint fp;
    fp.add(collection);
    dbg_log(cat, z, "DETERM", "%s kind=%s idx=%zu paths=%zu pts=%zu len=%.3fmm roles=%s hash=%016llx ord=%016llx", tag,
            kind, index, fp.count, fp.points, fp.length, fp.roles().c_str(), (unsigned long long) fp.hash,
            (unsigned long long) fp.ordered);
}

} // namespace Luminary
