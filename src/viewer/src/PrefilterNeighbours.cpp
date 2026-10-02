///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "PrefilterNeighbours.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace libvgcode
{

namespace
{

// Grid cell size (mm)
constexpr double CELL = 1.0;
// Search radius: the bead width times the factor, clamped to the bounds (mm)
constexpr double RADIUS_FACTOR = 1.5;
constexpr double RADIUS_MIN = 0.1;
constexpr double RADIUS_MAX = 5.0;
// A wall segment of the same layer occupies a side of a segment from this fraction of its width across; nearer, it is
// the bead itself
constexpr double SAME_BEAD_FACTOR = 0.25;
// Two layers are adjacent when one's bead bottom lies within this fraction of the smaller layer height of the
// other's bead top
constexpr double LAYER_TOLERANCE = 0.25;
// A miter is at most this many times the larger of its two offsets; a vertex offset shorter than the dead zone (mm)
// is 0
constexpr double MITER_LIMIT = 2.0;
constexpr double DEAD_ZONE = 0.005;
// Segment length the direction is divided by when the segment is shorter (mm)
constexpr double MIN_LENGTH = 1e-9;
// Segments with an end farther than this from the origin (mm) are not searched, which keeps the cell indices small
constexpr double COORDINATE_LIMIT = 1e6;
constexpr double PI_D = 3.14159265358979323846;
// Candidates run within 30 degrees of the searching segment; two segments turning by less than 20 degrees at a
// vertex average their offsets instead of solving the ill-conditioned miter
const double COS_MAX_ANGLE = std::cos(30.0 * (PI_D / 180.0));
const double SIN_MITER_TURN = std::sin(20.0 * (PI_D / 180.0));
// Smallest change of the searched fraction reported to the progress callback
constexpr double PROGRESS_STEP = 0.01;

constexpr uint32_t NO_SEGMENT = std::numeric_limits<uint32_t>::max();

// A wall segment: what the search reads on every visit. The ends, the midpoint and the end vertex's z, height and
// width are read from the vertices when needed, in the same double arithmetic.
struct Segment
{
    double len{0.0};         // length, at least MIN_LENGTH
    double ux{0.0}, uy{0.0}; // direction, (dx, dy) / len
    uint32_t layer{0};
    uint32_t start{0}; // vertex index of the start
};

// The start, the end and the difference of segment s in double, as the search computes with them
struct SegmentEnds
{
    double ax, ay, bx, by, dx, dy;
};

SegmentEnds ends_of(const PrefilterVertexView &vertices, const Segment &s)
{
    SegmentEnds e;
    e.ax = vertices.x(s.start);
    e.ay = vertices.y(s.start);
    e.bx = vertices.x(s.start + 1);
    e.by = vertices.y(s.start + 1);
    e.dx = e.bx - e.ax;
    e.dy = e.by - e.ay;
    return e;
}

// Wall segments of one layer by the grid cells they cross: the occupied cells ordered by x index, then y index,
// each with its segments in ascending order
struct LayerGrid
{
    std::vector<uint64_t> keys;
    std::vector<uint32_t> first; // per cell, its first entry in segments; one more at the end
    std::vector<uint32_t> segments;
};

bool usable_position(const PrefilterVertexView &vertices, size_t i)
{
    const float x = vertices.x(i);
    const float y = vertices.y(i);
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= COORDINATE_LIMIT && std::abs(y) <= COORDINATE_LIMIT;
}

int64_t cell_index(double v)
{
    return static_cast<int64_t>(std::floor(v / CELL));
}

// Orders the cells by x index, then y index; the indices stay within +-(COORDINATE_LIMIT / CELL + RADIUS_MAX)
uint64_t cell_key(int64_t ix, int64_t iy)
{
    constexpr int64_t BIAS = int64_t(1) << 31;
    return (static_cast<uint64_t>(ix + BIAS) << 32) | static_cast<uint64_t>(iy + BIAS);
}

// Runs work(i) for every i in [0, count) on up to `threads` threads, the calling thread included; each i runs
// exactly once. A thread that fails to start leaves its share to the others. After each of its own calls the
// calling thread calls step(taken), the count of i taken so far. Rethrows the first exception a call threw. Returns
// the threads that ran, the calling thread included.
template<class Work, class Step>
unsigned parallel_for(size_t count, unsigned threads, const Work &work, const Step &step)
{
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto worker = [&](bool calling)
    {
        for (size_t i = next.fetch_add(1); i < count && !failed.load(); i = next.fetch_add(1))
        {
            try
            {
                work(i);
                if (calling)
                    step(std::min(next.load(), count));
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!error)
                    error = std::current_exception();
                failed = true;
            }
        }
    };

    const size_t helpers = std::min<size_t>(std::max(threads, 1u), count) - (count > 0 ? 1 : 0);
    std::vector<std::thread> pool;
    pool.reserve(helpers);
    for (size_t t = 0; t < helpers; ++t)
    {
        try
        {
            pool.emplace_back(worker, false);
        }
        catch (...)
        {
            break;
        }
    }
    worker(true);
    for (std::thread &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
    return static_cast<unsigned>(pool.size()) + 1;
}

// The grid of one layer's wall segments; a segment is entered in every cell its points, sampled every half cell
// from start to end, fall in
void build_grid(const std::vector<Segment> &segs, const uint32_t *layer_segs, size_t layer_segs_count,
                const PrefilterVertexView &vertices, LayerGrid &grid)
{
    std::vector<std::pair<uint64_t, uint32_t>> entries;
    for (size_t i = 0; i < layer_segs_count; ++i)
    {
        const uint32_t si = layer_segs[i];
        const SegmentEnds e = ends_of(vertices, segs[si]);
        const double samples = std::ceil(std::hypot(e.bx - e.ax, e.by - e.ay) / (0.5 * CELL));
        const int64_t n = std::max<int64_t>(1, static_cast<int64_t>(samples));
        bool has_last = false;
        uint64_t last = 0;
        for (int64_t k = 0; k <= n; ++k)
        {
            const double t = double(k) / double(n);
            const uint64_t key = cell_key(cell_index(e.ax + t * (e.bx - e.ax)), cell_index(e.ay + t * (e.by - e.ay)));
            if (!has_last || key != last)
            {
                entries.emplace_back(key, si);
                last = key;
                has_last = true;
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    grid.keys.clear();
    grid.first.clear();
    grid.segments.resize(entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (grid.keys.empty() || grid.keys.back() != entries[i].first)
        {
            grid.keys.push_back(entries[i].first);
            grid.first.push_back(static_cast<uint32_t>(i));
        }
        grid.segments[i] = entries[i].second;
    }
    grid.first.push_back(static_cast<uint32_t>(entries.size()));
}

// A segment as its candidates are measured from it: its midpoint, direction, right, width, search radius, and the
// cell of its midpoint with the cells within the radius around it
struct Probe
{
    double mx, my;
    double ux, uy;
    double rx, ry;
    double width;
    double radius;
    int64_t cx, cy, k;
};

Probe probe_of(const PrefilterVertexView &vertices, const Segment &s)
{
    const SegmentEnds se = ends_of(vertices, s);
    Probe p;
    p.mx = 0.5 * (se.ax + se.bx);
    p.my = 0.5 * (se.ay + se.by);
    p.ux = s.ux;
    p.uy = s.uy;
    p.rx = s.uy;
    p.ry = -s.ux;
    p.width = vertices.width(s.start + 1);
    p.radius = std::min(std::max(RADIUS_FACTOR * p.width, RADIUS_MIN), RADIUS_MAX);
    p.k = static_cast<int64_t>(std::ceil(p.radius / CELL));
    p.cx = cell_index(p.mx);
    p.cy = cell_index(p.my);
    return p;
}

// Calls visit(ci, qx, qy, dist, along) for every candidate ci of the probe among the grid's segments: a segment
// running within 30 degrees of it whose closest point to the midpoint lies inside the search radius and no farther
// than the width along it. (qx, qy) is that point minus the midpoint, dist its length and along its component along
// the probe's direction. Visits cell by cell and in ascending order within a cell; a segment entered in several cells
// is visited once per cell.
template<class Visit>
void visit_candidates(const std::vector<Segment> &segs, const LayerGrid &grid, const PrefilterVertexView &vertices,
                      const Probe &p, const Visit &visit)
{
    if (grid.keys.empty())
        return;
    for (int64_t ix = p.cx - p.k; ix <= p.cx + p.k; ++ix)
    {
        const uint64_t row_last = cell_key(ix, p.cy + p.k);
        for (auto it = std::lower_bound(grid.keys.begin(), grid.keys.end(), cell_key(ix, p.cy - p.k));
             it != grid.keys.end() && *it <= row_last; ++it)
        {
            const size_t cell = static_cast<size_t>(it - grid.keys.begin());
            for (uint32_t j = grid.first[cell]; j < grid.first[cell + 1]; ++j)
            {
                const uint32_t ci = grid.segments[j];
                const Segment &c = segs[ci];
                if (std::abs(c.ux * p.ux + c.uy * p.uy) < COS_MAX_ANGLE)
                    continue;
                const SegmentEnds ce = ends_of(vertices, c);
                const double t = ((p.mx - ce.ax) * ce.dx + (p.my - ce.ay) * ce.dy) / (c.len * c.len);
                const double tc = std::min(1.0, std::max(0.0, t));
                const double qx = ce.ax + tc * ce.dx - p.mx;
                const double qy = ce.ay + tc * ce.dy - p.my;
                const double dist = std::hypot(qx, qy);
                const double along = qx * p.ux + qy * p.uy;
                if (std::abs(along) > p.width || dist >= p.radius)
                    continue;
                visit(ci, qx, qy, dist, along);
            }
        }
    }
}

// The sides of segment si that no other wall segment of its layer (the grid's) occupies: PREFILTER_OPEN_RIGHT and
// PREFILTER_OPEN_LEFT. A candidate occupies the side its closest point lies on when that point lies at least
// SAME_BEAD_FACTOR of the width across and no farther along than across: the next segments of the segment's own bead
// around a bend lie ahead of it.
uint8_t open_sides(const std::vector<Segment> &segs, const LayerGrid &grid, const PrefilterVertexView &vertices,
                   uint32_t si)
{
    const Probe p = probe_of(vertices, segs[si]);
    bool right = false;
    bool left = false;
    visit_candidates(segs, grid, vertices, p,
                     [&](uint32_t ci, double qx, double qy, double, double along)
                     {
                         const double offset = qx * p.rx + qy * p.ry;
                         if (ci == si || std::abs(offset) < SAME_BEAD_FACTOR * p.width ||
                             std::abs(along) > std::abs(offset))
                             return;
                         if (offset > 0.0)
                             right = true;
                         else
                             left = true;
                     });
    uint8_t open = 0;
    if (!right)
        open |= PREFILTER_OPEN_RIGHT;
    if (!left)
        open |= PREFILTER_OPEN_LEFT;
    return open;
}

// Whether candidate c continues the surface of segment s, from the open sides of each: a segment with no open side
// takes a candidate with none, a segment open on both sides one open on either, and a segment open on one side one
// with an open side whose outward normal points the same way (a candidate may run either way)
bool continues_surface(const Segment &s, uint8_t s_open, const Segment &c, uint8_t c_open)
{
    if (s_open == 0)
        return c_open == 0;
    if (s_open == (PREFILTER_OPEN_RIGHT | PREFILTER_OPEN_LEFT))
        return c_open != 0;
    const double nx = s_open == PREFILTER_OPEN_RIGHT ? s.uy : -s.uy;
    const double ny = s_open == PREFILTER_OPEN_RIGHT ? -s.ux : s.ux;
    if ((c_open & PREFILTER_OPEN_RIGHT) != 0 && c.uy * nx + -c.ux * ny > 0.0)
        return true;
    return (c_open & PREFILTER_OPEN_LEFT) != 0 && -c.uy * nx + c.ux * ny > 0.0;
}

// A segment's nearest candidate of any kind and its nearest candidate that continues its surface, each as the signed
// offset along the segment's right to the candidate's closest point
struct Nearest
{
    bool any{false};
    double any_offset{0.0};
    bool surface{false};
    double surface_offset{0.0};
};

// The nearest candidates of segment si among the wall segments of the given layers. Candidates are visited layer by
// layer, cell by cell and in ascending order, and only a strictly nearer one replaces a best, so a tie keeps the first.
Nearest search(const std::vector<Segment> &segs, const std::vector<uint8_t> &open, const std::vector<LayerGrid> &grids,
               const std::vector<uint32_t> &layers, const PrefilterVertexView &vertices, uint32_t si)
{
    const Segment &s = segs[si];
    const Probe p = probe_of(vertices, s);
    Nearest nearest;
    double any_dist = p.radius;
    double surface_dist = p.radius;
    for (const uint32_t li : layers)
    {
        visit_candidates(segs, grids[li], vertices, p,
                         [&](uint32_t ci, double qx, double qy, double dist, double)
                         {
                             const double offset = qx * p.rx + qy * p.ry;
                             if (dist < any_dist)
                             {
                                 nearest.any = true;
                                 nearest.any_offset = offset;
                                 any_dist = dist;
                             }
                             if (dist < surface_dist && continues_surface(s, open[si], segs[ci], open[ci]))
                             {
                                 nearest.surface = true;
                                 nearest.surface_offset = offset;
                                 surface_dist = dist;
                             }
                         });
    }
    return nearest;
}

// The offset of vertex v from its adjacent wall segments (the incoming one first), written into data with its flags
void write_vertex(const std::vector<Segment> &segs, const std::vector<double> &delta,
                  const std::vector<uint8_t> &has_delta, const uint32_t *adjacent, int adjacent_count, size_t v,
                  PrefilterNeighbourData &data)
{
    uint8_t flags = PREFILTER_FLAG_WALL;
    uint32_t used[2];
    int used_count = 0;
    for (int i = 0; i < adjacent_count; ++i)
        if (has_delta[adjacent[i]] != 0)
            used[used_count++] = adjacent[i];
    if (used_count > 0)
        flags |= PREFILTER_FLAG_FOUND;

    double mx = 0.0;
    double my = 0.0;
    if (used_count == 2)
    {
        const Segment &a = segs[used[0]];
        const Segment &b = segs[used[1]];
        const double da = delta[used[0]];
        const double db = delta[used[1]];
        const double rxa = a.uy;
        const double rya = -a.ux;
        const double rxb = b.uy;
        const double ryb = -b.ux;
        const double det = rxa * ryb - rya * rxb;
        if (std::abs(det) < SIN_MITER_TURN)
        {
            mx = 0.5 * (da * rxa + db * rxb);
            my = 0.5 * (da * rya + db * ryb);
        }
        else
        {
            // The point at offset da from segment a and db from segment b: [ra; rb] m = [da; db]
            mx = (da * ryb - rya * db) / det;
            my = (rxa * db - da * rxb) / det;
            const double limit = MITER_LIMIT * std::max(std::abs(da), std::abs(db));
            const double length = std::hypot(mx, my);
            if (length > limit && length > 0.0)
            {
                mx = mx * limit / length;
                my = my * limit / length;
            }
        }
    }
    else if (used_count == 1)
    {
        const Segment &a = segs[used[0]];
        const double da = delta[used[0]];
        mx = da * a.uy;
        my = da * -a.ux;
    }
    const double length = std::hypot(mx, my);
    if (length > 0.0 && length < DEAD_ZONE)
    {
        mx = 0.0;
        my = 0.0;
    }
    data.offset_x[v] = static_cast<float>(mx);
    data.offset_y[v] = static_cast<float>(my);
    data.flags[v] = flags;
    ++data.wall_vertices;
    if ((flags & PREFILTER_FLAG_FOUND) != 0)
        ++data.found_vertices;
}

// The wall segments of the vertices (at least two), ascending by start vertex; a segment with an end off the usable
// range or a non-finite end attribute is left out and counted in `excluded`. `layers_count` is one more than the
// highest layer id of a wall segment.
std::vector<Segment> collect_segments(const PrefilterVertexView &vertices, const std::vector<uint8_t> &wall_segment,
                                      size_t &layers_count, size_t &excluded)
{
    std::vector<Segment> segs;
    layers_count = 0;
    const size_t starts = std::min(wall_segment.size(), vertices.count - 1);
    // Reserved up front: growing by reallocation would hold the old and the new buffer at once
    segs.reserve(size_t(
        std::count_if(wall_segment.begin(), wall_segment.begin() + starts, [](uint8_t wall) { return wall != 0; })));
    for (size_t k = 0; k < starts; ++k)
    {
        if (wall_segment[k] == 0)
            continue;
        if (!usable_position(vertices, k) || !usable_position(vertices, k + 1) || !std::isfinite(vertices.z(k + 1)) ||
            !std::isfinite(vertices.height(k + 1)) || !std::isfinite(vertices.width(k + 1)))
        {
            ++excluded;
            continue;
        }
        Segment s;
        s.start = static_cast<uint32_t>(k);
        const SegmentEnds e = ends_of(vertices, s);
        s.len = std::hypot(e.dx, e.dy);
        if (s.len < MIN_LENGTH)
            s.len = MIN_LENGTH;
        s.ux = e.dx / s.len;
        s.uy = e.dy / s.len;
        s.layer = vertices.layer_id(k + 1);
        segs.push_back(s);
        layers_count = std::max(layers_count, size_t(s.layer) + 1);
    }
    return segs;
}

// Each layer's wall segments in ascending order, and the layers that have any
struct LayerSegments
{
    std::vector<uint32_t> first; // per layer, its first entry in segments; one more at the end
    std::vector<uint32_t> segments;
    std::vector<uint32_t> wall_layers;
};

LayerSegments layer_segments(const std::vector<Segment> &segs, size_t layers_count)
{
    LayerSegments by_layer;
    by_layer.first.assign(layers_count + 1, 0);
    for (const Segment &s : segs)
        ++by_layer.first[s.layer + 1];
    for (size_t l = 0; l < layers_count; ++l)
        by_layer.first[l + 1] += by_layer.first[l];
    by_layer.segments.resize(segs.size());
    {
        std::vector<uint32_t> fill(by_layer.first.begin(), by_layer.first.end() - 1);
        for (size_t i = 0; i < segs.size(); ++i)
            by_layer.segments[fill[segs[i].layer]++] = static_cast<uint32_t>(i);
    }
    for (size_t l = 0; l < layers_count; ++l)
        if (by_layer.first[l + 1] > by_layer.first[l])
            by_layer.wall_layers.push_back(static_cast<uint32_t>(l));
    return by_layer;
}

// The threads a pass runs on: `threads`, or the hardware concurrency when it is 0
unsigned thread_count_of(unsigned threads)
{
    return threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency());
}

// Every layer's grid, built on `threads` threads; a layer without wall segments keeps an empty one
std::vector<LayerGrid> build_grids(const std::vector<Segment> &segs, const LayerSegments &by_layer,
                                   const PrefilterVertexView &vertices, unsigned threads)
{
    std::vector<LayerGrid> grids(by_layer.first.size() - 1);
    parallel_for(
        by_layer.wall_layers.size(), threads,
        [&](size_t i)
        {
            const uint32_t l = by_layer.wall_layers[i];
            build_grid(segs, by_layer.segments.data() + by_layer.first[l], by_layer.first[l + 1] - by_layer.first[l],
                       vertices, grids[l]);
        },
        [](size_t) {});
    return grids;
}

// Every wall segment's open sides, indexed like segs, on `threads` threads. A layer's segments are written only by the
// thread that takes that layer.
std::vector<uint8_t> open_sides_by_segment(const std::vector<Segment> &segs, const LayerSegments &by_layer,
                                           const std::vector<LayerGrid> &grids, const PrefilterVertexView &vertices,
                                           unsigned threads)
{
    std::vector<uint8_t> open(segs.size(), 0);
    parallel_for(
        by_layer.wall_layers.size(), threads,
        [&](size_t i)
        {
            const uint32_t l = by_layer.wall_layers[i];
            for (uint32_t j = by_layer.first[l]; j < by_layer.first[l + 1]; ++j)
            {
                const uint32_t si = by_layer.segments[j];
                open[si] = open_sides(segs, grids[l], vertices, si);
            }
        },
        [](size_t) {});
    return open;
}

} // namespace

PrefilterVertexView prefilter_vertex_view(const std::vector<PrefilterVertex> &vertices)
{
    PrefilterVertexView view;
    view.base = reinterpret_cast<const unsigned char *>(vertices.data());
    view.count = vertices.size();
    view.stride = sizeof(PrefilterVertex);
    view.x_offset = offsetof(PrefilterVertex, x);
    view.y_offset = offsetof(PrefilterVertex, y);
    view.z_offset = offsetof(PrefilterVertex, z);
    view.height_offset = offsetof(PrefilterVertex, height);
    view.width_offset = offsetof(PrefilterVertex, width);
    view.layer_id_offset = offsetof(PrefilterVertex, layer_id);
    return view;
}

PrefilterNeighbourData compute_prefilter_neighbours(const std::vector<PrefilterVertex> &vertices,
                                                    const std::vector<uint8_t> &wall_segment, unsigned threads,
                                                    const std::function<void(float)> &progress)
{
    return compute_prefilter_neighbours(prefilter_vertex_view(vertices), wall_segment, threads, progress);
}

PrefilterNeighbourData compute_prefilter_neighbours(const PrefilterVertexView &vertices,
                                                    const std::vector<uint8_t> &wall_segment, unsigned threads,
                                                    const std::function<void(float)> &progress)
{
    PrefilterNeighbourData data;
    const size_t n = vertices.count;
    if (n < 2 || n >= size_t(NO_SEGMENT))
    {
        data.offset_x.assign(n, 0.0f);
        data.offset_y.assign(n, 0.0f);
        data.flags.assign(n, 0);
        return data;
    }

    size_t layers_count = 0;
    const std::vector<Segment> segs = collect_segments(vertices, wall_segment, layers_count, data.excluded_segments);

    // Per segment: the offset to the bead above, else minus the offset to the bead below
    std::vector<double> delta(segs.size(), 0.0);
    std::vector<uint8_t> has_delta(segs.size(), 0);
    if (!segs.empty())
    {
        const LayerSegments by_layer = layer_segments(segs, layers_count);

        // Each layer's top: the bead top of its highest wall segment, and that segment's height. On a tie the smaller
        // height, the layer step: a taller bead at the same top (an overlapping overhang wall) reaches into the layer
        // below.
        std::vector<double> layer_top(layers_count, -std::numeric_limits<double>::infinity());
        std::vector<double> layer_height(layers_count, 0.0);
        for (const Segment &s : segs)
        {
            const double z = vertices.z(s.start + 1);
            const double height = vertices.height(s.start + 1);
            if (z > layer_top[s.layer] || (z == layer_top[s.layer] && height < layer_height[s.layer]))
            {
                layer_top[s.layer] = z;
                layer_height[s.layer] = height;
            }
        }

        // The layers above (their bead bottom near this bead top) and below (their bead top near this bead bottom),
        // ascending by id. The tolerance is at most a quarter of this layer's height, so the candidates come from a
        // window of half its height around the matching z.
        std::vector<std::vector<uint32_t>> above(layers_count);
        std::vector<std::vector<uint32_t>> below(layers_count);
        {
            std::vector<std::pair<double, uint32_t>> by_bottom;
            std::vector<std::pair<double, uint32_t>> by_top;
            for (const uint32_t m : by_layer.wall_layers)
            {
                by_bottom.emplace_back(layer_top[m] - layer_height[m], m);
                by_top.emplace_back(layer_top[m], m);
            }
            std::sort(by_bottom.begin(), by_bottom.end());
            std::sort(by_top.begin(), by_top.end());
            for (const uint32_t l : by_layer.wall_layers)
            {
                const double tl = layer_top[l];
                const double hl = layer_height[l];
                if (!(hl > 0.0))
                    continue;
                const double reach = 0.5 * hl;
                const auto first_above = std::make_pair(tl - reach, uint32_t(0));
                for (auto it = std::lower_bound(by_bottom.begin(), by_bottom.end(), first_above);
                     it != by_bottom.end() && it->first <= tl + reach; ++it)
                {
                    const uint32_t m = it->second;
                    if (std::abs((layer_top[m] - layer_height[m]) - tl) <
                        LAYER_TOLERANCE * std::min(hl, layer_height[m]))
                        above[l].push_back(m);
                }
                const double bottom = tl - hl;
                const auto first_below = std::make_pair(bottom - reach, uint32_t(0));
                for (auto it = std::lower_bound(by_top.begin(), by_top.end(), first_below);
                     it != by_top.end() && it->first <= bottom + reach; ++it)
                {
                    const uint32_t m = it->second;
                    if (std::abs(layer_top[m] - (tl - hl)) < LAYER_TOLERANCE * std::min(hl, layer_height[m]))
                        below[l].push_back(m);
                }
                std::sort(above[l].begin(), above[l].end());
                std::sort(below[l].begin(), below[l].end());
            }
        }

        const unsigned thread_count = thread_count_of(threads);

        // Every layer's grid, then every segment's open sides, before any search reads them: a search reads the open
        // sides of other layers' segments
        const std::vector<LayerGrid> grids = build_grids(segs, by_layer, vertices, thread_count);
        const std::vector<uint8_t> open = open_sides_by_segment(segs, by_layer, grids, vertices, thread_count);

        // A layer's segments are written only by the thread searching that layer, and its unmatched segments are
        // added once. The grids, the open sides, the layer lists and the other per-layer data are released when this
        // block ends, before the per-vertex result is allocated.
        std::atomic<size_t> unmatched{0};
        double reported = 0.0;
        data.search_threads = parallel_for(
            by_layer.wall_layers.size(), thread_count,
            [&](size_t i)
            {
                const uint32_t l = by_layer.wall_layers[i];
                size_t layer_unmatched = 0;
                for (uint32_t j = by_layer.first[l]; j < by_layer.first[l + 1]; ++j)
                {
                    const uint32_t si = by_layer.segments[j];
                    // The nearest bead that continues the surface above, else below; else the nearest bead of any
                    // kind above, else below, counted as unmatched
                    const Nearest up = search(segs, open, grids, above[l], vertices, si);
                    const Nearest down = up.surface ? Nearest() : search(segs, open, grids, below[l], vertices, si);
                    if (up.surface)
                        delta[si] = up.surface_offset;
                    else if (down.surface)
                        delta[si] = -down.surface_offset;
                    else if (up.any)
                        delta[si] = up.any_offset;
                    else if (down.any)
                        delta[si] = -down.any_offset;
                    else
                        continue;
                    has_delta[si] = 1;
                    if (!up.surface && !down.surface)
                        ++layer_unmatched;
                }
                unmatched += layer_unmatched;
            },
            [&](size_t taken)
            {
                const double fraction = double(taken) / double(by_layer.wall_layers.size());
                if (progress && fraction - reported >= PROGRESS_STEP)
                {
                    reported = fraction;
                    progress(static_cast<float>(fraction));
                }
            });
        data.unmatched_segments = unmatched.load();
    }
    if (progress)
        progress(1.0f);

    // Per wall vertex: its incoming and outgoing wall segments. The segments ascend by start vertex, so a vertex's
    // incoming segment is the one before its outgoing one when it ends there, and a vertex that no segment starts at
    // is written with the segment that ends there.
    data.offset_x.assign(n, 0.0f);
    data.offset_y.assign(n, 0.0f);
    data.flags.assign(n, 0);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const size_t k = segs[i].start;
        uint32_t adjacent[2];
        int adjacent_count = 0;
        if (i > 0 && size_t(segs[i - 1].start) + 1 == k)
            adjacent[adjacent_count++] = static_cast<uint32_t>(i - 1);
        adjacent[adjacent_count++] = static_cast<uint32_t>(i);
        write_vertex(segs, delta, has_delta, adjacent, adjacent_count, k, data);
        if (i + 1 == segs.size() || size_t(segs[i + 1].start) != k + 1)
        {
            const uint32_t incoming = static_cast<uint32_t>(i);
            write_vertex(segs, delta, has_delta, &incoming, 1, k + 1, data);
        }
    }
    return data;
}

std::vector<uint8_t> compute_prefilter_open_sides(const std::vector<PrefilterVertex> &vertices,
                                                  const std::vector<uint8_t> &wall_segment, unsigned threads)
{
    return compute_prefilter_open_sides(prefilter_vertex_view(vertices), wall_segment, threads);
}

std::vector<uint8_t> compute_prefilter_open_sides(const PrefilterVertexView &vertices,
                                                  const std::vector<uint8_t> &wall_segment, unsigned threads)
{
    const size_t n = vertices.count;
    std::vector<uint8_t> result(n, 0);
    if (n < 2 || n >= size_t(NO_SEGMENT))
        return result;

    size_t layers_count = 0;
    size_t excluded = 0;
    const std::vector<Segment> segs = collect_segments(vertices, wall_segment, layers_count, excluded);
    if (segs.empty())
        return result;
    const unsigned thread_count = thread_count_of(threads);
    const LayerSegments by_layer = layer_segments(segs, layers_count);
    const std::vector<LayerGrid> grids = build_grids(segs, by_layer, vertices, thread_count);
    const std::vector<uint8_t> open = open_sides_by_segment(segs, by_layer, grids, vertices, thread_count);
    for (size_t i = 0; i < segs.size(); ++i)
        result[segs[i].start] = open[i];
    return result;
}

} // namespace libvgcode
