///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "TravelOptimization.hpp"
#include "Polyline.hpp" // For ThickPolyline

#include <algorithm>
#include <limits>

namespace Slic3r
{
namespace TravelOptimization
{

size_t nearest_vertex_index(const Points &points, const Point &target)
{
    if (points.empty())
        return 0;

    size_t best_idx = 0;
    double best_dist_sq = std::numeric_limits<double>::max();

    for (size_t i = 0; i < points.size(); ++i)
    {
        double dist_sq = distance_squared(points[i], target);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_idx = i;
        }
    }

    return best_idx;
}

size_t nearest_vertex_index_closed(const Points &points, const Point &target)
{
    if (points.size() < 2)
        return 0;

    // For closed loops, don't consider the last point (it's the same as first)
    // Check if it's actually closed
    bool is_closed = (points.front() == points.back());
    size_t search_limit = is_closed ? points.size() - 1 : points.size();

    size_t best_idx = 0;
    double best_dist_sq = std::numeric_limits<double>::max();

    for (size_t i = 0; i < search_limit; ++i)
    {
        double dist_sq = distance_squared(points[i], target);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_idx = i;
        }
    }

    return best_idx;
}

size_t rotate_polygon_to_nearest_vertex(Polygon &polygon, const Point &target)
{
    if (polygon.points.size() < 3)
        return 0;

    size_t idx = nearest_vertex_index(polygon.points, target);

    if (idx > 0 && idx < polygon.points.size())
    {
        std::rotate(polygon.points.begin(), polygon.points.begin() + idx, polygon.points.end());
    }

    return idx;
}

size_t rotate_thick_polyline_to_nearest_vertex(ThickPolyline &polyline, const Point &target)
{
    if (polyline.points.size() < 3)
        return 0;

    // ThickPolyline must be closed (front == back) for rotation to work
    // start_at_index() has this requirement
    if (polyline.points.front() != polyline.points.back())
        return 0;

    // Also need matching widths for proper rotation
    if (polyline.width.front() != polyline.width.back())
        return 0;

    // Find nearest vertex among unique vertices (exclude closing point)
    size_t idx = nearest_vertex_index_closed(polyline.points, target);

    if (idx > 0)
    {
        // ThickPolyline::start_at_index handles both points and widths rotation
        polyline.start_at_index(static_cast<int>(idx));
    }

    return idx;
}

LoopVertexLocation find_nearest_vertex_in_loop(const ExtrusionLoop &loop, const Point &target)
{
    LoopVertexLocation result;
    result.path_idx = 0;
    result.vertex_idx = 0;
    result.vertex = Point(0, 0);
    result.distance_sq = std::numeric_limits<double>::max();

    if (loop.paths.empty())
        return result;

    for (size_t path_idx = 0; path_idx < loop.paths.size(); ++path_idx)
    {
        const Polyline &polyline = loop.paths[path_idx].polyline;

        for (size_t vert_idx = 0; vert_idx < polyline.points.size(); ++vert_idx)
        {
            // For the last path, skip the last point if it matches the first path's first point
            // (to avoid counting the closing vertex twice)
            if (path_idx == loop.paths.size() - 1 && vert_idx == polyline.points.size() - 1 &&
                polyline.points.back() == loop.paths.front().polyline.points.front())
            {
                continue;
            }

            double dist_sq = distance_squared(polyline.points[vert_idx], target);
            if (dist_sq < result.distance_sq)
            {
                result.path_idx = path_idx;
                result.vertex_idx = vert_idx;
                result.vertex = polyline.points[vert_idx];
                result.distance_sq = dist_sq;
            }
        }
    }

    return result;
}

// =============================================================================
// GEOMETRY SIMPLIFICATION IMPLEMENTATION
// =============================================================================

