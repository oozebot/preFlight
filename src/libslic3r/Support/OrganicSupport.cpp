///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "OrganicSupport.hpp"

#include "BaobabSupport.hpp"
#include "libslic3r/DebugOutput.hpp"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/partitioner.h>
#include <boost/container/small_vector.hpp>
#include <boost/container/vector.hpp>
#include <cassert>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <cinttypes>
#include <cstddef>

#include "../AABBTreeLines.hpp"
#include "../ClipperUtils.hpp"
#include "../Polygon.hpp"
#include "../MutablePolygon.hpp"
#include "../Tesselate.hpp"
#include "../TriangleMeshSlicer.hpp"
#include "admesh/stl.h"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Support/TreeModelVolumes.hpp"
#include "libslic3r/Support/TreeSupport.hpp"
#include "libslic3r/Support/TreeSupportCommon.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/PerfTiming.hpp"

#define TREE_SUPPORT_ORGANIC_NUDGE_NEW 1

#ifndef TREE_SUPPORT_ORGANIC_NUDGE_NEW
#include <openvdb/tools/VolumeToSpheres.h>

// Old version using OpenVDB, works but it is extremely slow for complex meshes.
#include "../OpenVDBUtilsLegacy.hpp"
#endif // TREE_SUPPORT_ORGANIC_NUDGE_NEW

namespace Slic3r
{

namespace FFFTreeSupport
{

// Single slice through a single branch or trough a number of branches.
struct Slice
{
    // All polygons collected for this slice.
    Polygons polygons;
    // All bottom contacts collected for this slice.
    Polygons bottom_contacts;
    // Polygons that are tips (contact object), kept separate for routing to top_contacts
    Polygons tip_polygons;
    // How many branches were merged in this slice? Used to decide whether ClipperLib union is needed.
    size_t num_branches{0};
};

struct Element
{
    // Current position of the centerline including the Z coordinate, unscaled.
    Vec3f position;
    float radius;

    // Index of this layer, including the raft layers.
    LayerIndex layer_idx;

    // Limits where the centerline could be placed at the current layer Z.
    Polygons influence_area;

    // Locked node should not be moved. Locked nodes are at the top of an object or at the tips of branches.
    bool locked;

    // Previous position, for Laplacian smoothing, unscaled.
    Vec3f prev_position;

    // For sphere tracing and other collision detection optimizations.
    Vec3f last_collision;
    double last_collision_depth;

    struct CollisionSphere
    {
        // Minimum Z for which the sphere collision will be evaluated.
        // Limited by the minimum sloping angle and by the bottom of the tree.
        float min_z{-std::numeric_limits<float>::max()};
        // Maximum Z for which the sphere collision will be evaluated.
        // Limited by the minimum sloping angle and by the tip of the current branch.
        float max_z{std::numeric_limits<float>::max()};
        // Span of layers to test collision of this sphere against.
        uint32_t layer_begin;
        uint32_t layer_end;
    };

    CollisionSphere collision_sphere;
};

struct Branch;

struct Bifurcation
{
    Branch *branch;
    double area;
};

// Single branch of a tree.
struct Branch
{
    std::vector<Element> path;

    using Bifurcations =
#ifdef NDEBUG
        // To reduce memory allocation in release mode.
        boost::container::small_vector<Bifurcation, 4>;
#else  // NDEBUG
       // To ease debugging.
        std::vector<Bifurcation>;
#endif // NDEBUG

    Bifurcations up;
    Bifurcation down;

    // How many of the thick up branches are considered continuation of the trunk?
    // These will be smoothed out together.
    size_t num_up_trunk;

    bool has_root() const { return this->down.branch == nullptr; }
    bool has_tip() const { return this->up.empty(); }
};

struct Tree
{
    // Branches: Store of all branches.
    // The first branch is the root of the tree.
    Slic3r::deque<Branch> branches;

    Branch &root() { return branches.front(); }
    const Branch &root() const { return branches.front(); }

