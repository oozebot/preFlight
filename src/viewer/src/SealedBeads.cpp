///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "SealedBeads.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <thread>
#include <utility>

namespace libvgcode
{

namespace
{

// Air margin around the extrusions' XY bounds, besides half the widest bead (mm)
constexpr double MARGIN = 2.0;
// The raster origin lies this fraction of a pixel below the margin, so input on a round millimetre grid does not put
// probes on pixel edges or bead edges through pixel centres, where rounding would decide
constexpr double ORIGIN_SHIFT = 0.37;
// Smallest distance between two probe samples along a segment (mm); a coarser raster samples at its resolution
constexpr double SAMPLE_STEP = 0.25;
// Side probes at 0.5 w + k * resolution for k = 1..SIDE_PROBES; with exact capsule coverage and a half pixel margin
// the second and third pixels past a bead edge are past the rasterized bead
constexpr int SIDE_PROBES = 3;
// Probes in the layers above and below, across the segment, as fractions of its width
constexpr double ACROSS[3] = {-0.4, 0.0, 0.4};
// Segments with an end farther than this from the origin (mm) are not classified
constexpr double COORDINATE_LIMIT = 1e6;
// Segments wider than this (mm) are not classified, so a corrupt width cannot cover the whole print
constexpr double WIDTH_LIMIT = 20.0;
// Segments shorter than this (mm) are not classified
constexpr double MIN_LENGTH = 1e-9;
// Widths sampled for the resolution's width percentile, at most
constexpr size_t WIDTH_SAMPLES = 65536;
// Smallest change of the fraction done reported to the progress callback
constexpr double PROGRESS_STEP = 0.01;
// Layer bands per thread: more bands balance uneven layers, each band relabels two extra layers per pass
constexpr unsigned BANDS_PER_THREAD = 4;

constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
// The global id of the outside air; every region joined to it is outside
constexpr uint32_t OUTSIDE = 0;

// Pixel (c, r) covers [ox + c * res, ox + (c + 1) * res) x [oy + r * res, oy + (r + 1) * res)
struct Raster
{
    double ox{0.0}, oy{0.0};
    double res{0.1};
    int32_t cols{0}, rows{0};

    int32_t col_of(double x) const
    {
        const double c = std::floor((x - ox) / res);
        return static_cast<int32_t>(std::min(std::max(c, 0.0), double(cols - 1)));
    }
    int32_t row_of(double y) const
    {
        const double r = std::floor((y - oy) / res);
        return static_cast<int32_t>(std::min(std::max(r, 0.0), double(rows - 1)));
    }
};

// One bit per pixel, row by row; the bits past the last column stay 0
struct BitRows
{
    int32_t words{0}; // per row
    std::vector<uint64_t> bits;

    void reset(int32_t rows, int32_t cols)
    {
        words = (cols + 63) / 64;
        bits.assign(size_t(rows) * size_t(words), 0);
    }
    uint64_t *row(int32_t r) { return bits.data() + size_t(r) * size_t(words); }
    const uint64_t *row(int32_t r) const { return bits.data() + size_t(r) * size_t(words); }
    bool test(int32_t r, int32_t c) const { return ((row(r)[c >> 6] >> (c & 63)) & 1u) != 0; }
    // Sets columns [c0, c1] of row r
    void set_range(int32_t r, int32_t c0, int32_t c1)
    {
        uint64_t *w = row(r);
        const int32_t w0 = c0 >> 6;
        const int32_t w1 = c1 >> 6;
        const uint64_t first = ~uint64_t(0) << (c0 & 63);
        const uint64_t last = ~uint64_t(0) >> (63 - (c1 & 63));
        if (w0 == w1)
        {
            w[w0] |= first & last;
            return;
        }
        w[w0] |= first;
        for (int32_t i = w0 + 1; i < w1; ++i)
            w[i] = ~uint64_t(0);
        w[w1] |= last;
    }
};

// The first column at or after c whose bit is `one` in a row of `words` words, cols when there is none
int32_t next_bit(const uint64_t *row, int32_t words, int32_t cols, int32_t c, bool one)
{
    int32_t w = c >> 6;
    uint64_t v = (one ? row[w] : ~row[w]) & (~uint64_t(0) << (c & 63));
    for (;;)
    {
        if (v != 0)
            return std::min(cols, w * 64 + std::countr_zero(v));
        if (++w >= words)
            return cols;
        v = one ? row[w] : ~row[w];
    }
}

// A run of air pixels in a row and its region, numbered within the layer
struct Run
{
    int32_t c0, c1;
    uint32_t region;
};

// One layer's solid pixels and its air as runs, row by row, each row's runs ascending by column; for the joins between
// layers also its eroded air
struct LayerAir
{
    BitRows solid;
    BitRows eroded;
    std::vector<uint32_t> row_first; // per row, its first run; one more at the end
    std::vector<Run> runs;
    uint32_t regions{0};

    // The region of the air pixel (c, r), NONE when the pixel is solid
    uint32_t region_at(int32_t r, int32_t c) const
    {
        const auto first = runs.begin() + row_first[r];
        const auto last = runs.begin() + row_first[r + 1];
        auto it = std::upper_bound(first, last, c, [](int32_t v, const Run &run) { return v < run.c0; });
        if (it == first)
            return NONE;
        --it;
        return c <= it->c1 ? it->region : NONE;
    }
};

// Columns [c0, c1] of a row carrying the cavity span [lo, hi] (layer ids)
struct SpanInterval
{
    int32_t c0, c1;
    uint32_t lo, hi;
};

struct SpanPiece
{
    int32_t row, c0, c1;
    uint32_t lo, hi;
};

// One layer's cavity spans per pixel: the union of the spans of the segments covering it, as disjoint intervals row
// by row, ascending by column
struct SpanRows
{
    std::vector<uint32_t> row_first;
    std::vector<SpanInterval> intervals;

