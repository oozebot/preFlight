///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ViewChunks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace libvgcode
{

namespace
{

constexpr float UNBOUNDED = std::numeric_limits<float>::max();
// Fewest chunk ids the counting sort may address; more segments allow one id per segment
constexpr size_t MIN_ID_LIMIT = 65536;

// Segment k's XY midpoint when k is a segment of the vertices and its ends are finite
bool midpoint(const PrefilterVertexView &vertices, uint32_t k, double &mx, double &my)
{
    if (size_t(k) + 1 >= vertices.count)
        return false;
    mx = 0.5 * (double(vertices.x(k)) + double(vertices.x(size_t(k) + 1)));
    my = 0.5 * (double(vertices.y(k)) + double(vertices.y(size_t(k) + 1)));
    return std::isfinite(mx) && std::isfinite(my);
}

void make_unbounded(ViewChunk &chunk)
{
    for (int a = 0; a < 3; ++a)
    {
        chunk.min[a] = -UNBOUNDED;
        chunk.max[a] = UNBOUNDED;
    }
}

// The box of the segments order[first, first + count)
void finish_chunk(const PrefilterVertexView &vertices, const std::vector<uint32_t> &order, bool unbounded,
                  ViewChunk &chunk)
{
    if (unbounded)
    {
        make_unbounded(chunk);
        return;
    }
    double lo[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
    double hi[3] = {-lo[0], -lo[1], -lo[2]};
    double pad = 0.0;
    bool bounded = true;
    const uint32_t end = chunk.first + chunk.count;
    for (uint32_t j = chunk.first; j < end; ++j)
    {
        const uint32_t k = order[j];
        const size_t b = size_t(k) + 1;
        const double p[2][3] = {{vertices.x(k), vertices.y(k), vertices.z(k)},
                                {vertices.x(b), vertices.y(b), vertices.z(b)}};
        const double w = vertices.width(b);
        const double h = vertices.height(b);
        for (const auto &v : p)
            for (int a = 0; a < 3; ++a)
            {
                bounded = bounded && std::isfinite(v[a]);
                lo[a] = std::min(lo[a], v[a]);
                hi[a] = std::max(hi[a], v[a]);
            }
        bounded = bounded && std::isfinite(w) && std::isfinite(h);
        pad = std::max({pad, 0.5 * w, h});
    }
    if (!bounded)
    {
        make_unbounded(chunk);
        return;
    }
    for (int a = 0; a < 3; ++a)
    {
        chunk.min[a] = static_cast<float>(lo[a] - pad);
        chunk.max[a] = static_cast<float>(hi[a] + pad);
    }
}

} // namespace

ViewChunkSet build_view_chunks(const PrefilterVertexView &vertices, const std::vector<uint32_t> &enabled, float cell_mm,
                               uint32_t layers_per_chunk)
{
    ViewChunkSet set;
    const size_t n = enabled.size();
    if (n == 0 || n >= size_t(std::numeric_limits<uint32_t>::max()))
        return set;
    const double cell_start = std::isfinite(cell_mm) && cell_mm > 0.0f ? double(cell_mm) : double(VIEW_CHUNK_CELL_MM);
    const uint64_t layers_per = std::max<uint32_t>(layers_per_chunk, 1);
    const uint64_t id_limit = std::max<uint64_t>(n, MIN_ID_LIMIT);

    // The midpoints' XY bounds over the segments that have a midpoint, and their layer ids marked in a table indexed by
    // id while every id is below the id limit
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double max_x = -min_x;
    double max_y = -min_x;
    std::vector<uint32_t> ordinal;
    bool large_ids = false;
    bool any = false;
    for (const uint32_t k : enabled)
    {
        double mx, my;
        if (!midpoint(vertices, k, mx, my))
            continue;
        any = true;
        min_x = std::min(min_x, mx);
        max_x = std::max(max_x, mx);
        min_y = std::min(min_y, my);
        max_y = std::max(max_y, my);
        const uint32_t layer = vertices.layer_id(size_t(k) + 1);
        if (large_ids || layer >= id_limit)
        {
            large_ids = true;
            continue;
        }
        if (layer >= ordinal.size())
            ordinal.resize(std::max<size_t>(size_t(layer) + 1, 2 * ordinal.size()), 0);
        ordinal[layer] = 1;
    }

    // Layer ordinals: the table's prefix sums, or the sorted distinct ids when some id is too large for the table
    std::vector<uint32_t> distinct;
    uint64_t layers = 0;
    if (large_ids)
    {
        std::vector<uint32_t>().swap(ordinal);
        distinct.reserve(n);
        for (const uint32_t k : enabled)
        {
            double mx, my;
            if (midpoint(vertices, k, mx, my))
                distinct.push_back(vertices.layer_id(size_t(k) + 1));
        }
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        layers = distinct.size();
    }
    else
        for (uint32_t &o : ordinal)
        {
            const uint32_t present = o;
            o = static_cast<uint32_t>(layers);
            layers += present;
        }
    auto layer_ordinal = [&](uint32_t layer) -> uint64_t
    {
        if (!large_ids)
            return ordinal[layer];
        return uint64_t(std::lower_bound(distinct.begin(), distinct.end(), layer) - distinct.begin());
    };

    // The grid: coarser cells while the ids would outnumber the limit (the layer chunks alone never do)
    const uint64_t layer_chunks = (layers + layers_per - 1) / layers_per;
    double cell = cell_start;
    uint64_t cols = 1;
    uint64_t rows = 1;
    if (any)
    {
        for (;;)
        {
            const double c = std::floor((max_x - min_x) / cell) + 1.0;
            const double r = std::floor((max_y - min_y) / cell) + 1.0;
            if (c * r * double(layer_chunks) <= double(id_limit))
            {
                cols = static_cast<uint64_t>(c);
                rows = static_cast<uint64_t>(r);
                break;
            }
            cell *= 2.0;
        }
    }
    // Grid ids [0, grid_ids), then one id for the segments without a midpoint
    const uint64_t grid_ids = any ? cols * rows * layer_chunks : 0;
    const uint32_t loose_id = static_cast<uint32_t>(grid_ids);

    // Counting sort, stable: start[id + 1] counts id's segments, then the prefix sums make start[id] its first slot
    std::vector<uint32_t> key(n);
    std::vector<uint32_t> start(size_t(grid_ids) + 2, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t k = enabled[i];
        double mx, my;
        uint32_t id = loose_id;
        if (midpoint(vertices, k, mx, my))
        {
            const uint64_t col = std::min(static_cast<uint64_t>(std::floor((mx - min_x) / cell)), cols - 1);
            const uint64_t row = std::min(static_cast<uint64_t>(std::floor((my - min_y) / cell)), rows - 1);
            const uint64_t lc = layer_ordinal(vertices.layer_id(size_t(k) + 1)) / layers_per;
            id = static_cast<uint32_t>((lc * rows + row) * cols + col);
        }
        key[i] = id;
        ++start[size_t(id) + 1];
    }
    size_t used = 0;
    for (size_t id = 0; id + 1 < start.size(); ++id)
    {
        used += start[id + 1] != 0 ? 1 : 0;
        start[id + 1] += start[id];
    }
    // Scattering advances start[id] to the end of id's slots, which is where id + 1's begin
    set.order.resize(n);
    for (size_t i = 0; i < n; ++i)
        set.order[start[key[i]]++] = enabled[i];
    std::vector<uint32_t>().swap(key);

    // Sub-cells: cell / 4 across, and bands of the chunk's layers
    const double sub_cell = cell / double(VIEW_CHUNK_SUBDIVISIONS);
    const uint64_t band_layers = std::max<uint64_t>(layers_per / VIEW_CHUNK_SUBDIVISIONS, 1);
    const uint64_t bands = (layers_per + band_layers - 1) / band_layers;
    const size_t sub_keys = size_t(bands) * VIEW_CHUNK_SUBDIVISIONS * VIEW_CHUNK_SUBDIVISIONS;
    // A segment's sub-cell key within its chunk (0 without a midpoint, the loose chunk's only sub-cell)
    auto sub_key = [&](uint32_t k) -> uint32_t
    {
        double mx, my;
        if (!midpoint(vertices, k, mx, my))
            return 0;
        const uint64_t col = std::min(static_cast<uint64_t>(std::floor((mx - min_x) / cell)), cols - 1);
        const uint64_t row = std::min(static_cast<uint64_t>(std::floor((my - min_y) / cell)), rows - 1);
        const double sx = std::floor((mx - min_x) / sub_cell) - double(col * VIEW_CHUNK_SUBDIVISIONS);
        const double sy = std::floor((my - min_y) / sub_cell) - double(row * VIEW_CHUNK_SUBDIVISIONS);
        const uint64_t last = VIEW_CHUNK_SUBDIVISIONS - 1;
        const uint64_t sub_col = sx <= 0.0 ? 0 : std::min(static_cast<uint64_t>(sx), last);
        const uint64_t sub_row = sy <= 0.0 ? 0 : std::min(static_cast<uint64_t>(sy), last);
        const uint64_t band = std::min((layer_ordinal(vertices.layer_id(size_t(k) + 1)) % layers_per) / band_layers,
                                       bands - 1);
        return static_cast<uint32_t>((band * VIEW_CHUNK_SUBDIVISIONS + sub_row) * VIEW_CHUNK_SUBDIVISIONS + sub_col);
    };
    std::vector<uint32_t> members;
    std::vector<uint32_t> member_keys;
    std::vector<uint32_t> sub_start(sub_keys + 1, 0);

    set.chunks.reserve(used);
    set.subcell_first.reserve(used + 1);
    uint32_t begin = 0;
    for (size_t id = 0; id + 1 < start.size(); ++id)
    {
        const uint32_t end = start[id];
        if (end == begin)
            continue;
        ViewChunk chunk;
        chunk.first = begin;
        chunk.count = end - begin;
        chunk.first_subcell = static_cast<uint32_t>(set.subcell_first.size());
        if (id == loose_id)
        {
            set.subcell_first.push_back(begin);
            chunk.subcell_count = 1;
        }
        else
        {
            // Counting sort of the chunk's segments by sub-cell key, stable
            members.assign(set.order.begin() + begin, set.order.begin() + end);
            member_keys.resize(members.size());
            std::fill(sub_start.begin(), sub_start.end(), 0);
            for (size_t j = 0; j < members.size(); ++j)
            {
                member_keys[j] = sub_key(members[j]);
                ++sub_start[size_t(member_keys[j]) + 1];
            }
            for (size_t s = 0; s < sub_keys; ++s)
            {
                if (sub_start[s + 1] != 0)
                {
                    set.subcell_first.push_back(begin + sub_start[s]);
                    ++chunk.subcell_count;
                }
                sub_start[s + 1] += sub_start[s];
            }
            for (size_t j = 0; j < members.size(); ++j)
                set.order[begin + sub_start[member_keys[j]]++] = members[j];
        }
        finish_chunk(vertices, set.order, id == loose_id, chunk);
        set.chunks.push_back(chunk);
        begin = end;
    }
    set.subcell_first.push_back(static_cast<uint32_t>(n));
    return set;
}

size_t select_view_chunks(const ViewChunkSet &set, const ViewCullParams &params, std::vector<uint32_t> &out)
{
    // Gribb/Hartmann: with row i of the matrix, the planes row3 + row_a and row3 - row_a for a = x, y, z
    float planes[6][4];
    const float *m = params.view_proj;
    for (int a = 0; a < 3; ++a)
        for (int c = 0; c < 4; ++c)
        {
            planes[2 * a][c] = m[4 * c + 3] + m[4 * c + a];
            planes[2 * a + 1][c] = m[4 * c + 3] - m[4 * c + a];
        }

    // The box lies wholly on the negative side of a plane: even its corner farthest along the plane's normal does
    auto outside_frustum = [&](const ViewChunk &c)
    {
        for (const auto &p : planes)
        {
            const float d = p[0] * (p[0] >= 0.0f ? c.max[0] : c.min[0]) + p[1] * (p[1] >= 0.0f ? c.max[1] : c.min[1]) +
                            p[2] * (p[2] >= 0.0f ? c.max[2] : c.min[2]) + p[3];
            if (d < 0.0f)
                return true;
        }
        return false;
    };

    size_t kept = 0;
    const size_t total = set.order.size();
    for (const ViewChunk &c : set.chunks)
    {
        if (c.count == 0 || size_t(c.first) >= total)
            continue;
        if (outside_frustum(c))
            continue;
        const size_t last = std::min(total, size_t(c.first) + size_t(c.count));
        out.insert(out.end(), set.order.begin() + c.first, set.order.begin() + last);
        ++kept;
    }
    return kept;
}

std::vector<uint32_t> subcell_of_order(const ViewChunkSet &set)
{
    std::vector<uint32_t> out;
    if (set.subcell_first.size() < 2)
        return out;
    out.assign(set.order.size(), 0);
    const size_t subcells = set.subcell_first.size() - 1;
    for (size_t s = 0; s < subcells; ++s)
    {
        const size_t lo = std::min<size_t>(set.subcell_first[s], out.size());
        const size_t hi = std::min<size_t>(set.subcell_first[s + 1], out.size());
        for (size_t p = lo; p < hi; ++p)
            out[p] = static_cast<uint32_t>(s);
    }
    return out;
}

} // namespace libvgcode
