///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ ported from lib/Slic3r/Fill/Concentric.pm:
///|/ Copyright (c) Prusa Research 2016 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <vector>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "FillPlanePath.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"

namespace Slic3r
{

class InfillPolylineClipper : public FillPlanePath::InfillPolylineOutput
{
public:
    InfillPolylineClipper(const BoundingBox bbox, const double scale_out)
        : FillPlanePath::InfillPolylineOutput(scale_out), m_bbox(bbox)
    {
    }

    void add_point(const Vec2d &pt);
    Points &&result() { return std::move(m_out); }
    bool clips() const override { return true; }

private:
    enum class Side
    {
        Left = 1,
        Right = 2,
        Top = 4,
        Bottom = 8
    };

    int sides(const Point &p) const
    {
        return int(p.x() < m_bbox.min.x()) * int(Side::Left) + int(p.x() > m_bbox.max.x()) * int(Side::Right) +
               int(p.y() < m_bbox.min.y()) * int(Side::Bottom) + int(p.y() > m_bbox.max.y()) * int(Side::Top);
    };

    // Bounding box to clip the polyline with.
    BoundingBox m_bbox;

    // Classification of the two last points processed.
    int m_sides_prev;
    int m_sides_this;
};

void InfillPolylineClipper::add_point(const Vec2d &fpt)
{
    const Point pt{this->scaled(fpt)};

    if (m_out.size() < 2)
    {
        // Collect the two first points and their status.
        (m_out.empty() ? m_sides_prev : m_sides_this) = sides(pt);
        m_out.emplace_back(pt);
    }
    else
    {
        // Classify the last inserted point, possibly remove it.
        int sides_next = sides(pt);
        if ( // This point is inside. Take it.
            m_sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            (m_sides_prev & m_sides_this & sides_next) == 0)
        {
            // Keep the last point.
            m_sides_prev = m_sides_this;
        }
        else
        {
            // All the three points (this, prev, next) are outside at the same side.
            // Ignore the last point.
            m_out.pop_back();
        }
        // And save the current point.
        m_out.emplace_back(pt);
        m_sides_this = sides_next;
    }
}

// Process polyline segments by spatial region to minimize travel moves.
// Similar to process_concentric_loops_by_region in FillConcentric.cpp, but adapted for open polylines.
// Groups segments that are spatially close together and processes each group completely before moving
// to the next, using nearest-neighbor ordering within and between groups.
namespace
{

// Structure to hold a polyline segment with metadata for spatial grouping
struct SegmentNode
{
    size_t original_index;
    Point centroid;
    BoundingBox bbox;
};

void process_planepath_segments_by_region(Polylines &segments, Polylines &polylines_out, Point &last_pos)
{
    if (segments.empty())
        return;

    if (segments.size() == 1)
    {
        // Single segment - just output it, possibly reversing to start from nearest end
        Polyline &seg = segments.front();
        if (seg.size() >= 2)
        {
            double dist_front = (seg.first_point() - last_pos).cast<double>().squaredNorm();
            double dist_back = (seg.last_point() - last_pos).cast<double>().squaredNorm();
            if (dist_back < dist_front)
                seg.reverse();
        }
        polylines_out.emplace_back(std::move(seg));
        last_pos = polylines_out.back().last_point();
        return;
    }

    // Build nodes with spatial metadata
    std::vector<SegmentNode> nodes;
    nodes.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const Polyline &seg = segments[i];
        if (seg.empty())
            continue;

        SegmentNode node;
        node.original_index = i;
        node.bbox = get_extents(seg);
        // Centroid is the center of the bounding box
        node.centroid = node.bbox.center();
        nodes.push_back(std::move(node));
    }

    if (nodes.empty())
        return;

    // Use spatial clustering based on bounding box containment/overlap.
    // Segments whose bounding boxes overlap or are very close belong to the same region.
    // This groups segments that fill the same spatial area.

    // Compute the average segment bounding box size for proximity threshold
    double avg_bbox_size = 0;
    for (const SegmentNode &node : nodes)
    {
        avg_bbox_size += node.bbox.size().x() + node.bbox.size().y();
    }
    avg_bbox_size /= (2.0 * nodes.size());

    // Proximity threshold: segments within this distance are considered same region
    // Use 3x average bbox size to catch segments that are clearly in the same area
    const double proximity_threshold_sq = (avg_bbox_size * 3.0) * (avg_bbox_size * 3.0);