    // Result of slicing the branches.
    std::vector<Slice> slices;
    // First layer index of the first slice in the vector above.
    LayerIndex first_layer_id{-1};
};

using Forest = std::vector<Tree>;
using Trees = std::vector<Tree>;

Element to_tree_element(const TreeSupportSettings &config, const SlicingParameters &slicing_params,
                        SupportElement &element, bool is_root)
{
    Element out;
    out.position = to_3d(unscaled<float>(element.state.result_on_layer),
                         float(layer_z(slicing_params, config, element.state.layer_idx)));
    out.radius = support_element_radius(config, element);
    out.layer_idx = element.state.layer_idx;
    out.influence_area = std::move(element.influence_area);
    out.locked = (is_root && element.state.layer_idx > 0) || element.state.locked();
    return out;
}

// Convert move bounds into a forest of trees, each tree made of a graph of branches and bifurcation points.
// Destroys move_bounds.
Forest make_forest(const TreeSupportSettings &config, const SlicingParameters &slicing_params,
                   std::vector<SupportElements> &&move_bounds)
{
    struct TreeVisitor
    {
        void visit_recursive(std::vector<SupportElements> &move_bounds, SupportElement &start_element,
                             Branch *parent_branch, Tree &out) const
        {
            assert(!start_element.state.marked && !start_element.parents.empty());
            // Collect elements up to a bifurcation above.
            start_element.state.marked = true;
            // For each branch bifurcating from this point:
            //            SupportElements &layer       = move_bounds[start_element.state.layer_idx];
            SupportElements &layer_above = move_bounds[start_element.state.layer_idx + 1];
            for (size_t parent_idx = 0; parent_idx < start_element.parents.size(); ++parent_idx)
            {
                Branch branch;
                if (parent_branch)
                    // Duplicate the last element of the trunk below.
                    // If this branch has a smaller diameter than the trunk below, its centerline will not be aligned with the centerline of the trunk.
                    branch.path.emplace_back(parent_branch->path.back());
                branch.path.emplace_back(
                    to_tree_element(config, slicing_params, start_element, parent_branch == nullptr));
                // Traverse each branch until it branches again.
                SupportElement &first_parent = layer_above[start_element.parents[parent_idx]];
                assert(!first_parent.state.marked);
                assert(branch.path.back().layer_idx + 1 == first_parent.state.layer_idx);
                branch.path.emplace_back(to_tree_element(config, slicing_params, first_parent, false));
                if (first_parent.parents.size() < 2)
                    first_parent.state.marked = true;
                SupportElement *next_branch = nullptr;
                if (first_parent.parents.size() == 1)
                {
                    for (SupportElement *parent = &first_parent;;)
                    {
                        assert(parent->state.marked);
                        SupportElement &next_parent = move_bounds[parent->state.layer_idx + 1][parent->parents.front()];
                        assert(!next_parent.state.marked);
                        assert(branch.path.back().layer_idx + 1 == next_parent.state.layer_idx);
                        branch.path.emplace_back(to_tree_element(config, slicing_params, next_parent, false));
                        if (next_parent.parents.size() > 1)
                        {
                            // Branching point was reached.
                            next_branch = &next_parent;
                            break;
                        }
                        next_parent.state.marked = true;
                        if (next_parent.parents.size() == 0)
                            // Tip is reached.
                            break;
                        parent = &next_parent;
                    }
                }
                else if (first_parent.parents.size() > 1)
                    // Branching point was reached.
                    next_branch = &first_parent;
                assert(branch.path.size() >= 2);
                assert(next_branch == nullptr || !next_branch->state.marked);
                out.branches.emplace_back(std::move(branch));
                Branch *pbranch = &out.branches.back();
                if (parent_branch)
                {
                    parent_branch->up.push_back({pbranch});
                    pbranch->down = {parent_branch};
                }
                if (next_branch)
                    this->visit_recursive(move_bounds, *next_branch, pbranch, out);
            }

            if (parent_branch)
            {
                // Update initial radii of thin branches merging with a trunk.
                auto it_up_max_r = std::max_element(parent_branch->up.begin(), parent_branch->up.end(),
                                                    [](const Bifurcation &l, const Bifurcation &r)
                                                    { return l.branch->path[1].radius < r.branch->path[1].radius; });
                const float r1 = it_up_max_r->branch->path[1].radius;
                const float radius_increment = unscaled<float>(config.branch_radius_increase_per_layer);
                for (auto it = parent_branch->up.begin(); it != parent_branch->up.end(); ++it)
                    if (it != it_up_max_r)
                    {
                        Element &el = it->branch->path.front();
                        Element &el2 = it->branch->path[1];
                        if (!is_approx(r1, el2.radius))
                            el.radius = std::min(el.radius, el2.radius + radius_increment);
                    }
                // Sort children of parent_branch by decreasing radius.
                std::sort(parent_branch->up.begin(), parent_branch->up.end(),
                          [](const Bifurcation &l, const Bifurcation &r)
                          { return l.branch->path.front().radius > r.branch->path.front().radius; });
                // Update number of branches to be considered a continuation of the trunk during smoothing.
                {
                    const float r_trunk = 0.75 * it_up_max_r->branch->path.front().radius;
                    parent_branch->num_up_trunk = 0;
                    for (const Bifurcation &up : parent_branch->up)
                        if (up.branch->path.front().radius < r_trunk)
                            break;
                        else
                            ++parent_branch->num_up_trunk;
                }
            }
        }

        const TreeSupportSettings &config;
        const SlicingParameters &slicing_params;
    };

    TreeVisitor visitor{config, slicing_params};

    for (SupportElements &elements : move_bounds)
        for (SupportElement &el : elements)
            el.state.marked = false;

    Trees trees;
    for (LayerIndex layer_idx = 0; layer_idx + 1 < LayerIndex(move_bounds.size()); ++layer_idx)
    {
        for (SupportElement &start_element : move_bounds[layer_idx])
        {
            if (!start_element.state.marked && !start_element.parents.empty())
            {
#if 0
                {
                    // Verify that this node is a root, such that there is no element in the layer below
                    // that points to it.
                    int ielement = &start_element - move_bounds.data();
                    int found = 0;
                    if (layer_idx > 0) {
                        for (auto &el : move_bounds[layer_idx - 1]) {
                            for (auto iparent : el.parents)
                                if (iparent == ielement)
                                    ++ found;
                        }
                        if (found != 0)
                            printf("Found: %d\n", found);
                    }
                }
#endif
                trees.push_back({});
                visitor.visit_recursive(move_bounds, start_element, nullptr, trees.back());
                assert(!trees.back().branches.empty());
                assert(!trees.back().branches.front().path.empty());
#if 0
                // Debugging: Only build trees with specific properties.
                if (start_element.state.lost) {
                }
                else if (start_element.state.verylost) {
                }
                else
                    trees.pop_back();
#endif
            }
        }
    }

#if 1
    move_bounds.clear();
#else
    for (SupportElements &elements : move_bounds)
        for (SupportElement &el : elements)
            el.state.marked = false;
#endif

    return trees;
}

// Move bounds were propagated top to bottom. At each joint of branches the move bounds were reduced significantly.
// Now reflect the reduction of tree space by propagating the reduction of tree centerline space
// bottom-up starting with the bottom-most joint.
void trim_influence_areas_bottom_up(Forest &forest, const float dxy_dlayer)
{
    struct Trimmer
    {
        static void trim_recursive(Branch &branch, const float delta_r, const float dxy_dlayer)
        {
            assert(delta_r >= 0);
            if (delta_r > 0)
                branch.path.front().influence_area = offset(branch.path.front().influence_area, delta_r);
            for (size_t i = 1; i < branch.path.size(); ++i)
                branch.path[i].influence_area = intersection(branch.path[i].influence_area,
                                                             offset(branch.path[i - 1].influence_area, dxy_dlayer));
            const float r0 = branch.path.back().radius;
            for (Bifurcation &up : branch.up)
            {
                up.branch->path.front().influence_area = branch.path.back().influence_area;
                trim_recursive(*up.branch, r0 - up.branch->path.front().radius, dxy_dlayer);
            }
        }
    };

    for (Tree &tree : forest)
    {
        Branch &root = tree.root();
        const float r0 = root.path.back().radius;
        for (Bifurcation &up : root.up)
            Trimmer::trim_recursive(*up.branch, r0 - up.branch->path.front().radius, dxy_dlayer);
    }
}

// Straighten up and smooth centerlines inside their influence areas.
void smooth_trees_inside_influence_areas(Branch &root, bool is_root)
{
    // Smooth the subtree:
    //
    // Apply laplacian and bilaplacian smoothing inside a branch,
    // apply laplacian smoothing only at a bifurcation point.
    //
    // Applying a bilaplacian smoothing inside a branch should ensure curvature of the brach to be lower
    // than the radius at each particular point of the centerline,
    // while omitting bilaplacian smoothing at bifurcation points will create sharp bifurcations.
    // Sharp bifurcations have a smaller volume, but just a tiny bit larger surfaces than smooth bifurcations
    // where each continuation of the trunk satifies the path radius > centerline element radius.
    const size_t num_iterations = 100;
    struct StackElement
    {
        Branch &branch;
        size_t idx_up;
    };
    std::vector<StackElement> stack;

    auto adjust_position = [](Element &el, Vec2f new_pos)
    {
        Point new_pos_scaled = scaled<coord_t>(new_pos);
        if (!contains(el.influence_area, new_pos_scaled))
        {
            int64_t min_dist = std::numeric_limits<int64_t>::max();
            Point min_proj_scaled;
            for (const Polygon &polygon : el.influence_area)
            {
                Point proj_scaled = polygon.point_projection(new_pos_scaled);
                if (int64_t dist = (proj_scaled - new_pos_scaled).cast<int64_t>().squaredNorm(); dist < min_dist)
                {
                    min_dist = dist;
                    min_proj_scaled = proj_scaled;
                }
            }
            new_pos = unscaled<float>(min_proj_scaled);
        }
        el.position.head<2>() = new_pos;
    };

    for (size_t iter = 0; iter < num_iterations; ++iter)
    {
        // 1) Back-up the current positions.
        stack.push_back({root, 0});
        while (!stack.empty())
        {
            StackElement &state = stack.back();
            if (state.idx_up == state.branch.num_up_trunk)
            {
                // Process this path.
                for (auto &el : state.branch.path)
                    el.prev_position = el.position;
                stack.pop_back();
            }
            else
            {
                // Open another up node of this branch.
                stack.push_back({*state.branch.up[state.idx_up].branch, 0});
                ++state.idx_up;
            }
        }
        // 2) Calculate new position.
        stack.push_back({root, 0});
        while (!stack.empty())
        {
            StackElement &state = stack.back();
            if (state.idx_up == state.branch.num_up_trunk)
            {
                // Process this path.
                for (size_t i = 1; i + 1 < state.branch.path.size(); ++i)
                    if (auto &el = state.branch.path[i]; !el.locked)
                    {
                        // Laplacian smoothing with 0.5 weight.
                        const Vec3f &p0 = state.branch.path[i - 1].prev_position;
                        const Vec3f &p1 = el.prev_position;
                        const Vec3f &p2 = state.branch.path[i + 1].prev_position;
                        adjust_position(el, 0.5 * p1.head<2>() + 0.25 * (p0.head<2>() + p2.head<2>()));
#if 0
                        // Only apply bilaplacian smoothing if the current curvature is smaller than el.radius.
                        // Interpolate p0, p1, p2 with a circle.
                        // First project p0, p1, p2 into a common plane.
                        const Vec3f n = (p1 - p0).cross(p2 - p1);
                        const Vec3f y = Vec3f(n.y(), n.x(), 0).normalized();
                        const Vec2f q0{ p0.z(), p0.dot(y) };
                        const Vec2f q1{ p1.z(), p1.dot(y) };
                        const Vec2f q2{ p2.z(), p2.dot(y) };
                        // Interpolate q0, q1, q2 with a circle, calculate its radius.
                        Vec2f b = q1 - q0;
                        Vec2f c = q2 - q0;
                        float lb = b.squaredNorm();
                        float lc = c.squaredNorm();
                        if (float d = b.x() * c.y() - b.y() * c.x(); std::abs(d) > EPSILON) {
                            Vec2f v = lc * b - lb * c;
                            float r2 = 0.25f * v.squaredNorm() / sqr(d);
                            if (r2 )
                        }
#endif
                    }
                {
                    // Laplacian smoothing with 0.5 weight, branching point.
                    float weight = 0;
                    Vec2f new_pos = Vec2f::Zero();
                    for (size_t i = 0; i < state.branch.num_up_trunk; ++i)
                    {
                        const Element &el = state.branch.up[i].branch->path.front();
                        new_pos += el.prev_position.head<2>();
                        weight += el.radius;
                    }
                    {
                        const Element &el = state.branch.path[state.branch.path.size() - 2];
                        new_pos += el.prev_position.head<2>();
                        weight *= 2.f;
                    }
                    adjust_position(state.branch.path.back(),
                                    0.5f * state.branch.path.back().prev_position.head<2>() + 0.5f * weight * new_pos);
                }
                stack.pop_back();
            }
            else
            {
                // Open another up node of this branch.
                stack.push_back({*state.branch.up[state.idx_up].branch, 0});
                ++state.idx_up;
            }
        }
    }
    // Also smoothen start of the path.
    if (Element &first = root.path.front(); !first.locked)
    {
        Element &second = root.path[1];
        Vec2f new_pos = 0.75f * first.prev_position.head<2>() + 0.25f * second.prev_position.head<2>();
        if (is_root)
            // Let the root of the tree float inside its influence area.
            adjust_position(first, new_pos);
        else
        {
            // Keep the start of a thin branch inside the trunk.
            const Element &trunk = root.down.branch->path.back();
            const float rdif = trunk.radius - root.path.front().radius;
            assert(rdif >= 0);
            Vec2f vdif = new_pos - trunk.prev_position.head<2>();
            float ldif = vdif.squaredNorm();
            if (ldif > sqr(rdif))
                // Clamp new position.
                new_pos = trunk.prev_position.head<2>() + vdif * rdif / sqrt(ldif);
            first.position.head<2>() = new_pos;
        }
    }
}

void smooth_trees_inside_influence_areas(Forest &forest)
{
    // Parallel for!
    for (Tree &tree : forest)
        smooth_trees_inside_influence_areas(tree.root(), true);
}

#if 0
// Test whether two circles, each on its own plane in 3D intersect.
// Circles are considered intersecting, if the lowest point on one circle is below the other circle's plane.
// Assumption: The two planes are oriented the same way.
static bool circles_intersect(
    const Vec3d &p1, const Vec3d &n1, const double r1, 
    const Vec3d &p2, const Vec3d &n2, const double r2)
{
    assert(n1.dot(n2) >= 0);

    const Vec3d z = n1.cross(n2);
    const Vec3d dir1 = z.cross(n1);
    const Vec3d lowest_point1 = p1 + dir1 * (r1 / dir1.norm());
    assert(n2.dot(p1) >= n2.dot(lowest_point1));
    if (n2.dot(lowest_point1) <= 0)
        return true;
    const Vec3d dir2 = z.cross(n2);
    const Vec3d lowest_point2 = p2 + dir2 * (r2 / dir2.norm());
    assert(n1.dot(p2) >= n1.dot(lowest_point2));
    return n1.dot(lowest_point2) <= 0;
}
#endif

template<bool flip_normals>
void triangulate_fan(indexed_triangle_set &its, int ifan, int ibegin, int iend)
{
    // at least 3 vertices, increasing order.
    assert(ibegin + 3 <= iend);
    assert(ibegin >= 0 && iend <= its.vertices.size());
    assert(ifan >= 0 && ifan < its.vertices.size());
    int num_faces = iend - ibegin;
    its.indices.reserve(its.indices.size() + num_faces * 3);
    for (int v = ibegin, u = iend - 1; v < iend; u = v++)
    {
        if (flip_normals)
            its.indices.push_back({ifan, u, v});
        else
            its.indices.push_back({ifan, v, u});
    }
}

static void triangulate_strip(indexed_triangle_set &its, int ibegin1, int iend1, int ibegin2, int iend2)
{
    // at least 3 vertices, increasing order.
    assert(ibegin1 + 3 <= iend1);
    assert(ibegin1 >= 0 && iend1 <= its.vertices.size());
    assert(ibegin2 + 3 <= iend2);
    assert(ibegin2 >= 0 && iend2 <= its.vertices.size());
    int n1 = iend1 - ibegin1;
    int n2 = iend2 - ibegin2;
    its.indices.reserve(its.indices.size() + (n1 + n2) * 3);

    // For the first vertex of 1st strip, find the closest vertex on the 2nd strip.
    int istart2 = ibegin2;
    {
        const Vec3f &p1 = its.vertices[ibegin1];
        auto d2min = std::numeric_limits<float>::max();
        for (int i = ibegin2; i < iend2; ++i)
        {
            const Vec3f &p2 = its.vertices[i];
            const float d2 = (p2 - p1).squaredNorm();
            if (d2 < d2min)
            {
                d2min = d2;
                istart2 = i;
            }
        }
    }

    // Now triangulate the strip zig-zag fashion taking always the shortest connection if possible.
    for (int u = ibegin1, v = istart2; n1 > 0 || n2 > 0;)
    {
        bool take_first;
        int u2, v2;
        auto update_u2 = [&u2, u, ibegin1, iend1]()
        {
            u2 = u;
            if (++u2 == iend1)
                u2 = ibegin1;
        };
        auto update_v2 = [&v2, v, ibegin2, iend2]()
        {
            v2 = v;
            if (++v2 == iend2)
                v2 = ibegin2;
        };
        if (n1 == 0)
        {
            take_first = false;
            update_v2();
        }
        else if (n2 == 0)
        {
            take_first = true;
            update_u2();
        }
        else
        {
            update_u2();
            update_v2();
            float l1 = (its.vertices[u2] - its.vertices[v]).squaredNorm();
            float l2 = (its.vertices[v2] - its.vertices[u]).squaredNorm();
            take_first = l1 < l2;
        }
        if (take_first)
        {
            its.indices.push_back({u, u2, v});
            --n1;
            u = u2;
        }
        else
        {
            its.indices.push_back({u, v2, v});
            --n2;
            v = v2;
        }
    }
}

// Discretize 3D circle, append to output vector, return ranges of indices of the points added.
static std::pair<int, int> discretize_circle(const Vec3f &center, const Vec3f &normal, const float radius,
                                             const float eps, std::vector<Vec3f> &pts)
{
    // Calculate discretization step and number of steps.
    float angle_step = 2. * acos(1. - eps / radius);
    auto nsteps = int(ceil(2 * M_PI / angle_step));
    angle_step = 2 * M_PI / nsteps;

    // Prepare coordinate system for the circle plane.
    Vec3f x = normal.cross(Vec3f(0.f, -1.f, 0.f)).normalized();
    Vec3f y = normal.cross(x).normalized();
    assert(std::abs(x.cross(y).dot(normal) - 1.f) < EPSILON);

    // Discretize the circle.
    int begin = int(pts.size());
    pts.reserve(pts.size() + nsteps);
    float angle = 0;
    x *= radius;
    y *= radius;
    for (int i = 0; i < nsteps; ++i)
    {
        pts.emplace_back(center + x * cos(angle) + y * sin(angle));
        angle += angle_step;
    }
    return {begin, int(pts.size())};
}

// Returns Z span of the generated mesh.
static std::pair<float, float> extrude_branch(const std::vector<const SupportElement *> &path,
                                              const TreeSupportSettings &config,
                                              const SlicingParameters &slicing_params,
                                              const std::vector<SupportElements> &move_bounds,
                                              indexed_triangle_set &result)
{
    Vec3d p1, p2, p3;
    Vec3d v1, v2;
    Vec3d nprev;
    Vec3d ncurrent;
    assert(path.size() >= 2);
    static constexpr const float eps = 0.015f;
    std::pair<int, int> prev_strip;

    //    char fname[2048];
    //    static int irun = 0;

    float zmin = 0;
    float zmax = 0;

    for (size_t ipath = 1; ipath < path.size(); ++ipath)
    {
        const SupportElement &prev = *path[ipath - 1];
        const SupportElement &current = *path[ipath];
        assert(prev.state.layer_idx + 1 == current.state.layer_idx);
        p1 = to_3d(unscaled<double>(prev.state.result_on_layer), layer_z(slicing_params, config, prev.state.layer_idx));
        p2 = to_3d(unscaled<double>(current.state.result_on_layer),
                   layer_z(slicing_params, config, current.state.layer_idx));
        v1 = (p2 - p1).normalized();
        if (ipath == 1)
        {
            nprev = v1;
            // Extrude the bottom half sphere.
            float radius = unscaled<float>(support_element_radius(config, prev));
            float angle_step = 2. * acos(1. - eps / radius);
            auto nsteps = int(ceil(M_PI / (2. * angle_step)));
            angle_step = M_PI / (2. * nsteps);
            int ifan = int(result.vertices.size());
            result.vertices.emplace_back((p1 - nprev * radius).cast<float>());
            zmin = result.vertices.back().z();
            float angle = angle_step;
            for (int i = 1; i < nsteps; ++i, angle += angle_step)
            {
                std::pair<int, int> strip = discretize_circle((p1 - nprev * radius * cos(angle)).cast<float>(),
                                                              nprev.cast<float>(), radius * sin(angle), eps,
                                                              result.vertices);
                if (i == 1)
                    triangulate_fan<false>(result, ifan, strip.first, strip.second);
                else
                    triangulate_strip(result, prev_strip.first, prev_strip.second, strip.first, strip.second);
                //                sprintf(fname, "d:\\temp\\meshes\\tree-partial-%d.obj", ++ irun);
                //                its_write_obj(result, fname);
                prev_strip = strip;
            }
        }
        if (ipath + 1 == path.size())
        {
            // End of the tube.
            ncurrent = v1;
            // Extrude the top half sphere.
            float radius = unscaled<float>(support_element_radius(config, current));
            float angle_step = 2. * acos(1. - eps / radius);
            auto nsteps = int(ceil(M_PI / (2. * angle_step)));
            angle_step = M_PI / (2. * nsteps);
            auto angle = float(M_PI / 2.);
            for (int i = 0; i < nsteps; ++i, angle -= angle_step)
            {
                std::pair<int, int> strip = discretize_circle((p2 + ncurrent * radius * cos(angle)).cast<float>(),
                                                              ncurrent.cast<float>(), radius * sin(angle), eps,
                                                              result.vertices);
                triangulate_strip(result, prev_strip.first, prev_strip.second, strip.first, strip.second);
                //                sprintf(fname, "d:\\temp\\meshes\\tree-partial-%d.obj", ++ irun);
                //                its_write_obj(result, fname);
                prev_strip = strip;
            }
            int ifan = int(result.vertices.size());
            result.vertices.emplace_back((p2 + ncurrent * radius).cast<float>());
            zmax = result.vertices.back().z();
            triangulate_fan<true>(result, ifan, prev_strip.first, prev_strip.second);
            //            sprintf(fname, "d:\\temp\\meshes\\tree-partial-%d.obj", ++ irun);
            //            its_write_obj(result, fname);
        }
        else
        {
            const SupportElement &next = *path[ipath + 1];
            assert(current.state.layer_idx + 1 == next.state.layer_idx);
            p3 = to_3d(unscaled<double>(next.state.result_on_layer),
                       layer_z(slicing_params, config, next.state.layer_idx));
            v2 = (p3 - p2).normalized();
            ncurrent = (v1 + v2).normalized();
            float radius = unscaled<float>(support_element_radius(config, current));
            std::pair<int, int> strip = discretize_circle(p2.cast<float>(), ncurrent.cast<float>(), radius, eps,
                                                          result.vertices);
            triangulate_strip(result, prev_strip.first, prev_strip.second, strip.first, strip.second);
            prev_strip = strip;
            //            sprintf(fname, "d:\\temp\\meshes\\tree-partial-%d.obj", ++irun);
            //            its_write_obj(result, fname);
        }
#if 0
        if (circles_intersect(p1, nprev, support_element_radius(settings, prev), p2, ncurrent, support_element_radius(settings, current))) {
            // Cannot connect previous and current slice using a simple zig-zag triangulation,
            // because the two circles intersect.

        } else {
            // Continue with chaining.

        }
#endif
    }

    return std::make_pair(zmin, zmax);
}

// One layer of a trunk's canopy, extruded as a prism spanning that layer's own z range so slicing it
// at mid-layer returns exactly this outline. A prism, not a lofted ring: the outline is a disk
// clipped to the interface, so its topology changes freely with depth - it may gain the interface's
// holes, or split around a notch - and a prism does not care, where a strip loft would.
static void baobab_emit_prism(const ExPolygon &ring, const double z_bottom, const double z_top,
                              indexed_triangle_set &out)
{
    auto emit_cap = [&out](const ExPolygon &poly, const double z, const bool flip)
    {
        // Tessellated, not fanned: the outline is arbitrary, and may enclose holes.
        const std::vector<Vec3d> soup = triangulate_expolygon_3d(poly, z, flip);
        for (size_t i = 0; i + 2 < soup.size(); i += 3)
        {
            const int base = int(out.vertices.size());
            out.vertices.emplace_back(soup[i].cast<float>());
            out.vertices.emplace_back(soup[i + 1].cast<float>());
            out.vertices.emplace_back(soup[i + 2].cast<float>());
            out.indices.emplace_back(base, base + 1, base + 2);
        }
    };
    emit_cap(ring, z_bottom, true);
    emit_cap(ring, z_top, false);

    // Side walls. A contour runs CCW and a hole CW, so one winding rule faces both outward.
    for (size_t loop_idx = 0; loop_idx < ring.num_contours(); ++loop_idx)
    {
        const Polygon &loop = ring.contour_or_hole(loop_idx);
        const size_t n = loop.points.size();
        if (n < 3)
            continue;
        const int base = int(out.vertices.size());
        for (const Point &p : loop.points)
        {
            out.vertices.emplace_back(float(unscaled<double>(p.x())), float(unscaled<double>(p.y())), float(z_bottom));
            out.vertices.emplace_back(float(unscaled<double>(p.x())), float(unscaled<double>(p.y())), float(z_top));
        }
        for (size_t i = 0; i < n; ++i)
        {
            const int a0 = base + int(2 * i), a1 = a0 + 1;
            const int b0 = base + int(2 * ((i + 1) % n)), b1 = b0 + 1;
            out.indices.emplace_back(a0, b0, b1);
            out.indices.emplace_back(a0, b1, a1);
        }
    }
}

// Geometry debug (--debug baobab): emit ExPolygons as WKT in mm, one line per call, so a canopy's
// interface share and its rings can be loaded into shapely and the coverage measured instead of
// eyeballed. Lines from parallel canopy builds are unordered across tips; each line carries its
// tip identity, so sort on those fields before diffing runs.
static void baobab_dbg_wkt(double z, const char *kind, LayerIndex tip_layer, const Point &tip_xy, const ExPolygons &eps)
{
    if (!debug_enabled(DBG_BAOBAB) || eps.empty())
        return;
    dbg_log(DBG_BAOBAB, z, "BAOBAB-WKT", "kind=%s tip_layer=%d tip=(%.2f,%.2f) %s", kind, int(tip_layer),
            unscaled<double>(tip_xy.x()), unscaled<double>(tip_xy.y()), baobab_wkt(eps).c_str());
}

// Nearest point of any polygon boundary to `from`, for anchoring a bridge corridor.
static Point baobab_nearest_point(const Polygons &polys, const Point &from)
{
    Point best = from;
    double best_d2 = std::numeric_limits<double>::max();
    for (const Polygon &poly : polys)
        for (size_t i = 0; i < poly.points.size(); ++i)
        {
            const Point &a = poly.points[i];
            const Point &b = poly.points[(i + 1) % poly.points.size()];
            const Vec2d ap = (from - a).cast<double>();
            const Vec2d ab = (b - a).cast<double>();
            const double t = ab.squaredNorm() > 0. ? std::clamp(ap.dot(ab) / ab.squaredNorm(), 0., 1.) : 0.;
            const Point p = a + Point(coord_t(std::round(ab.x() * t)), coord_t(std::round(ab.y() * t)));
            const double d2 = (p - from).cast<double>().squaredNorm();
            if (d2 < best_d2)
            {
                best_d2 = d2;
                best = p;
            }
        }
    return best;
}

// The canopy: the trunk's own circle, widened by  at the interface and narrowing back onto the
// trunk as it descends, clipped at every layer to the interface footprint.
//
//     ring(d) = disk(stem_centre(d), trunk_radius(d) + reach(d))  ∩  interface
//
// Size a canopy's initial reach to its seed's actual coverage duty: the farthest region point whose
// nearest seed is this one, plus half a bead of margin - instead of one full gather spacing for
// every seed. An oversized envelope pins the interface outline for its whole decay (a straight
// prism under the interface) and prints pure excess. The nearest-seed assignment only SIZES the
// envelope; the ring clip stays disk-with-interface, an overlapping union, so coverage stays a
// union property and no partition seam can open between neighbouring canopies. Peers are limited to
// the band window in z, so a mouth stacked above another does not steal its samples.
static double baobab_sized_reach_mm(const Polygons &region, const Point &tip_pos, const LayerIndex tip_layer,
                                    const std::vector<std::pair<Point, LayerIndex>> &all_tips,
                                    const LayerIndex layer_window)
{
    std::vector<Vec2d> peers;
    peers.reserve(all_tips.size());
    for (const auto &[pos, layer] : all_tips)
        if (std::abs(long(layer) - long(tip_layer)) <= long(layer_window))
            peers.emplace_back(unscaled<double>(pos));
    const Vec2d self = unscaled<double>(tip_pos);
    double need2 = 0.;
    auto consider = [&](const Vec2d &sample)
    {
        const double d2_self = (sample - self).squaredNorm();
        if (d2_self <= need2)
            return;
        for (const Vec2d &peer : peers)
            if ((sample - peer).squaredNorm() < d2_self - 1e-9)
                return;
        need2 = d2_self;
    };
    const double boundary_step = scaled<double>(1.);
    for (const Polygon &contour : region)
    {
        double carry = 0.;
        for (size_t i = 0; i < contour.points.size(); ++i)
        {
            const Vec2d a = contour.points[i].cast<double>();
            const Vec2d b = contour.points[(i + 1) % contour.points.size()].cast<double>();
            const double len = (b - a).norm();
            double t = carry;
            for (; t < len; t += boundary_step)
                consider(unscaled<double>(Point((a + (b - a) * (t / len)).cast<coord_t>())));
            carry = t - len;
        }
    }
    const BoundingBox bbox = get_extents(region);
    const auto grid = scaled<coord_t>(2.);
    for (coord_t x = bbox.min.x(); x <= bbox.max.x(); x += grid)
        for (coord_t y = bbox.min.y(); y <= bbox.max.y(); y += grid)
            if (const Point p(x, y); contains(region, p))
                consider(unscaled<double>(p));
    // Uncapped: after the trunk economy prunes redundant trees, a surviving great trunk's
    // duty legitimately exceeds the gather spacing, and the canopy depth cap already scales
    // with the reach. The floor keeps a crowded-out seed printable.
    return std::max(std::sqrt(need2), 0.5) + 0.5;
}

// The redundancy gate of the trunk economy: what would this tip's canopy actually deliver at
// this reach? Scalar capacity arithmetic cannot answer it - the envelope rides the drifting
// trunk, so the ramp toward a far target must fit inside a MOVING cone, and only the builder's
// own construction knows whether it does. This runs that construction: the identical pre-pass
// envelope walk, then the bottom-up chain of dilate-then-clip against the per-layer envelope
// disk and tube, on the real chain positions and radii. The interface clip and the bridge
// corridor are omitted - the first only removes area outside the interface (samples are on
// it), the second only adds - so the result is the conservative floor of the built canopy.
// Returns the rim ring: what the canopy holds at the tip layer.
static ExPolygons baobab_sim_rim(const std::vector<const SupportElement *> &chain, const Polygons &region,
                                 const TreeSupportSettings &config, const double g_nominal, const double growth_budget,
                                 const double reach_mm)
{
    if (chain.size() < 2 || g_nominal <= 0.)
        return {};
    const size_t tip_idx = chain.size() - 1;
    std::vector<double> reach_at;
    double probe = unscaled<double>(support_element_radius(config, *chain[tip_idx])) + reach_mm;
    const size_t max_layers = std::min(tip_idx, size_t(3. * reach_mm / g_nominal) + 8);
    for (size_t depth = 0; depth <= max_layers; ++depth)
    {
        const SupportElement &element = *chain[tip_idx - depth];
        reach_at.emplace_back(probe);
        if (probe <= unscaled<double>(support_element_radius(config, element)))
            break;
        if (depth == max_layers || element.state.layer_idx < 1)
            break;
        const Vec2d centre = unscaled<double>(element.state.result_on_layer);
        const Vec2d next = unscaled<double>(chain[tip_idx - depth - 1]->state.result_on_layer);
        probe -= std::max(0., std::min(g_nominal, growth_budget - (next - centre).norm()));
    }
    ExPolygons rim;
    for (size_t d = reach_at.size() - 1;; --d)
    {
        const SupportElement &element = *chain[tip_idx - d];
        const double r_trunk = unscaled<double>(support_element_radius(config, element));
        const Polygon trunk_disk = baobab_disk(element.state.result_on_layer, scaled<coord_t>(r_trunk));
        if (d == reach_at.size() - 1)
            rim = union_ex(Polygons{trunk_disk});
        else
        {
            // The builder's clip: (envelope disk ∩ region) ∪ tube, plus the straight bridge
            // corridor when the trunk stands outside its share, so a leaning arrival keeps a
            // foundation path exactly as the real construction gives it one.
            Polygons region_part = intersection(Polygons{baobab_disk(element.state.result_on_layer,
                                                                     scaled<coord_t>(reach_at[d]))},
                                                region);
            Polygons clip = region_part;
            if (!region_part.empty() && !contains(region_part, element.state.result_on_layer))
            {
                const Point anchor = baobab_nearest_point(region_part, element.state.result_on_layer);
                Polyline bridge;
                bridge.points = {element.state.result_on_layer, anchor};
                polygons_append(clip, offset(bridge, scaled<float>(r_trunk)));
            }
            clip.emplace_back(trunk_disk);
            rim = intersection_ex(offset_ex(rim, scaled<float>(growth_budget)), union_(clip));
            if (rim.empty())
                rim = union_ex(Polygons{trunk_disk});
        }
        if (d == 0)
            break;
    }
    return rim;
}

// This IS the doc's insight, written down: a mouth that splits downward into N trunks is N trunks
// whose tops widen and FUSE. Dilation distributes over union, so the union of every trunk's rings is
// exactly dilate(all trunks) intersected with the interface - one continuous mouth at the top,
// separating into trunks with depth, wherever the trunks themselves separate. Nothing here decides
// where a mouth splits.
//
// Three properties fall out, none of which the earlier per-trunk-share construction had:
//   - Coverage is exact at EVERY layer, not just the rim. Voronoi shares tile the interface only at
//     the rim; one layer down each has shrunk toward its own seed and gaps open between them.
//   - The canopy inherits the interface's holes and no others. If the interface has none, nor does
//     the canopy, because the ring is an intersection with it.
//   - The ring shrinks onto its own trunk, so it can never recede from it and detach.
//
// The reach falls by the printable allowance each layer while the trunk thickens, so the two meet in
// reach / (allowance + trunk growth) layers - convergence is guaranteed, even when a hard lean leaves
// no allowance at all.
static size_t baobab_extrude_canopy(const std::vector<std::pair<LayerIndex, Polygons>> &bands,
                                    const std::vector<const SupportElement *> &path, const TreeSupportSettings &config,
                                    const SlicingParameters &slicing_params, const double taper_rad,
                                    const double growth_budget, const double reach_mm, const LayerIndex tip_extension,
                                    LayerIndex &canopy_bottom, BaobabCanopyStats &stats, BaobabCanopyOutcome &outcome,
                                    std::vector<std::pair<LayerIndex, Polygons>> &rings_out, indexed_triangle_set &out)
{
    const size_t tip_idx = path.size() - 1;
    const double g_nominal = unscaled<double>(config.layer_height) * std::tan(taper_rad);
    outcome.reach0 = reach_mm;
    if (g_nominal <= 0. || bands.empty())
    {
        outcome.reason = "no_taper";
        return 0;
    }
    // suffix_clip[k] holds the bands from k upward; a ring at layer L clips to the first k
    // whose band lies strictly above L, so the mouth never rises into an interface band.
    std::vector<Polygons> suffix_clip(bands.size() + 1);
    for (size_t k = bands.size(); k-- > 0;)
    {
        suffix_clip[k] = suffix_clip[k + 1];
        polygons_append(suffix_clip[k], bands[k].second);
        suffix_clip[k] = union_(suffix_clip[k]);
    }

    // Walk the trunk and record the reach envelope at every layer. The envelope decays by the
    // taper allowance NET OF LEAN: the chain below grows at most one bead budget per layer and
    // the lean spends part of it, so this is the fastest the mouth can converge without leaving
    // its rim share uncovered (a plain taper-rate decay shrinks the envelope faster than a
    // leaning chain can grow, and the rim loses coverage). Give up rather than trail a canopy
    // down the whole trunk when a hard lean leaves no allowance.
    const size_t max_layers = std::min(tip_idx, size_t(3. * reach_mm / g_nominal) + 8);
    std::vector<double> reach_at;
    reach_at.reserve(max_layers + 1);
    {
        double probe = unscaled<double>(support_element_radius(config, *path[tip_idx])) + reach_mm;
        for (size_t depth = 0; depth <= max_layers; ++depth)
        {
            const SupportElement &element = *path[tip_idx - depth];
            reach_at.emplace_back(probe);
            if (probe <= unscaled<double>(support_element_radius(config, element)))
                break;
            if (depth == max_layers || element.state.layer_idx < 1)
            {
                // The envelope did not close on the trunk within the sane depth (a hard lean
                // leaves it no allowance) or the chain ran out. Build the canopy anyway, to the
                // depth that exists: the bottom-up chain is founded on the tube at any depth,
                // so a hard-leaning trunk gets a partial mouth instead of a bare hemisphere tip.
                outcome.reason = element.state.layer_idx < 1 ? "capped_hit_plate" : "capped_never_closes";
                break;
            }
            const Vec2d centre = unscaled<double>(element.state.result_on_layer);
            const Vec2d next = unscaled<double>(path[tip_idx - depth - 1]->state.result_on_layer);
            outcome.travel += (next - centre).norm();
            probe -= std::max(0., std::min(g_nominal, growth_budget - (next - centre).norm()));
        }
    }
    outcome.net = (unscaled<double>(path[tip_idx]->state.result_on_layer) -
                   unscaled<double>(path[tip_idx - (reach_at.size() - 1)]->state.result_on_layer))
                      .norm();

    baobab_dbg_wkt(layer_z(slicing_params, config, size_t(path[tip_idx]->state.layer_idx)), "region",
                   path[tip_idx]->state.layer_idx, path[tip_idx]->state.result_on_layer, union_ex(suffix_clip[0]));
    size_t depth_converged = reach_at.size() - 1;
    while (depth_converged > 0 && path[tip_idx - depth_converged]->state.layer_idx < 1)
        --depth_converged;
    outcome.depth = depth_converged;
    outcome.trunk_radius = unscaled<double>(support_element_radius(config, *path[tip_idx - depth_converged]));

    // The ring sequence is built BOTTOM-UP from the trunk: each layer may grow from the ring
    // below by at most the bead budget, clipped to (disk ∩ interface) ∪ tube. Foundation is
    // constructive - no ring can hold material that does not stand on the ring below - and
    // nothing lands outside the interface footprint but the tube itself. Built top-down
    // (disk ∩ interface at every depth) a leaning trunk's deepest rings detach and float;
    // grown along the trunk's wake instead, the canopy bulges outside the interface. The
    // deepest tube-only layers, before the growing ring first meets the interface, are not
    // emitted: they would only duplicate the tube.
    size_t built = 0;
    ExPolygons chain;
    for (size_t d = depth_converged;; --d)
    {
        const SupportElement &element = *path[tip_idx - d];
        const LayerIndex layer = element.state.layer_idx;
        const double trunk_radius = unscaled<double>(support_element_radius(config, element));
        const Polygon trunk_disk = baobab_disk(element.state.result_on_layer, scaled<coord_t>(trunk_radius));
        // Clip to the bands strictly above this ring's layer: the terrace rule.
        size_t band_k = 0;
        while (band_k < bands.size() && bands[band_k].first <= layer)
            ++band_k;
        Polygons region_part;
        if (const Polygon disk = baobab_disk(element.state.result_on_layer, scaled<coord_t>(reach_at[d]));
            disk.points.size() >= 3 && band_k < bands.size())
            region_part = intersection(suffix_clip[band_k], Polygons{disk});
        if (d == depth_converged)
            chain = union_ex(Polygons{trunk_disk});
        else
        {
            Polygons clip = region_part;
            // Bridge: a straight corridor from the trunk to the nearest point of its interface
            // share, so the mouth of a trunk that approaches at an angle develops over the whole
            // approach instead of arriving at the rim as a knife edge (the chain may otherwise
            // hold nothing between tube and footprint, and only crosses the gap in the final
            // layers). Smooth by construction - it tracks current geometry, not the trunk's
            // jagged history - and the chain dilation still bounds every step, so it cannot
            // float or cliff.
            if (!region_part.empty())
            {
                bool trunk_inside_share = false;
                for (const ExPolygon &piece : union_ex(region_part))
                    if (piece.contains(element.state.result_on_layer))
                    {
                        trunk_inside_share = true;
                        break;
                    }
                if (!trunk_inside_share)
                {
                    const Point anchor = baobab_nearest_point(region_part, element.state.result_on_layer);
                    Polyline bridge;
                    bridge.points = {element.state.result_on_layer, anchor};
                    polygons_append(clip, offset(bridge, scaled<float>(trunk_radius)));
                }
            }
            clip.emplace_back(trunk_disk);
            chain = intersection_ex(offset_ex(chain, scaled<float>(growth_budget)), union_(clip));
            if (chain.empty())
                chain = union_ex(Polygons{trunk_disk});
        }
        if (built > 0 || !region_part.empty())
        {
            // The rim carries straight up to the topmost layer this branch is sliced at, or that
            // layer - the one directly under the interface - holds nothing but the pin-sized cap.
            const double z_top = layer_z(slicing_params, config, size_t(layer + (d == 0 ? tip_extension - 1 : 0)));
            for (const ExPolygon &ring : chain)
            {
                if (!ring.holes.empty())
                {
                    stats.ring_holes.fetch_add(ring.holes.size(), std::memory_order_relaxed);
                    stats.rings_holed.fetch_add(1, std::memory_order_relaxed);
                    baobab_atomic_min(stats.ring_holed_d_min, d);
                    baobab_atomic_max(stats.ring_holed_d_max, d);
                }
                baobab_emit_prism(ring, layer_z(slicing_params, config, size_t(layer - 1)), z_top, out);
            }
            baobab_dbg_wkt(layer_z(slicing_params, config, size_t(layer)), "ring", path[tip_idx]->state.layer_idx,
                           path[tip_idx]->state.result_on_layer, chain);
            // The canopy-vs-trunk record: every layer this ring occupies, including the carried
            // rim layers, so the fill knows where a canopy exists.
            rings_out.emplace_back(layer, to_polygons(chain));
            if (d == 0)
                for (LayerIndex e = 1; e < tip_extension; ++e)
                    rings_out.emplace_back(layer + e, to_polygons(chain));
            if (built == 0)
                canopy_bottom = layer;
            ++built;
        }
        if (d == 0)
            break;
    }
    outcome.built = built;
    outcome.reach_left = reach_at[depth_converged];
    outcome.truncated = reach_at[depth_converged] >
                        unscaled<double>(support_element_radius(config, *path[tip_idx - depth_converged])) + EPSILON;
    return built;
}

#ifdef TREE_SUPPORT_ORGANIC_NUDGE_NEW

// New version using per layer AABB trees of lines for nudging spheres away from an object.
static void organic_smooth_branches_avoid_collisions(
    const PrintObject &print_object, const TreeModelVolumes &volumes, const TreeSupportSettings &config,
    std::vector<SupportElements> &move_bounds,
    const std::vector<std::pair<SupportElement *, int>> &elements_with_link_down,
    const std::vector<size_t> &linear_data_layers, std::function<void()> throw_on_cancel)
{
    struct LayerCollisionCache
    {
        coord_t min_element_radius{std::numeric_limits<coord_t>::max()};
        bool min_element_radius_known() const
        {
            return this->min_element_radius != std::numeric_limits<coord_t>::max();
        }
        coord_t collision_radius{0};
        std::vector<Linef> lines;
        AABBTreeIndirect::Tree<2, double> aabbtree_lines;
        bool empty() const { return this->lines.empty(); }
    };
    std::vector<LayerCollisionCache> layer_collision_cache;
    layer_collision_cache.reserve(1024);
    const SlicingParameters &slicing_params = print_object.slicing_parameters();
    for (const std::pair<SupportElement *, int> &element : elements_with_link_down)
    {
        LayerIndex layer_idx = element.first->state.layer_idx;
        if (size_t num_layers = layer_idx + 1; num_layers > layer_collision_cache.size())
        {
            if (num_layers > layer_collision_cache.capacity())
                reserve_power_of_2(layer_collision_cache, num_layers);
            layer_collision_cache.resize(num_layers, {});
        }
        auto &l = layer_collision_cache[layer_idx];
        l.min_element_radius = std::min(l.min_element_radius, support_element_radius(config, *element.first));
    }

    throw_on_cancel();

    for (LayerIndex layer_idx = 0; layer_idx < LayerIndex(layer_collision_cache.size()); ++layer_idx)
        if (LayerCollisionCache &l = layer_collision_cache[layer_idx]; !l.min_element_radius_known())
            l.min_element_radius = 0;
        else
        {
            //FIXME
            l.min_element_radius = 0;
            std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> res =
                volumes.get_collision_lower_bound_area(layer_idx, l.min_element_radius);
            assert(res.has_value());
            l.collision_radius = res->first;
            Lines alines = to_lines(res->second.get());
            l.lines.reserve(alines.size());
            for (const Line &line : alines)
                l.lines.push_back({unscaled<double>(line.a), unscaled<double>(line.b)});
            l.aabbtree_lines = AABBTreeLines::build_aabb_tree_over_indexed_lines(l.lines);
            throw_on_cancel();
        }

    struct CollisionSphere
    {
        const SupportElement &element;
        int element_below_id;
        const bool locked;
        float radius;
        // Current position, when nudged away from the collision.
        Vec3f position;
        // Previous position, for Laplacian smoothing.
        Vec3f prev_position;
        //
        Vec3f last_collision;
        double last_collision_depth;
        // Minimum Z for which the sphere collision will be evaluated.
        // Limited by the minimum sloping angle and by the bottom of the tree.
        float min_z{-std::numeric_limits<float>::max()};
        // Maximum Z for which the sphere collision will be evaluated.
        // Limited by the minimum sloping angle and by the tip of the current branch.
        float max_z{std::numeric_limits<float>::max()};
        uint32_t layer_begin;
        uint32_t layer_end;
    };

    std::vector<CollisionSphere> collision_spheres;
    collision_spheres.reserve(elements_with_link_down.size());
    for (const std::pair<SupportElement *, int> &element_with_link : elements_with_link_down)
    {
        const SupportElement &element = *element_with_link.first;
        const int link_down = element_with_link.second;
        collision_spheres.push_back({element, link_down,
                                     // locked
                                     element.parents.empty() || (link_down == -1 && element.state.layer_idx > 0),
                                     unscaled<float>(support_element_radius(config, element)),
                                     // 3D position
                                     to_3d(unscaled<float>(element.state.result_on_layer),
                                           float(layer_z(slicing_params, config, element.state.layer_idx)))});
        // Update min_z coordinate to min_z of the tree below.
        CollisionSphere &collision_sphere = collision_spheres.back();
        if (link_down != -1)
        {
            const size_t offset_below = linear_data_layers[element.state.layer_idx - 1];
            collision_sphere.min_z = collision_spheres[offset_below + link_down].min_z;
        }
        else
            collision_sphere.min_z = collision_sphere.position.z();
    }
    // Update max_z by propagating max_z from the tips of the branches.
    for (int collision_sphere_id = int(collision_spheres.size()) - 1; collision_sphere_id >= 0; --collision_sphere_id)
    {
        CollisionSphere &collision_sphere = collision_spheres[collision_sphere_id];
        if (collision_sphere.element.parents.empty())
            // Tip
            collision_sphere.max_z = collision_sphere.position.z();
        else
        {
            // Below tip
            const size_t offset_above = linear_data_layers[collision_sphere.element.state.layer_idx + 1];
            for (auto iparent : collision_sphere.element.parents)
            {
                float parent_z = collision_spheres[offset_above + iparent].max_z;
                //                    collision_sphere.max_z = collision_sphere.max_z == std::numeric_limits<float>::max() ? parent_z : std::max(collision_sphere.max_z, parent_z);
                collision_sphere.max_z = std::min(collision_sphere.max_z, parent_z);
            }
        }
    }
    // Update min_z / max_z to limit the search Z span of a given sphere for collision detection.
    for (CollisionSphere &collision_sphere : collision_spheres)
    {
        //FIXME limit the collision span by the tree slope.
        collision_sphere.min_z = std::max(collision_sphere.min_z,
                                          collision_sphere.position.z() - collision_sphere.radius);
        collision_sphere.max_z = std::min(collision_sphere.max_z,
                                          collision_sphere.position.z() + collision_sphere.radius);
        collision_sphere.layer_begin = std::min(collision_sphere.element.state.layer_idx,
                                                layer_idx_ceil(slicing_params, config, collision_sphere.min_z));
        assert(collision_sphere.layer_begin < layer_collision_cache.size());
        collision_sphere.layer_end = std::min(LayerIndex(layer_collision_cache.size()),
                                              std::max(collision_sphere.element.state.layer_idx,
                                                       layer_idx_floor(slicing_params, config,
                                                                       collision_sphere.max_z)) +
                                                  1);
    }

    throw_on_cancel();

    static constexpr const double collision_extra_gap = 0.1;
    static constexpr const double max_nudge_collision_avoidance = 0.5;
    static constexpr const double max_nudge_smoothing = 0.2;
    static constexpr const size_t num_iter = 100; // 1000;
    for (size_t iter = 0; iter < num_iter; ++iter)
    {
        // Back up prev position before Laplacian smoothing.
        for (CollisionSphere &collision_sphere : collision_spheres)
            collision_sphere.prev_position = collision_sphere.position;
        std::atomic<size_t> num_moved{0};
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, collision_spheres.size()),
            [&collision_spheres, &layer_collision_cache, &slicing_params, &config, &linear_data_layers, &num_moved,
             &throw_on_cancel](const tbb::blocked_range<size_t> range)
            {
                for (size_t collision_sphere_id = range.begin(); collision_sphere_id < range.end();
                     ++collision_sphere_id)
                    if (CollisionSphere &collision_sphere = collision_spheres[collision_sphere_id];
                        !collision_sphere.locked)
                    {
                        // Calculate collision of multiple 2D layers against a collision sphere.
                        collision_sphere.last_collision_depth = -std::numeric_limits<double>::max();
                        for (uint32_t layer_id = collision_sphere.layer_begin; layer_id != collision_sphere.layer_end;
                             ++layer_id)
                        {
                            double dz = (layer_id - collision_sphere.element.state.layer_idx) *
                                        slicing_params.layer_height;
                            if (double r2 = sqr(collision_sphere.radius) - sqr(dz); r2 > 0)
                            {
                                if (const LayerCollisionCache &layer_collision_cache_item =
                                        layer_collision_cache[layer_id];
                                    !layer_collision_cache_item.empty())
                                {
                                    size_t hit_idx_out;
                                    Vec2d hit_point_out;
                                    if (double dist = sqrt(AABBTreeLines::squared_distance_to_indexed_lines(
                                            layer_collision_cache_item.lines, layer_collision_cache_item.aabbtree_lines,
                                            Vec2d(to_2d(collision_sphere.position).cast<double>()), hit_idx_out,
                                            hit_point_out, r2));
                                        dist >= 0.)
                                    {
                                        double collision_depth = sqrt(r2) - dist;
                                        if (collision_depth > collision_sphere.last_collision_depth)
                                        {
                                            collision_sphere.last_collision_depth = collision_depth;
                                            collision_sphere.last_collision = to_3d(hit_point_out.cast<float>(),
                                                                                    float(layer_z(slicing_params,
                                                                                                  config, layer_id)));
                                        }
                                    }
                                }
                            }
                        }
                        if (collision_sphere.last_collision_depth > 0)
                        {
                            // Collision detected to be removed.
                            // Nudge the circle center away from the collision.
                            if (collision_sphere.last_collision_depth > EPSILON)
                                // a little bit of hysteresis to detect end of
                                ++num_moved;
                            // Shift by maximum 2mm.
                            double nudge_dist = std::min(std::max(0., collision_sphere.last_collision_depth +
                                                                          collision_extra_gap),
                                                         max_nudge_collision_avoidance);
                            Vec2d nudge_vector = (to_2d(collision_sphere.position) -
                                                  to_2d(collision_sphere.last_collision))
                                                     .cast<double>()
                                                     .normalized() *
                                                 nudge_dist;
                            collision_sphere.position.head<2>() += (nudge_vector * nudge_dist).cast<float>();
                        }
                        // Laplacian smoothing
                        Vec2d avg{0, 0};
                        //const SupportElements &above = move_bounds[collision_sphere.element.state.layer_idx + 1];
                        const size_t offset_above = linear_data_layers[collision_sphere.element.state.layer_idx + 1];
                        double weight = 0.;
                        for (auto iparent : collision_sphere.element.parents)
                        {
                            double w = collision_sphere.radius;
                            avg += w * to_2d(collision_spheres[offset_above + iparent].prev_position.cast<double>());
                            weight += w;
                        }
                        if (collision_sphere.element_below_id != -1)
                        {
                            const size_t offset_below = linear_data_layers[collision_sphere.element.state.layer_idx - 1];
                            const double w =
                                weight; //  support_element_radius(config, move_bounds[element.state.layer_idx - 1][below]);
                            avg += w * to_2d(collision_spheres[offset_below + collision_sphere.element_below_id]
                                                 .prev_position.cast<double>());
                            weight += w;
                        }
                        avg /= weight;
                        static constexpr const double smoothing_factor = 0.5;
                        Vec2d old_pos = to_2d(collision_sphere.position).cast<double>();
                        Vec2d new_pos = (1. - smoothing_factor) * old_pos + smoothing_factor * avg;
                        Vec2d shift = new_pos - old_pos;
                        double nudge_dist_max = shift.norm();
                        // Shift by maximum 1mm, less than the collision avoidance factor.
                        double nudge_dist = std::min(std::max(0., nudge_dist_max), max_nudge_smoothing);
                        collision_sphere.position.head<2>() += (shift.normalized() * nudge_dist).cast<float>();

                        throw_on_cancel();
                    }
            });
#if 0
        std::vector<double> stat;
        for (CollisionSphere& collision_sphere : collision_spheres)
            if (!collision_sphere.locked)
                stat.emplace_back(collision_sphere.last_collision_depth);
        std::sort(stat.begin(), stat.end());
        printf("iteration: %d, moved: %d, collision depth: min %lf, max %lf, median %lf\n", int(iter), int(num_moved), stat.front(), stat.back(), stat[stat.size() / 2]);
#endif
        if (num_moved == 0)
            break;
    }

