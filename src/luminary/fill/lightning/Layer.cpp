///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2021 Ultimaker B.V.
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "Layer.hpp" //The class we're implementing.

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/blocked_range2d.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_reduce.h>
#include <algorithm>
#include <limits>
#include <utility>
#include <cassert>
#include <cstddef>

#include "DistanceField.hpp"
#include "TreeNode.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/geometry/transform/Geometry.hpp"
#include "luminary/geometry/index/EdgeGrid.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/geometry/contours/Polygon.hpp"

namespace Luminary::FillLightning
{

coord_t Layer::getWeightedDistance(const Point &boundary_loc, const Point &unsupported_location)
{
    return coord_t((boundary_loc - unsupported_location).cast<double>().norm());
}

Point GroundingLocation::p() const
{
    assert(tree_node || boundary_location);
    return tree_node ? tree_node->getLocation() : *boundary_location;
}

inline static Point to_grid_point(const Point &point, const BoundingBox &bbox)
{
    return (point - bbox.min) / locator_cell_size;
}

void Layer::fillLocator(SparseNodeGrid &tree_node_locator, const BoundingBox &current_outlines_bbox)
{
    std::function<void(NodeSPtr)> add_node_to_locator_func =
        [&tree_node_locator, &current_outlines_bbox](const NodeSPtr &node)
    {
        tree_node_locator.insert(std::make_pair(to_grid_point(node->getLocation(), current_outlines_bbox), node));
    };
    for (auto &tree : tree_roots)
        tree->visitNodes(add_node_to_locator_func);
}

void Layer::generateNewTrees(const Polygons &current_overhang, const Polygons &current_outlines,
                             const BoundingBox &current_outlines_bbox, const EdgeGrid::Grid &outlines_locator,
                             const coord_t supporting_radius, const coord_t wall_supporting_radius,
                             const std::function<void()> &throw_on_cancel_callback)
{
    DistanceField distance_field(supporting_radius, current_outlines, current_outlines_bbox, current_overhang);
    throw_on_cancel_callback();

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);

    // Until no more points need to be added to support all:
    // Determine next point from tree/outline areas via distance-field
    size_t unsupported_cell_idx = 0;
    Point unsupported_location;
    while (distance_field.tryGetNextPoint(&unsupported_location, &unsupported_cell_idx, unsupported_cell_idx))
    {
        throw_on_cancel_callback();
        GroundingLocation grounding_loc = getBestGroundingLocation(unsupported_location, current_outlines,
                                                                   current_outlines_bbox, outlines_locator,
                                                                   supporting_radius, wall_supporting_radius,
                                                                   tree_node_locator);

        NodeSPtr new_parent;
        NodeSPtr new_child;
        this->attach(unsupported_location, grounding_loc, new_child, new_parent);
        tree_node_locator.insert(
            std::make_pair(to_grid_point(new_child->getLocation(), current_outlines_bbox), new_child));
        if (new_parent)
            tree_node_locator.insert(
                std::make_pair(to_grid_point(new_parent->getLocation(), current_outlines_bbox), new_parent));
        // update distance field
        distance_field.update(grounding_loc.p(), unsupported_location);
    }

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_to_svg(debug_out_path("FillLightning-TreeNodes-%d.svg", iRun++), current_outlines, this->tree_roots);
    }
#endif /* LIGHTNING_TREE_NODE_DEBUG_OUTPUT */
}

static bool polygonCollidesWithLineSegment(const Point &from, const Point &to, const EdgeGrid::Grid &loc_to_line)
{
    struct Visitor
    {
        explicit Visitor(const EdgeGrid::Grid &grid, const Line &line) : grid(grid), line(line) {}

        bool operator()(coord_t iy, coord_t ix)
        {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                 ++it_contour_and_segment)
            {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, line.a, line.b))
                {
                    this->intersect = true;
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid &grid;
        Line line;
        bool intersect = false;
    } visitor(loc_to_line, {from, to});

    loc_to_line.visit_cells_intersecting_line(from, to, visitor);
    return visitor.intersect;
}