    // Build region groups using union-find approach based on centroid proximity
    std::vector<size_t> region_id(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        region_id[i] = i; // Initially each node is its own region

    // Union-find helper functions
    std::function<size_t(size_t)> find_root = [&](size_t i) -> size_t
    {
        while (region_id[i] != i)
        {
            region_id[i] = region_id[region_id[i]]; // Path compression
            i = region_id[i];
        }
        return i;
    };

    auto union_regions = [&](size_t a, size_t b)
    {
        size_t root_a = find_root(a);
        size_t root_b = find_root(b);
        if (root_a != root_b)
            region_id[root_a] = root_b;
    };

    // Group nodes that are close together
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        for (size_t j = i + 1; j < nodes.size(); ++j)
        {
            double dist_sq = (nodes[i].centroid - nodes[j].centroid).cast<double>().squaredNorm();
            if (dist_sq < proximity_threshold_sq)
            {
                union_regions(i, j);
            }
            // Also check if bounding boxes overlap
            else if (nodes[i].bbox.overlap(nodes[j].bbox))
            {
                union_regions(i, j);
            }
        }
    }

    // Collect nodes by region
    std::map<size_t, std::vector<size_t>> regions; // root -> node indices
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        size_t root = find_root(i);
        regions[root].push_back(i);
    }

    // Process regions using nearest-neighbor ordering
    std::vector<bool> region_processed(nodes.size(), false);
    std::vector<bool> node_processed(nodes.size(), false);

    // Convert regions map to vector for easier nearest-neighbor processing
    std::vector<std::pair<size_t, std::vector<size_t>>> region_list(regions.begin(), regions.end());

    for (size_t r = 0; r < region_list.size(); ++r)
    {
        // Find nearest unprocessed region
        double best_region_dist = std::numeric_limits<double>::max();
        size_t best_region_idx = 0;

        for (size_t ri = 0; ri < region_list.size(); ++ri)
        {
            size_t root = region_list[ri].first;
            if (region_processed[root])
                continue;

            // Find distance to nearest node in this region
            for (size_t node_idx : region_list[ri].second)
            {
                double dist = (nodes[node_idx].centroid - last_pos).cast<double>().squaredNorm();
                if (dist < best_region_dist)
                {
                    best_region_dist = dist;
                    best_region_idx = ri;
                }
            }
        }

        size_t root = region_list[best_region_idx].first;
        region_processed[root] = true;
        std::vector<size_t> &region_nodes = region_list[best_region_idx].second;

        // Process all nodes in this region using nearest-neighbor ordering
        for (size_t n = 0; n < region_nodes.size(); ++n)
        {
            // Find nearest unprocessed node in this region
            double best_dist = std::numeric_limits<double>::max();
            size_t best_node_idx = 0;
            bool best_reverse = false;

            for (size_t node_idx : region_nodes)
            {
                if (node_processed[node_idx])
                    continue;

                Polyline &seg = segments[nodes[node_idx].original_index];
                if (seg.size() < 2)
                    continue;

                // Check distance to both ends of the segment
                double dist_front = (seg.first_point() - last_pos).cast<double>().squaredNorm();
                double dist_back = (seg.last_point() - last_pos).cast<double>().squaredNorm();
                double dist = std::min(dist_front, dist_back);

                if (dist < best_dist)
                {
                    best_dist = dist;
                    best_node_idx = node_idx;
                    best_reverse = (dist_back < dist_front);
                }
            }

            if (best_dist == std::numeric_limits<double>::max())
                break; // All nodes in region processed

            node_processed[best_node_idx] = true;
            Polyline seg = std::move(segments[nodes[best_node_idx].original_index]);
            if (best_reverse)
                seg.reverse();
            last_pos = seg.last_point();
            polylines_out.emplace_back(std::move(seg));
        }
    }
}

} // anonymous namespace

