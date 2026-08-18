///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ ported from lib/Slic3r/Fill/Concentric.pm:
///|/ Copyright (c) Prusa Research 2016 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <algorithm>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Athena/WallToolPaths.hpp"
#include "libslic3r/Athena/utils/ExtrusionLine.hpp"
#include "FillConcentric.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/DebugOutput.hpp"
#include "libslic3r/TravelOptimization.hpp"

namespace Slic3r
{

// Builds a containment tree and traverses depth-first, clustering spatially
// adjacent siblings to complete each region before moving on.
// Works with both Athena::ExtrusionLine and Arachne::ExtrusionLine.
// line_spacing is used to determine the cluster gap threshold (3x spacing).
namespace
{

template<typename ExtrusionLineT, typename VariableWidthLinesT, typename ToThickPolylineFn>
void process_concentric_loops_by_region(std::vector<VariableWidthLinesT> &loops, ThickPolylines &thick_polylines_out,
                                        Point &last_pos, bool prefer_clockwise_movements, coord_t line_spacing,
                                        ToThickPolylineFn to_thick_polyline_fn)
{
    // Structure to hold extrusion with metadata for containment tree
    struct ExtrusionNode
    {
        const ExtrusionLineT *extrusion;
        Polygon polygon;
        std::vector<size_t> children;
        size_t parent = SIZE_MAX;
    };
    std::vector<ExtrusionNode> nodes;

    // Collect all extrusions with their polygons
    for (VariableWidthLinesT &loop : loops)
    {
        if (loop.empty())
            continue;
        for (const ExtrusionLineT &wall : loop)
        {
            if (wall.empty())
                continue;
            ExtrusionNode node;
            node.extrusion = &wall;
            node.polygon = wall.toPolygon();
            nodes.push_back(std::move(node));
        }
    }

    if (nodes.empty())
        return;

    // Build containment tree: for each node, find its smallest containing parent
    // CRITICAL: Parent must have LARGER area than child to prevent cycles
    // CRITICAL: Parent's bounding box must contain child's bounding box (prevents sibling misassignment)

    // First, compute areas and bounding boxes for all nodes
    std::vector<double> node_areas(nodes.size());
    std::vector<BoundingBox> node_bboxes(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        node_areas[i] = std::abs(nodes[i].polygon.area());
        node_bboxes[i] = nodes[i].polygon.bounding_box();
    }

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        Point test_point = nodes[i].polygon.points.empty() ? Point(0, 0) : nodes[i].polygon.points.front();
        double smallest_parent_area = std::numeric_limits<double>::max();
        size_t best_parent = SIZE_MAX;

        for (size_t j = 0; j < nodes.size(); ++j)
        {
            if (i == j)
                continue;
            // Parent must be LARGER than child (prevents cycles)
            if (node_areas[j] <= node_areas[i])
                continue;
            // Parent's bbox must contain child's bbox (prevents sibling misassignment)
            if (!node_bboxes[j].contains(node_bboxes[i]))
                continue;
            // Looking for smallest parent that still contains us
            if (node_areas[j] >= smallest_parent_area)
                continue;
            if (nodes[j].polygon.contains(test_point))
            {
                smallest_parent_area = node_areas[j];
                best_parent = j;
            }
        }
        nodes[i].parent = best_parent;
        if (best_parent != SIZE_MAX)
            nodes[best_parent].children.push_back(i);
    }