    for (size_t i = 0; i < collision_spheres.size(); ++i)
        elements_with_link_down[i].first->state.result_on_layer = scaled<coord_t>(to_2d(collision_spheres[i].position));
}
#else  // TREE_SUPPORT_ORGANIC_NUDGE_NEW
// Old version using OpenVDB, works but it is extremely slow for complex meshes.
static void organic_smooth_branches_avoid_collisions(
    const PrintObject &print_object, const TreeModelVolumes &volumes, const TreeSupportSettings &config,
    std::vector<SupportElements> &move_bounds,
    const std::vector<std::pair<SupportElement *, int>> &elements_with_link_down,
    const std::vector<size_t> &linear_data_layers, std::function<void()> throw_on_cancel)
{
    TriangleMesh mesh = print_object.model_object()->raw_mesh();
    mesh.transform(print_object.trafo_centered());
    double scale = 10.;
    openvdb::FloatGrid::Ptr grid = mesh_to_grid(mesh.its, openvdb::math::Transform{}, scale, 0., 0.);
    std::unique_ptr<openvdb::tools::ClosestSurfacePoint<openvdb::FloatGrid>> closest_surface_point =
        openvdb::tools::ClosestSurfacePoint<openvdb::FloatGrid>::create(*grid);
    std::vector<openvdb::Vec3R> pts, prev, projections;
    std::vector<float> distances;
    for (const std::pair<SupportElement *, int> &element : elements_with_link_down)
    {
        Vec3d pt = to_3d(unscaled<double>(element.first->state.result_on_layer),
                         layer_z(print_object.slicing_parameters(), config, element.first->state.layer_idx)) *
                   scale;
        pts.push_back({pt.x(), pt.y(), pt.z()});
    }

    const double collision_extra_gap = 1. * scale;
    const double max_nudge_collision_avoidance = 2. * scale;
    const double max_nudge_smoothing = 1. * scale;

    static constexpr const size_t num_iter = 100; // 1000;
    for (size_t iter = 0; iter < num_iter; ++iter)
    {
        prev = pts;
        projections = pts;
        distances.assign(pts.size(), std::numeric_limits<float>::max());
        closest_surface_point->searchAndReplace(projections, distances);
        size_t num_moved = 0;
        for (size_t i = 0; i < projections.size(); ++i)
        {
            const SupportElement &element = *elements_with_link_down[i].first;
            const int below = elements_with_link_down[i].second;
            const bool locked = (below == -1 && element.state.layer_idx > 0) || element.state.locked();
            if (!locked && pts[i] != projections[i])
            {
                // Nudge the circle center away from the collision.
                Vec3d v{projections[i].x() - pts[i].x(), projections[i].y() - pts[i].y(),
                        projections[i].z() - pts[i].z()};
                double depth = v.norm();
                assert(std::abs(distances[i] - depth) < EPSILON);
                double radius = unscaled<double>(support_element_radius(config, element)) * scale;
                if (depth < radius)
                {
                    // Collision detected to be removed.
                    ++num_moved;
                    double dxy = sqrt(sqr(radius) - sqr(v.z()));
                    double nudge_dist_max = dxy -
                                            std::hypot(v.x(), v.y())
                                            //FIXME 1mm gap
                                            + collision_extra_gap;
                    // Shift by maximum 2mm.
                    double nudge_dist = std::min(std::max(0., nudge_dist_max), max_nudge_collision_avoidance);
                    Vec2d nudge_v = to_2d(v).normalized() * (-nudge_dist);
                    pts[i].x() += nudge_v.x();
                    pts[i].y() += nudge_v.y();
                }
            }
            // Laplacian smoothing
            if (!locked && !element.parents.empty())
            {
                Vec2d avg{0, 0};
                const SupportElements &above = move_bounds[element.state.layer_idx + 1];
                const size_t offset_above = linear_data_layers[element.state.layer_idx + 1];
                double weight = 0.;
                for (auto iparent : element.parents)
                {
                    double w = support_element_radius(config, above[iparent]);
                    avg.x() += w * prev[offset_above + iparent].x();
                    avg.y() += w * prev[offset_above + iparent].y();
                    weight += w;
                }
                size_t cnt = element.parents.size();
                if (below != -1)
                {
                    const size_t offset_below = linear_data_layers[element.state.layer_idx - 1];
                    const double w =
                        weight; //  support_element_radius(config, move_bounds[element.state.layer_idx - 1][below]);
                    avg.x() += w * prev[offset_below + below].x();
                    avg.y() += w * prev[offset_below + below].y();
                    ++cnt;
                    weight += w;
                }
                //avg /= double(cnt);
                avg /= weight;
                static constexpr const double smoothing_factor = 0.5;
                Vec2d old_pos{pts[i].x(), pts[i].y()};
                Vec2d new_pos = (1. - smoothing_factor) * old_pos + smoothing_factor * avg;
                Vec2d shift = new_pos - old_pos;
                double nudge_dist_max = shift.norm();
                // Shift by maximum 1mm, less than the collision avoidance factor.
                double nudge_dist = std::min(std::max(0., nudge_dist_max), max_nudge_smoothing);
                Vec2d nudge_v = shift.normalized() * nudge_dist;
                pts[i].x() += nudge_v.x();
                pts[i].y() += nudge_v.y();
            }
        }
        //            printf("iteration: %d, moved: %d\n", int(iter), int(num_moved));
        if (num_moved == 0)
            break;
    }

    for (size_t i = 0; i < projections.size(); ++i)
    {
        elements_with_link_down[i].first->state.result_on_layer.x() = scaled<coord_t>(pts[i].x()) / scale;
        elements_with_link_down[i].first->state.result_on_layer.y() = scaled<coord_t>(pts[i].y()) / scale;
    }
}
#endif // TREE_SUPPORT_ORGANIC_NUDGE_NEW

