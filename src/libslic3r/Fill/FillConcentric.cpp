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
#include "libslic3r/TravelOptimization.hpp"

namespace Slic3r
{

// Builds a containment tree and traverses depth-first to complete each region's inner loops
// before moving to the next region. Works with both Athena::ExtrusionLine and Arachne::ExtrusionLine.
namespace
{

template<typename ExtrusionLineT, typename VariableWidthLinesT, typename ToThickPolylineFn>
void process_concentric_loops_by_region(std::vector<VariableWidthLinesT> &loops, ThickPolylines &thick_polylines_out,
                                        Point &last_pos, bool prefer_clockwise_movements,
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

            // Remove collinear points (eliminates 3 o'clock artifacts)
            TravelOptimization::remove_collinear_points(thick_polyline, 1.0);
        }
        thick_polylines_out.emplace_back(std::move(thick_polyline));
        last_pos = thick_polylines_out.back().last_point();
    };

    // Get children of a node sorted by nearest neighbor to last_pos
    auto get_sorted_children = [&](size_t idx) -> std::vector<size_t>
    {
        std::vector<size_t> result;
        std::vector<size_t> &children = nodes[idx].children;
        std::vector<bool> child_used(children.size(), false);
        for (size_t n = 0; n < children.size(); ++n)
        {
            double best_dist = std::numeric_limits<double>::max();
            size_t best_idx = 0;
            for (size_t c = 0; c < children.size(); ++c)
            {
                if (child_used[c])
                    continue;
                Point child_start = nodes[children[c]].polygon.points.empty()
                                        ? Point(0, 0)
                                        : nodes[children[c]].polygon.points.front();
                double dist = (child_start - last_pos).cast<double>().squaredNorm();
                if (dist < best_dist)
                {
                    best_dist = dist;
                    best_idx = c;
                }
            }
            child_used[best_idx] = true;
            result.push_back(children[best_idx]);
        }
        return result;
    };

    // Track which nodes have been processed to avoid infinite loops from cycles
    std::vector<bool> processed(nodes.size(), false);

    // Iterative depth-first traversal using explicit stack
    struct StackFrame
    {
        size_t node_idx;
        std::vector<size_t> sorted_children;
        size_t next_child;
    };

    // Process roots by nearest neighbor
    std::vector<bool> root_used(roots.size(), false);
    for (size_t n = 0; n < roots.size(); ++n)
    {
        double best_dist = std::numeric_limits<double>::max();
        size_t best_idx = 0;
        for (size_t r = 0; r < roots.size(); ++r)
        {
            if (root_used[r])
                continue;
            Point root_start = nodes[roots[r]].polygon.points.empty() ? Point(0, 0)
                                                                      : nodes[roots[r]].polygon.points.front();
            double dist = (root_start - last_pos).cast<double>().squaredNorm();
            if (dist < best_dist)
            {
                best_dist = dist;
                best_idx = r;
            }
        }
        root_used[best_idx] = true;
        size_t root_node = roots[best_idx];

        // Skip if already processed (can happen with cycle fixes)
        if (processed[root_node])
            continue;

        // Iterative traversal from this root
        std::vector<StackFrame> traverse_stack;
        traverse_stack.push_back({root_node, {}, 0});

        while (!traverse_stack.empty())
        {
            StackFrame &frame = traverse_stack.back();

            // First visit to this frame?
            if (frame.sorted_children.empty() && frame.next_child == 0)
            {
                // Skip if already processed (cycle)
                if (processed[frame.node_idx])
                {
                    traverse_stack.pop_back();
                    continue;
                }
                // Process this node
                processed[frame.node_idx] = true;
                process_node(frame.node_idx);
                frame.sorted_children = get_sorted_children(frame.node_idx);
            }

            // Find next unprocessed child
            while (frame.next_child < frame.sorted_children.size() &&
                   processed[frame.sorted_children[frame.next_child]])
            {
                frame.next_child++;
            }

            if (frame.next_child < frame.sorted_children.size())
            {
                // Push next child onto stack
                size_t child = frame.sorted_children[frame.next_child++];
                traverse_stack.push_back({child, {}, 0});
            }
            else
            {
                // Done with this node
                traverse_stack.pop_back();
            }
        }
    }