void FillPlanePath::_fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                                         const std::pair<float, Point> &direction, ExPolygon expolygon,
                                         Polylines &polylines_out)
{
    // PlanePath patterns (Archimedean, Hilbert, Octagram) divide by density to calculate spacing.
    // Invalid density causes divide-by-zero crashes with no debug output (silent app closure).
    if (params.density <= 0.0001f || std::isnan(params.density) || std::isinf(params.density))
    {
        return; // Can't generate PlanePath infill with invalid density
    }

    expolygon.rotate(-direction.first);

    //FIXME Vojtech: We are not sure whether the user expects the fill patterns on visible surfaces to be aligned across all the islands of a single layer.
    // One may align for this->centered() to align the patterns for Archimedean Chords and Octagram Spiral patterns.
    const bool align = params.density < 0.995;

    BoundingBox snug_bounding_box = get_extents(expolygon).inflated(SCALED_EPSILON);

    // Rotated bounding box of the area to fill in with the pattern.
    BoundingBox bounding_box =
        align
            ?
            // Sparse infill needs to be aligned across layers. Align infill across layers using the object's bounding box.
            this->bounding_box.rotated(-direction.first)
            :
            // Solid infill does not need to be aligned across layers, generate the infill pattern
            // around the clipping expolygon only.
            snug_bounding_box;

    Point shift = this->centered() ? bounding_box.center() : bounding_box.min;
    expolygon.translate(-shift.x(), -shift.y());
    bounding_box.translate(-shift.x(), -shift.y());

    Polyline polyline;
    {
        auto distance_between_lines = scaled<double>(this->spacing) / params.density;
        auto min_x = coord_t(ceil(coordf_t(bounding_box.min.x()) / distance_between_lines));
        auto min_y = coord_t(ceil(coordf_t(bounding_box.min.y()) / distance_between_lines));
        auto max_x = coord_t(ceil(coordf_t(bounding_box.max.x()) / distance_between_lines));
        auto max_y = coord_t(ceil(coordf_t(bounding_box.max.y()) / distance_between_lines));
        auto resolution = scaled<double>(params.resolution) / distance_between_lines;
        if (align)
        {
            // Filling in a bounding box over the whole object, clip generated polyline against the snug bounding box.
            snug_bounding_box.translate(-shift.x(), -shift.y());
            InfillPolylineClipper output(snug_bounding_box, distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, output);
            polyline.points = std::move(output.result());
        }
        else
        {
            // Filling in a snug bounding box, no need to clip.
            InfillPolylineOutput output(distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, output);
            polyline.points = std::move(output.result());
        }
    }

    if (polyline.size() >= 2)
    {
        Polylines polylines = intersection_pl(polyline, expolygon);
        Polylines chained;

        // Use region-based processing to minimize travel moves.
        // This groups spatially close segments together and processes each group completely
        // before moving to the next, similar to how Concentric fill handles regions.
        Point last_pos = params.start_near ? *params.start_near : Point(0, 0);
        process_planepath_segments_by_region(polylines, chained, last_pos);

        // If the pattern produced insufficient fill, fall back to simple rectilinear lines.
        // This handles edge cases where spiral/hilbert/octagram patterns fail or produce
        // only tiny fragments for certain shape geometries.
        const coord_t line_spacing = coord_t(scaled<double>(this->spacing) / params.density);
        const double expolygon_area = std::abs(expolygon.area());

        // Calculate total length of generated fill
        double total_fill_length = 0;
        for (const Polyline &pl : chained)
            total_fill_length += pl.length();

        // Expected fill length: area / line_spacing (approximate)
        // If actual fill is less than 50% of expected, use fallback
        const double expected_fill_length = expolygon_area / line_spacing;
        const bool fill_is_insufficient = (total_fill_length < expected_fill_length * 0.5) &&
                                          (expolygon_area > line_spacing * line_spacing);

        if (fill_is_insufficient)
        {
            // Clear the inadequate fill and generate simple horizontal lines as fallback
            chained.clear();
            for (coord_t y = bounding_box.min.y(); y <= bounding_box.max.y(); y += line_spacing)
            {
                Polyline line;
                line.points.emplace_back(bounding_box.min.x(), y);
                line.points.emplace_back(bounding_box.max.x(), y);
                Polylines clipped = intersection_pl(line, expolygon);
                append(chained, std::move(clipped));
            }
            chained = chain_polylines(std::move(chained), params.start_near ? &(*params.start_near) : nullptr);
        }

        // paths must be repositioned and rotated back
        for (Polyline &pl : chained)
        {
            pl.translate(shift.x(), shift.y());
            pl.rotate(direction.first);
        }
        append(polylines_out, std::move(chained));
    }
}