GroundingLocation Layer::getBestGroundingLocation(const Point &unsupported_location, const Polygons &current_outlines,
                                                  const BoundingBox &current_outlines_bbox,
                                                  const EdgeGrid::Grid &outline_locator,
                                                  const coord_t supporting_radius, const coord_t wall_supporting_radius,
                                                  const SparseNodeGrid &tree_node_locator, const NodeSPtr &exclude_tree)
{
    // Closest point on current_outlines to unsupported_location:
    Point node_location;
    {
        double d2 = std::numeric_limits<double>::max();
        for (const Polygon &contour : current_outlines)
            if (contour.size() > 2)
            {
                Point prev = contour.points.back();
                for (const Point &p2 : contour.points)
                {
                    Point closest_point;
                    if (double d = line_alg::distance_to_squared(Line{prev, p2}, unsupported_location, &closest_point);
                        d < d2)
                    {
                        d2 = d;
                        node_location = closest_point;
                    }
                    prev = p2;
                }
            }
    }

    const auto within_dist = coord_t((node_location - unsupported_location).cast<double>().norm());

    NodeSPtr sub_tree{nullptr};
    coord_t current_dist = getWeightedDistance(node_location, unsupported_location);
    if (current_dist >= wall_supporting_radius)
    { // Only reconnect tree roots to other trees if they are not already close to the outlines.
        const coord_t search_radius = std::min(current_dist, within_dist);
        BoundingBox region(unsupported_location - Point(search_radius, search_radius),
                           unsupported_location +
                               Point(search_radius + locator_cell_size, search_radius + locator_cell_size));
        region.min = to_grid_point(region.min, current_outlines_bbox);
        region.max = to_grid_point(region.max, current_outlines_bbox);

        struct BestCandidate
        {
            coord_t dist;
            NodeSPtr tree;
            Point grid_addr;

            bool is_better_than(const BestCandidate &other) const
            {
                return dist < other.dist ||
                       (dist == other.dist &&
                        (grid_addr.y() < other.grid_addr.y() ||
                         (grid_addr.y() == other.grid_addr.y() && grid_addr.x() < other.grid_addr.x())));
            }
        };

        BestCandidate best = tbb::parallel_reduce(
            tbb::blocked_range2d<coord_t>(region.min.y(), region.max.y(), region.min.x(), region.max.x()),
            BestCandidate{current_dist,
                          nullptr,
                          {std::numeric_limits<coord_t>::lowest(), std::numeric_limits<coord_t>::lowest()}},
            [&exclude_tree = std::as_const(exclude_tree), &outline_locator = std::as_const(outline_locator),
             &supporting_radius = std::as_const(supporting_radius),
             &tree_node_locator = std::as_const(tree_node_locator),
             &unsupported_location = std::as_const(unsupported_location)](const tbb::blocked_range2d<coord_t> &range,
                                                                          BestCandidate best_so_far) -> BestCandidate
            {
                for (coord_t grid_addr_y = range.rows().begin(); grid_addr_y < range.rows().end(); ++grid_addr_y)
                    for (coord_t grid_addr_x = range.cols().begin(); grid_addr_x < range.cols().end(); ++grid_addr_x)
                    {
                        const Point local_grid_addr{grid_addr_x, grid_addr_y};
                        const auto it_range = tree_node_locator.equal_range(local_grid_addr);
                        for (auto it = it_range.first; it != it_range.second; ++it)
                        {
                            const NodeSPtr candidate_sub_tree = it->second.lock();
                            if ((candidate_sub_tree && candidate_sub_tree != exclude_tree) &&
                                !(exclude_tree && exclude_tree->hasOffspring(candidate_sub_tree)) &&
                                !polygonCollidesWithLineSegment(unsupported_location, candidate_sub_tree->getLocation(),
                                                                outline_locator))
                            {
                                const coord_t candidate_dist =
                                    candidate_sub_tree->getWeightedDistance(unsupported_location, supporting_radius);
                                BestCandidate candidate{candidate_dist, candidate_sub_tree, local_grid_addr};
                                if (candidate.is_better_than(best_so_far))
                                    best_so_far = candidate;
                            }
                        }
                    }
                return best_so_far;
            },
            [](BestCandidate a, const BestCandidate &b) -> BestCandidate
            { return b.is_better_than(a) ? b : a; }); // end of parallel_reduce

        current_dist = best.dist;
        sub_tree = best.tree;
    }

    return !sub_tree ? GroundingLocation{nullptr, node_location} : GroundingLocation{sub_tree, std::optional<Point>()};
}