    // Find root nodes (no parent)
    std::vector<size_t> roots;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].parent == SIZE_MAX)
            roots.push_back(i);
    }

    // If two loops have test points inside each other, they form a cycle and neither
    // becomes a root. This causes gaps. Detect these nodes and process them AFTER
    // the main tree to maintain proper ordering.
    std::vector<bool> visitable(nodes.size(), false);
    std::vector<size_t> cycle_nodes; // Nodes stuck in cycles, processed last

    // Mark all nodes reachable from roots (iterative to avoid stack overflow)
    std::vector<size_t> stack;
    for (size_t root : roots)
        stack.push_back(root);

    while (!stack.empty())
    {
        size_t idx = stack.back();
        stack.pop_back();
        if (visitable[idx])
            continue;
        visitable[idx] = true;
        for (size_t child : nodes[idx].children)
            if (!visitable[child])
                stack.push_back(child);
    }

    // Collect nodes stuck in cycles (don't add to roots - process them last)
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (!visitable[i])
        {
            cycle_nodes.push_back(i);
            visitable[i] = true;
            // Mark descendants too so we don't double-count
            stack.push_back(i);
            while (!stack.empty())
            {
                size_t idx = stack.back();
                stack.pop_back();
                for (size_t child : nodes[idx].children)
                {
                    if (!visitable[child])
                    {
                        visitable[child] = true;
                        stack.push_back(child);
                    }
                }
            }
        }
    }

    // Process node: output its polyline
    auto process_node = [&](size_t idx)
    {
        const ExtrusionLineT *extrusion = nodes[idx].extrusion;
        ThickPolyline thick_polyline = to_thick_polyline_fn(*extrusion);
        if (extrusion->is_closed)
        {
            if (const bool extrusion_reverse = prefer_clockwise_movements ? !extrusion->is_contour()
                                                                          : extrusion->is_contour();
                extrusion_reverse)
                thick_polyline.reverse();

            // Ensure loop is properly closed (front == back)
            if (thick_polyline.points.size() > 2 && thick_polyline.points.front() != thick_polyline.points.back())
            {
                thick_polyline.width.push_back(thick_polyline.width.back());
                thick_polyline.width.push_back(thick_polyline.width.front());
                thick_polyline.points.push_back(thick_polyline.points.front());
            }

            // Rotate closed loop to start at the vertex nearest to last_pos.
            // Without this, all loops start at Arachne's default 3 o'clock point,
            // so last_pos is always on the right side of every loop. This gives the
            // ordering algorithm a completely distorted view of nozzle proximity,
            // causing it to drift rightward and strand left-side regions.
            if (thick_polyline.points.size() >= 4)
            {
                size_t n = thick_polyline.points.size() - 1; // unique points (exclude closing duplicate)
                size_t nearest = 0;
                double nearest_dist = std::numeric_limits<double>::max();
                for (size_t i = 0; i < n; ++i)
                {
                    double d = (thick_polyline.points[i] - last_pos).cast<double>().squaredNorm();
                    if (d < nearest_dist)
                    {
                        nearest_dist = d;
                        nearest = i;
                    }
                }

                if (nearest > 0)
                {
                    std::rotate(thick_polyline.points.begin(),
                                thick_polyline.points.begin() + static_cast<ptrdiff_t>(nearest),
                                thick_polyline.points.begin() + static_cast<ptrdiff_t>(n));
                    thick_polyline.points.back() = thick_polyline.points.front();

                    if (thick_polyline.width.size() >= n * 2)
                    {
                        std::rotate(thick_polyline.width.begin(),
                                    thick_polyline.width.begin() + static_cast<ptrdiff_t>(nearest * 2),
                                    thick_polyline.width.begin() + static_cast<ptrdiff_t>(n * 2));
                    }
                }
            }

            // Remove collinear points (eliminates 3 o'clock artifacts)
            TravelOptimization::remove_collinear_points(thick_polyline, 1.0);
        }
        thick_polylines_out.emplace_back(std::move(thick_polyline));
        last_pos = thick_polylines_out.back().last_point();
    };

    // Track which nodes have been processed
    std::vector<bool> processed(nodes.size(), false);

    // --- Cluster-based depth-first traversal ---
    // The key insight: when concentric rings split around holes, sibling loops (same parent)
    // scatter across separate spatial regions. Treating them as a flat pool causes the nozzle
    // to drift between regions. Instead, we cluster spatially adjacent siblings and process
    // each cluster completely before moving to the next.
    //
    // Cluster gap = 3x line spacing (~1.2mm). This groups loops within the same gap region
    // while keeping loops across holes (5mm+ apart) in separate clusters.

    double cluster_gap_sq = double(line_spacing) * double(line_spacing) * 9.0;

    // Cluster sibling indices by spatial adjacency using flood-fill.
    // Two siblings are adjacent if any vertex-to-vertex distance < cluster_gap.
    auto cluster_siblings = [&](const std::vector<size_t> &siblings) -> std::vector<std::vector<size_t>>
    {
        if (siblings.empty())
            return {};
        if (siblings.size() == 1)
            return {siblings};

        coord_t expand = coord_t(double(line_spacing) * 3.0) + 1;
        std::vector<std::vector<size_t>> clusters;
        std::vector<bool> assigned(siblings.size(), false);

        for (size_t s = 0; s < siblings.size(); ++s)
        {
            if (assigned[s])
                continue;

            std::vector<size_t> cluster;
            std::vector<size_t> flood_queue;
            assigned[s] = true;
            cluster.push_back(siblings[s]);
            flood_queue.push_back(s);

            while (!flood_queue.empty())
            {
                size_t cur_local = flood_queue.back();
                flood_queue.pop_back();
                size_t cur_node = siblings[cur_local];

                // Expand current node's bbox for quick rejection
                BoundingBox expanded_bbox = node_bboxes[cur_node];
                expanded_bbox.offset(expand);

                for (size_t o = 0; o < siblings.size(); ++o)
                {
                    if (assigned[o])
                        continue;
                    size_t other_node = siblings[o];

                    // Quick bbox rejection
                    const BoundingBox &other_bbox = node_bboxes[other_node];
                    if (expanded_bbox.max.x() < other_bbox.min.x() || expanded_bbox.min.x() > other_bbox.max.x() ||
                        expanded_bbox.max.y() < other_bbox.min.y() || expanded_bbox.min.y() > other_bbox.max.y())
                        continue;

                    // Check min vertex-to-vertex distance
                    bool adjacent = false;
                    for (const Point &pa : nodes[cur_node].polygon.points)
                    {
                        for (const Point &pb : nodes[other_node].polygon.points)
                        {
                            if ((pa - pb).cast<double>().squaredNorm() <= cluster_gap_sq)
                            {
                                adjacent = true;
                                break;
                            }
                        }
                        if (adjacent)
                            break;
                    }

                    if (adjacent)
                    {
                        assigned[o] = true;
                        cluster.push_back(other_node);
                        flood_queue.push_back(o);
                    }
                }
            }

            clusters.push_back(std::move(cluster));
        }

        return clusters;
    };

    // Nearest-vertex distance from last_pos to the closest unprocessed node in a cluster
    auto cluster_min_dist = [&](const std::vector<size_t> &cluster) -> double
    {
        double best = std::numeric_limits<double>::max();
        for (size_t idx : cluster)
        {
            if (processed[idx])
                continue;
            for (const Point &pt : nodes[idx].polygon.points)
            {
                double d = (pt - last_pos).cast<double>().squaredNorm();
                if (d < best)
                    best = d;
            }
        }
        return best;
    };

    // Recursive cluster traversal. Two mutually-recursive functions:
    // - process_peer_clusters: given a set of peer clusters, repeatedly picks the nearest
    //   cluster (re-evaluating proximity each time!) and processes it completely.
    // - process_single_cluster: processes all loops in one cluster by nearest-neighbor,
    //   recursing into children's clusters before continuing with remaining siblings.
    //
    // The re-evaluation after each cluster completes is critical: after finishing one
    // region, the nozzle position has changed, so the "nearest" peer cluster may differ
    // from what it was at push time.

    std::function<void(std::vector<size_t> &)> process_single_cluster;
    std::function<void(std::vector<std::vector<size_t>> &)> process_peer_clusters;

    process_single_cluster = [&](std::vector<size_t> &cluster)
    {
        while (true)
        {
            // Find nearest unprocessed loop in this cluster
            double best_dist = std::numeric_limits<double>::max();
            size_t best_node = SIZE_MAX;
            for (size_t idx : cluster)
            {
                if (processed[idx])
                    continue;
                for (const Point &pt : nodes[idx].polygon.points)
                {
                    double d = (pt - last_pos).cast<double>().squaredNorm();
                    if (d < best_dist)
                    {
                        best_dist = d;
                        best_node = idx;
                    }
                }
            }

            if (best_node == SIZE_MAX)
                return; // Cluster exhausted

            processed[best_node] = true;
            process_node(best_node);

            // Recurse into children's clusters before continuing with remaining siblings
            if (!nodes[best_node].children.empty())
            {
                auto child_clusters = cluster_siblings(nodes[best_node].children);
                process_peer_clusters(child_clusters);
            }
        }
    };

    process_peer_clusters = [&](std::vector<std::vector<size_t>> &clusters)
    {
        while (true)
        {
            // Re-evaluate: find the nearest cluster based on CURRENT nozzle position
            double best_dist = std::numeric_limits<double>::max();
            size_t best_idx = SIZE_MAX;
            for (size_t c = 0; c < clusters.size(); ++c)
            {
                double d = cluster_min_dist(clusters[c]);
                if (d < best_dist)
                {
                    best_dist = d;
                    best_idx = c;
                }
            }

            if (best_idx == SIZE_MAX)
                return; // All clusters done

            // Process this cluster completely (including recursive children)
            process_single_cluster(clusters[best_idx]);
        }
    };

    // Start: cluster roots + cycle nodes and process
    std::vector<size_t> all_initial;
    all_initial.reserve(roots.size() + cycle_nodes.size());
    all_initial.insert(all_initial.end(), roots.begin(), roots.end());
    all_initial.insert(all_initial.end(), cycle_nodes.begin(), cycle_nodes.end());
    auto initial_clusters = cluster_siblings(all_initial);
    process_peer_clusters(initial_clusters);
}

} // anonymous namespace

