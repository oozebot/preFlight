///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "PrintChunks.hpp"
#include "PreparedLoadData.hpp" // path_vertex_view
#include "Settings.hpp"
#include "ViewRange.hpp"
#include "SealedBeads.hpp" // sealed_bead_drawn
#include "ViewTables.hpp"  // segment_type_bit, segment_enabled, enabled_lists_range, visible_segment_types

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace libvgcode
{

namespace
{

constexpr float UNBOUNDED = std::numeric_limits<float>::max();
constexpr double INF = std::numeric_limits<double>::infinity();
// The loops report their progress every 65536 segments
constexpr size_t PROGRESS_MASK = 0xFFFF;
// The share of the build's progress each part ends at: the segment list, the sort into chunks, the sub-cell tables
constexpr float PROGRESS_LIST_END = 0.2f;
constexpr float PROGRESS_SORT_END = 0.6f;

// The position the shader reads for a vertex: an extrusion vertex half its height below its top, any other vertex
// where it is, in float as the position buffer's fill computes it
void shader_position(const PathVertex &v, double p[3])
{
    float z = v.position[2];
    if (v.type == EMoveType::Extrude)
        z -= 0.5f * v.height;
    p[0] = v.position[0];
    p[1] = v.position[1];
    p[2] = z;
}

// Half the height and half the width the shader reads for a vertex: the radius of a travel or a wipe, the vertex's own
// sizes otherwise (their magnitude: the imposter's offsets are symmetric)
void shader_half_sizes(const PathVertex &v, float travels_radius, float wipes_radius, double &hh, double &hw)
{
    double h = v.height;
    double w = v.width;
    if (v.type == EMoveType::Travel)
        h = w = travels_radius;
    else if (v.type == EMoveType::Wipe)
        h = w = wipes_radius;
    hh = 0.5 * std::abs(h);
    hw = 0.5 * std::abs(w);
}

// Widens [lo, hi] by the bounds of segment k (the box rule); false when a value it reads is not finite
bool add_segment_bounds(const std::vector<PathVertex> &vertices, size_t k, float travels_radius, float wipes_radius,
                        double lo[3], double hi[3])
{
    const PathVertex *ends[2] = {&vertices[k], &vertices[k + 1]};
    double p[2][3];
    double hh[2];
    double hw[2];
    for (int e = 0; e < 2; ++e)
    {
        shader_position(*ends[e], p[e]);
        shader_half_sizes(*ends[e], travels_radius, wipes_radius, hh[e], hw[e]);
        if (!std::isfinite(p[e][0]) || !std::isfinite(p[e][1]) || !std::isfinite(p[e][2]) || !std::isfinite(hh[e]) ||
            !std::isfinite(hw[e]))
            return false;
    }
    // The line's direction decides the reach per axis only where the shader's rounding cannot change its branch
    const double d[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
    const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const double dir_z = length > 0.0 ? std::abs(d[2]) / length : 1.0;
    const bool level = length >= double(PRINT_CHUNK_SHORT_LINE_MM) && dir_z <= double(PRINT_CHUNK_LEVEL_DIR_Z);
    for (int e = 0; e < 2; ++e)
    {
        const double reach_xy = level ? std::max(hw[e], hh[e] * dir_z) : std::max(hw[e], hh[e]);
        const double reach_z = level ? std::max(hh[e], hw[e] * dir_z) : std::max(hw[e], hh[e]);
        const double reach[3] = {reach_xy, reach_xy, reach_z};
        for (int a = 0; a < 3; ++a)
        {
            lo[a] = std::min(lo[a], p[e][a] - reach[a]);
            hi[a] = std::max(hi[a], p[e][a] + reach[a]);
        }
    }
    return true;
}

// The largest float not above x, and the smallest not below it
float float_down(double x)
{
    float f = static_cast<float>(x);
    if (double(f) > x)
        f = std::nextafter(f, -std::numeric_limits<float>::infinity());
    return f;
}
float float_up(double x)
{
    float f = static_cast<float>(x);
    if (double(f) < x)
        f = std::nextafter(f, std::numeric_limits<float>::infinity());
    return f;
}

void make_unbounded(float *box)
{
    for (int a = 0; a < 3; ++a)
    {
        box[a] = -UNBOUNDED;
        box[3 + a] = UNBOUNDED;
    }
}

// Sub-cell s's box from its segments (the box rule)
void compute_subcell_box(PrintChunks &chunks, const std::vector<PathVertex> &vertices, size_t s)
{
    float *box = &chunks.box[6 * s];
    const size_t first = chunks.set.subcell_first[s];
    const size_t last = chunks.set.subcell_first[s + 1];
    double lo[3] = {INF, INF, INF};
    double hi[3] = {-INF, -INF, -INF};
    bool bounded = first < last;
    for (size_t j = first; j < last && bounded; ++j)
    {
        const size_t k = chunks.set.order[j];
        bounded = k + 1 < vertices.size() &&
                  add_segment_bounds(vertices, k, chunks.travels_radius, chunks.wipes_radius, lo, hi);
    }
    if (bounded)
        for (int a = 0; a < 3; ++a)
        {
            box[a] = float_down(lo[a]);
            box[3 + a] = float_up(hi[a]);
            // A bound past the float range is no bound
            bounded = bounded && box[a] >= -UNBOUNDED && box[3 + a] <= UNBOUNDED;
        }
    if (!bounded)
        make_unbounded(box);
}

// Chunk c's box: the union of its sub-cells' boxes
void compute_chunk_box(PrintChunks &chunks, size_t c)
{
    const ViewChunk &chunk = chunks.set.chunks[c];
    float *box = &chunks.chunk_box[6 * c];
    for (int a = 0; a < 3; ++a)
    {
        box[a] = UNBOUNDED;
        box[3 + a] = -UNBOUNDED;
    }
    const size_t end = size_t(chunk.first_subcell) + chunk.subcell_count;
    for (size_t s = chunk.first_subcell; s < end; ++s)
    {
        const float *sub = &chunks.box[6 * s];
        for (int a = 0; a < 3; ++a)
        {
            box[a] = std::min(box[a], sub[a]);
            box[3 + a] = std::max(box[3 + a], sub[3 + a]);
        }
    }
    if (chunk.subcell_count == 0)
        make_unbounded(box);
}

// Whether segment k is enabled under the filter: compute_enabled_lists()'s test, with its sealed bead test when the
// filter carries one (a segment's layer is its end vertex's)
bool segment_passes(const std::vector<PathVertex> &vertices, const PrintChunkFilter &filter, uint32_t k)
{
    if (filter.valid_lines == nullptr || k < filter.first || k >= filter.last || size_t(k) + 1 >= vertices.size() ||
        k >= filter.valid_lines->size)
        return false;
    if (!segment_enabled(vertices[k], (*filter.valid_lines)[k], filter.types))
        return false;
    return filter.sealed == nullptr || !vertices[k].is_extrusion() ||
           sealed_bead_drawn(*filter.sealed, k, vertices[size_t(k) + 1].layer_id, filter.sealed_first_layer,
                             filter.sealed_last_layer);
}

// Whether a sub-cell can hold no enabled segment, from its tables alone
bool subcell_excluded(const PrintChunks &chunks, const PrintChunkFilter &filter, size_t s)
{
    return filter.valid_lines == nullptr || (chunks.types[s] & filter.types) == 0 ||
           chunks.first_id[s] >= filter.last || chunks.last_id[s] < filter.first;
}

// The enabled segments of the visible sub-cells, each handed to `take`; their count
template<class Take>
size_t visit_enabled(const PrintChunks &chunks, const std::vector<PathVertex> &vertices, const PrintChunkFilter &filter,
                     const std::vector<uint8_t> &visible_subcells, Take take)
{
    if (!chunks.valid || filter.valid_lines == nullptr || filter.first >= filter.last)
        return 0;
    const size_t subcells = std::min(chunks.subcells(), visible_subcells.size());
    size_t count = 0;
    for (size_t s = 0; s < subcells; ++s)
    {
        // A sub-cell no segment of which can pass is skipped whole
        if (visible_subcells[s] == 0 || subcell_excluded(chunks, filter, s))
            continue;
        const size_t end = chunks.set.subcell_first[s + 1];
        for (size_t j = chunks.set.subcell_first[s]; j < end; ++j)
        {
            const uint32_t k = chunks.set.order[j];
            if (!segment_passes(vertices, filter, k))
                continue;
            take(k);
            ++count;
        }
    }
    return count;
}

// Runs work(first, last) over the blocks of `block` items that cover [0, count), on up to `threads` threads, the
// calling thread included (0: the hardware concurrency); a thread that fails to start leaves its blocks to the others.
// Rethrows the first exception a block threw, after every thread has stopped.
template<class Work>
void parallel_blocks(size_t count, size_t block, unsigned threads, const Work &work)
{
    const size_t blocks = (count + block - 1) / block;
    if (threads == 0)
        threads = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    const auto worker = [&]()
    {
        for (size_t b = next.fetch_add(1); b < blocks && !failed.load(); b = next.fetch_add(1))
        {
            try
            {
                work(b * block, std::min(count, (b + 1) * block));
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
    const size_t helpers = blocks > 1 ? std::min<size_t>(threads, blocks) - 1 : 0;
    std::vector<std::thread> pool;
    try
    {
        pool.reserve(helpers);
        for (size_t t = 0; t < helpers; ++t)
            pool.emplace_back(worker);
    }
    catch (...)
    {
        // The threads started share the blocks with the calling thread
    }
    worker();
    for (std::thread &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}

// Sub-cells partitioned per block of the filter application
constexpr size_t APPLY_BLOCK = 4096;

} // namespace

size_t PrintChunks::bytes() const
{
    size_t total = set.order.capacity() * sizeof(uint32_t);
    total += set.chunks.capacity() * sizeof(ViewChunk);
    total += set.subcell_first.capacity() * sizeof(uint32_t);
    total += (box.capacity() + chunk_box.capacity()) * sizeof(float);
    for (const std::vector<uint32_t> *table : {&types, &first_id, &last_id, &first_layer, &last_layer, &chunk, &enabled,
                                               &cavity_lo_min, &cavity_lo_max, &cavity_hi_min, &cavity_hi_max})
        total += table->capacity() * sizeof(uint32_t);
    return total;
}

PrintChunks build_print_chunks(const std::vector<PathVertex> &vertices, const BitSet<> &valid_lines,
                               float travels_radius, float wipes_radius, const std::function<void(float)> &progress)
{
    PrintChunks chunks;
    chunks.travels_radius = travels_radius;
    chunks.wipes_radius = wipes_radius;
    const size_t count = vertices.size();
    // Segment indices are uint32_t
    if (count >= size_t(std::numeric_limits<uint32_t>::max()))
        return chunks;
    const auto report = [&progress](float fraction)
    {
        if (progress)
            progress(fraction);
    };

    // The segments the Preview can draw, in id order: counted first so the list is allocated once
    const size_t lines = std::min(count > 0 ? count - 1 : 0, valid_lines.size);
    const auto qualifies = [&](size_t k)
    {
        return valid_lines[k] && segment_type_bit(vertices[k]) != 0;
    };
    size_t n = 0;
    for (size_t k = 0; k < lines; ++k)
    {
        if ((k & PROGRESS_MASK) == PROGRESS_MASK)
            report(0.5f * PROGRESS_LIST_END * static_cast<float>(k) / static_cast<float>(lines));
        n += qualifies(k) ? 1 : 0;
    }
    if (n >= size_t(std::numeric_limits<uint32_t>::max()))
        return chunks;
    if (n == 0)
    {
        chunks.valid = true;
        report(1.0f);
        return chunks;
    }
    std::vector<uint32_t> segments;
    segments.reserve(n);
    for (size_t k = 0; k < lines; ++k)
    {
        if ((k & PROGRESS_MASK) == PROGRESS_MASK)
            report(PROGRESS_LIST_END * (0.5f + 0.5f * static_cast<float>(k) / static_cast<float>(lines)));
        if (qualifies(k))
            segments.push_back(static_cast<uint32_t>(k));
    }
    report(PROGRESS_LIST_END);

    // The chunks and sub-cells; the arrays the sort grew are trimmed to their size
    chunks.set = build_view_chunks(path_vertex_view(vertices), segments);
    std::vector<uint32_t>().swap(segments);
    if (chunks.set.order.size() != n || chunks.set.subcell_first.size() < 2)
    {
        chunks = PrintChunks();
        return chunks;
    }
    chunks.set.chunks.shrink_to_fit();
    chunks.set.subcell_first.shrink_to_fit();
    report(PROGRESS_SORT_END);

    // The sub-cell tables
    const size_t subcells = chunks.subcells();
    chunks.box.resize(6 * subcells);
    chunks.types.assign(subcells, 0);
    chunks.first_id.assign(subcells, std::numeric_limits<uint32_t>::max());
    chunks.last_id.assign(subcells, 0);
    chunks.first_layer.assign(subcells, std::numeric_limits<uint32_t>::max());
    chunks.last_layer.assign(subcells, 0);
    chunks.chunk.assign(subcells, 0);
    for (size_t c = 0; c < chunks.set.chunks.size(); ++c)
    {
        const ViewChunk &chunk = chunks.set.chunks[c];
        const size_t end = std::min(size_t(chunk.first_subcell) + chunk.subcell_count, subcells);
        for (size_t s = chunk.first_subcell; s < end; ++s)
            chunks.chunk[s] = static_cast<uint32_t>(c);
    }
    size_t done = 0;
    for (size_t s = 0; s < subcells; ++s)
    {
        const size_t end = chunks.set.subcell_first[s + 1];
        for (size_t j = chunks.set.subcell_first[s]; j < end; ++j)
        {
            if ((++done & PROGRESS_MASK) == PROGRESS_MASK)
                report(PROGRESS_SORT_END +
                       (1.0f - PROGRESS_SORT_END) * static_cast<float>(done) / static_cast<float>(n));
            const uint32_t k = chunks.set.order[j];
            chunks.types[s] |= segment_type_bit(vertices[k]);
            chunks.first_id[s] = std::min(chunks.first_id[s], k);
            chunks.last_id[s] = std::max(chunks.last_id[s], k);
            for (const size_t v : {size_t(k), size_t(k) + 1})
            {
                chunks.first_layer[s] = std::min(chunks.first_layer[s], vertices[v].layer_id);
                chunks.last_layer[s] = std::max(chunks.last_layer[s], vertices[v].layer_id);
            }
        }
        compute_subcell_box(chunks, vertices, s);
    }
    chunks.chunk_box.resize(6 * chunks.set.chunks.size());
    for (size_t c = 0; c < chunks.set.chunks.size(); ++c)
        compute_chunk_box(chunks, c);
    chunks.valid = true;
    report(1.0f);
    return chunks;
}

void set_print_chunk_radii(PrintChunks &chunks, const std::vector<PathVertex> &vertices, float travels_radius,
                           float wipes_radius)
{
    if (!chunks.valid || (travels_radius == chunks.travels_radius && wipes_radius == chunks.wipes_radius))
        return;
    chunks.travels_radius = travels_radius;
    chunks.wipes_radius = wipes_radius;
    const uint32_t radius_types = SEGMENT_TYPE_TRAVEL | SEGMENT_TYPE_WIPE;
    std::vector<uint8_t> chunk_changed(chunks.set.chunks.size(), 0);
    for (size_t s = 0; s < chunks.subcells(); ++s)
        if ((chunks.types[s] & radius_types) != 0)
        {
            compute_subcell_box(chunks, vertices, s);
            chunk_changed[chunks.chunk[s]] = 1;
        }
    for (size_t c = 0; c < chunk_changed.size(); ++c)
        if (chunk_changed[c] != 0)
            compute_chunk_box(chunks, c);
}

PrintChunkFilter make_print_chunk_filter(const std::vector<PathVertex> &vertices, const ViewRange &view_range,
                                         const Interval &layers_range, const Settings &settings,
                                         const BitSet<> &valid_lines)
{
    PrintChunkFilter filter;
    filter.valid_lines = &valid_lines;
    if (vertices.empty())
        return filter;
    const Interval range = enabled_lists_range(vertices, view_range, layers_range, settings);
    filter.first = range[0];
    filter.last = range[1];
    filter.types = visible_segment_types(settings);
    return filter;
}

PrintChunkFilter with_sealed_beads(PrintChunkFilter filter, const SealedBeadData &sealed, const Interval &layers_range)
{
    filter.sealed = &sealed;
    filter.sealed_first_layer = static_cast<uint32_t>(layers_range[0]);
    filter.sealed_last_layer = static_cast<uint32_t>(layers_range[1]);
    return filter;
}

size_t print_chunk_flags(const PrintChunks &chunks, const PrintChunkFilter &filter, std::vector<uint8_t> &flags)
{
    const size_t subcells = chunks.valid ? chunks.subcells() : 0;
    flags.assign(subcells, 0);
    if (filter.valid_lines == nullptr || filter.first >= filter.last)
        return 0;
    size_t set = 0;
    for (size_t s = 0; s < subcells; ++s)
        if ((chunks.types[s] & filter.types) != 0 && chunks.first_id[s] < filter.last &&
            chunks.last_id[s] >= filter.first)
        {
            flags[s] = 1;
            ++set;
        }
    return set;
}

size_t emit_print_chunks(const PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                         const PrintChunkFilter &filter, const std::vector<uint8_t> &visible_subcells,
                         std::vector<uint32_t> &out)
{
    return visit_enabled(chunks, vertices, filter, visible_subcells, [&out](uint32_t k) { out.push_back(k); });
}

size_t count_print_chunks(const PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                          const PrintChunkFilter &filter, const std::vector<uint8_t> &visible_subcells)
{
    return visit_enabled(chunks, vertices, filter, visible_subcells, [](uint32_t) {});
}

PrintChunkApplication apply_print_chunk_filter(PrintChunks &chunks, const std::vector<PathVertex> &vertices,
                                               const PrintChunkFilter &filter, unsigned threads, bool incremental)
{
    PrintChunkApplication result;
    if (!chunks.valid)
        return result;
    const size_t subcells = chunks.subcells();
    const PrintChunkFilter &old = chunks.applied_filter;
    // The sealed test's layers moved on the same data: with that data's cavity spans known per sub-cell, only the
    // sub-cells the move can change sealed_bead_drawn() for are partitioned again
    const bool sealed_moved = filter.sealed != nullptr && old.sealed == filter.sealed &&
                              (old.sealed_first_layer != filter.sealed_first_layer ||
                               old.sealed_last_layer != filter.sealed_last_layer);
    const bool cavities_known = filter.sealed != nullptr && chunks.cavities_of == filter.sealed &&
                                chunks.cavity_lo_min.size() == subcells && chunks.cavity_lo_max.size() == subcells &&
                                chunks.cavity_hi_min.size() == subcells && chunks.cavity_hi_max.size() == subcells;
    // A sub-cell keeps its partition when no segment of it changes status: the valid lines and the sealed data are the
    // same, so only an id in one range and not the other, a type in one mask and not the other, or a moved sealed layer
    // can change one
    const bool full = !incremental || !chunks.applied || chunks.enabled.size() != subcells ||
                      old.valid_lines != filter.valid_lines || old.sealed != filter.sealed ||
                      (sealed_moved && !cavities_known);
    // The ids in one range and not the other lie in [min first, max first) and [min last, max last)
    const size_t low_first = std::min(old.first, filter.first);
    const size_t high_first = std::max(old.first, filter.first);
    const size_t low_last = std::min(old.last, filter.last);
    const size_t high_last = std::max(old.last, filter.last);
    const uint32_t changed_types = old.types ^ filter.types;
    // The sealed test's first and last layer, each from the lower to the higher of its two values
    const uint64_t sealed_first_low = std::min(old.sealed_first_layer, filter.sealed_first_layer);
    const uint64_t sealed_first_high = std::max(old.sealed_first_layer, filter.sealed_first_layer);
    const uint64_t sealed_last_low = std::min(old.sealed_last_layer, filter.sealed_last_layer);
    const uint64_t sealed_last_high = std::max(old.sealed_last_layer, filter.sealed_last_layer);
    // Whether a segment of sub-cell s can be drawn by sealed_bead_drawn() under one value of a moved layer and not the
    // other. A segment's layer lies in the sub-cell's layer range; the term "layer + 2 >= last" differs between the two
    // values of the last layer for layer + 2 in [low, high), the term "layer <= first + 2" between those of the first
    // for layer in (low + 2, high + 2], and the term "lo <= x <= hi" of a cavity span between x = low and x = high for lo
    // in (low, high] or hi in [low, high).
    const auto sealed_affected = [&](size_t s)
    {
        const uint64_t layer_lo = chunks.first_layer[s];
        const uint64_t layer_hi = chunks.last_layer[s];
        if (sealed_last_low != sealed_last_high && layer_lo + 2 < sealed_last_high && layer_hi + 2 >= sealed_last_low)
            return true;
        if (sealed_first_low != sealed_first_high && layer_hi > sealed_first_low + 2 &&
            layer_lo <= sealed_first_high + 2)
            return true;
        if (chunks.cavity_lo_min[s] > chunks.cavity_lo_max[s])
            return false;
        const auto cavity_end_between = [&](uint64_t low, uint64_t high)
        {
            return low != high && ((chunks.cavity_lo_min[s] <= high && chunks.cavity_lo_max[s] > low) ||
                                   (chunks.cavity_hi_min[s] < high && chunks.cavity_hi_max[s] >= low));
        };
        return cavity_end_between(sealed_last_low, sealed_last_high) ||
               cavity_end_between(sealed_first_low, sealed_first_high);
    };
    const auto affected = [&](size_t s)
    {
        if (full || (chunks.types[s] & changed_types) != 0)
            return true;
        const size_t lo = chunks.first_id[s];
        const size_t hi = chunks.last_id[s];
        if ((lo < high_first && hi >= low_first) || (lo < high_last && hi >= low_last))
            return true;
        return sealed_moved && sealed_affected(s);
    };
    // An application of the sealed test that partitions every sub-cell records the cavity spans of that sealed data
    const SealedBeadData *sealed = filter.sealed;
    const bool record_cavities = full && sealed != nullptr;

    chunks.applied = false;
    if (chunks.enabled.size() != subcells)
        chunks.enabled.assign(subcells, 0);
    if (record_cavities)
    {
        chunks.cavities_of = nullptr;
        chunks.cavity_lo_min.assign(subcells, std::numeric_limits<uint32_t>::max());
        chunks.cavity_lo_max.assign(subcells, 0);
        chunks.cavity_hi_min.assign(subcells, std::numeric_limits<uint32_t>::max());
        chunks.cavity_hi_max.assign(subcells, 0);
    }
    std::atomic<size_t> redone{0};
    std::atomic<int64_t> enabled_change{0};
    parallel_blocks(subcells, APPLY_BLOCK, threads,
                    [&](size_t first, size_t last)
                    {
                        std::vector<uint32_t> rest;
                        size_t count = 0;
                        int64_t change = 0;
                        for (size_t s = first; s < last; ++s)
                        {
                            if (!affected(s))
                                continue;
                            uint32_t *begin = chunks.set.order.data() + chunks.set.subcell_first[s];
                            uint32_t *end = chunks.set.order.data() + chunks.set.subcell_first[s + 1];
                            // Reserved first, so the slot is rewritten only once nothing can throw
                            rest.reserve(size_t(end - begin));
                            std::sort(begin, end);
                            // The enabled segments move to the front in their order, the others follow in theirs
                            rest.clear();
                            uint32_t *out = begin;
                            const bool excluded = subcell_excluded(chunks, filter, s);
                            for (uint32_t *p = begin; p != end; ++p)
                            {
                                if (!excluded && segment_passes(vertices, filter, *p))
                                    *out++ = *p;
                                else
                                    rest.push_back(*p);
                            }
                            std::copy(rest.begin(), rest.end(), out);
                            change += int64_t(out - begin) - int64_t(chunks.enabled[s]);
                            chunks.enabled[s] = uint32_t(out - begin);
                            if (record_cavities)
                                for (uint32_t *p = begin; p != end; ++p)
                                {
                                    const size_t k = *p;
                                    if (!vertices[k].is_extrusion() || k >= sealed->touches_outside.size() ||
                                        k >= sealed->cavity_lo.size() || k >= sealed->cavity_hi.size() ||
                                        sealed->touches_outside[k] != 0 || sealed->cavity_lo[k] > sealed->cavity_hi[k])
                                        continue;
                                    chunks.cavity_lo_min[s] = std::min(chunks.cavity_lo_min[s], sealed->cavity_lo[k]);
                                    chunks.cavity_lo_max[s] = std::max(chunks.cavity_lo_max[s], sealed->cavity_lo[k]);
                                    chunks.cavity_hi_min[s] = std::min(chunks.cavity_hi_min[s], sealed->cavity_hi[k]);
                                    chunks.cavity_hi_max[s] = std::max(chunks.cavity_hi_max[s], sealed->cavity_hi[k]);
                                }
                            ++count;
                        }
                        redone += count;
                        enabled_change += change;
                    });
    // A full application sums the counts afresh: one that failed before may have left the total behind
    if (full)
    {
        chunks.enabled_total = 0;
        for (const uint32_t n : chunks.enabled)
            chunks.enabled_total += n;
    }
    else
        chunks.enabled_total = size_t(int64_t(chunks.enabled_total) + enabled_change.load());
    if (record_cavities)
        chunks.cavities_of = sealed;
    chunks.applied_filter = filter;
    chunks.applied = true;
    result.subcells = redone.load();
    result.full = full;
    return result;
}

size_t emit_applied_print_chunks(const PrintChunks &chunks, const std::vector<uint8_t> &visible_subcells,
                                 std::vector<uint32_t> &out)
{
    if (!chunks.valid || !chunks.applied)
        return 0;
    const size_t subcells = std::min({chunks.subcells(), visible_subcells.size(), chunks.enabled.size()});
    const uint32_t *order = chunks.set.order.data();
    size_t count = 0;
    for (size_t s = 0; s < subcells; ++s)
        if (visible_subcells[s] != 0 && chunks.enabled[s] != 0)
        {
            const uint32_t *slot = order + chunks.set.subcell_first[s];
            out.insert(out.end(), slot, slot + chunks.enabled[s]);
            count += chunks.enabled[s];
        }
    return count;
}

size_t write_applied_print_chunks(const PrintChunks &chunks, const std::vector<uint32_t> &chunk_order,
                                  const std::vector<uint8_t> &visible_subcells, size_t limit, uint32_t *out)
{
    if (!chunks.valid || !chunks.applied)
        return 0;
    const size_t subcells = std::min({chunks.subcells(), visible_subcells.size(), chunks.enabled.size()});
    const uint32_t *order = chunks.set.order.data();
    size_t count = 0;
    for (const uint32_t c : chunk_order)
    {
        if (c >= chunks.set.chunks.size())
            continue;
        const ViewChunk &chunk = chunks.set.chunks[c];
        const size_t end = std::min(size_t(chunk.first_subcell) + chunk.subcell_count, subcells);
        for (size_t s = chunk.first_subcell; s < end && count < limit; ++s)
        {
            if (visible_subcells[s] == 0 || chunks.enabled[s] == 0)
                continue;
            const size_t n = std::min<size_t>(chunks.enabled[s], limit - count);
            if (out != nullptr)
            {
                const uint32_t *slot = order + chunks.set.subcell_first[s];
                std::copy(slot, slot + n, out + count);
            }
            count += n;
        }
        if (count >= limit)
            break;
    }
    return count;
}

} // namespace libvgcode