bool Layer::attach(const Point &unsupported_location, const GroundingLocation &grounding_loc, NodeSPtr &new_child,
                   NodeSPtr &new_root)
{
    // Update trees & distance fields.
    if (grounding_loc.boundary_location)
    {
        new_root = Node::create(grounding_loc.p(), std::make_optional(grounding_loc.p()));
        new_child = new_root->addChild(unsupported_location);
        tree_roots.push_back(new_root);
        return true;
    }
    else
    {
        new_child = grounding_loc.tree_node->addChild(unsupported_location);
        return false;
    }
}

void Layer::reconnectRoots(std::vector<NodeSPtr> &to_be_reconnected_tree_roots, const Polygons &current_outlines,
                           const BoundingBox &current_outlines_bbox, const EdgeGrid::Grid &outline_locator,
                           const coord_t supporting_radius, const coord_t wall_supporting_radius)
{
    constexpr coord_t tree_connecting_ignore_offset = 100;

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);

    const coord_t within_max_dist = outline_locator.resolution() * 2;
    for (const auto &root_ptr : to_be_reconnected_tree_roots)
    {
        auto old_root_it = std::find(tree_roots.begin(), tree_roots.end(), root_ptr);

        if (root_ptr->getLastGroundingLocation())
        {
            const Point &ground_loc = *root_ptr->getLastGroundingLocation();
            if (ground_loc != root_ptr->getLocation())
            {
                Point new_root_pt;
                // Find an intersection of the line segment from root_ptr->getLocation() to ground_loc, at within_max_dist from ground_loc.
                if (lineSegmentPolygonsIntersection(root_ptr->getLocation(), ground_loc, outline_locator, new_root_pt,
                                                    within_max_dist))
                {
                    auto new_root = Node::create(new_root_pt, new_root_pt);
                    root_ptr->addChild(new_root);
                    new_root->reroot();

                    tree_node_locator.insert(
                        std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

                    *old_root_it = std::move(new_root); // replace old root with new root
                    continue;
                }
            }
        }

        const coord_t tree_connecting_ignore_width =
            wall_supporting_radius -
            tree_connecting_ignore_offset; // Ideally, the boundary size in which the valence rule is ignored would be configurable.
        GroundingLocation ground = getBestGroundingLocation(root_ptr->getLocation(), current_outlines,
                                                            current_outlines_bbox, outline_locator, supporting_radius,
                                                            tree_connecting_ignore_width, tree_node_locator, root_ptr);
        if (ground.boundary_location)
        {
            if (*ground.boundary_location == root_ptr->getLocation())
                continue; // Already on the boundary.

            auto new_root = Node::create(ground.p(), ground.p());
            auto attach_ptr = root_ptr->closestNode(new_root->getLocation());
            attach_ptr->reroot();

            new_root->addChild(attach_ptr);
            tree_node_locator.insert(
                std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

            *old_root_it = std::move(new_root); // replace old root with new root
        }
        else
        {
            assert(ground.tree_node);
            assert(ground.tree_node != root_ptr);
            assert(!root_ptr->hasOffspring(ground.tree_node));
            assert(!ground.tree_node->hasOffspring(root_ptr));

            auto attach_ptr = root_ptr->closestNode(ground.tree_node->getLocation());
            attach_ptr->reroot();

            ground.tree_node->addChild(attach_ptr);

            // remove old root
            *old_root_it = std::move(tree_roots.back());
            tree_roots.pop_back();
        }
    }
}

Polylines Layer::convertToLines(const Polygons &limit_to_outline, const coord_t line_overlap) const
{
    if (tree_roots.empty())
        return {};

    Polylines result_lines;
    for (const auto &tree : tree_roots)
        tree->convertToPolylines(result_lines, line_overlap);

    return intersection_pl(result_lines, limit_to_outline);
}

} // namespace Luminary::FillLightning