void FillConcentric::_fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                                          const std::pair<float, Point> &direction, ExPolygon expolygon,
                                          Polylines &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density <= 0.0f || std::isnan(params.density) || std::isinf(params.density))
    {
        // Return empty - can't generate infill with invalid density
        return;
    }

    coord_t distance = coord_t(min_spacing / params.density);

    if (params.density > 0.9999f && !params.dont_adjust)
    {
        distance = Slic3r::FillConcentric::_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    // If the bounding box minimum dimension is smaller than 2x the offset distance,
    // concentric fill won't fit and will either produce nothing or loop endlessly.
    coord_t min_dimension = std::min(bounding_box.size()(0), bounding_box.size()(1));
    if (min_dimension < 2 * distance)
    {
        // Area too small for concentric infill at this spacing
        return;
    }

    // When concentric offset processes ExPolygons with holes, the offset operation can shrink the outer contour
    // to nothing while leaving orphaned small regions that won't shrink further and get stuck in offset_ex.
    // Filter based on number of loops that can fit: regions that can't fit at least MIN_LOOPS are too small
    // to produce meaningful concentric infill and cause Clipper2 offset_ex to hang on degenerate geometry.
    //
    // Calculation: loops_that_fit = region_diameter / line_spacing
    //              where line_spacing = extrusion_width / density = distance
    //              threshold = MIN_LOOPS × distance
    //
    // At 20% density (0.4mm width): threshold = 5 × 2mm = 10mm (catches 7.64mm orphaned holes)
    // At 100% density (0.4mm width): threshold = 5 × 0.4mm = 2mm (tighter threshold for solid fill)
    constexpr double MIN_LOOPS = 5.0;
    coord_t min_size_for_concentric = coord_t(distance * MIN_LOOPS);
    coord_t max_dimension = std::max(bounding_box.size()(0), bounding_box.size()(1));

    if (max_dimension < min_size_for_concentric)
    {
        // Too small to fit meaningful concentric infill (< MIN_LOOPS)
        return;
    }

    Polygons loops = to_polygons(expolygon);
    ExPolygons last{std::move(expolygon)};

    // CRITICAL: Clipper2 FRAGMENTS geometry catastrophically with offset2_ex morphological operations.
    // On complex shapes with holes, offset2_ex creates exponential polygon fragmentation:
    //   - Iteration 20: 184 loops → 1,005 loops (5x jump)
    //   - Iteration 30: 3,366 loops
    //   - Iteration 50: 44,378 loops (then hangs in union_pt)
    //
    // ROOT CAUSE: offset2_ex does morphological closing (shrink then expand). Clipper2's algorithm
    // fragments complex geometry instead of keeping it unified like Clipper1 did.
    //
    // SOLUTION: Use simple offset_ex (just shrink) instead of offset2_ex (shrink+expand).
    // This prevents fragmentation and naturally shrinks geometry to empty without MAX_LOOPS.
    // Trade-off: Slightly less smooth infill paths, but MASSIVELY faster and actually works.
    // With certain geometry configurations, offset may never shrink to exactly empty.
    // Add safety limit to prevent infinite loops while still allowing complex geometry to process.
    // preFlight: Compute a physically-derived loop limit from the geometry. The maximum number
    // of concentric rings that can fit = largest_dimension / spacing. Clipper2 fragmentation can
    // cause the offset loop to produce far more polygons than rings (slivers at each iteration),
    // which chokes downstream processing. Cap total accumulated loops at 2x the physical maximum
    // to allow headroom for geometry with holes while preventing pathological accumulation.
    size_t max_physical_rings = std::max(size_t(2), size_t(max_dimension / distance));
    size_t max_loops = max_physical_rings * 2 + loops.size(); // 2x headroom + initial contour
    size_t max_iterations = max_physical_rings * 2;           // same 2x headroom for iterations
    size_t iteration = 0;
    // If geometry doesn't shrink for several consecutive iterations, break out early.
    // This prevents accumulating thousands of identical loops when Clipper2 gets stuck.
    // IMPORTANT: Use AREA, not point count! A square shrinking inward always has 4 points
    // but decreasing area. Point-count check would exit after 5 iterations on any rectangle!
    constexpr size_t MAX_STUCK_ITERATIONS = 5;
    size_t stuck_iterations = 0;
    double last_total_area = std::numeric_limits<double>::max();
    while (!last.empty() && iteration < max_iterations && loops.size() < max_loops)
    {
        ++iteration;

        last = offset_ex(last, -distance); // Simple offset, no morphological closing

        // Calculate total AREA after offset (not point count!)
        // A square shrinking inward always has 4 points but decreasing area.
        double current_total_area = 0;
        for (const ExPolygon &ep : last)
        {
            current_total_area += std::abs(ep.area());
        }

        // Check if geometry stopped shrinking (area not decreasing)
        // Use 0.9999 multiplier to handle floating point precision
        if (iteration > 1 && current_total_area >= last_total_area * 0.9999)
        {
            ++stuck_iterations;
            if (stuck_iterations >= MAX_STUCK_ITERATIONS)
            {
                break; // Exit loop early - geometry is stuck
            }
        }
        else
        {
            // Geometry is shrinking, reset counter
            stuck_iterations = 0;
        }
        last_total_area = current_total_area;

        // At exactly 50% density (distance = min_spacing * 2.0), offset operations can create
        // zero-area polygons, coincident points, or collapsed loops due to perfect coordinate alignment.
        // These degenerate geometries trigger access violations in Clipper2's union operation.
        // Filter them out before they cause problems.
        last.erase(std::remove_if(last.begin(), last.end(),
                                  [](const ExPolygon &ep)
                                  {
                                      double area = ep.area();
                                      return area < 1.0 || std::isnan(area) || std::isinf(area);
                                  }),
                   last.end());

        append(loops, to_polygons(last));
    }

    // Generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops.
    // preFlight: Do NOT use union_pt_chained_outside_in here. It feeds all accumulated loops
    // into a single Clipper2 Union operation which is O(N^2) on intersections and hangs when
    // the loop count is high (thousands of loops from iterative offset fragmentation).
    // Concentric loops are nested and non-overlapping by construction - they don't need a
    // Union to resolve overlaps. Sort by descending area (outside-in) instead: O(N log N).
    std::sort(loops.begin(), loops.end(),
              [](const Polygon &a, const Polygon &b) { return std::abs(a.area()) > std::abs(b.area()); });

    // Nearest-neighbor loop ordering with smallest-first bias: among nearby unprocessed
    // loops, pick the smallest (by area) first. Small regions finish quickly without
    // pulling the nozzle far away. Uses closest-vertex distance for proximity.
    size_t iPathFirst = polylines_out.size();
    Point last_pos = params.start_near ? *params.start_near : Point(0, 0);

    // Precompute loop areas for the smallest-first heuristic
    std::vector<double> loop_areas(loops.size());
    for (size_t i = 0; i < loops.size(); ++i)
        loop_areas[i] = std::abs(loops[i].area());

    // Proximity radius: 25x ring spacing (~10mm for 0.4mm spacing). Large enough to
    // catch small nearby regions, small enough to not reach distant hole regions.
    double proximity_radius = double(distance) * 25.0;

    std::vector<bool> loop_used(loops.size(), false);
    for (size_t n = 0; n < loops.size(); ++n)
    {
        // Pass 1: find nearest vertex distance across all unused loops
        double nearest_dist_sq = std::numeric_limits<double>::max();
        for (size_t i = 0; i < loops.size(); ++i)
        {
            if (loop_used[i])
                continue;
            size_t nv = TravelOptimization::nearest_vertex_index_closed(loops[i].points, last_pos);
            double dist = (loops[i].points[nv] - last_pos).cast<double>().squaredNorm();
            if (dist < nearest_dist_sq)
                nearest_dist_sq = dist;
        }

        double threshold = std::sqrt(nearest_dist_sq) + proximity_radius;
        double threshold_sq = threshold * threshold;

        // Pass 2: among loops within proximity threshold, pick smallest area
        double smallest_area = std::numeric_limits<double>::max();
        double best_dist = std::numeric_limits<double>::max();
        size_t best_idx = 0;
        for (size_t i = 0; i < loops.size(); ++i)
        {
            if (loop_used[i])
                continue;
            size_t nv = TravelOptimization::nearest_vertex_index_closed(loops[i].points, last_pos);
            double dist = (loops[i].points[nv] - last_pos).cast<double>().squaredNorm();
            if (dist > threshold_sq)
                continue;
            if (loop_areas[i] < smallest_area || (loop_areas[i] == smallest_area && dist < best_dist))
            {
                smallest_area = loop_areas[i];
                best_dist = dist;
                best_idx = i;
            }
        }
        loop_used[best_idx] = true;
        size_t split_idx = TravelOptimization::nearest_vertex_index_closed(loops[best_idx].points, last_pos);
        polylines_out.emplace_back(loops[best_idx].split_at_index(split_idx));
        last_pos = polylines_out.back().last_point();
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++i)
    {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid())
        {
            if (params.prefer_clockwise_movements)
                polylines_out[i].reverse();

            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);

            ++j;
        }
    }

    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + int(j), polylines_out.end());

    //TODO: return ExtrusionLoop objects to get better chained paths,
    // otherwise the outermost loop starts at the closest point to (0, 0).
    // We want the loops to be split inside the G-code generator to get optimum path planning.
}