// Follow an Archimedean spiral, in polar coordinates: r=a+b\theta
template<typename Output>
static void generate_archimedean_chords(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y,
                                        const double resolution, Output &output)
{
    // Radius to achieve.
    coordf_t rmax = std::sqrt(coordf_t(max_x) * coordf_t(max_x) + coordf_t(max_y) * coordf_t(max_y)) * std::sqrt(2.) +
                    1.5;
    // Now unwind the spiral.
    coordf_t a = 1.;
    coordf_t b = 1. / (2. * M_PI);
    coordf_t theta = 0.;
    coordf_t r = 1;
    Pointfs out;
    //FIXME Vojtech: If used as a solid infill, there is a gap left at the center.
    output.add_point({0, 0});
    output.add_point({1, 0});
    while (r < rmax)
    {
        // Discretization angle to achieve a discretization error lower than resolution.
        // acos() domain is [-1, 1]. If (1. - resolution / r) is outside this range,
        // acos() returns NaN, causing silent crashes. Clamp input to valid range.
        coordf_t acos_input = 1. - resolution / r;
        acos_input = std::max(-1., std::min(1., acos_input)); // Clamp to [-1, 1]
        theta += 2. * acos(acos_input);
        r = a + b * theta;
        output.add_point({r * cos(theta), r * sin(theta)});
    }
}

void FillArchimedeanChords::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y,
                                     const double resolution, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution,
                                    static_cast<InfillPolylineClipper &>(output));
    else
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, output);
}

// Adapted from
// http://cpansearch.perl.org/src/KRYDE/Math-PlanePath-122/lib/Math/PlanePath/HilbertCurve.pm
//
// state=0    3--2   plain
//               |
//            0--1
//
// state=4    1--2  transpose
//            |  |
//            0  3
//
// state=8
//
// state=12   3  0  rot180 + transpose
//            |  |
//            2--1
//
static inline Point hilbert_n_to_xy(const size_t n)
{
    static constexpr const int next_state[16]{4, 0, 0, 12, 0, 4, 4, 8, 12, 8, 8, 4, 8, 12, 12, 0};
    static constexpr const int digit_to_x[16]{0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0};
    static constexpr const int digit_to_y[16]{0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1};

    // Number of 2 bit digits.
    size_t ndigits = 0;
    {
        size_t nc = n;
        while (nc > 0)
        {
            nc >>= 2;
            ++ndigits;
        }
    }
    int state = (ndigits & 1) ? 4 : 0;
    coord_t x = 0;
    coord_t y = 0;
    for (int i = (int) ndigits - 1; i >= 0; --i)
    {
        int digit = (n >> (i * 2)) & 3;
        state += digit;
        x |= digit_to_x[state] << i;
        y |= digit_to_y[state] << i;
        state = next_state[state];
    }
    return Point(x, y);
}

template<typename Output>
static void generate_hilbert_curve(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    // Minimum power of two square to fit the domain.
    size_t sz = 2;
    size_t pw = 1;
    {
        size_t sz0 = std::max(max_x + 1 - min_x, max_y + 1 - min_y);
        while (sz < sz0)
        {
            sz = sz << 1;
            ++pw;
        }
    }

    size_t sz2 = sz * sz;
    output.reserve(sz2);
    for (size_t i = 0; i < sz2; ++i)
    {
        Point p = hilbert_n_to_xy(i);
        output.add_point({p.x() + min_x, p.y() + min_y});
    }
}

void FillHilbertCurve::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y,
                                const double /* resolution */, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_hilbert_curve(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper &>(output));
    else
        generate_hilbert_curve(min_x, min_y, max_x, max_y, output);
}

template<typename Output>
static void generate_octagram_spiral(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    // Radius to achieve.
    coordf_t rmax = std::sqrt(coordf_t(max_x) * coordf_t(max_x) + coordf_t(max_y) * coordf_t(max_y)) * std::sqrt(2.) +
                    1.5;
    // Now unwind the spiral.
    coordf_t r = 0;
    coordf_t r_inc = sqrt(2.);
    output.add_point({0., 0.});
    while (r < rmax)
    {
        r += r_inc;
        coordf_t rx = r / sqrt(2.);
        coordf_t r2 = r + rx;
        output.add_point({r, 0.});
        output.add_point({r2, rx});
        output.add_point({rx, rx});
        output.add_point({rx, r2});
        output.add_point({0., r});
        output.add_point({-rx, r2});
        output.add_point({-rx, rx});
        output.add_point({-r2, rx});
        output.add_point({-r, 0.});
        output.add_point({-r2, -rx});
        output.add_point({-rx, -rx});
        output.add_point({-rx, -r2});
        output.add_point({0., -r});
        output.add_point({rx, -r2});
        output.add_point({rx, -rx});
        output.add_point({r2 + r_inc, -rx});
    }
}

void FillOctagramSpiral::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y,
                                  const double /* resolution */, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_octagram_spiral(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper &>(output));
    else
        generate_octagram_spiral(min_x, min_y, max_x, max_y, output);
}

} // namespace Slic3r