// Organic specific: Smooth branches and produce one cummulative mesh to be sliced.
void organic_draw_branches(PrintObject &print_object, TreeModelVolumes &volumes, const TreeSupportSettings &config,
                           std::vector<SupportElements> &move_bounds,

                           // I/O:
                           SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts,
                           InterfacePlacer &interface_placer,

                           // Output:
                           SupportGeneratorLayersPtr &intermediate_layers, SupportGeneratorLayerStorage &layer_storage,

                           std::function<void()> throw_on_cancel,
                           // Areas to exclude from organic support (e.g., snug/grid support regions)
                           const std::vector<Polygons> &excluded_areas,
                           // Per-layer overhang areas: the canopy's authority when interface layers
                           // are disabled and no contact layer exists to clip against.
                           const std::vector<Polygons> &baobab_overhangs,
                           // Out: per-layer canopy ring footprints keyed by print z in microns, so
                           // the toolpath generator can tell a canopy from a trunk.
                           BaobabCanopyFootprints *baobab_canopy_footprints)
{
    PerfStageTimer pf_organic;
    pf_organic.reset();

    // All SupportElements are put into a layer independent storage to improve parallelization.
    std::vector<std::pair<SupportElement *, int>> elements_with_link_down;
    std::vector<size_t> linear_data_layers;
    {
        std::vector<std::pair<SupportElement *, int>> map_downwards_old;
        std::vector<std::pair<SupportElement *, int>> map_downwards_new;
        linear_data_layers.emplace_back(0);
        for (LayerIndex layer_idx = 0; layer_idx < LayerIndex(move_bounds.size()); ++layer_idx)
        {
            SupportElements *layer_above = layer_idx + 1 < LayerIndex(move_bounds.size()) ? &move_bounds[layer_idx + 1]
                                                                                          : nullptr;
            map_downwards_new.clear();
            std::sort(map_downwards_old.begin(), map_downwards_old.end(),
                      [](auto &l, auto &r) { return l.first < r.first; });
            SupportElements &layer = move_bounds[layer_idx];
            for (size_t elem_idx = 0; elem_idx < layer.size(); ++elem_idx)
            {
                SupportElement &elem = layer[elem_idx];
                int child = -1;
                if (layer_idx > 0)
                {
                    auto it = std::lower_bound(map_downwards_old.begin(), map_downwards_old.end(), &elem,
                                               [](auto &l, const SupportElement *r) { return l.first < r; });
                    if (it != map_downwards_old.end() && it->first == &elem)
                    {
                        child = it->second;
                        // Only one link points to a node above from below.
                        assert(!(++it != map_downwards_old.end() && it->first == &elem));
                    }
#ifndef NDEBUG
                    {
                        const SupportElement *pchild = child == -1 ? nullptr : &move_bounds[layer_idx - 1][child];
                        assert(pchild ? pchild->state.result_on_layer_is_set() : elem.state.target_height > layer_idx);
                    }
#endif // NDEBUG
                }
                for (int32_t parent_idx : elem.parents)
                {
                    SupportElement &parent = (*layer_above)[parent_idx];
                    if (parent.state.result_on_layer_is_set())
                        map_downwards_new.emplace_back(&parent, elem_idx);
                }

                elements_with_link_down.push_back({&elem, int(child)});
            }
            std::swap(map_downwards_old, map_downwards_new);
            linear_data_layers.emplace_back(elements_with_link_down.size());
        }
    }

    throw_on_cancel();
    pf_organic.stage("    organic: linearize elements");

    organic_smooth_branches_avoid_collisions(print_object, volumes, config, move_bounds, elements_with_link_down,
                                             linear_data_layers, throw_on_cancel);
    pf_organic.stage("    organic: smooth_branches_avoid_collisions");

    // Reduce memory footprint. After this point only finalize_interface_and_support_areas() will use volumes and from that only collisions with zero radius will be used.
    volumes.clear_all_but_object_collision();

    // Unmark all nodes.
    for (SupportElements &elements : move_bounds)
        for (SupportElement &element : elements)
            element.state.marked = false;

    // Traverse all nodes, generate tubes.
    // Traversal stack with nodes and their current parent

    struct Branch
    {
        std::vector<const SupportElement *> path;
        bool has_root{false};
        bool has_tip{false};
    };

    struct Slice
    {
        Polygons polygons;
        Polygons bottom_contacts;
        Polygons tip_polygons; // Polygons that are tips (contact object), kept separate
        size_t num_branches{0};
    };

    struct Tree
    {
        std::vector<Branch> branches;

        std::vector<Slice> slices;
        LayerIndex first_layer_id{-1};
    };

    std::vector<Tree> trees;

    struct TreeVisitor
    {
        static void visit_recursive(std::vector<SupportElements> &move_bounds, SupportElement &start_element, Tree &out)
        {
            assert(!start_element.state.marked && !start_element.parents.empty());
            // Collect elements up to a bifurcation above.
            start_element.state.marked = true;
            // For each branch bifurcating from this point:
            //SupportElements &layer       = move_bounds[start_element.state.layer_idx];

            LayerIndex current_layer = start_element.state.layer_idx;
            if (current_layer + 1 >= LayerIndex(move_bounds.size()))
                return;
            SupportElements &layer_above = move_bounds[start_element.state.layer_idx + 1];
            bool root = out.branches.empty();
            for (size_t parent_idx = 0; parent_idx < start_element.parents.size(); ++parent_idx)
            {
                Branch branch;
                branch.path.emplace_back(&start_element);
                // Traverse each branch until it branches again.
                int32_t parent_elem_idx = start_element.parents[parent_idx];
                if (parent_elem_idx < 0 || parent_elem_idx >= (int32_t) layer_above.size())
                    continue;
                SupportElement &first_parent = layer_above[start_element.parents[parent_idx]];
                assert(!first_parent.state.marked);
                assert(branch.path.back()->state.layer_idx + 1 == first_parent.state.layer_idx);
                branch.path.emplace_back(&first_parent);
                if (first_parent.parents.size() < 2)
                    first_parent.state.marked = true;
                SupportElement *next_branch = nullptr;
                if (first_parent.parents.size() == 1)
                {
                    for (SupportElement *parent = &first_parent;;)
                    {
                        assert(parent->state.marked);
                        LayerIndex next_layer_idx = parent->state.layer_idx + 1;
                        if (next_layer_idx >= LayerIndex(move_bounds.size()))
                            break;
                        int32_t next_parent_idx = parent->parents.front();
                        if (next_parent_idx < 0 || next_parent_idx >= (int32_t) move_bounds[next_layer_idx].size())
                            break;
                        SupportElement &next_parent = move_bounds[parent->state.layer_idx + 1][parent->parents.front()];
                        assert(!next_parent.state.marked);
                        assert(branch.path.back()->state.layer_idx + 1 == next_parent.state.layer_idx);
                        branch.path.emplace_back(&next_parent);
                        if (next_parent.parents.size() > 1)
                        {
                            // Branching point was reached.
                            next_branch = &next_parent;
                            break;
                        }
                        next_parent.state.marked = true;
                        if (next_parent.parents.size() == 0)
                            // Tip is reached.
                            break;
                        parent = &next_parent;
                    }
                }
                else if (first_parent.parents.size() > 1)
                    // Branching point was reached.
                    next_branch = &first_parent;
                assert(branch.path.size() >= 2);
                assert(next_branch == nullptr || !next_branch->state.marked);
                branch.has_root = root;
                branch.has_tip = !next_branch;
                out.branches.emplace_back(std::move(branch));
                if (next_branch)
                    visit_recursive(move_bounds, *next_branch, out);
            }
        }
    };

    for (LayerIndex layer_idx = 0; layer_idx + 1 < LayerIndex(move_bounds.size()); ++layer_idx)
    {
        //        int ielement;
        for (SupportElement &start_element : move_bounds[layer_idx])
        {
            if (!start_element.state.marked && !start_element.parents.empty())
            {
#if 0
                int found = 0;
                if (layer_idx > 0) {
                    for (auto& el : move_bounds[layer_idx - 1]) {
                        for (auto iparent : el.parents)
                            if (iparent == ielement)
                                ++found;
                    }
                    if (found != 0)
                        printf("Found: %d\n", found);
                }
#endif
                trees.push_back({});
                TreeVisitor::visit_recursive(move_bounds, start_element, trees.back());
                assert(!trees.back().branches.empty());
                //FIXME debugging
#if 0
                if (start_element.state.lost) {
                }
                else if (start_element.state.verylost) {
                } else
                    trees.pop_back();
#endif
            }
            //            ++ ielement;
        }
    }

    pf_organic.stage("    organic: collect trees + branches");

    const SlicingParameters &slicing_params = print_object.slicing_parameters();
    MeshSlicingParams mesh_slicing_params;
    mesh_slicing_params.mode = MeshSlicingParams::SlicingMode::Positive;

    // Baobab canopies. Seeds are collected here, sequentially, so their indices - and therefore
    // every Voronoi share - are identical from run to run.
    // Baobab builds canopies whether or not interface layers exist: with them, the canopy clips to
    // the contact layers; without, to the overhang itself (the baobab_overhangs fallback), and
    // the filled mouth becomes the support's top surface.
    const bool baobab = is_baobab_object(print_object);
    const double baobab_taper = baobab ? baobab_taper_angle_rad(config.settings) : 0.;
    const double baobab_growth_budget = unscaled<double>(baobab_max_growth_per_layer(config.support_line_width));
    const size_t baobab_contact_search = interface_placer.support_parameters.num_top_interface_layers + 3;
    // A branch is sliced this many layers above its tip element (see layer_end below). The canopy
    // rim must reach the topmost of them, or the last layer under the interface keeps only a pin.
    const size_t baobab_z_dist = size_t(std::round(slicing_params.gap_support_object / slicing_params.layer_height));
    const LayerIndex baobab_tip_extension = (slicing_params.gap_support_object >=
                                             double(baobab_z_dist) * slicing_params.layer_height - EPSILON)
                                                ? 1
                                                : 2;
    BaobabCanopyStats baobab_canopy_stats;
    // The canopy clips to the interface as it PRINTS: interface layers get holes below the
    // closing area removed, so the canopy's region drops them too. Larger holes are genuine
    // interface holes and stay, so the canopy still obeys them.
    const coordf_t baobab_closing_radius = print_object.config().support_material_closing_radius.value;
    const double baobab_region_max_hole_area = baobab_closing_radius > 0.
                                                   ? M_PI * sqr(scaled<double>(baobab_closing_radius))
                                                   : 0.;
    // Per-tip records of canopies that were rejected or stopped early, collected across the
    // parallel pass and printed sorted so the output stays diffable.
    struct BaobabCanopyReject
    {
        LayerIndex tip_layer;
        double x, y;
        BaobabCanopyOutcome outcome;
    };
    std::mutex baobab_rejects_mutex;
    std::vector<BaobabCanopyReject> baobab_rejects;
    std::mutex baobab_footprints_mutex;

    // Down-links, keyed by element. A Branch spans only junction-to-tip, and tips that merge
    // early give branches a handful of layers - far fewer than a canopy needs to close. The canopy
    // must be free to descend past the junction into the trunk below, so it walks the element
    // chain rather than the branch. Elements store links UP (`parents`); this inverts them.
    // Keyed by pointer because a layer is a deque: there is no index to recover from a pointer.
    std::unordered_map<const SupportElement *, const SupportElement *> baobab_down;
    if (baobab)
        for (size_t l = 0; l + 1 < move_bounds.size(); ++l)
            for (const SupportElement &below : move_bounds[l])
                for (const int32_t p : below.parents)
                    if (p >= 0 && size_t(p) < move_bounds[l + 1].size())
                        baobab_down.emplace(&move_bounds[l + 1][size_t(p)], &below);

    // THE TRUNK ECONOMY. Routing merges tips by proximity, so a mouth is carried by however
    // many trees drift produced - a forest. A grounded tree is REDUNDANT when, in every region
    // where it holds tips, the surviving trees' canopies can still cover the whole region:
    // tested at envelope level (nearest-tip distance over the sampled region) and then against
    // each surviving tip's real chain (capacity net of lean), so removal can never lose
    // coverage. Smallest trees are tried first and the sweep repeats until nothing more can be
    // proven redundant; what survives is however many great trees the geometry demands. A
    // failed proof keeps the tree: the worst case is the unpruned forest.
    if (baobab && trees.size() > 1)
    {
        const double g_nominal = unscaled<double>(config.layer_height) * std::tan(baobab_taper);
        struct EconomyTip
        {
            size_t tree;
            LayerIndex layer;
            Vec2d pos;
            std::vector<const SupportElement *> chain;
        };
        std::vector<EconomyTip> etips;
        for (size_t tree_id = 0; tree_id < trees.size(); ++tree_id)
            for (const Branch &branch : trees[tree_id].branches)
                if (branch.has_tip && branch.path.size() >= 2)
                {
                    std::vector<const SupportElement *> chain = branch.path;
                    for (const SupportElement *lowest = chain.front(); chain.size() < 512;)
                    {
                        const auto below = baobab_down.find(lowest);
                        if (below == baobab_down.end())
                            break;
                        lowest = below->second;
                        chain.insert(chain.begin(), lowest);
                    }
                    etips.push_back({tree_id, branch.path.back()->state.layer_idx,
                                     unscaled<double>(branch.path.back()->state.result_on_layer), std::move(chain)});
                }

        // Group tips into regions: tips within one contact window share an interface stack.
        std::vector<LayerIndex> region_floor; // ascending window starts
        {
            std::vector<LayerIndex> tip_layers;
            for (const EconomyTip &t : etips)
                tip_layers.push_back(t.layer);
            std::sort(tip_layers.begin(), tip_layers.end());
            tip_layers.erase(std::unique(tip_layers.begin(), tip_layers.end()), tip_layers.end());
            for (const LayerIndex l : tip_layers)
                if (region_floor.empty() || l - region_floor.back() > LayerIndex(baobab_contact_search) + 2)
                    region_floor.push_back(l);
        }
        auto region_of = [&region_floor](const LayerIndex l) -> size_t
        {
            size_t r = 0;
            while (r + 1 < region_floor.size() && region_floor[r + 1] <= l)
                ++r;
            return r;
        };

        // Sample each region's interface stack per BAND, tagged with the band's layer: under
        // the terrace law a canopy clips only to bands at or above its own tip layer, so a tip
        // can never serve a band below itself and coverage must be tested band by band.
        // Boundary at 1 mm plus a 2 mm interior grid - the discretization the sizing uses.
        struct EconomySample
        {
            LayerIndex band;
            Vec2d pos;
            Point scaled_pos;
        };
        std::vector<std::vector<EconomySample>> region_samples(region_floor.size());
        std::vector<Polygons> region_union(region_floor.size());
        for (size_t r = 0; r < region_floor.size(); ++r)
        {
            std::set<size_t> band_layers;
            for (const EconomyTip &t : etips)
                if (region_of(t.layer) == r)
                    for (size_t d = 0; d <= baobab_contact_search + 1; ++d)
                        band_layers.insert(size_t(t.layer) + d);
            std::vector<EconomySample> &samples = region_samples[r];
            for (const size_t l : band_layers)
            {
                const Polygons *band = nullptr;
                if (l < top_contacts.size() && top_contacts[l] != nullptr && !top_contacts[l]->polygons.empty())
                    band = &top_contacts[l]->polygons;
                else if (l < baobab_overhangs.size() && !baobab_overhangs[l].empty())
                    band = &baobab_overhangs[l];
                if (band == nullptr)
                    continue;
                polygons_append(region_union[r], *band);
                const double step = scaled<double>(1.);
                for (const Polygon &contour : *band)
                {
                    double carry = 0.;
                    for (size_t i = 0; i < contour.points.size(); ++i)
                    {
                        const Vec2d a = contour.points[i].cast<double>();
                        const Vec2d b = contour.points[(i + 1) % contour.points.size()].cast<double>();
                        const double len = (b - a).norm();
                        double t = carry;
                        for (; t < len; t += step)
                        {
                            const Point p((a + (b - a) * (t / len)).cast<coord_t>());
                            samples.push_back({LayerIndex(l), unscaled<double>(p), p});
                        }
                        carry = t - len;
                    }
                }
                const BoundingBox bbox = get_extents(*band);
                const auto grid = scaled<coord_t>(2.);
                for (coord_t x = bbox.min.x(); x <= bbox.max.x(); x += grid)
                    for (coord_t y = bbox.min.y(); y <= bbox.max.y(); y += grid)
                        if (const Point p(x, y); contains(*band, p))
                            samples.push_back({LayerIndex(l), unscaled<double>(p), p});
            }
            region_union[r] = union_(region_union[r]);
        }

        std::vector<char> pruned(trees.size(), 0);
        size_t rejected_chain = 0;
        // Simulated rims, cached per (tip, reach quantized to 0.5 mm): trial needs repeat
        // across sweeps, so each tip's canopy is simulated only a handful of times.
        std::map<std::pair<size_t, int>, ExPolygons> rim_cache;
        auto rim_of = [&](const size_t i, const double reach) -> const ExPolygons &
        {
            const auto key = std::make_pair(i, int(std::ceil(reach * 2.)));
            auto it = rim_cache.find(key);
            if (it == rim_cache.end())
                it = rim_cache
                         .emplace(key, baobab_sim_rim(etips[i].chain, region_union[region_of(etips[i].layer)], config,
                                                      g_nominal, baobab_growth_budget, double(key.second) * 0.5))
                         .first;
            return it->second;
        };

        // Can the region still be covered without `excluded` trees? Reaches are re-sized to
        // the trial's nearest-tip needs (mirroring the build), each live tip's canopy is
        // simulated at that reach, and every band sample must fall inside the rim of a tip
        // that can legally serve its band (terrace law: tip layer at or below the band).
        auto region_covered = [&](const size_t r, const std::vector<char> &excluded) -> bool
        {
            std::vector<size_t> live;
            for (size_t i = 0; i < etips.size(); ++i)
                if (!excluded[etips[i].tree] && region_of(etips[i].layer) == r)
                    live.push_back(i);
            if (live.empty())
                return false;
            std::vector<double> need(live.size(), 0.);
            for (const EconomySample &s : region_samples[r])
            {
                size_t best = 0;
                double best_d2 = std::numeric_limits<double>::max();
                for (size_t k = 0; k < live.size(); ++k)
                    if (const double d2 = (s.pos - etips[live[k]].pos).squaredNorm(); d2 < best_d2)
                    {
                        best_d2 = d2;
                        best = k;
                    }
                need[best] = std::max(need[best], std::sqrt(best_d2));
            }
            for (const EconomySample &s : region_samples[r])
            {
                bool served = false;
                for (size_t k = 0; !served && k < live.size(); ++k)
                {
                    const EconomyTip &tip = etips[live[k]];
                    if (s.band < tip.layer || s.band > tip.layer + LayerIndex(baobab_contact_search) + 1)
                        continue;
                    for (const ExPolygon &piece : rim_of(live[k], need[k] + 0.5))
                        if (piece.contains(s.scaled_pos))
                        {
                            served = true;
                            break;
                        }
                }
                if (!served)
                {
                    ++rejected_chain;
                    return false;
                }
            }
            return true;
        };

        for (bool changed = true; changed;)
        {
            changed = false;
            std::vector<size_t> order;
            for (size_t tree_id = 0; tree_id < trees.size(); ++tree_id)
                if (!pruned[tree_id])
                    order.push_back(tree_id);
            if (order.size() <= 1)
                break;
            std::sort(order.begin(), order.end(),
                      [&etips](const size_t a, const size_t b)
                      {
                          size_t na = 0, nb = 0;
                          for (const EconomyTip &t : etips)
                          {
                              na += t.tree == a;
                              nb += t.tree == b;
                          }
                          return na != nb ? na < nb : a < b;
                      });
            for (const size_t candidate : order)
            {
                std::vector<char> trial = pruned;
                trial[candidate] = 1;
                bool ok = true;
                for (size_t r = 0; ok && r < region_floor.size(); ++r)
                {
                    bool candidate_holds = false;
                    for (const EconomyTip &t : etips)
                        if (t.tree == candidate && region_of(t.layer) == r)
                        {
                            candidate_holds = true;
                            break;
                        }
                    if (candidate_holds)
                        ok = region_covered(r, trial);
                }
                if (ok)
                {
                    pruned = std::move(trial);
                    changed = true;
                    break;
                }
            }
        }

        const size_t n_pruned = size_t(std::count(pruned.begin(), pruned.end(), char(1)));
        dbg_log(DBG_BAOBAB, 0., "BAOBAB-ECONOMY",
                "regions=%zu trees_before=%zu pruned=%zu rejected_chain=%zu trees_after=%zu", region_floor.size(),
                trees.size(), n_pruned, rejected_chain, trees.size() - n_pruned);
        if (n_pruned > 0)
        {
            std::vector<Tree> kept;
            kept.reserve(trees.size() - n_pruned);
            for (size_t tree_id = 0; tree_id < trees.size(); ++tree_id)
                if (!pruned[tree_id])
                    kept.emplace_back(std::move(trees[tree_id]));
            trees = std::move(kept);
        }
    }

    // Baobab telemetry. Emitted here because the tree is complete and the walk is still
    // sequential, so the block is byte-identical for a given object and settings, and can
    // be diffed across runs to A/B a knob. A trunk is a branch that reached its landing
    // without merging into a neighbour, so on_plate is the trunk count the gather spacing
    // produced.
    if (debug_enabled(DBG_BAOBAB) && is_baobab_object(print_object))
    {
        size_t n_branches = 0;
        size_t n_tips = 0;
        size_t n_roots = 0;
        size_t n_on_plate = 0;
        coord_t max_radius = 0;
        for (size_t tree_id = 0; tree_id < trees.size(); ++tree_id)
            for (const Branch &branch : trees[tree_id].branches)
            {
                ++n_branches;
                if (branch.has_tip)
                    ++n_tips;
                for (const SupportElement *element : branch.path)
                    max_radius = std::max(max_radius, support_element_radius(config, *element));
                if (!branch.has_root || branch.path.empty())
                    continue;
                ++n_roots;
                const SupportElement &root = *branch.path.front();
                const bool on_plate = root.state.to_buildplate;
                if (on_plate)
                    ++n_on_plate;
                dbg_log(DBG_BAOBAB, layer_z(slicing_params, config, root.state.layer_idx), "BAOBAB-ROOT",
                        "tree=%zu layer=%d x=%.3f y=%.3f radius=%.3f rests_on=%s", tree_id, int(root.state.layer_idx),
                        unscaled<double>(root.state.result_on_layer.x()),
                        unscaled<double>(root.state.result_on_layer.y()),
                        unscaled<double>(support_element_radius(config, root)), on_plate ? "plate" : "model");
            }
        dbg_log(DBG_BAOBAB, 0., "BAOBAB-SUMMARY",
                "gather_spacing=%.3f trunk_diameter=%.3f trees=%zu branches=%zu tips=%zu roots=%zu on_plate=%zu "
                "on_model=%zu max_radius=%.3f",
                unscaled<double>(config.settings.support_tree_branch_distance),
                unscaled<double>(config.settings.support_tree_branch_diameter), trees.size(), n_branches, n_tips,
                n_roots, n_on_plate, n_roots - n_on_plate, unscaled<double>(max_radius));
    }

    // When interface_layers > 0, tips should stay in regular polygons (InterfacePlacer handles
    // interfaces). Baobab never separates: the topmost slice is the canopy's mouth, not a pin
    // tip, and it prints as walls plus fill like every canopy layer below it.
    const bool separate_tips = !interface_placer.support_parameters.has_top_contacts && !baobab;
    dbg_log(DBG_BAOBAB, 0., "BAOBAB-GATE",
            "baobab=%d has_top_contacts=%d separate_tips=%d tip_extension=%d plant_on_model=%d", int(baobab),
            int(interface_placer.support_parameters.has_top_contacts), int(separate_tips), int(baobab_tip_extension),
            int(config.settings.support_baobab_plant_on_model && config.support_rests_on_model));

    // Every tip of the object, for the per-seed envelope sizing: a canopy's reach is measured
    // against the samples whose nearest seed it is, so the tip list must be complete before any
    // canopy is built.
    std::vector<std::pair<Point, LayerIndex>> baobab_tips;
    if (baobab)
        for (const Tree &tree : trees)
            for (const Branch &branch : tree.branches)
                if (branch.has_tip && branch.path.size() >= 2)
                    baobab_tips.emplace_back(branch.path.back()->state.result_on_layer,
                                             branch.path.back()->state.layer_idx);

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, trees.size(), 1),
        [&trees, &volumes, &config, &slicing_params, &move_bounds, &mesh_slicing_params, &throw_on_cancel,
         separate_tips, baobab, baobab_taper, baobab_growth_budget, baobab_contact_search, baobab_tip_extension,
         baobab_region_max_hole_area, &baobab_canopy_stats, &baobab_down, &baobab_rejects_mutex, &baobab_rejects,
         &baobab_overhangs, &top_contacts, &baobab_tips, &baobab_footprints_mutex,
         baobab_canopy_footprints](const tbb::blocked_range<size_t> &range)
        {
            indexed_triangle_set partial_mesh;
            std::vector<float> slice_z;
            std::vector<Polygons> bottom_contacts;
            for (size_t tree_id = range.begin(); tree_id < range.end(); ++tree_id)
            {
                Tree &tree = trees[tree_id];
                for (size_t branch_idx = 0; branch_idx < tree.branches.size(); ++branch_idx)
                {
                    const Branch &branch = tree.branches[branch_idx];
                    if (branch.path.size() < 2)
                        continue;
                    // Triangulate the tube.
                    partial_mesh.clear();
                    std::pair<float, float> zspan = extrude_branch(branch.path, config, slicing_params, move_bounds,
                                                                   partial_mesh);

                    // Baobab: the tip carries its Voronoi share of the interface down onto the
                    // branch as a canopy, instead of ending in a pin. The loft is appended to this
                    // branch's own mesh, so canopies that overlap fuse when the slices union - which
                    // is the baobab mouth, and is why no code here decides how a canopy splits.
                    // Every rejection leaves the stock hemisphere tip: shape is lost, support never is.
                    LayerIndex baobab_canopy_bottom = -1;
                    if (baobab && branch.has_tip)
                    {
                        const LayerIndex tip_layer = branch.path.back()->state.layer_idx;
                        // Tip-to-tree membership, for the offline trunk-economy analysis: rings
                        // are logged per tip and roots per tree, and this line joins the two.
                        if (debug_enabled(DBG_BAOBAB))
                        {
                            const Vec2d tip_xy = unscaled<double>(branch.path.back()->state.result_on_layer);
                            dbg_log(DBG_BAOBAB, layer_z(slicing_params, config, size_t(tip_layer)), "BAOBAB-TIP",
                                    "tree=%zu tip_layer=%d x=%.2f y=%.2f", tree_id, int(tip_layer), tip_xy.x(),
                                    tip_xy.y());
                        }

                        // Follow the chain below this branch's own root so the canopy may narrow onto
                        // the merged trunk. Without it, a tip that merges within a few layers has too
                        // few layers to close over.
                        std::vector<const SupportElement *> canopy_path = branch.path;
                        for (const SupportElement *lowest = canopy_path.front(); canopy_path.size() < 512;)
                        {
                            const auto below = baobab_down.find(lowest);
                            if (below == baobab_down.end())
                                break;
                            lowest = below->second;
                            canopy_path.insert(canopy_path.begin(), lowest);
                        }

                        // The interface is a HEIGHT FIELD, not a footprint: on a tapering face
                        // the stack descends band by band, and the canopy may cover a point only
                        // while it stays below that point's interface. Collect the stack as
                        // per-layer bands; the builder clips each ring to the bands strictly
                        // above the ring's own layer, so the mouth terraces down under a
                        // tapering face instead of growing into the lower interface bands. The
                        // whole stack still enters the window (roof layers erode against
                        // avoidance as they descend, and neighbouring pads land on different
                        // layers, so no single layer is authoritative).
                        std::vector<std::pair<LayerIndex, Polygons>> bands;
                        for (size_t d = 0; d <= baobab_contact_search; ++d)
                            if (const size_t l = size_t(tip_layer) + d; l < top_contacts.size() &&
                                                                        top_contacts[l] != nullptr &&
                                                                        !top_contacts[l]->polygons.empty())
                                bands.emplace_back(LayerIndex(l), top_contacts[l]->polygons);

                        // With interface layers disabled no contact layer exists; the canopy's
                        // authority is then the overhang itself - the area the support holds up.
                        // The scan window is one layer wider: the overhang sits at the object's
                        // layer, one above where a contact layer would.
                        if (bands.empty())
                            for (size_t d = 0; d <= baobab_contact_search + 1; ++d)
                                if (const size_t l = size_t(tip_layer) + d;
                                    l < baobab_overhangs.size() && !baobab_overhangs[l].empty())
                                {
                                    bands.emplace_back(LayerIndex(l), baobab_overhangs[l]);
                                    baobab_canopy_stats.regions_from_overhang.fetch_add(1, std::memory_order_relaxed);
                                    break;
                                }

                        // Same hole-closing as the printed interface, per band.
                        for (auto &band : bands)
                        {
                            ExPolygons closed = union_ex(band.second);
                            if (baobab_region_max_hole_area > 0.)
                                for (ExPolygon &piece : closed)
                                    piece.holes.erase(std::remove_if(piece.holes.begin(), piece.holes.end(),
                                                                     [baobab_region_max_hole_area](const Polygon &hole)
                                                                     {
                                                                         return std::abs(hole.area()) <
                                                                                baobab_region_max_hole_area;
                                                                     }),
                                                      piece.holes.end());
                            for (const ExPolygon &piece : closed)
                                if (!piece.holes.empty())
                                    baobab_canopy_stats.region_holes.fetch_add(piece.holes.size(),
                                                                               std::memory_order_relaxed);
                            band.second = to_polygons(std::move(closed));
                        }

                        // Size this canopy's envelope to its seed's own coverage duty.
                        double canopy_reach = 0.;
                        if (!bands.empty())
                        {
                            Polygons sizing_region;
                            for (const auto &band : bands)
                                polygons_append(sizing_region, band.second);
                            canopy_reach = baobab_sized_reach_mm(union_(sizing_region),
                                                                 branch.path.back()->state.result_on_layer, tip_layer,
                                                                 baobab_tips, LayerIndex(baobab_contact_search + 2));
                            const auto reach_cmm = int64_t(std::lround(canopy_reach * 100.));
                            baobab_atomic_min(baobab_canopy_stats.reach_sized_min_cmm, reach_cmm);
                            baobab_atomic_max(baobab_canopy_stats.reach_sized_max_cmm, reach_cmm);
                        }

                        BaobabCanopyOutcome canopy_outcome;
                        std::vector<std::pair<LayerIndex, Polygons>> canopy_rings;
                        if (bands.empty())
                        {
                            baobab_canopy_stats.reject_no_interface.fetch_add(1, std::memory_order_relaxed);
                            canopy_outcome.reason = "no_interface";
                        }
                        else if (const size_t layers = baobab_extrude_canopy(
                                     bands, canopy_path, config, slicing_params, baobab_taper, baobab_growth_budget,
                                     canopy_reach, baobab_tip_extension, baobab_canopy_bottom, baobab_canopy_stats,
                                     canopy_outcome, canopy_rings, partial_mesh);
                                 layers == 0)
                            baobab_canopy_stats.reject_trunk_outside_region.fetch_add(1, std::memory_order_relaxed);
                        else
                        {
                            baobab_canopy_stats.canopies_built.fetch_add(1, std::memory_order_relaxed);
                            if (canopy_outcome.truncated)
                                baobab_canopy_stats.canopies_truncated.fetch_add(1, std::memory_order_relaxed);
                            size_t deepest = baobab_canopy_stats.deepest_canopy_layers.load(std::memory_order_relaxed);
                            while (layers > deepest && !baobab_canopy_stats.deepest_canopy_layers.compare_exchange_weak(
                                                           deepest, layers, std::memory_order_relaxed))
                                ;
                        }
                        if (baobab_canopy_footprints != nullptr && !canopy_rings.empty())
                        {
                            std::lock_guard<std::mutex> lock(baobab_footprints_mutex);
                            for (auto &[ring_layer, ring_polys] : canopy_rings)
                                polygons_append((*baobab_canopy_footprints)[int64_t(std::llround(
                                                    layer_z(slicing_params, config, size_t(ring_layer)) * 1000.))],
                                                std::move(ring_polys));
                        }
                        if (debug_enabled(DBG_BAOBAB) && (canopy_outcome.built == 0 || canopy_outcome.truncated))
                        {
                            const Vec2d tip_xy = unscaled<double>(branch.path.back()->state.result_on_layer);
                            std::lock_guard<std::mutex> lock(baobab_rejects_mutex);
                            baobab_rejects.push_back({tip_layer, tip_xy.x(), tip_xy.y(), canopy_outcome});
                        }
                    }

                    LayerIndex layer_begin = branch.has_root
                                                 ? branch.path.front()->state.layer_idx
                                                 : std::min(branch.path.front()->state.layer_idx,
                                                            layer_idx_ceil(slicing_params, config, zspan.first));
                    // A canopy may close below this branch's own root, inside the trunk. Slice
                    // down to it, or its lower rings are modelled and then never sampled.
                    if (baobab_canopy_bottom >= 0)
                        layer_begin = std::min(layer_begin, baobab_canopy_bottom);
                    // Extension depends on where gap falls relative to z_distance_top_layers.
                    // z_dist = round(gap / lh) determines tip/interface positioning.
                    //
                    // The gap can be in "lower half" or "upper half" of a z_dist range:
                    // - Lower half (gap >= z_dist * lh): positioning accurate, +1 works
                    // - Upper half (gap < z_dist * lh): positioning overestimates, need +2
                    //
                    // Examples with lh=0.25mm:
                    //   gap=0.10mm: z_dist=0, 0.10 >= 0*0.25=0 → lower half → +1
                    //   gap=0.20mm: z_dist=1, 0.20 < 1*0.25=0.25 → upper half → +2
                    //   gap=0.35mm: z_dist=1, 0.35 >= 0.25 → lower half → +1
                    //   gap=0.40mm: z_dist=2, 0.40 < 2*0.25=0.50 → upper half → +2
                    //   gap=0.55mm: z_dist=2, 0.55 >= 0.50 → lower half → +1
                    const coordf_t gap = slicing_params.gap_support_object;
                    const coordf_t lh = slicing_params.layer_height;
                    const size_t z_dist = size_t(std::round(gap / lh));
                    const bool lower_half = (gap >= z_dist * lh - EPSILON);
                    LayerIndex tip_extension = lower_half ? 1 : 2;
                    LayerIndex layer_end = branch.has_tip
                                               ? branch.path.back()->state.layer_idx + tip_extension
                                               : std::max(branch.path.back()->state.layer_idx,
                                                          layer_idx_floor(slicing_params, config, zspan.second)) +
                                                     1;

                    if (layer_begin >= layer_end)
                        continue;

                    slice_z.clear();
                    for (LayerIndex layer_idx = layer_begin; layer_idx < layer_end; ++layer_idx)
                    {
                        const double print_z = layer_z(slicing_params, config, layer_idx);
                        const double bottom_z = layer_idx > 0 ? layer_z(slicing_params, config, layer_idx - 1) : 0.;
                        slice_z.emplace_back(float(0.5 * (bottom_z + print_z)));
                    }
                    std::vector<Polygons> slices = slice_mesh(partial_mesh, slice_z, mesh_slicing_params,
                                                              throw_on_cancel);

                    if (slices.empty())
                        continue;

                    bottom_contacts.clear();
                    //FIXME parallelize?
                    const bool baobab_dbg = baobab && debug_enabled(DBG_BAOBAB);
                    const bool clip_dbg = baobab_dbg || debug_enabled(DBG_SUPPORT);
                    for (LayerIndex i = 0; i < LayerIndex(slices.size()); ++i)
                    {
                        const size_t holes_before = baobab_dbg ? baobab_count_holes(slices[i]) : 0;
                        const double area_before = clip_dbg ? area(slices[i]) : 0.;
                        slices[i] = diff_clipped(
                            slices[i], volumes.getCollision(
                                           0, layer_begin + i,
                                           true)); //FIXME parent_uses_min || draw_area.element->state.use_min_xy_dist);
                        // Log collision clips that remove branch area: a support column missing
                        // below its interfaces was usually removed here, not skipped upstream.
                        if (debug_enabled(DBG_SUPPORT))
                            if (const double cut = (area_before - area(slices[i])) * SCALING_FACTOR * SCALING_FACTOR;
                                cut > 1.)
                                dbg_log(DBG_SUPPORT, layer_z(slicing_params, config, size_t(layer_begin + i)),
                                        "TREE-CLIP", "layer=%d area_cut=%.1f area_left=%.1f", int(layer_begin + i), cut,
                                        area(slices[i]) * SCALING_FACTOR * SCALING_FACTOR);
                        if (baobab_dbg)
                        {
                            const LayerIndex layer = layer_begin + i;
                            if (area(slices[i]) < area_before - 100.)
                            {
                                baobab_canopy_stats.clip_layers_cut.fetch_add(1, std::memory_order_relaxed);
                                baobab_atomic_min(baobab_canopy_stats.clip_cut_layer_min, int32_t(layer));
                                baobab_atomic_max(baobab_canopy_stats.clip_cut_layer_max, int32_t(layer));
                            }
                            const size_t holes_after = baobab_count_holes(slices[i]);
                            baobab_canopy_stats.clip_pre_holes.fetch_add(holes_before, std::memory_order_relaxed);
                            baobab_canopy_stats.clip_post_holes.fetch_add(holes_after, std::memory_order_relaxed);
                            if (holes_after > holes_before)
                            {
                                baobab_canopy_stats.clip_layers_holed.fetch_add(1, std::memory_order_relaxed);
                                baobab_atomic_min(baobab_canopy_stats.clip_holed_layer_min, int32_t(layer));
                                baobab_atomic_max(baobab_canopy_stats.clip_holed_layer_max, int32_t(layer));
                            }
                        }
                    }

                    size_t num_empty = 0;
                    if (slices.front().empty())
                    {
                        // Some of the initial layers are empty.
                        num_empty = std::find_if(slices.begin(), slices.end(), [](auto &s) { return !s.empty(); }) -
                                    slices.begin();
                    }
                    else
                    {
                        if (branch.has_root)
                        {
                            if (branch.path.front()->state.to_model_gracious)
                            {
                                if (config.settings.support_floor_layers > 0)
                                    //FIXME one may just take the whole tree slice as bottom interface.
                                    bottom_contacts.emplace_back(
                                        intersection_clipped(slices.front(),
                                                             volumes.getPlaceableAreas(0, layer_begin, [] {})));
                            }
                            else if (layer_begin > 0)
                            {
                                // Drop down areas that do rest non - gracefully on the model to ensure the branch actually rests on something.
                                struct BottomExtraSlice
                                {
                                    Polygons polygons;
                                    double area;
                                };
                                std::vector<BottomExtraSlice> bottom_extra_slices;
                                Polygons rest_support;
                                coord_t bottom_radius = support_element_radius(config, *branch.path.front());
                                // Don't propagate further than 1.5 * bottom radius.
                                //LayerIndex                      layers_propagate_max = 2 * bottom_radius / config.layer_height;
                                LayerIndex layers_propagate_max = 5 * bottom_radius / config.layer_height;
                                LayerIndex layer_bottommost =
                                    branch.path.front()->state.verylost
                                        ?
                                        // If the tree bottom is hanging in the air, bring it down to some surface.
                                        0
                                        :
                                        //FIXME the "verylost" branches should stop when crossing another support.
                                        std::max(0, layer_begin - layers_propagate_max);
                                double support_area_min_radius = M_PI * sqr(double(config.branch_radius));
                                double support_area_stop = std::max(0.2 * M_PI * sqr(double(bottom_radius)),
                                                                    0.5 * support_area_min_radius);
                                // Only propagate until the rest area is smaller than this threshold.
                                //double                          support_area_min = 0.1 * support_area_min_radius;
                                for (LayerIndex layer_idx = layer_begin - 1; layer_idx >= layer_bottommost; --layer_idx)
                                {
                                    rest_support = diff_clipped(rest_support.empty() ? slices.front() : rest_support,
                                                                volumes.getCollision(0, layer_idx, false));
                                    double rest_support_area = area(rest_support);
                                    if (rest_support_area < support_area_stop)
                                        // Don't propagate a fraction of the tree contact surface.
                                        break;
                                    bottom_extra_slices.push_back({rest_support, rest_support_area});
                                }
                            // Now remove those bottom slices that are not supported at all.
#if 0
                                while (! bottom_extra_slices.empty()) {
                                    Polygons this_bottom_contacts = intersection_clipped(
                                        bottom_extra_slices.back().polygons, volumes.getPlaceableAreas(0, layer_begin - LayerIndex(bottom_extra_slices.size()), [] {}));
                                    if (area(this_bottom_contacts) < support_area_min)
                                        bottom_extra_slices.pop_back();
                                    else {
                                        // At least a fraction of the tree bottom is considered to be supported.
                                        if (config.settings.support_floor_layers > 0)
                                            // Turn this fraction of the tree bottom into a contact layer.
                                            bottom_contacts.emplace_back(std::move(this_bottom_contacts));
                                        break;
                                    }
                                }
#endif
                                if (config.settings.support_floor_layers > 0)
                                    for (int i = int(bottom_extra_slices.size()) - 2; i >= 0; --i)
                                        bottom_contacts.emplace_back(
                                            intersection_clipped(bottom_extra_slices[i].polygons,
                                                                 volumes.getPlaceableAreas(0, layer_begin - i - 1,
                                                                                           [] {})));
                                layer_begin -= LayerIndex(bottom_extra_slices.size());
                                slices.insert(slices.begin(), bottom_extra_slices.size(), {});
                                auto it_dst = slices.begin();
                                for (auto it_src = bottom_extra_slices.rbegin(); it_src != bottom_extra_slices.rend();
                                     ++it_src)
                                    *it_dst++ = std::move(it_src->polygons);
                            }
                        }

#if 0
                        //FIXME branch.has_tip seems to not be reliable.
                        if (branch.has_tip && interface_placer.support_parameters.has_top_contacts)
                            // Add top slices to top contacts / interfaces / base interfaces.
                            for (int i = int(branch.path.size()) - 1; i >= 0; -- i) {
                                const SupportElement &el = *branch.path[i];
                                if (el.state.missing_roof_layers == 0)
                                    break;
                                //FIXME Move or not?
                                interface_placer.add_roof(std::move(slices[int(slices.size()) - i - 1]), el.state.layer_idx,
                                    interface_placer.support_parameters.num_top_interface_layers + 1 - el.state.missing_roof_layers);
                            }
#endif
                    }

                    layer_begin += LayerIndex(num_empty);
                    while (!slices.empty() && slices.back().empty())
                    {
                        slices.pop_back();
                        --layer_end;
                    }
                    if (layer_begin < layer_end)
                    {
                        LayerIndex new_begin = tree.first_layer_id == -1 ? layer_begin
                                                                         : std::min(tree.first_layer_id, layer_begin);
                        LayerIndex new_end = tree.first_layer_id == -1
                                                 ? layer_end
                                                 : std::max(tree.first_layer_id + LayerIndex(tree.slices.size()),
                                                            layer_end);
                        size_t new_size = size_t(new_end - new_begin);
                        if (tree.first_layer_id == -1)
                        {
                        }
                        else if (tree.slices.capacity() < new_size)
                        {
                            std::vector<Slice> new_slices;
                            new_slices.reserve(new_size);
                            if (LayerIndex dif = tree.first_layer_id - new_begin; dif > 0)
                                new_slices.insert(new_slices.end(), dif, {});
                            append(new_slices, std::move(tree.slices));
                            tree.slices.swap(new_slices);
                        }
                        else if (LayerIndex dif = tree.first_layer_id - new_begin; dif > 0)
                            tree.slices.insert(tree.slices.begin(), tree.first_layer_id - new_begin, {});
                        tree.slices.insert(tree.slices.end(), new_size - tree.slices.size(), {});
                        layer_begin -= LayerIndex(num_empty);
                        for (LayerIndex i = layer_begin; i != layer_end; ++i)
                        {
                            int j = i - layer_begin;
                            if (Polygons &src = slices[j]; !src.empty())
                            {
                                Slice &dst = tree.slices[i - new_begin];
                                // When branch has a tip, the last layer (layer_end - 1) is the tip
                                // Only separate tips when interface_layers == 0 (separate_tips is true)
                                bool is_tip_layer = separate_tips && branch.has_tip && (i == layer_end - 1);
                                if (is_tip_layer)
                                {
                                    // Store in tip_polygons for separate handling
                                    if (dst.tip_polygons.empty())
                                        dst.tip_polygons = std::move(src);
                                    else
                                        append(dst.tip_polygons, std::move(src));
                                }
                                else
                                {
                                    if (++dst.num_branches > 1)
                                    {
                                        append(dst.polygons, std::move(src));
                                        if (j < int(bottom_contacts.size()))
                                            append(dst.bottom_contacts, std::move(bottom_contacts[j]));
                                    }
                                    else
                                    {
                                        dst.polygons = std::move(std::move(src));
                                        if (j < int(bottom_contacts.size()))
                                            dst.bottom_contacts = std::move(bottom_contacts[j]);
                                    }
                                }
                            }
                        }
                        tree.first_layer_id = new_begin;
                    }
                }
            }
        },
        tbb::simple_partitioner());
    pf_organic.stage("    organic: extrude + slice branches");

    if (baobab && debug_enabled(DBG_BAOBAB))
    {
        // taper_eff is what the bead rule allowed; a trunk's lean narrows it further, per layer.
        // Counters are atomic and summed once, so the lines are deterministic and diffable.
        const BaobabCanopyStats &st = baobab_canopy_stats;
        const size_t rings_holed = st.rings_holed.load(std::memory_order_relaxed);
        const int64_t reach_min_cmm = st.reach_sized_min_cmm.load(std::memory_order_relaxed);
        const int64_t reach_max_cmm = st.reach_sized_max_cmm.load(std::memory_order_relaxed);
        dbg_log(DBG_BAOBAB, 0., "BAOBAB-CANOPY",
                "taper_set=%.1f taper_eff=%.1f growth_budget=%.4f reach_cap=%.3f reach_eff=[%.2f..%.2f] canopies=%zu "
                "canopies_truncated=%zu "
                "deepest_layers=%zu regions_from_overhang=%zu region_holes=%zu ring_holes=%zu rings_holed=%zu "
                "holed_depths=[%lld..%lld] rej_no_interface=%zu rej_trunk_outside_region=%zu",
                config.settings.support_baobab_max_canopy_angle * 180. / M_PI, baobab_taper * 180. / M_PI,
                baobab_growth_budget, unscaled<double>(config.settings.support_tree_branch_distance),
                reach_max_cmm >= 0 ? double(reach_min_cmm) / 100. : -1.,
                reach_max_cmm >= 0 ? double(reach_max_cmm) / 100. : -1.,
                st.canopies_built.load(std::memory_order_relaxed),
                st.canopies_truncated.load(std::memory_order_relaxed),
                st.deepest_canopy_layers.load(std::memory_order_relaxed),
                st.regions_from_overhang.load(std::memory_order_relaxed),
                st.region_holes.load(std::memory_order_relaxed), st.ring_holes.load(std::memory_order_relaxed),
                rings_holed, rings_holed ? (long long) st.ring_holed_d_min.load(std::memory_order_relaxed) : -1LL,
                rings_holed ? (long long) st.ring_holed_d_max.load(std::memory_order_relaxed) : -1LL,
                st.reject_no_interface.load(std::memory_order_relaxed),
                st.reject_trunk_outside_region.load(std::memory_order_relaxed));
        std::sort(baobab_rejects.begin(), baobab_rejects.end(),
                  [](const BaobabCanopyReject &a, const BaobabCanopyReject &b)
                  {
                      if (a.tip_layer != b.tip_layer)
                          return a.tip_layer < b.tip_layer;
                      return a.x != b.x ? a.x < b.x : a.y < b.y;
                  });
        for (const BaobabCanopyReject &reject : baobab_rejects)
            dbg_log(DBG_BAOBAB, 0., "BAOBAB-CANOPY-REJ",
                    "tip_layer=%d x=%.2f y=%.2f built=%zu reason=%s depth=%zu reach_left=%.2f trunk_r=%.2f "
                    "travel=%.2f net=%.2f reach0=%.2f",
                    int(reject.tip_layer), reject.x, reject.y, reject.outcome.built, reject.outcome.reason,
                    reject.outcome.depth, reject.outcome.reach_left, reject.outcome.trunk_radius, reject.outcome.travel,
                    reject.outcome.net, reject.outcome.reach0);
        const size_t layers_cut = st.clip_layers_cut.load(std::memory_order_relaxed);
        const size_t layers_holed = st.clip_layers_holed.load(std::memory_order_relaxed);
        dbg_log(DBG_BAOBAB, 0., "BAOBAB-CLIP",
                "zwin=[-%zu..+%zu] layers_cut=%zu cut_layers=[%d..%d] pre_holes=%zu post_holes=%zu "
                "layers_holed=%zu holed_layers=[%d..%d]",
                config.z_distance_bottom_layers, config.z_distance_top_layers, layers_cut,
                layers_cut ? st.clip_cut_layer_min.load(std::memory_order_relaxed) : -1,
                layers_cut ? st.clip_cut_layer_max.load(std::memory_order_relaxed) : -1,
                st.clip_pre_holes.load(std::memory_order_relaxed), st.clip_post_holes.load(std::memory_order_relaxed),
                layers_holed, layers_holed ? st.clip_holed_layer_min.load(std::memory_order_relaxed) : -1,
                layers_holed ? st.clip_holed_layer_max.load(std::memory_order_relaxed) : -1);
    }

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, trees.size(), 1),
        [&trees, &throw_on_cancel](const tbb::blocked_range<size_t> &range)
        {
            for (size_t tree_id = range.begin(); tree_id < range.end(); ++tree_id)
            {
                Tree &tree = trees[tree_id];
                for (Slice &slice : tree.slices)
                    if (slice.num_branches > 1)
                    {
                        slice.polygons = union_(slice.polygons);
                        slice.bottom_contacts = union_(slice.bottom_contacts);
                        if (!slice.tip_polygons.empty())
                            slice.tip_polygons = union_(slice.tip_polygons);
                        slice.num_branches = 1;
                    }
                throw_on_cancel();
            }
        },
        tbb::simple_partitioner());
    pf_organic.stage("    organic: union per-tree slices");

    size_t num_layers = 0;
    for (Tree &tree : trees)
        if (tree.first_layer_id >= 0)
            num_layers = std::max(num_layers, size_t(tree.first_layer_id + tree.slices.size()));

    std::vector<Slice> slices(num_layers, Slice{});
    for (Tree &tree : trees)
        if (tree.first_layer_id >= 0)
        {
            for (LayerIndex i = tree.first_layer_id; i != tree.first_layer_id + LayerIndex(tree.slices.size()); ++i)
                if (Slice &src = tree.slices[i - tree.first_layer_id];
                    !src.polygons.empty() || !src.tip_polygons.empty())
                {
                    Slice &dst = slices[i];
                    if (++dst.num_branches > 1)
                    {
                        append(dst.polygons, std::move(src.polygons));
                        append(dst.bottom_contacts, std::move(src.bottom_contacts));
                        append(dst.tip_polygons, std::move(src.tip_polygons));
                    }
                    else
                    {
                        dst.polygons = std::move(src.polygons);
                        dst.bottom_contacts = std::move(src.bottom_contacts);
                        dst.tip_polygons = std::move(src.tip_polygons);
                    }
                }
        }

    // Only route tip polygons to top_contacts when interface_layers == 0
    // When interface_layers > 0, InterfacePlacer already handles interface layers
    const bool route_tips_to_top_contacts = !interface_placer.support_parameters.has_top_contacts;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, std::min(move_bounds.size(), slices.size()), 1),
        [&print_object, &config, &slices, &bottom_contacts, &top_contacts, &intermediate_layers, &layer_storage,
         &throw_on_cancel, &excluded_areas, route_tips_to_top_contacts, baobab,
         &baobab_canopy_stats](const tbb::blocked_range<size_t> &range)
        {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx)
            {
                Slice &slice = slices[layer_idx];
                assert(intermediate_layers[layer_idx] == nullptr);
                Polygons base_layer_polygons = slice.num_branches > 1 ? union_(slice.polygons)
                                                                      : std::move(slice.polygons);
                Polygons bottom_contact_polygons = slice.num_branches > 1 ? union_(slice.bottom_contacts)
                                                                          : std::move(slice.bottom_contacts);
                Polygons tip_layer_polygons = slice.num_branches > 1 ? union_(slice.tip_polygons)
                                                                     : std::move(slice.tip_polygons);

                if (!base_layer_polygons.empty())
                {
                    // Most of the time in this function is this union call. Can take 300+ ms when a lot of areas are to be unioned.
                    base_layer_polygons = smooth_outward(union_(base_layer_polygons),
                                                         config.support_line_width); //FIXME was .smooth(50);
                    //smooth_outward(closing(std::move(bottom), closing_distance + minimum_island_radius, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance) :
                    // simplify a bit, to ensure the output does not contain outrageous amounts of vertices. Should not be necessary, just a precaution.
                    base_layer_polygons = polygons_simplify(base_layer_polygons,
                                                            std::min(scaled<double>(0.03), double(config.resolution)),
                                                            polygons_strictly_simple);

                    // Subtract excluded areas (snug/grid support regions) from organic support
                    // This prevents overlap when both support types are painted on the same model
                    if (layer_idx < excluded_areas.size() && !excluded_areas[layer_idx].empty())
                    {
                        const double area_before = debug_enabled(DBG_SUPPORT) ? area(base_layer_polygons) : 0.;
                        base_layer_polygons = diff(base_layer_polygons, excluded_areas[layer_idx]);
                        // Log base area removed by the prior-support exclusion, so a reduced
                        // column reads as excluded here rather than never generated.
                        if (debug_enabled(DBG_SUPPORT))
                            if (const double cut = (area_before - area(base_layer_polygons)) * SCALING_FACTOR *
                                                   SCALING_FACTOR;
                                cut > 1.)
                                dbg_log(DBG_SUPPORT, layer_z(print_object.slicing_parameters(), config, layer_idx),
                                        "TREE-EXCLUDE", "layer=%zu area_cut=%.1f area_left=%.1f", layer_idx, cut,
                                        area(base_layer_polygons) * SCALING_FACTOR * SCALING_FACTOR);
                    }
                    if (baobab && debug_enabled(DBG_BAOBAB))
                        baobab_canopy_stats.asm_union_holes.fetch_add(baobab_count_holes(base_layer_polygons),
                                                                      std::memory_order_relaxed);
                }
                if (!tip_layer_polygons.empty())
                {
                    tip_layer_polygons = smooth_outward(union_(tip_layer_polygons), config.support_line_width);
                    tip_layer_polygons = polygons_simplify(tip_layer_polygons,
                                                           std::min(scaled<double>(0.03), double(config.resolution)),
                                                           polygons_strictly_simple);
                    if (layer_idx < excluded_areas.size() && !excluded_areas[layer_idx].empty())
                    {
                        tip_layer_polygons = diff(tip_layer_polygons, excluded_areas[layer_idx]);
                    }
                }

                // Subtract top contact layer polygons from support base.
                SupportGeneratorLayer *top_contact_layer = top_contacts.empty() ? nullptr : top_contacts[layer_idx];
                // The contact at this index competes with the base only if their z spans overlap:
                // gap-height adjustment prints contacts with reduced heights above the base
                // layer's span, and subtracting one from the other then carves a notch that
                // nothing refills - directly under the sheet that needed the support. Compare
                // against the z the base layer at this index is given, which is the support grid,
                // not the object's own layers.
                bool contact_overlaps_base = top_contact_layer != nullptr;
                if (baobab && contact_overlaps_base)
                    contact_overlaps_base = top_contact_layer->bottom_z <
                                            layer_z(print_object.slicing_parameters(), config, layer_idx) - EPSILON;
                if (top_contact_layer && contact_overlaps_base && !top_contact_layer->polygons.empty() &&
                    !base_layer_polygons.empty())
                {
                    base_layer_polygons = diff(base_layer_polygons, top_contact_layer->polygons);
                    if (!bottom_contact_polygons.empty())
                        //FIXME it may be better to clip bottom contacts with top contacts first after they are propagated to produce interface layers.
                        bottom_contact_polygons = diff(bottom_contact_polygons, top_contact_layer->polygons);
                }
                if (!bottom_contact_polygons.empty())
                {
                    base_layer_polygons = diff(base_layer_polygons, bottom_contact_polygons);
                    SupportGeneratorLayer *bottom_contact_layer = bottom_contacts[layer_idx] =
                        &layer_allocate(layer_storage, SupporLayerType::BottomContact,
                                        print_object.slicing_parameters(), config, layer_idx);
                    bottom_contact_layer->polygons = std::move(bottom_contact_polygons);
                }
                if (!base_layer_polygons.empty())
                {
                    SupportGeneratorLayer *base_layer = intermediate_layers[layer_idx] = &layer_allocate(
                        layer_storage, SupporLayerType::Base, print_object.slicing_parameters(), config, layer_idx);
                    base_layer->polygons = union_(base_layer_polygons);
                    if (baobab && debug_enabled(DBG_BAOBAB))
                        if (const size_t h = baobab_count_holes(base_layer->polygons); h > 0)
                        {
                            baobab_canopy_stats.asm_final_holes.fetch_add(h, std::memory_order_relaxed);
                            baobab_canopy_stats.asm_layers_holed.fetch_add(1, std::memory_order_relaxed);
                            baobab_atomic_min(baobab_canopy_stats.asm_holed_layer_min, int32_t(layer_idx));
                            baobab_atomic_max(baobab_canopy_stats.asm_holed_layer_max, int32_t(layer_idx));
                        }
                }
                // This allows generate_support_toolpaths to apply width reduction to organic tips
                // Only route tips when interface_layers == 0; otherwise InterfacePlacer handles it
                if (route_tips_to_top_contacts && !tip_layer_polygons.empty() && layer_idx < top_contacts.size())
                {
                    if (top_contacts[layer_idx] == nullptr)
                    {
                        top_contacts[layer_idx] = &layer_allocate(layer_storage, SupporLayerType::TopContact,
                                                                  print_object.slicing_parameters(), config, layer_idx);
                        top_contacts[layer_idx]->polygons = std::move(tip_layer_polygons);
                    }
                    else
                    {
                        // Merge with existing top_contacts if any
                        append(top_contacts[layer_idx]->polygons, std::move(tip_layer_polygons));
                        top_contacts[layer_idx]->polygons = union_(top_contacts[layer_idx]->polygons);
                    }
                    // Subtract tip polygons from base layer to avoid overlap
                    if (intermediate_layers[layer_idx] != nullptr && !intermediate_layers[layer_idx]->polygons.empty())
                    {
                        intermediate_layers[layer_idx]->polygons = diff(intermediate_layers[layer_idx]->polygons,
                                                                        top_contacts[layer_idx]->polygons);
                    }
                }

                throw_on_cancel();
            }
        },
        tbb::simple_partitioner());
    pf_organic.stage("    organic: finalize layers (union/smooth/clip)");

    if (baobab && debug_enabled(DBG_BAOBAB))
    {
        const BaobabCanopyStats &st = baobab_canopy_stats;
        const size_t layers_holed = st.asm_layers_holed.load(std::memory_order_relaxed);
        dbg_log(DBG_BAOBAB, 0., "BAOBAB-ASSEMBLY",
                "union_holes=%zu final_holes=%zu layers_holed=%zu holed_layers=[%d..%d]",
                st.asm_union_holes.load(std::memory_order_relaxed), st.asm_final_holes.load(std::memory_order_relaxed),
                layers_holed, layers_holed ? st.asm_holed_layer_min.load(std::memory_order_relaxed) : -1,
                layers_holed ? st.asm_holed_layer_max.load(std::memory_order_relaxed) : -1);
    }
}

} // namespace FFFTreeSupport

} // namespace Slic3r
