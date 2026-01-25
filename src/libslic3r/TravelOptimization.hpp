///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_TravelOptimization_hpp_
#define slic3r_TravelOptimization_hpp_

#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "ExtrusionEntity.hpp"

namespace Slic3r
{

// Forward declarations
struct ThickPolyline;

namespace TravelOptimization
{

// =============================================================================
// SNAP-TO-VERTEX API
// =============================================================================
//
// These functions find the VERTEX (not arbitrary point) closest to a target.
// This is critical for travel optimization:
//
// 1. Splitting loops at vertices avoids creating artificial points
//    - A square has 4 corners; splitting at 3 o'clock creates a 5th point
//    - Splitting at a corner keeps 4 points
//
// 2. Starting at the optimal vertex minimizes travel distance
//    - After finishing perimeters, start infill at the nearest corner
//    - Not at a fixed position like "rear" or "3 o'clock"
//
// Usage:
//   Point nozzle_pos = get_current_nozzle_position();
//   size_t best_vertex = nearest_vertex_index(polygon.points, nozzle_pos);
//   polygon.rotate_to_start_at(best_vertex);
// =============================================================================

/// Find the index of the vertex closest to the target point.
/// This is O(n) where n is the number of vertices.
/// @param points Vector of points to search
/// @param target The reference point (typically nozzle position)
/// @return Index of the closest vertex (0 if points is empty)
size_t nearest_vertex_index(const Points &points, const Point &target);

/// Overload for Polygon
inline size_t nearest_vertex_index(const Polygon &polygon, const Point &target)
{
    return nearest_vertex_index(polygon.points, target);
}

/// Overload for Polyline
inline size_t nearest_vertex_index(const Polyline &polyline, const Point &target)
{
    return nearest_vertex_index(polyline.points, target);
}

/// Find the index of the vertex closest to target, excluding the last point
/// if it's a closing point (same as first). Use this for closed loops.
/// @param points Vector of points (closed loop where front == back)
/// @param target The reference point
/// @return Index of the closest vertex among unique vertices
size_t nearest_vertex_index_closed(const Points &points, const Point &target);

// =============================================================================
// LOOP ROTATION API
// =============================================================================
//
// Rotate a closed loop to start at the vertex nearest to target.
// The loop remains closed (front == back) after rotation.
// =============================================================================

/// Rotate a closed polygon to start at the vertex nearest to target.
/// @param polygon The polygon to rotate (modified in place)
/// @param target The reference point (typically nozzle position)
/// @return Index of the new starting vertex (in the original ordering)
size_t rotate_polygon_to_nearest_vertex(Polygon &polygon, const Point &target);

/// Rotate a closed ThickPolyline to start at the vertex nearest to target.
/// The polyline must be closed (front == back) for this to work.
/// @param polyline The polyline to rotate (modified in place)
/// @param target The reference point
/// @return Index of the new starting vertex, or 0 if rotation failed
size_t rotate_thick_polyline_to_nearest_vertex(ThickPolyline &polyline, const Point &target);

// =============================================================================
// EXTRUSION LOOP API
// =============================================================================
//
// For ExtrusionLoop objects, find the optimal split point among vertices.
// ExtrusionLoop contains multiple ExtrusionPath objects, each with their own
// polyline. We need to find the vertex across all paths that's closest to target.
// =============================================================================

/// Result of finding the nearest vertex in an ExtrusionLoop
struct LoopVertexLocation
{
    size_t path_idx;    ///< Index of the path containing the vertex
    size_t vertex_idx;  ///< Index of the vertex within that path's polyline
    Point vertex;       ///< The actual vertex point
    double distance_sq; ///< Squared distance to target (for comparison)

    bool valid() const { return distance_sq < std::numeric_limits<double>::max(); }
};

/// Find the vertex in an ExtrusionLoop closest to the target point.
/// Searches all paths in the loop to find the globally closest vertex.
/// @param loop The ExtrusionLoop to search
/// @param target The reference point (typically nozzle position)
/// @return Location of the closest vertex
LoopVertexLocation find_nearest_vertex_in_loop(const ExtrusionLoop &loop, const Point &target);

// =============================================================================
// DISTANCE UTILITIES
// =============================================================================

/// Calculate squared distance between two points (faster than actual distance)
inline double distance_squared(const Point &a, const Point &b)
{
    return (a - b).cast<double>().squaredNorm();
}

/// Calculate actual distance between two points
inline double distance(const Point &a, const Point &b)
{
    return std::sqrt(distance_squared(a, b));
}

// =============================================================================
// GEOMETRY SIMPLIFICATION API
// =============================================================================
//
// Remove unnecessary collinear points from paths. This is critical because:
//
// 1. Arachne/WallToolPaths generates paths starting at "3 o'clock" (rightmost point)
// 2. For a square, this creates a 5th point on the right edge (between corners)
// 3. This artificial point is COLLINEAR with the two corners - it's redundant
// 4. More points = more G-code segments = slower processing on the printer
//
// By removing collinear points, a square goes from 5 points back to 4 (corners only).
// =============================================================================

/// Check if three points are collinear within a tolerance.
/// Uses cross-product to calculate the area of the triangle formed by the points.
/// If area is below tolerance, points are considered collinear.
/// @param a First point
/// @param b Middle point (candidate for removal)
/// @param c Third point
/// @param tolerance_sq Squared tolerance for collinearity (default: 1 scaled unit squared)
/// @return true if b is collinear with a and c (can be removed)
bool is_collinear(const Point &a, const Point &b, const Point &c, double tolerance_sq = 1.0);

/// Remove collinear points from a polygon (in place).
/// Points that lie on a straight line between their neighbors are removed.
/// For closed polygons, also checks collinearity across the closing point.
/// @param polygon The polygon to simplify (modified in place)
/// @param tolerance_sq Squared tolerance for collinearity
/// @return Number of points removed
size_t remove_collinear_points(Polygon &polygon, double tolerance_sq = 1.0);

/// Remove collinear points from a polyline (in place).
/// Points that lie on a straight line between their neighbors are removed.
/// First and last points are never removed.
/// @param polyline The polyline to simplify (modified in place)
/// @param tolerance_sq Squared tolerance for collinearity
/// @return Number of points removed
size_t remove_collinear_points(Polyline &polyline, double tolerance_sq = 1.0);

/// Remove collinear points from a ThickPolyline (in place).
/// Also handles the width array - merges widths when points are removed.
/// For closed ThickPolylines (front == back), handles wrap-around.
/// @param polyline The thick polyline to simplify (modified in place)
/// @param tolerance_sq Squared tolerance for collinearity
/// @return Number of points removed
size_t remove_collinear_points(ThickPolyline &polyline, double tolerance_sq = 1.0);

/// Remove collinear points from a vector of points (in place).
/// Generic version that works with any Points container.
/// @param points The points to simplify (modified in place)
/// @param is_closed Whether the path is closed (first == last conceptually)
/// @param tolerance_sq Squared tolerance for collinearity
/// @return Number of points removed
size_t remove_collinear_points(Points &points, bool is_closed, double tolerance_sq = 1.0);

} // namespace TravelOptimization
} // namespace Slic3r

#endif // slic3r_TravelOptimization_hpp_