void FillConcentric::_fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                                          const std::pair<float, Point> &direction, ExPolygon expolygon,
                                          ThickPolylines &thick_polylines_out)
{
    assert(params.use_advanced_perimeters);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    // no rotation is supported for this infill pattern
    Point bbox_size = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density > 0.9999f && !params.dont_adjust)
    {
        coord_t loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons polygons = offset(expolygon, float(min_spacing) / 2.f);
        size_t firts_poly_idx = thick_polylines_out.size();
        Point last_pos = params.start_near ? *params.start_near : Point(0, 0);

        if (params.perimeter_generator == PerimeterGeneratorType::Athena)
        {
            // Use min_spacing for both width and spacing, matching Arachne's behavior.
            // min_spacing already has the standard overlap baked in via Flow spacing.
            Athena::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height,
                                                *this->print_object_config, *this->print_config, min_spacing,
                                                min_spacing, min_spacing, min_spacing, 0, -1, 1.0, coord_t(10000),
                                                params.max_bead_width);
            wallToolPaths.set_debug_print_z(this->z);

            std::vector<Athena::VariableWidthLines> loops = wallToolPaths.getToolPaths();
            if (Slic3r::debug_enabled(Slic3r::DBG_FILL))
            {
                double band_area = std::abs(expolygon.area()) * SCALING_FACTOR * SCALING_FACTOR;
                dbg_log(Slic3r::DBG_FILL, this->z, "FILL", "CONC_IN area=%.3fmm2 holes=%zu spacing=%.4f loop_sets=%zu",
                        band_area, expolygon.holes.size(), unscaled<double>(min_spacing), loops.size());
                for (size_t li = 0; li < loops.size(); ++li)
                    for (const Athena::ExtrusionLine &el : loops[li])
                    {
                        if (el.junctions.empty())
                            continue;
                        coord_t wmin = std::numeric_limits<coord_t>::max(), wmax = 0;
                        for (const auto &j : el.junctions)
                        {
                            wmin = std::min(wmin, j.w);
                            wmax = std::max(wmax, j.w);
                        }
                        dbg_log(Slic3r::DBG_FILL, this->z, "FILL",
                                "CONC_LINE set=%zu inset=%zu closed=%d odd=%d pts=%zu w=%.3f-%.3fmm len=%.2fmm", li,
                                size_t(el.inset_idx), int(el.is_closed), int(el.is_odd), el.junctions.size(),
                                unscaled<double>(wmin), unscaled<double>(wmax),
                                unscaled<double>(double(el.getLength())));
                    }
            }

            process_concentric_loops_by_region<Athena::ExtrusionLine>(loops, thick_polylines_out, last_pos,
                                                                      params.prefer_clockwise_movements, min_spacing,
                                                                      [](const Athena::ExtrusionLine &e)
                                                                      { return Athena::to_thick_polyline(e); });
        }
        else
        {
            Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0,
                                                 params.layer_height, *this->print_object_config, *this->print_config);
            std::vector<Arachne::VariableWidthLines> loops = wallToolPaths.getToolPaths();

            process_concentric_loops_by_region<Arachne::ExtrusionLine>(loops, thick_polylines_out, last_pos,
                                                                       params.prefer_clockwise_movements, min_spacing,
                                                                       [](const Arachne::ExtrusionLine &e)
                                                                       { return Arachne::to_thick_polyline(e); });
        }

        // Clip open paths to prevent the extruder from getting exactly on the first point.
        // But DO NOT clip closed loops (front == back) - they become ExtrusionLoop objects.
        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i)
        {
            ThickPolyline &tp = thick_polylines_out[i];
            bool is_closed_loop = tp.points.size() >= 3 && tp.points.front() == tp.points.back();
            if (!is_closed_loop)
            {
                tp.clip_end(this->loop_clipping);
            }
            if (tp.is_valid())
            {
                if (j < i)
                    thick_polylines_out[j] = std::move(tp);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());
    }
    else
    {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

} // namespace Slic3r