bool is_collinear(const Point &a, const Point &b, const Point &c, double tolerance_sq)
{
    // Calculate the cross product (a-b) × (c-b) which gives 2× the signed area of triangle abc
    // If the area is near zero, the points are collinear
    // Using 64-bit integers to avoid overflow with scaled coordinates
    Vec2d ab = (a - b).cast<double>();
    Vec2d cb = (c - b).cast<double>();

    // Cross product: ab.x * cb.y - ab.y * cb.x
    double cross = ab.x() * cb.y() - ab.y() * cb.x();

    // The cross product gives twice the signed area of the triangle
    // For collinearity, we want the area to be small relative to the edge lengths
    // Using squared values to avoid sqrt
    double cross_sq = cross * cross;

    // Normalize by the squared lengths of the edges to make tolerance scale-independent
    double len_ab_sq = ab.squaredNorm();
    double len_cb_sq = cb.squaredNorm();

    // Avoid division by zero for degenerate cases
    if (len_ab_sq < 1.0 || len_cb_sq < 1.0)
        return true; // Very short edges - consider collinear (can remove the point)

    // The "height" of the triangle (distance from b to line ac) can be computed as:
    // height = |cross| / |ac|
    // We want height² < tolerance_sq
    // height² = cross² / |ac|²
    // So: cross² < tolerance_sq * |ac|²

    Vec2d ac = (a - c).cast<double>();
    double len_ac_sq = ac.squaredNorm();

    if (len_ac_sq < 1.0)
        return true; // a and c are nearly the same point

    return cross_sq < tolerance_sq * len_ac_sq;
}

size_t remove_collinear_points(Points &points, bool is_closed, double tolerance_sq)
{
    if (points.size() < 3)
        return 0;

    size_t removed = 0;

    // For closed paths, we need to handle wrap-around
    // Work backwards to avoid index invalidation issues

    if (is_closed)
    {
        // For closed paths, check all points including wrap-around
        // But don't remove the first/last point (they're the same and needed for closure)
        size_t n = points.size();

        // Safety: closed loops need at least 3 unique points (+ closing = 4 total)
        // Don't attempt collinear removal on triangles or smaller
        bool has_closing_point = (n > 0 && points.front() == points.back());
        size_t unique_points = has_closing_point ? n - 1 : n;
        if (unique_points < 4) // Triangle or smaller, can't remove any
            return 0;

        // First pass: mark points for removal (can't remove while iterating)
        std::vector<bool> to_remove(n, false);

        for (size_t i = 0; i < n; ++i)
        {
            // Safety: don't remove if it would leave fewer than 3 unique points
            if (unique_points - removed <= 3)
                break;

            // Skip the closing point (last == first)
            if (i == n - 1 && points[i] == points[0])
                continue;

            size_t prev = (i == 0) ? n - 2 : i - 1; // Skip closing point
            size_t next = (i == n - 2) ? 0 : i + 1;
            if (next == n - 1 && points[next] == points[0])
                next = 0;

            if (is_collinear(points[prev], points[i], points[next], tolerance_sq))
            {
                to_remove[i] = true;
                ++removed;
            }
        }

        // Second pass: actually remove the marked points
        if (removed > 0)
        {
            Points new_points;
            new_points.reserve(n - removed);
            for (size_t i = 0; i < n; ++i)
            {
                if (!to_remove[i])
                    new_points.push_back(points[i]);
            }
            // Ensure closure is maintained
            if (!new_points.empty() && new_points.front() != new_points.back())
                new_points.push_back(new_points.front());
            points = std::move(new_points);
        }
    }
    else
    {
        // For open paths, don't remove first or last point
        std::vector<bool> to_remove(points.size(), false);

        for (size_t i = 1; i < points.size() - 1; ++i)
        {
            if (is_collinear(points[i - 1], points[i], points[i + 1], tolerance_sq))
            {
                to_remove[i] = true;
                ++removed;
            }
        }

        if (removed > 0)
        {
            Points new_points;
            new_points.reserve(points.size() - removed);
            for (size_t i = 0; i < points.size(); ++i)
            {
                if (!to_remove[i])
                    new_points.push_back(points[i]);
            }
            points = std::move(new_points);
        }
    }

    return removed;
}

size_t remove_collinear_points(Polygon &polygon, double tolerance_sq)
{
    // Polygon is implicitly closed (last point connects to first)
    // But we treat points as the unique vertices
    return remove_collinear_points(polygon.points, true, tolerance_sq);
}

size_t remove_collinear_points(Polyline &polyline, double tolerance_sq)
{
    // Polyline is open unless first == last
    bool is_closed = !polyline.points.empty() && polyline.points.front() == polyline.points.back();
    return remove_collinear_points(polyline.points, is_closed, tolerance_sq);
}