    // These are nodes that were stuck in containment cycles and couldn't be
    // reached from proper roots. Process them after the main tree in nearest-neighbor order.
    std::vector<bool> cycle_used(cycle_nodes.size(), false);
    for (size_t n = 0; n < cycle_nodes.size(); ++n)
    {
        double best_dist = std::numeric_limits<double>::max();
        size_t best_idx = 0;
        for (size_t c = 0; c < cycle_nodes.size(); ++c)
        {
            if (cycle_used[c] || processed[cycle_nodes[c]])
                continue;
            Point start = nodes[cycle_nodes[c]].polygon.points.empty() ? Point(0, 0)
                                                                       : nodes[cycle_nodes[c]].polygon.points.front();
            double dist = (start - last_pos).cast<double>().squaredNorm();
            if (dist < best_dist)
            {
                best_dist = dist;
                best_idx = c;
            }
        }
        if (best_dist == std::numeric_limits<double>::max())
            break; // All done
        cycle_used[best_idx] = true;
        size_t cycle_node = cycle_nodes[best_idx];

        if (processed[cycle_node])
            continue;

        // Traverse from this cycle node
        std::vector<StackFrame> traverse_stack;
        traverse_stack.push_back({cycle_node, {}, 0});

        while (!traverse_stack.empty())
        {
            StackFrame &frame = traverse_stack.back();

            if (frame.sorted_children.empty() && frame.next_child == 0)
            {
                if (processed[frame.node_idx])
                {
                    traverse_stack.pop_back();
                    continue;
                }
                processed[frame.node_idx] = true;
                process_node(frame.node_idx);
                frame.sorted_children = get_sorted_children(frame.node_idx);
            }

            while (frame.next_child < frame.sorted_children.size() &&
                   processed[frame.sorted_children[frame.next_child]])
            {
                frame.next_child++;
            }

            if (frame.next_child < frame.sorted_children.size())
            {
                size_t child = frame.sorted_children[frame.next_child++];
                traverse_stack.push_back({child, {}, 0});
            }
            else
            {
                traverse_stack.pop_back();
            }
        }
    }
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
    constexpr size_t MAX_ITERATIONS = 10000;
    size_t iteration = 0;
    // If geometry doesn't shrink for several consecutive iterations, break out early.
    // This prevents accumulating thousands of identical loops when Clipper2 gets stuck.
    // IMPORTANT: Use AREA, not point count! A square shrinking inward always has 4 points
    // but decreasing area. Point-count check would exit after 5 iterations on any rectangle!
    constexpr size_t MAX_STUCK_ITERATIONS = 5;
    size_t stuck_iterations = 0;
    double last_total_area = std::numeric_limits<double>::max();
    while (!last.empty() && iteration < MAX_ITERATIONS)
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

    // generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops
    loops = union_pt_chained_outside_in(loops);

    // split paths using a nearest neighbor search
    size_t iPathFirst = polylines_out.size();
    Point last_pos = params.start_near ? *params.start_near : Point(0, 0);

    for (const Polygon &loop : loops)
    {
        // Use nearest_vertex_index_closed to ensure we split at an actual vertex,
        // not a point that might be mid-edge (which would create an artificial segment)
        size_t nearest_idx = TravelOptimization::nearest_vertex_index_closed(loop.points, last_pos);
        polylines_out.emplace_back(loop.split_at_index(nearest_idx));
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
            // Athena requires separate width and spacing parameters for precise control
            coord_t extrusion_width = min_spacing;

            // No overlap for concentric infill (matches Arachne behavior)
            // TODO: Make overlap percentage configurable via settings if needed
            coord_t spacing = extrusion_width; // 0% overlap

            // Use extended constructor with separate width and spacing parameters
            Athena::WallToolPaths wallToolPaths(polygons, extrusion_width, extrusion_width, loops_count, 0,
                                                params.layer_height, *this->print_object_config, *this->print_config,
                                                extrusion_width, extrusion_width, spacing, spacing);

            std::vector<Athena::VariableWidthLines> loops = wallToolPaths.getToolPaths();

            process_concentric_loops_by_region<Athena::ExtrusionLine>(loops, thick_polylines_out, last_pos,
                                                                      params.prefer_clockwise_movements,
                                                                      [](const Athena::ExtrusionLine &e)
                                                                      { return Athena::to_thick_polyline(e); });
        }
        else
        {
            Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0,
                                                 params.layer_height, *this->print_object_config, *this->print_config);
            std::vector<Arachne::VariableWidthLines> loops = wallToolPaths.getToolPaths();

            process_concentric_loops_by_region<Arachne::ExtrusionLine>(loops, thick_polylines_out, last_pos,
                                                                       params.prefer_clockwise_movements,
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