    const SpanInterval *at(int32_t r, int32_t c) const
    {
        const auto first = intervals.begin() + row_first[r];
        const auto last = intervals.begin() + row_first[r + 1];
        auto it = std::upper_bound(first, last, c, [](int32_t v, const SpanInterval &iv) { return v < iv.c0; });
        if (it == first)
            return nullptr;
        --it;
        return c <= it->c1 ? &*it : nullptr;
    }
};

// Buffers a thread reuses from layer to layer
struct Scratch
{
    BitRows set; // the grown solid of the erosion
    std::vector<uint64_t> both;
    std::vector<uint32_t> parent;
    std::vector<uint32_t> label;
    std::vector<SpanPiece> pieces;
    std::vector<SpanPiece> sorted;
    std::vector<SpanPiece> active;
    std::vector<uint32_t> piece_first;
    std::vector<uint32_t> cursor;
};

// A segment as the raster reads it: its ends, direction, length, and the radius it covers
struct Capsule
{
    double ax, ay, bx, by;
    double ux, uy, len;
    double radius;
};

Capsule capsule_of(const PrefilterVertexView &vertices, uint32_t k, double res)
{
    Capsule c;
    c.ax = vertices.x(k);
    c.ay = vertices.y(k);
    c.bx = vertices.x(k + 1);
    c.by = vertices.y(k + 1);
    const double dx = c.bx - c.ax;
    const double dy = c.by - c.ay;
    c.len = std::hypot(dx, dy);
    c.ux = dx / c.len;
    c.uy = dy / c.len;
    // At least half a pixel diagonal, so a bead thinner than a pixel still sets a connected line of pixels, plus half
    // a pixel so touching beads leave no seam
    c.radius = std::max(0.5 * double(vertices.width(k + 1)), 0.5 * std::sqrt(2.0) * res) + 0.5 * res;
    return c;
}

// The x interval where the line y = yc lies within the capsule's radius of the segment; false when they do not meet.
// The capsule is convex, so the union of its end discs' and its strip's intervals is one interval.
bool capsule_row(const Capsule &c, double yc, double &lo, double &hi)
{
    lo = std::numeric_limits<double>::infinity();
    hi = -std::numeric_limits<double>::infinity();
    const double r2 = c.radius * c.radius;
    const double ends[2][2] = {{c.ax, c.ay}, {c.bx, c.by}};
    for (const auto &e : ends)
    {
        const double d = yc - e[1];
        const double q = r2 - d * d;
        if (q >= 0.0)
        {
            const double s = std::sqrt(q);
            lo = std::min(lo, e[0] - s);
            hi = std::max(hi, e[0] + s);
        }
    }
    // The strip: |n . (p - a)| <= radius and 0 <= u . (p - a) <= len for p = (x, yc), n = (-uy, ux)
    double s_lo = -std::numeric_limits<double>::infinity();
    double s_hi = std::numeric_limits<double>::infinity();
    const double ey = yc - c.ay;
    auto clip = [&](double coef, double constant, double low, double high)
    {
        // low <= coef * (x - ax) + constant <= high
        if (coef == 0.0)
        {
            if (constant < low || constant > high)
                s_lo = std::numeric_limits<double>::infinity();
            return;
        }
        double t0 = (low - constant) / coef;
        double t1 = (high - constant) / coef;
        if (t0 > t1)
            std::swap(t0, t1);
        s_lo = std::max(s_lo, c.ax + t0);
        s_hi = std::min(s_hi, c.ax + t1);
    };
    clip(-c.uy, c.ux * ey, -c.radius, c.radius);
    clip(c.ux, c.uy * ey, 0.0, c.len);
    if (s_lo <= s_hi)
    {
        lo = std::min(lo, s_lo);
        hi = std::max(hi, s_hi);
    }
    return lo <= hi;
}

// Calls emit(row, c0, c1) for every row holding pixel centres within the capsule's radius, with those columns
template<class Emit>
void footprint(const Raster &raster, const Capsule &c, const Emit &emit)
{
    const double res = raster.res;
    const double ylo = std::min(c.ay, c.by) - c.radius;
    const double yhi = std::max(c.ay, c.by) + c.radius;
    const double r0 = std::max(std::ceil((ylo - raster.oy) / res - 0.5), 0.0);
    const double r1 = std::min(std::floor((yhi - raster.oy) / res - 0.5), double(raster.rows - 1));
    for (int32_t r = static_cast<int32_t>(r0); r <= static_cast<int32_t>(r1); ++r)
    {
        double lo, hi;
        if (!capsule_row(c, raster.oy + (r + 0.5) * res, lo, hi))
            continue;
        const double c0 = std::max(std::ceil((lo - raster.ox) / res - 0.5), 0.0);
        const double c1 = std::min(std::floor((hi - raster.ox) / res - 0.5), double(raster.cols - 1));
        if (c0 <= c1)
            emit(r, static_cast<int32_t>(c0), static_cast<int32_t>(c1));
    }
}

// Rasterizes segments: the pixels whose centres lie within a capsule's radius
void rasterize(const Raster &raster, const PrefilterVertexView &vertices, const uint32_t *segs, size_t count,
               BitRows &out)
{
    out.reset(raster.rows, raster.cols);
    for (size_t i = 0; i < count; ++i)
        footprint(raster, capsule_of(vertices, segs[i], raster.res),
                  [&](int32_t r, int32_t c0, int32_t c1) { out.set_range(r, c0, c1); });
}

// The air pixels whose 8 neighbours are air too (pixels past the raster border count as air): the solid dilated by a
// 3 x 3 square, inverted
void erode_air(const BitRows &solid, int32_t rows, int32_t cols, BitRows &grown, BitRows &out)
{
    const int32_t words = solid.words;
    grown.reset(rows, cols);
    for (int32_t r = 0; r < rows; ++r)
    {
        const uint64_t *src = solid.row(r);
        uint64_t *dst = grown.row(r);
        for (int32_t w = 0; w < words; ++w)
        {
            const uint64_t left = w > 0 ? src[w - 1] >> 63 : 0;
            const uint64_t right = w + 1 < words ? src[w + 1] << 63 : 0;
            dst[w] = src[w] | (src[w] << 1) | left | (src[w] >> 1) | right;
        }
    }
    out.reset(rows, cols);
    const uint64_t last_mask = (cols & 63) == 0 ? ~uint64_t(0) : (uint64_t(1) << (cols & 63)) - 1;
    for (int32_t r = 0; r < rows; ++r)
    {
        uint64_t *dst = out.row(r);
        for (int32_t w = 0; w < words; ++w)
        {
            uint64_t v = grown.row(r)[w];
            if (r > 0)
                v |= grown.row(r - 1)[w];
            if (r + 1 < rows)
                v |= grown.row(r + 1)[w];
            dst[w] = ~v & (w + 1 == words ? last_mask : ~uint64_t(0));
        }
    }
}

uint32_t find_root(std::vector<uint32_t> &parent, uint32_t a)
{
    while (parent[a] != a)
    {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}

// Joins the sets of a and b under the smaller root, so every parent is at most its child
void unite(std::vector<uint32_t> &parent, uint32_t a, uint32_t b)
{
    a = find_root(parent, a);
    b = find_root(parent, b);
    if (a < b)
        parent[b] = a;
    else if (b < a)
        parent[a] = b;
}

// Rasterizes one layer's segments and labels its air 4-connected, and with `eroded` finds its eroded air. Regions are
// numbered by their first run in row-major order, so the labelling depends only on the layer's segments.
void label_layer(const Raster &raster, const PrefilterVertexView &vertices, const uint32_t *segs, size_t count,
                 bool eroded, Scratch &s, LayerAir &air)
{
    const int32_t rows = raster.rows;
    const int32_t cols = raster.cols;
    rasterize(raster, vertices, segs, count, air.solid);
    if (eroded)
        erode_air(air.solid, rows, cols, s.set, air.eroded);

    // Each row's air: its runs of unset bits
    air.row_first.assign(size_t(rows) + 1, 0);
    air.runs.clear();
    for (int32_t r = 0; r < rows; ++r)
    {
        const uint64_t *row = air.solid.row(r);
        for (int32_t c = next_bit(row, air.solid.words, cols, 0, false); c < cols;)
        {
            const int32_t end = next_bit(row, air.solid.words, cols, c, true);
            air.runs.push_back({c, end - 1, 0});
            if (end >= cols)
                break;
            c = next_bit(row, air.solid.words, cols, end, false);
        }
        air.row_first[size_t(r) + 1] = static_cast<uint32_t>(air.runs.size());
    }

    // Runs of consecutive rows that share a column are 4-connected
    const size_t runs = air.runs.size();
    s.parent.resize(runs);
    std::iota(s.parent.begin(), s.parent.end(), uint32_t(0));
    for (int32_t r = 0; r + 1 < rows; ++r)
    {
        uint32_t i = air.row_first[size_t(r)];
        uint32_t j = air.row_first[size_t(r) + 1];
        const uint32_t i_end = air.row_first[size_t(r) + 1];
        const uint32_t j_end = air.row_first[size_t(r) + 2];
        while (i < i_end && j < j_end)
        {
            const Run &a = air.runs[i];
            const Run &b = air.runs[j];
            if (a.c0 <= b.c1 && b.c0 <= a.c1)
                unite(s.parent, i, j);
            if (a.c1 < b.c1)
                ++i;
            else
                ++j;
        }
    }
    // A root is its set's smallest run, so it is met before the set's other runs
    s.label.assign(runs, NONE);
    air.regions = 0;
    for (size_t i = 0; i < runs; ++i)
    {
        const uint32_t root = find_root(s.parent, static_cast<uint32_t>(i));
        if (s.label[root] == NONE)
            s.label[root] = air.regions++;
        air.runs[i].region = s.label[root];
    }
}

// The cavity spans of one layer's segments per pixel of their footprint (the union where they overlap); a
// segment without a cavity adds nothing
void span_layer(const Raster &raster, const PrefilterVertexView &vertices, const uint32_t *segs, size_t count,
                const std::vector<uint32_t> &cavity_lo, const std::vector<uint32_t> &cavity_hi, Scratch &s,
                SpanRows &out)
{
    const int32_t rows = raster.rows;
    s.pieces.clear();
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t k = segs[i];
        const uint32_t lo = cavity_lo[k];
        const uint32_t hi = cavity_hi[k];
        if (lo > hi)
            continue;
        footprint(raster, capsule_of(vertices, k, raster.res),
                  [&](int32_t r, int32_t c0, int32_t c1) { s.pieces.push_back({r, c0, c1, lo, hi}); });
    }

    s.piece_first.assign(size_t(rows) + 1, 0);
    for (const SpanPiece &p : s.pieces)
        ++s.piece_first[size_t(p.row) + 1];
    for (int32_t r = 0; r < rows; ++r)
        s.piece_first[size_t(r) + 1] += s.piece_first[size_t(r)];
    s.cursor.assign(s.piece_first.begin(), s.piece_first.end() - 1);
    s.sorted.resize(s.pieces.size());
    for (const SpanPiece &p : s.pieces)
        s.sorted[s.cursor[size_t(p.row)]++] = p;

    out.row_first.assign(size_t(rows) + 1, 0);
    out.intervals.clear();
    for (int32_t r = 0; r < rows; ++r)
    {
        const auto first = s.sorted.begin() + s.piece_first[size_t(r)];
        const auto last = s.sorted.begin() + s.piece_first[size_t(r) + 1];
        std::sort(first, last, [](const SpanPiece &a, const SpanPiece &b) { return a.c0 < b.c0; });
        // Sweep the columns: each step covers the columns until the next piece starts or an active one ends
        const size_t row_start = out.intervals.size();
        s.active.clear();
        int32_t pos = 0;
        for (auto it = first; it != last || !s.active.empty();)
        {
            if (s.active.empty())
                pos = it->c0;
            for (; it != last && it->c0 <= pos; ++it)
                s.active.push_back(*it);
            int32_t stop = std::numeric_limits<int32_t>::max();
            uint32_t lo = NONE;
            uint32_t hi = 0;
            for (const SpanPiece &a : s.active)
            {
                stop = std::min(stop, a.c1);
                lo = std::min(lo, a.lo);
                hi = std::max(hi, a.hi);
            }
            if (it != last)
                stop = std::min(stop, it->c0 - 1);
            SpanInterval *back = out.intervals.size() > row_start ? &out.intervals.back() : nullptr;
            if (back != nullptr && back->c1 + 1 == pos && back->lo == lo && back->hi == hi)
                back->c1 = stop;
            else
                out.intervals.push_back({pos, stop, lo, hi});
            pos = stop + 1;
            s.active.erase(std::remove_if(s.active.begin(), s.active.end(),
                                          [pos](const SpanPiece &a) { return a.c1 < pos; }),
                           s.active.end());
        }
        out.row_first[size_t(r) + 1] = static_cast<uint32_t>(out.intervals.size());
    }
}

// Calls join(region below, region above) for every pair of regions of two adjacent layers that share a pixel of eroded
// air in both: openings narrower than about three pixels (a slit between beads) do not join the layers, so looking
// down one shows only the layers the range end exposes. A run of such pixels lies in one air run of each layer.
template<class Join>
void join_layers(const LayerAir &below, const LayerAir &above, int32_t rows, int32_t cols, std::vector<uint64_t> &both,
                 const Join &join)
{
    const int32_t words = below.eroded.words;
    both.resize(size_t(words));
    uint32_t last_a = NONE;
    uint32_t last_b = NONE;
    for (int32_t r = 0; r < rows; ++r)
    {
        const uint64_t *a = below.eroded.row(r);
        const uint64_t *b = above.eroded.row(r);
        bool any = false;
        for (int32_t w = 0; w < words; ++w)
        {
            both[size_t(w)] = a[w] & b[w];
            any = any || both[size_t(w)] != 0;
        }
        if (!any)
            continue;
        for (int32_t c = next_bit(both.data(), words, cols, 0, true); c < cols;)
        {
            const uint32_t region_a = below.region_at(r, c);
            const uint32_t region_b = above.region_at(r, c);
            if (region_a != NONE && region_b != NONE && (region_a != last_a || region_b != last_b))
            {
                join(region_a, region_b);
                last_a = region_a;
                last_b = region_b;
            }
            const int32_t end = next_bit(both.data(), words, cols, c, false);
            if (end >= cols)
                break;
            c = next_bit(both.data(), words, cols, end, true);
        }
    }
}

// Calls mark(region) for every run touching the raster border
template<class Mark>
void border_regions(const LayerAir &air, int32_t rows, int32_t cols, const Mark &mark)
{
    for (int32_t r = 0; r < rows; ++r)
        for (uint32_t i = air.row_first[size_t(r)]; i < air.row_first[size_t(r) + 1]; ++i)
        {
            const Run &run = air.runs[i];
            if (r == 0 || r == rows - 1 || run.c0 == 0 || run.c1 == cols - 1)
                mark(run.region);
        }
}

// Calls probe(layer, x, y) for the probes of segment k (layer 0: its own, 1: the one above, -1: the one below) at
// samples every `step` along the segment (at least one, at the midpoint), 0.5 w + j * resolution (j = 1..SIDE_PROBES)
// along its normal on either side in its own layer, then at ACROSS times w across it above and below. The probing ends
// once `done` is set; probe may set it.
template<class Probe>
void probe_segment(const PrefilterVertexView &vertices, const Raster &raster, double step, uint32_t k, const bool &done,
                   const Probe &probe)
{
    const double ax = vertices.x(k);
    const double ay = vertices.y(k);
    const double dx = double(vertices.x(k + 1)) - ax;
    const double dy = double(vertices.y(k + 1)) - ay;
    const double w = vertices.width(k + 1);
    const double len = std::hypot(dx, dy);
    const double nx = -dy / len;
    const double ny = dx / len;
    const int64_t samples = std::max<int64_t>(1, static_cast<int64_t>(len / step));
    for (int64_t i = 0; i < samples && !done; ++i)
    {
        const double t = (double(i) + 0.5) / double(samples);
        const double px = ax + dx * t;
        const double py = ay + dy * t;
        for (int side = -1; side <= 1 && !done; side += 2)
            for (int j = 1; j <= SIDE_PROBES && !done; ++j)
            {
                const double off = side * (0.5 * w + j * raster.res);
                probe(0, px + nx * off, py + ny * off);
            }
        for (const double f : ACROSS)
        {
            if (!done)
                probe(1, px + nx * w * f, py + ny * w * f);
            if (!done)
                probe(-1, px + nx * w * f, py + ny * w * f);
        }
    }
}

// Runs work(i) for every i in [0, count) on up to `threads` threads, the calling thread included; each i runs
// exactly once. A thread that fails to start leaves its share to the others. After the first exception `failed` is
// set, no further i is taken, and the exception is rethrown. Returns the threads that ran, the calling thread included.
template<class Work>
unsigned parallel_for(size_t count, unsigned threads, std::atomic<bool> &failed, const Work &work)
{
    std::atomic<size_t> next{0};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto worker = [&]()
    {
        for (size_t i = next.fetch_add(1); i < count && !failed.load(); i = next.fetch_add(1))
        {
            try
            {
                work(i);
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
            pool.emplace_back(worker);
        }
        catch (...)
        {
            break;
        }
    }
    worker();
    for (std::thread &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
    return static_cast<unsigned>(pool.size()) + 1;
}

// A contiguous range of layers one thread works through in order
struct Band
{
    uint32_t first{0}, last{0};         // layer ordinals [first, last)
    std::vector<uint32_t> region_count; // per layer of the band
    std::vector<uint32_t> parent;       // union-find over the band's regions, numbered layer by layer
    std::vector<uint8_t> outside;       // per region: touches the border or lies in the first or the last layer
    // Joins of the layer below the band with its first layer: (region of that layer, region of the band)
    std::vector<std::pair<uint32_t, uint32_t>> boundary;
};

bool usable_position(const PrefilterVertexView &vertices, size_t i)
{
    const float x = vertices.x(i);
    const float y = vertices.y(i);
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= COORDINATE_LIMIT && std::abs(y) <= COORDINATE_LIMIT;
}

} // namespace

SealedBeadData compute_sealed_beads(const std::vector<PrefilterVertex> &vertices,
                                    const std::vector<uint8_t> &extrusion_segment, unsigned threads,
                                    const std::function<void(float)> &progress)
{
    return compute_sealed_beads(prefilter_vertex_view(vertices), extrusion_segment, threads, progress);
}

SealedBeadData compute_sealed_beads(const PrefilterVertexView &vertices, const std::vector<uint8_t> &extrusion_segment,
                                    unsigned threads, const std::function<void(float)> &progress)
{
    SealedBeadData data;
    const size_t n = vertices.count;
    // Every vertex starts drawn; only classified segments may be culled
    data.touches_outside.assign(n, 1);
    data.cavity_lo.assign(n, NONE);
    data.cavity_hi.assign(n, 0);
    if (n < 2 || n >= size_t(NONE))
    {
        if (progress)
            progress(1.0f);
        return data;
    }

    // The classified segments by start vertex, with the bounds the raster is sized from
    std::vector<uint32_t> segs;
    std::vector<uint32_t> ids;
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double max_x = -min_x;
    double max_y = -min_x;
    double max_w = 0.0;
    const size_t starts = std::min(extrusion_segment.size(), n - 1);
    segs.reserve(size_t(std::count_if(extrusion_segment.begin(), extrusion_segment.begin() + starts,
                                      [](uint8_t extrusion) { return extrusion != 0; })));
    for (size_t k = 0; k < starts; ++k)
    {
        if (extrusion_segment[k] == 0 || !usable_position(vertices, k) || !usable_position(vertices, k + 1))
            continue;
        const double w = vertices.width(k + 1);
        if (!std::isfinite(w) || !(w > 0.0) || w > WIDTH_LIMIT)
            continue;
        const double ax = vertices.x(k);
        const double ay = vertices.y(k);
        const double bx = vertices.x(k + 1);
        const double by = vertices.y(k + 1);
        if (std::hypot(bx - ax, by - ay) < MIN_LENGTH)
            continue;
        segs.push_back(static_cast<uint32_t>(k));
        ids.push_back(vertices.layer_id(k + 1));
        min_x = std::min({min_x, ax, bx});
        max_x = std::max({max_x, ax, bx});
        min_y = std::min({min_y, ay, by});
        max_y = std::max({max_y, ay, by});
        max_w = std::max(max_w, w);
    }
    data.segments = segs.size();
    if (segs.empty())
    {
        if (progress)
            progress(1.0f);
        return data;
    }

    // The width percentile over every stride-th segment in start order, so a few thin beads do not set the resolution
    double width_percentile = 0.0;
    {
        const size_t stride = std::max<size_t>(1, (segs.size() + WIDTH_SAMPLES - 1) / WIDTH_SAMPLES);
        std::vector<float> widths;
        widths.reserve(segs.size() / stride + 1);
        for (size_t i = 0; i < segs.size(); i += stride)
            widths.push_back(vertices.width(size_t(segs[i]) + 1));
        const size_t nth = static_cast<size_t>(double(widths.size()) * double(SEALED_BEADS_WIDTH_PERCENTILE));
        std::nth_element(widths.begin(), widths.begin() + nth, widths.end());
        width_percentile = widths[nth];
    }

    // Layers: the distinct layer ids in ascending order; each layer's segments ascending by start vertex
    std::vector<uint32_t> seg_layer(std::move(ids));
    ids = seg_layer;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const uint32_t layers = static_cast<uint32_t>(ids.size());
    std::vector<uint32_t> layer_first(size_t(layers) + 1, 0);
    for (uint32_t &l : seg_layer)
    {
        l = static_cast<uint32_t>(std::lower_bound(ids.begin(), ids.end(), l) - ids.begin());
        ++layer_first[size_t(l) + 1];
    }
    for (uint32_t l = 0; l < layers; ++l)
        layer_first[size_t(l) + 1] += layer_first[size_t(l)];
    std::vector<uint32_t> layer_segs(segs.size());
    {
        std::vector<uint32_t> fill(layer_first.begin(), layer_first.end() - 1);
        for (size_t i = 0; i < segs.size(); ++i)
            layer_segs[fill[seg_layer[i]]++] = segs[i];
    }
    std::vector<uint32_t>().swap(seg_layer);
    std::vector<uint32_t>().swap(segs);

    // The raster: the resolution from the width percentile, coarsened when the print does not fit. One column and
    // row more than the extent holds the origin shift.
    Raster raster;
    raster.res = std::min(std::max(width_percentile / SEALED_BEADS_WIDTH_DIVISOR, double(SEALED_BEADS_MIN_RESOLUTION)),
                          double(SEALED_BEADS_RESOLUTION));
    const double margin = MARGIN + 0.5 * max_w;
    const double extent_x = max_x - min_x + 2.0 * margin;
    const double extent_y = max_y - min_y + 2.0 * margin;
    auto sides_fit = [&]()
    {
        const double cols = std::floor(extent_x / raster.res) + 2.0;
        const double rows = std::floor(extent_y / raster.res) + 2.0;
        if (cols > SEALED_BEADS_MAX_RASTER || rows > SEALED_BEADS_MAX_RASTER)
            return false;
        raster.cols = static_cast<int32_t>(cols);
        raster.rows = static_cast<int32_t>(rows);
        return true;
    };
    if (!sides_fit())
    {
        raster.res = std::max(extent_x, extent_y) / double(SEALED_BEADS_MAX_RASTER - 2);
        while (!sides_fit())
            raster.res *= 1.0 + 1e-6;
    }
    raster.ox = min_x - margin - ORIGIN_SHIFT * raster.res;
    raster.oy = min_y - margin - ORIGIN_SHIFT * raster.res;
    data.resolution_mm = static_cast<float>(raster.res);
    const double step = std::max(SAMPLE_STEP, raster.res);

    const unsigned thread_count = threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency());
    // One band on one thread; the partition into bands does not change the result
    const uint32_t band_count = thread_count == 1 ? 1u : std::min(layers, thread_count * BANDS_PER_THREAD);
    std::vector<Band> bands(band_count);
    for (uint32_t b = 0; b < band_count; ++b)
    {
        bands[b].first = static_cast<uint32_t>(uint64_t(layers) * b / band_count);
        bands[b].last = static_cast<uint32_t>(uint64_t(layers) * (b + 1) / band_count);
    }

    // Progress: every layer is labelled in pass 1, classified in pass 2 and probed against ring 1 in pass 3
    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<size_t> layers_done{0};
    double reported = 0.0;
    auto layer_done = [&]()
    {
        const size_t done = layers_done.fetch_add(1) + 1;
        if (progress && std::this_thread::get_id() == caller)
        {
            const double fraction = std::min(1.0, double(done) / (3.0 * double(layers)));
            if (fraction - reported >= PROGRESS_STEP)
            {
                reported = fraction;
                progress(static_cast<float>(fraction));
            }
        }
    };
    auto label = [&](uint32_t l, bool eroded, Scratch &scratch, LayerAir &air)
    {
        label_layer(raster, vertices, layer_segs.data() + layer_first[l], layer_first[size_t(l) + 1] - layer_first[l],
                    eroded, scratch, air);
    };

    // Pass 1: each band labels its layers in order, joins the regions of its consecutive layers and marks the
    // outside ones. Region ids are numbered layer by layer within the band. A band after the first also relabels the
    // layer below it (its labelling is the other band's) and records the joins with it.
    std::atomic<bool> failed{false};
    auto join_band = [&](size_t b)
    {
        Band &band = bands[b];
        Scratch scratch;
        LayerAir prev;
        LayerAir cur;
        uint32_t prev_base = 0;
        if (band.first > 0)
            label(band.first - 1, true, scratch, prev);
        for (uint32_t l = band.first; l < band.last && !failed.load(); ++l)
        {
            label(l, true, scratch, cur);
            const uint64_t count = uint64_t(band.parent.size()) + cur.regions;
            if (count >= uint64_t(NONE))
                throw std::bad_alloc();
            const uint32_t base = static_cast<uint32_t>(band.parent.size());
            band.region_count.push_back(cur.regions);
            band.parent.resize(size_t(count));
            std::iota(band.parent.begin() + base, band.parent.end(), base);
            band.outside.resize(size_t(count), 0);
            if (l == 0 || l + 1 == layers)
                std::fill(band.outside.begin() + base, band.outside.end(), uint8_t(1));
            else
                border_regions(cur, raster.rows, raster.cols, [&](uint32_t r) { band.outside[base + r] = 1; });
            if (l > band.first)
                join_layers(prev, cur, raster.rows, raster.cols, scratch.both,
                            [&](uint32_t a, uint32_t c) { unite(band.parent, prev_base + a, base + c); });
            else if (l > 0)
                join_layers(prev, cur, raster.rows, raster.cols, scratch.both,
                            [&](uint32_t a, uint32_t c) { band.boundary.emplace_back(a, base + c); });
            std::swap(prev, cur);
            prev_base = base;
            layer_done();
        }
    };
    const unsigned used = parallel_for(band_count, thread_count, failed, join_band);

    // The bands' regions in one union-find, global id = 1 + the region's position in layer order; 0 is the outside
    std::vector<uint32_t> layer_base(size_t(layers) + 1, 0);
    uint64_t total = 1;
    for (const Band &band : bands)
    {
        for (uint32_t j = 0; j < band.region_count.size(); ++j)
        {
            layer_base[band.first + j] = static_cast<uint32_t>(total);
            total += band.region_count[j];
        }
        if (total >= uint64_t(NONE))
            throw std::bad_alloc();
    }
    layer_base[layers] = static_cast<uint32_t>(total);
    std::vector<uint32_t> root(size_t(total), 0);
    for (const Band &band : bands)
    {
        const uint32_t base = layer_base[band.first];
        for (size_t i = 0; i < band.parent.size(); ++i)
            root[base + i] = base + band.parent[i];
    }
    for (Band &band : bands)
    {
        const uint32_t base = layer_base[band.first];
        for (size_t i = 0; i < band.outside.size(); ++i)
            if (band.outside[i] != 0)
                unite(root, static_cast<uint32_t>(base + i), OUTSIDE);
        std::vector<uint32_t>().swap(band.parent);
        std::vector<uint8_t>().swap(band.outside);
    }
    for (Band &band : bands)
    {
        if (band.first == 0)
            continue;
        const uint32_t below_base = layer_base[band.first - 1];
        const uint32_t above_base = layer_base[band.first];
        for (const auto &[a, c] : band.boundary)
            unite(root, below_base + a, above_base + c);
        std::vector<std::pair<uint32_t, uint32_t>>().swap(band.boundary);
    }
    // Every parent is at most its child, so one ascending pass leaves each id's root
    for (size_t g = 1; g < root.size(); ++g)
        root[g] = root[root[g]];

    // Per cavity root, its layer span as layer ids reaching into the id gaps next to it
    std::vector<uint32_t> span_lo(root.size(), NONE);
    std::vector<uint32_t> span_hi(root.size(), 0);
    for (uint32_t l = 0; l < layers; ++l)
        for (uint32_t g = layer_base[l]; g < layer_base[l + 1]; ++g)
        {
            const uint32_t r = root[g];
            if (r == OUTSIDE)
                continue;
            if (span_lo[r] == NONE)
                span_lo[r] = l;
            span_hi[r] = l;
        }
    for (size_t g = 1; g < root.size(); ++g)
    {
        if (span_lo[g] == NONE)
            continue;
        const uint32_t lo = span_lo[g];
        const uint32_t hi = span_hi[g];
        span_lo[g] = lo > 0 ? ids[lo - 1] + 1 : ids[lo];
        span_hi[g] = hi + 1 < layers ? ids[hi + 1] - 1 : ids[hi];
    }

    // Pass 2: each band relabels its layers with the one below and above in a sliding window and probes every
    // segment. A segment is written only by the band holding its layer.
    auto classify = [&](uint32_t k, const LayerAir *below, uint32_t below_base, const LayerAir &own, uint32_t own_base,
                        const LayerAir *above, uint32_t above_base)
    {
        bool outside = false;
        uint32_t lo = NONE;
        uint32_t hi = 0;
        // The first probe reaching outside air ends the probing; a segment finding none runs every probe, so its span
        // is the full union
        probe_segment(vertices, raster, step, k, outside,
                      [&](int layer, double px, double py)
                      {
                          const LayerAir *air = layer == 0 ? &own : (layer > 0 ? above : below);
                          const uint32_t base = layer == 0 ? own_base : (layer > 0 ? above_base : below_base);
                          if (air == nullptr)
                          {
                              outside = true;
                              return;
                          }
                          const int32_t r = raster.row_of(py);
                          const int32_t c = raster.col_of(px);
                          if (air->solid.test(r, c))
                              return;
                          const uint32_t local = air->region_at(r, c);
                          if (local == NONE)
                              return;
                          const uint32_t g = root[base + local];
                          if (g == OUTSIDE)
                          {
                              outside = true;
                              return;
                          }
                          lo = std::min(lo, span_lo[g]);
                          hi = std::max(hi, span_hi[g]);
                      });
        // A segment touching outside air is drawn whatever its cavities, so they are not recorded
        data.touches_outside[k] = outside ? 1 : 0;
        if (!outside)
        {
            data.cavity_lo[k] = lo;
            data.cavity_hi[k] = hi;
        }
    };
    auto classify_band = [&](size_t b)
    {
        const Band &band = bands[b];
        Scratch scratch;
        LayerAir below;
        LayerAir own;
        LayerAir above;
        bool has_below = band.first > 0;
        if (has_below)
            label(band.first - 1, false, scratch, below);
        label(band.first, false, scratch, own);
        bool has_above = band.first + 1 < layers;
        if (has_above)
            label(band.first + 1, false, scratch, above);
        for (uint32_t l = band.first; l < band.last && !failed.load(); ++l)
        {
            const uint32_t below_base = has_below ? layer_base[l - 1] : 0;
            const uint32_t above_base = has_above ? layer_base[l + 1] : 0;
            for (uint32_t i = layer_first[l]; i < layer_first[size_t(l) + 1]; ++i)
                classify(layer_segs[i], has_below ? &below : nullptr, below_base, own, layer_base[l],
                         has_above ? &above : nullptr, above_base);
            layer_done();
            if (l + 1 == band.last)
                break;
            std::swap(below, own);
            std::swap(own, above);
            has_below = true;
            has_above = l + 2 < layers;
            if (has_above)
                label(l + 2, false, scratch, above);
        }
    };
    const unsigned used_classify = parallel_for(band_count, thread_count, failed, classify_band);
    std::vector<uint32_t>().swap(root);
    std::vector<uint32_t>().swap(span_lo);
    std::vector<uint32_t>().swap(span_hi);

    // Pass 3, one ring inward. The preview's bead profile leaves grooves between beads through which the next bead
    // inward shows: a segment whose probes land on a segment touching outside air (ring 1) is drawn too. A bead
    // touching a bead that reaches a cavity shows when the range opens that cavity: a segment takes the cavity spans
    // found at its probes' pixels. Ring 1 and the spans are the pass 2 results, left untouched until every band is
    // done, so no band reads another band's result.
    std::vector<uint32_t> ring1_first(size_t(layers) + 1, 0);
    std::vector<uint32_t> ring1_segs;
    for (uint32_t l = 0; l < layers; ++l)
    {
        for (uint32_t i = layer_first[l]; i < layer_first[size_t(l) + 1]; ++i)
            if (data.touches_outside[layer_segs[i]] != 0)
                ring1_segs.push_back(layer_segs[i]);
        ring1_first[size_t(l) + 1] = static_cast<uint32_t>(ring1_segs.size());
    }
    // Per entry of layer_segs: 1 when its probes land on ring 1, and the spans merged from the probes
    std::vector<uint8_t> ring2_hit(layer_segs.size(), 0);
    std::vector<uint32_t> merged_lo(layer_segs.size(), NONE);
    std::vector<uint32_t> merged_hi(layer_segs.size(), 0);
    // One layer of the window: its ring 1 pixels and its span pixels
    struct Ring
    {
        BitRows ring1;
        SpanRows spans;
    };
    auto ring_layer = [&](uint32_t l, Scratch &scratch, Ring &ring)
    {
        const uint32_t *ring1 = ring1_segs.data() + ring1_first[l];
        const size_t ring1_count = ring1_first[size_t(l) + 1] - ring1_first[l];
        rasterize(raster, vertices, ring1, ring1_count, ring.ring1);
        span_layer(raster, vertices, layer_segs.data() + layer_first[l], layer_first[size_t(l) + 1] - layer_first[l],
                   data.cavity_lo, data.cavity_hi, scratch, ring.spans);
    };
    auto ring2_band = [&](size_t b)
    {
        const Band &band = bands[b];
        Scratch scratch;
        Ring below;
        Ring own;
        Ring above;
        bool has_below = band.first > 0;
        if (has_below)
            ring_layer(band.first - 1, scratch, below);
        ring_layer(band.first, scratch, own);
        bool has_above = band.first + 1 < layers;
        if (has_above)
            ring_layer(band.first + 1, scratch, above);
        for (uint32_t l = band.first; l < band.last && !failed.load(); ++l)
        {
            for (uint32_t i = layer_first[l]; i < layer_first[size_t(l) + 1]; ++i)
            {
                const uint32_t k = layer_segs[i];
                if (data.touches_outside[k] != 0)
                    continue;
                bool hit = false;
                uint32_t lo = data.cavity_lo[k];
                uint32_t hi = data.cavity_hi[k];
                // The first probe landing on ring 1 ends the probing; a segment landing on none runs every probe
                probe_segment(vertices, raster, step, k, hit,
                              [&](int layer, double px, double py)
                              {
                                  // No layer above the last or below the first: nothing there
                                  const Ring *ring = layer == 0 ? &own
                                                                : (layer > 0 ? (has_above ? &above : nullptr)
                                                                             : (has_below ? &below : nullptr));
                                  if (ring == nullptr)
                                      return;
                                  const int32_t r = raster.row_of(py);
                                  const int32_t c = raster.col_of(px);
                                  if (ring->ring1.test(r, c))
                                  {
                                      // This bead shows through the groove next to the ring 1 bead
                                      hit = true;
                                      return;
                                  }
                                  if (const SpanInterval *iv = ring->spans.at(r, c))
                                  {
                                      lo = std::min(lo, iv->lo);
                                      hi = std::max(hi, iv->hi);
                                  }
                              });
                if (hit)
                    ring2_hit[i] = 1;
                merged_lo[i] = lo;
                merged_hi[i] = hi;
            }
            layer_done();
            if (l + 1 == band.last)
                break;
            std::swap(below, own);
            std::swap(own, above);
            has_below = true;
            has_above = l + 2 < layers;
            if (has_above)
                ring_layer(l + 2, scratch, above);
        }
    };
    const unsigned used_ring2 = parallel_for(band_count, thread_count, failed, ring2_band);
    for (size_t i = 0; i < layer_segs.size(); ++i)
    {
        const uint32_t k = layer_segs[i];
        if (data.touches_outside[k] != 0)
            continue;
        if (ring2_hit[i] != 0)
        {
            data.touches_outside[k] = 1;
            ++data.ring2;
        }
        else
        {
            data.cavity_lo[k] = merged_lo[i];
            data.cavity_hi[k] = merged_hi[i];
        }
    }
    data.threads = std::max({used, used_classify, used_ring2});
    if (progress)
        progress(1.0f);

    for (uint32_t l = 0; l < layers; ++l)
        for (uint32_t i = layer_first[l]; i < layer_first[size_t(l) + 1]; ++i)
            if (!sealed_bead_drawn(data, layer_segs[i], ids[l], ids.front(), ids.back()))
                ++data.sealed_full_view;
    return data;
}

bool sealed_bead_drawn(const SealedBeadData &data, size_t k, uint32_t layer, uint32_t first_layer, uint32_t last_layer)
{
    if (k >= data.touches_outside.size() || k >= data.cavity_lo.size() || k >= data.cavity_hi.size() ||
        data.touches_outside[k] != 0)
        return true;
    // The top and bottom layers of the range and the two next to each: ring 1 and ring 2 of the exposed faces
    if (uint64_t(layer) + 2 >= last_layer || layer <= uint64_t(first_layer) + 2)
        return true;
    const uint32_t lo = data.cavity_lo[k];
    const uint32_t hi = data.cavity_hi[k];
    if (lo > hi)
        return false;
    return (lo <= last_layer && last_layer <= hi) || (lo <= first_layer && first_layer <= hi);
}

} // namespace libvgcode