size_t remove_collinear_points(ThickPolyline &polyline, double tolerance_sq)
{
    if (polyline.points.size() < 3)
        return 0;

    bool is_closed = polyline.points.front() == polyline.points.back();
    size_t n = polyline.points.size();

    // For closed loops, need at least 3 unique points (+ closing = 4 total)
    // Don't attempt collinear removal on very small loops
    if (is_closed && n < 5) // 4 points = triangle, can't remove any
        return 0;

    // Mark points for removal
    std::vector<bool> to_remove(n, false);
    size_t removed = 0;

    if (is_closed)
    {
        // For closed ThickPolylines, check all points except the closing point
        size_t unique_points = n - 1; // Exclude closing point
        for (size_t i = 0; i < n - 1; ++i)
        {
            // Safety: don't remove if it would leave fewer than 3 unique points
            if (unique_points - removed <= 3)
                break;

            size_t prev = (i == 0) ? n - 2 : i - 1;
            size_t next = (i == n - 2) ? 0 : i + 1;

            if (is_collinear(polyline.points[prev], polyline.points[i], polyline.points[next], tolerance_sq))
            {
                to_remove[i] = true;
                ++removed;
            }
        }
        // Also mark the closing point if its "original" (first point) is marked
        if (to_remove[0])
            to_remove[n - 1] = true;
    }
    else
    {
        // For open ThickPolylines, don't remove first or last
        for (size_t i = 1; i < n - 1; ++i)
        {
            if (is_collinear(polyline.points[i - 1], polyline.points[i], polyline.points[i + 1], tolerance_sq))
            {
                to_remove[i] = true;
                ++removed;
            }
        }
    }

    if (removed == 0)
        return 0;

    // Build new points and widths arrays
    // Width structure: 2 widths per segment (start_width, end_width)
    // For n points, there are (n-1) segments, so 2*(n-1) widths
    Points new_points;
    std::vector<coordf_t> new_widths;
    new_points.reserve(n - removed + (is_closed ? 1 : 0));

    // Track which segments to keep
    // When we remove point i, we merge segments (i-1,i) and (i,i+1) into (i-1,i+1)
    // The merged segment's widths should be the outer widths: start of first, end of second

    size_t last_kept = SIZE_MAX;
    for (size_t i = 0; i < n; ++i)
    {
        if (!to_remove[i])
        {
            new_points.push_back(polyline.points[i]);

            if (last_kept != SIZE_MAX && !new_widths.empty())
            {
                // Add widths for the segment from last_kept to i
                // We need to handle cases where intermediate points were removed
                size_t seg_start = last_kept;
                size_t seg_end = i;

                // Width indices: segment j has widths at [2*j] and [2*j + 1]
                // Start width comes from the first segment leaving last_kept
                // End width comes from the last segment arriving at i
                if (2 * seg_start + 1 < polyline.width.size())
                {
                    new_widths.push_back(polyline.width[2 * seg_start]); // Start width
                }
                if (seg_end > 0 && 2 * (seg_end - 1) + 1 < polyline.width.size())
                {
                    new_widths.push_back(polyline.width[2 * (seg_end - 1) + 1]); // End width
                }
            }
            last_kept = i;
        }
    }

    // For closed polylines, ensure closure
    if (is_closed && !new_points.empty() && new_points.front() != new_points.back())
    {
        // Add closing segment widths if needed
        if (last_kept != SIZE_MAX && last_kept != 0)
        {
            // Segment from last_kept back to 0
            if (2 * last_kept < polyline.width.size())
                new_widths.push_back(polyline.width[2 * last_kept]);
            if (!polyline.width.empty())
                new_widths.push_back(polyline.width.back());
        }
        new_points.push_back(new_points.front());
    }

    polyline.points = std::move(new_points);

    // Only update widths if we have the right count
    // Width count should be 2 * (points - 1) for valid ThickPolyline
    size_t expected_widths = polyline.points.empty() ? 0 : 2 * (polyline.points.size() - 1);
    if (new_widths.size() == expected_widths)
    {
        polyline.width = std::move(new_widths);
    }
    // If width count doesn't match, keep original widths (safer than corrupting data)

    return removed;
}

} // namespace TravelOptimization
} // namespace Slic3r
