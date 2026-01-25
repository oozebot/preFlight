///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

#include <clipper2/clipper.h>
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/ClipperZUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"

#include "LineSegmentation.hpp"

namespace Slic3r::Algorithm::LineSegmentation
{

const constexpr coord_t POINT_IS_ON_LINE_THRESHOLD_SQR = Slic3r::sqr(scaled<coord_t>(EPSILON));

struct ZAttributes
{
    bool is_clip_point = false;
    bool is_new_point = false;
    uint32_t point_index = 0;

    ZAttributes() = default;

    explicit ZAttributes(const uint32_t clipper_coord)
        : is_clip_point((clipper_coord >> 31) & 0x1)
        , is_new_point((clipper_coord >> 30) & 0x1)
        , point_index(clipper_coord & 0x3FFFFFFF)
    {
    }

    // With USINGZ enabled, Point64 has z member - can construct directly from Point64
    explicit ZAttributes(const Clipper2Lib::Point64 &clipper_pt) : ZAttributes(uint32_t(clipper_pt.z)) {}

    // Constructor for ZPoint (kept for API compatibility)
    explicit ZAttributes(const ClipperZUtils::ZPoint &zpt) : ZAttributes(uint32_t(zpt.z)) {}

    ZAttributes(const bool is_clip_point, const bool is_new_point, const uint32_t point_index)
        : is_clip_point(is_clip_point), is_new_point(is_new_point), point_index(point_index)
    {
        assert(this->point_index < (1u << 30) && "point_index exceeds 30 bits!");
    }

    // Encode the structure to uint32_t.
    constexpr uint32_t encode() const
    {
        assert(this->point_index < (1u << 30) && "point_index exceeds 30 bits!");
        return (this->is_clip_point << 31) | (this->is_new_point << 30) | (this->point_index & 0x3FFFFFFF);
    }

    // Decode the uint32_t to the structure.
    static ZAttributes decode(const uint32_t clipper_coord)
    {
        return {bool((clipper_coord >> 31) & 0x1), bool((clipper_coord >> 30) & 0x1), clipper_coord & 0x3FFFFFFF};
    }

    // With USINGZ enabled, Point64 has z member - can decode directly
    static ZAttributes decode(const Clipper2Lib::Point64 &pt) { return ZAttributes::decode(uint32_t(pt.z)); }
    // Decode from ZPoint (kept for API compatibility)
    static ZAttributes decode(const ClipperZUtils::ZPoint &zpt) { return ZAttributes::decode(uint32_t(zpt.z)); }
};

struct LineRegionRange
{
    size_t begin_idx; // Index of the line on which the region begins.
    double begin_t; // Scalar position on the begin_idx line in which the region begins. The value is from range <0., 1.>.
    size_t end_idx;  // Index of the line on which the region ends.
    double end_t;    // Scalar position on the end_idx line in which the region ends. The value is from range <0., 1.>.
    size_t clip_idx; // Index of clipping ExPolygons to identified which ExPolygons group contains this line.

    LineRegionRange(size_t begin_idx, double begin_t, size_t end_idx, double end_t, size_t clip_idx)
        : begin_idx(begin_idx), begin_t(begin_t), end_idx(end_idx), end_t(end_t), clip_idx(clip_idx)
    {
    }

    // Check if 'other' overlaps with this LineRegionRange.
    bool is_overlap(const LineRegionRange &other) const
    {
        if (this->end_idx < other.begin_idx || this->begin_idx > other.end_idx)
        {
            return false;
        }
        else if (this->end_idx == other.begin_idx && this->end_t <= other.begin_t)
        {
            return false;
        }
        else if (this->begin_idx == other.end_idx && this->begin_t >= other.end_t)
        {
            return false;
        }

        return true;
    }

    // Check if 'inner' is whole inside this LineRegionRange.
    bool is_inside(const LineRegionRange &inner) const
    {
        if (!this->is_overlap(inner))
        {
            return false;
        }

        const bool starts_after = (this->begin_idx < inner.begin_idx) ||
                                  (this->begin_idx == inner.begin_idx && this->begin_t <= inner.begin_t);
        const bool ends_before = (this->end_idx > inner.end_idx) ||
                                 (this->end_idx == inner.end_idx && this->end_t >= inner.end_t);

        return starts_after && ends_before;
    }

    bool is_zero_length() const { return this->begin_idx == this->end_idx && this->begin_t == this->end_t; }

    bool operator<(const LineRegionRange &rhs) const
    {
        return this->begin_idx < rhs.begin_idx || (this->begin_idx == rhs.begin_idx && this->begin_t < rhs.begin_t);
    }
};

using LineRegionRanges = std::vector<LineRegionRange>;

inline Point make_point(const ClipperZUtils::ZPoint &clipper_pt)
{
    return {clipper_pt.x, clipper_pt.y};
}

inline ClipperZUtils::ZPaths to_clip_zpaths(const ExPolygons &clips)
{
    return ClipperZUtils::expolygons_to_zpaths_with_same_z<false>(clips, coord_t(ZAttributes(true, false, 0).encode()));
}

static ClipperZUtils::ZPath subject_to_zpath(const Points &subject, const bool is_closed)
{
    ZAttributes z_attributes(false, false, 0);

    ClipperZUtils::ZPath out;
    if (!subject.empty())
    {
        out.reserve((subject.size() + is_closed) ? 1 : 0);
        for (const Point &p : subject)
        {
            out.emplace_back(p.x(), p.y(), z_attributes.encode());
            ++z_attributes.point_index;
        }

        if (is_closed)
        {
            // If it is closed, then duplicate the first point at the end to make a closed path open.
            out.emplace_back(subject.front().x(), subject.front().y(), z_attributes.encode());
        }
    }

    return out;
}

static ClipperZUtils::ZPath subject_to_zpath(const Arachne::ExtrusionLine &subject)
{
    // Closed Arachne::ExtrusionLine already has duplicated the last point.
    ZAttributes z_attributes(false, false, 0);

    ClipperZUtils::ZPath out;
    if (!subject.empty())
    {
        out.reserve(subject.size());
        for (const Arachne::ExtrusionJunction &junction : subject)
        {
            out.emplace_back(junction.p.x(), junction.p.y(), z_attributes.encode());
            ++z_attributes.point_index;
        }
    }

    return out;
}

static ClipperZUtils::ZPath subject_to_zpath(const Polyline &subject)
{
    return subject_to_zpath(subject.points, false);
}

[[maybe_unused]] static ClipperZUtils::ZPath subject_to_zpath(const Polygon &subject)
{
    return subject_to_zpath(subject.points, true);
}

static ClipperZUtils::ZPath subject_to_zpath(const Athena::ExtrusionLine &subject)
{
    // Closed Athena::ExtrusionLine already has duplicated the last point.
    ZAttributes z_attributes(false, false, 0);

    ClipperZUtils::ZPath out;
    if (!subject.empty())
    {
        out.reserve(subject.size());
        for (const Athena::ExtrusionJunction &junction : subject)
        {
            out.emplace_back(junction.p.x(), junction.p.y(), z_attributes.encode());
            ++z_attributes.point_index;
        }
    }

    return out;
}

struct ProjectionInfo
{
    double projected_t;
    double distance_sqr;
};

static ProjectionInfo project_point_on_line(const Point &line_from_pt, const Point &line_to_pt, const Point &query_pt)
{
    const Vec2d line_vec = (line_to_pt - line_from_pt).template cast<double>();
    const Vec2d query_vec = (query_pt - line_from_pt).template cast<double>();
    const double line_length_sqr = line_vec.squaredNorm();

    if (line_length_sqr <= 0.)
    {
        return {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    }

    const double projected_t = query_vec.dot(line_vec);
    const double projected_t_normalized = std::clamp(projected_t / line_length_sqr, 0., 1.);
    // Projected point have to line on the line.
    if (projected_t < 0. || projected_t > line_length_sqr)
    {
        return {projected_t_normalized, std::numeric_limits<double>::max()};
    }

    const Vec2d projected_vec = projected_t_normalized * line_vec;
    const double distance_sqr = (projected_vec - query_vec).squaredNorm();

    return {projected_t_normalized, distance_sqr};
}

static int32_t find_closest_line_to_point(const ClipperZUtils::ZPath &subject, const ClipperZUtils::ZPoint &query)
{
    auto it_min = subject.end();
    double distance_sqr_min = std::numeric_limits<double>::max();

    const Point query_pt = make_point(query);
    Point prev_pt = make_point(subject.front());
    for (auto it_curr = std::next(subject.begin()); it_curr != subject.end(); ++it_curr)
    {
        const Point curr_pt = make_point(*it_curr);

        const double distance_sqr = project_point_on_line(prev_pt, curr_pt, query_pt).distance_sqr;
        if (distance_sqr <= POINT_IS_ON_LINE_THRESHOLD_SQR)
        {
            return int32_t(std::distance(subject.begin(), std::prev(it_curr)));
        }

        if (distance_sqr < distance_sqr_min)
        {
            distance_sqr_min = distance_sqr;
            it_min = std::prev(it_curr);
        }

        prev_pt = curr_pt;
    }

    if (it_min != subject.end())
    {
        return int32_t(std::distance(subject.begin(), it_min));
    }

    return -1;
}

std::optional<LineRegionRange> create_line_region_range(ClipperZUtils::ZPath &&intersection,
                                                        const ClipperZUtils::ZPath &subject, const size_t region_idx)
{
    if (intersection.size() < 2)
    {
        return std::nullopt;
    }

    auto need_reverse = [&subject](const ClipperZUtils::ZPath &intersection) -> bool
    {
        for (size_t curr_idx = 1; curr_idx < intersection.size(); ++curr_idx)
        {
            ZAttributes prev_z(intersection[curr_idx - 1]);
            ZAttributes curr_z(intersection[curr_idx]);

            if (!prev_z.is_clip_point && !curr_z.is_clip_point)
            {
                if (prev_z.point_index > curr_z.point_index)
                {
                    return true;
                }
                else if (curr_z.point_index == prev_z.point_index)
                {
                    assert(curr_z.point_index < subject.size());
                    const Point subject_pt = make_point(subject[curr_z.point_index]);
                    const Point prev_pt = make_point(intersection[curr_idx - 1]);
                    const Point curr_pt = make_point(intersection[curr_idx]);

                    const double prev_dist = (prev_pt - subject_pt).cast<double>().squaredNorm();
                    const double curr_dist = (curr_pt - subject_pt).cast<double>().squaredNorm();
                    if (prev_dist > curr_dist)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    };

    for (ClipperZUtils::ZPoint &clipper_pt : intersection)
    {
        const ZAttributes clipper_pt_z(clipper_pt);
        if (!clipper_pt_z.is_clip_point)
        {
            continue;
        }

        // FIXME @hejllukas: We could save searing for the source line in some cases using other intersection points,
        //                   but in reality, the clip point will be inside the intersection in very rare cases.
        if (int32_t subject_line_idx = find_closest_line_to_point(subject, clipper_pt); subject_line_idx != -1)
        {
            clipper_pt.z = coord_t(ZAttributes(false, true, subject_line_idx).encode());
        }

        assert(!ZAttributes(clipper_pt).is_clip_point);
        if (ZAttributes(clipper_pt).is_clip_point)
        {
            return std::nullopt;
        }
    }

    // Ensure that indices of source input are ordered in increasing order.
    if (need_reverse(intersection))
    {
        std::reverse(intersection.begin(), intersection.end());
    }

    ZAttributes begin_z(intersection.front());
    ZAttributes end_z(intersection.back());

    assert(begin_z.point_index <= subject.size() && end_z.point_index <= subject.size());
    const size_t begin_idx = begin_z.point_index;
    const size_t end_idx = end_z.point_index;
    const double begin_t = begin_z.is_new_point ? project_point_on_line(make_point(subject[begin_idx]),
                                                                        make_point(subject[begin_idx + 1]),
                                                                        make_point(intersection.front()))
                                                      .projected_t
                                                : 0.;
    const double end_t = end_z.is_new_point
                             ? project_point_on_line(make_point(subject[end_idx]), make_point(subject[end_idx + 1]),
                                                     make_point(intersection.back()))
                                   .projected_t
                             : 0.;

    if (begin_t == std::numeric_limits<double>::max() || end_t == std::numeric_limits<double>::max())
    {
        return std::nullopt;
    }

    return LineRegionRange{begin_idx, begin_t, end_idx, end_t, region_idx};
}

LineRegionRanges intersection_with_region(const ClipperZUtils::ZPath &subject, const ClipperZUtils::ZPaths &clips,
                                          const size_t region_config_idx)
{
    Clipper2Lib::Clipper64 clipper;
    clipper.PreserveCollinear(true); // Especially with Arachne, we don't want to remove collinear edges.

    // Set up Z callback for intersection tracking
    // Z encodes: bit31=is_clip_point, bit30=is_new_point, bits0-29=point_index (line segment index)
    // BUG FIX: In Clipper2, bot/top refer to Y ordering not path ordering.
    // Line segment index should be min(bot.point_index, top.point_index) to get the
    // first vertex of the segment in the original path order.
    clipper.SetZCallback(
        [&subject](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top,
                   const Clipper2Lib::Point64 &e2bot, const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
        {
            // Decode Z attributes from each edge endpoint
            ZAttributes a1bot(uint32_t(e1bot.z)), a1top(uint32_t(e1top.z));
            ZAttributes a2bot(uint32_t(e2bot.z)), a2top(uint32_t(e2top.z));

            // Find the subject edge (non-clip edge) and compute line segment index
            uint32_t subject_line_idx = 0;
            if (!a1bot.is_clip_point && !a1top.is_clip_point)
            {
                // e1 is from subject - line segment index is the minimum point_index
                // (the first vertex of the segment in original path order)
                subject_line_idx = std::min(a1bot.point_index, a1top.point_index);
            }
            else if (!a2bot.is_clip_point && !a2top.is_clip_point)
            {
                // e2 is from subject - line segment index is the minimum point_index
                subject_line_idx = std::min(a2bot.point_index, a2top.point_index);
            }
            // Mark as intersection point from subject at the computed line index
            pt.z = ZAttributes(false, true, subject_line_idx).encode();
        });

    // Convert ZPath subject to Path64 with Z = encoded attributes
    // Each vertex gets point_index = its position in the path
    Clipper2Lib::Path64 subject_path;
    subject_path.reserve(subject.size());
    for (size_t i = 0; i < subject.size(); ++i)
    {
        const auto &zpt = subject[i];
        // Subject points: is_clip_point=false, is_new_point=false, point_index=vertex index
        subject_path.emplace_back(zpt.x, zpt.y, ZAttributes(false, false, uint32_t(i)).encode());
    }

    // Convert ZPaths clips to Paths64 with Z indicating clip points
    Clipper2Lib::Paths64 clips_paths;
    clips_paths.reserve(clips.size());
    for (const auto &zpath : clips)
    {
        Clipper2Lib::Path64 path;
        path.reserve(zpath.size());
        for (const auto &zpt : zpath)
            // Clip points: is_clip_point=true, is_new_point=false, point_index=0
            path.emplace_back(zpt.x, zpt.y, ZAttributes(true, false, 0).encode());
        clips_paths.emplace_back(std::move(path));
    }

    clipper.AddOpenSubject({subject_path});
    clipper.AddClip(clips_paths);

    Clipper2Lib::Paths64 intersections_closed;
    Clipper2Lib::Paths64 intersections_open;
    clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero, intersections_closed,
                    intersections_open);

    // For open subjects, the intersections will be in intersections_open
    Clipper2Lib::Paths64 intersections = std::move(intersections_open);

    LineRegionRanges line_region_ranges;
    line_region_ranges.reserve(intersections.size());
    for (Clipper2Lib::Path64 &intersection : intersections)
    {
        // With USINGZ enabled, Z values are already set by the callback for intersection points
        // and preserved from input vertices for pass-through points
        ClipperZUtils::ZPath zintersection;
        zintersection.reserve(intersection.size());
        for (const auto &pt : intersection)
        {
            zintersection.emplace_back(pt.x, pt.y, pt.z);
        }
        if (std::optional<LineRegionRange> region_range = create_line_region_range(std::move(zintersection), subject,
                                                                                   region_config_idx);
            region_range.has_value())
        {
            line_region_ranges.emplace_back(*region_range);
        }
    }

    return line_region_ranges;
}

LineRegionRanges create_continues_line_region_ranges(LineRegionRanges &&line_region_ranges,
                                                     const size_t default_clip_idx, const size_t total_lines_cnt)
{
    if (line_region_ranges.empty())
    {
        return line_region_ranges;
    }

    std::sort(line_region_ranges.begin(), line_region_ranges.end());

    // Resolve overlapping regions if it happens, but it should never happen.
    for (size_t region_range_idx = 1; region_range_idx < line_region_ranges.size(); ++region_range_idx)
    {
        LineRegionRange &prev_range = line_region_ranges[region_range_idx - 1];
        LineRegionRange &curr_range = line_region_ranges[region_range_idx];

        assert(!prev_range.is_overlap(curr_range));
        if (prev_range.is_inside(curr_range))
        {
            // Make the previous range zero length to remove it later.
            curr_range = prev_range;
            prev_range.begin_idx = curr_range.begin_idx;
            prev_range.begin_t = curr_range.begin_t;
            prev_range.end_idx = curr_range.begin_idx;
            prev_range.end_t = curr_range.begin_t;
        }
        else if (prev_range.is_overlap(curr_range))
        {
            curr_range.begin_idx = prev_range.end_idx;
            curr_range.begin_t = prev_range.end_t;
        }
    }

    // Fill all gaps between regions with the default region.
    LineRegionRanges line_region_ranges_out;
    size_t prev_line_idx = 0.;
    double prev_t = 0.;
    for (const LineRegionRange &curr_line_region : line_region_ranges)
    {
        if (curr_line_region.is_zero_length())
        {
            continue;
        }

        assert(prev_line_idx < curr_line_region.begin_idx ||
               (prev_line_idx == curr_line_region.begin_idx && prev_t <= curr_line_region.begin_t));

        // Fill the gap if it is necessary.
        if (prev_line_idx != curr_line_region.begin_idx || prev_t != curr_line_region.begin_t)
        {
            line_region_ranges_out.emplace_back(prev_line_idx, prev_t, curr_line_region.begin_idx,
                                                curr_line_region.begin_t, default_clip_idx);
        }

        // Add the current region.
        line_region_ranges_out.emplace_back(curr_line_region);
        prev_line_idx = curr_line_region.end_idx;
        prev_t = curr_line_region.end_t;
    }

    // Fill the last remaining gap if it exists.
    const size_t last_line_idx = total_lines_cnt - 1;
    if ((prev_line_idx == last_line_idx && prev_t == 1.) || ((prev_line_idx == total_lines_cnt && prev_t == 0.)))
    {
        // There is no gap at the end.
        return line_region_ranges_out;
    }

    // Fill the last remaining gap.
    line_region_ranges_out.emplace_back(prev_line_idx, prev_t, last_line_idx, 1., default_clip_idx);

    return line_region_ranges_out;
}

LineRegionRanges subject_segmentation(const ClipperZUtils::ZPath &subject,
                                      const std::vector<ExPolygons> &expolygons_clips,
                                      const size_t default_clip_idx = 0)
{
    LineRegionRanges line_region_ranges;
    for (const ExPolygons &expolygons_clip : expolygons_clips)
    {
        const size_t expolygons_clip_idx = &expolygons_clip - expolygons_clips.data();
        const ClipperZUtils::ZPaths clips = to_clip_zpaths(expolygons_clip);
        Slic3r::append(line_region_ranges,
                       intersection_with_region(subject, clips, expolygons_clip_idx + default_clip_idx + 1));
    }

    return create_continues_line_region_ranges(std::move(line_region_ranges), default_clip_idx, subject.size() - 1);
}

PolylineSegment create_polyline_segment(const LineRegionRange &line_region_range, const Polyline &subject)
{
    Polyline polyline_out;
    if (line_region_range.begin_t == 0.)
    {
        polyline_out.points.emplace_back(subject[line_region_range.begin_idx]);
    }
    else
    {
        assert(line_region_range.begin_idx <= subject.size());
        Point interpolated_start_pt = lerp(subject[line_region_range.begin_idx],
                                           subject[line_region_range.begin_idx + 1], line_region_range.begin_t);
        polyline_out.points.emplace_back(interpolated_start_pt);
    }

    for (size_t line_idx = line_region_range.begin_idx + 1; line_idx <= line_region_range.end_idx; ++line_idx)
    {
        polyline_out.points.emplace_back(subject[line_idx]);
    }

    if (line_region_range.end_t == 0.)
    {
        polyline_out.points.emplace_back(subject[line_region_range.end_idx]);
    }
    else if (line_region_range.end_t == 1.)
    {
        assert(line_region_range.end_idx <= subject.size());
        polyline_out.points.emplace_back(subject[line_region_range.end_idx + 1]);
    }
    else
    {
        assert(line_region_range.end_idx <= subject.size());
        Point interpolated_end_pt = lerp(subject[line_region_range.end_idx], subject[line_region_range.end_idx + 1],
                                         line_region_range.end_t);
        polyline_out.points.emplace_back(interpolated_end_pt);
    }

    return {polyline_out, line_region_range.clip_idx};
}

PolylineSegments create_polyline_segments(const LineRegionRanges &line_region_ranges, const Polyline &subject)
{
    PolylineSegments polyline_segments;
    polyline_segments.reserve(line_region_ranges.size());
    for (const LineRegionRange &region_range : line_region_ranges)
    {
        polyline_segments.emplace_back(create_polyline_segment(region_range, subject));
    }

    return polyline_segments;
}

ExtrusionSegment create_extrusion_segment(const LineRegionRange &line_region_range,
                                          const Arachne::ExtrusionLine &subject)
{
    // When we call this function, we split ExtrusionLine into at least two segments, so none of those segments are closed.
    Arachne::ExtrusionLine extrusion_out(subject.inset_idx, subject.is_odd);
    if (line_region_range.begin_t == 0.)
    {
        extrusion_out.junctions.emplace_back(subject[line_region_range.begin_idx]);
    }
    else
    {
        assert(line_region_range.begin_idx <= subject.size());
        const Arachne::ExtrusionJunction &junction_from = subject[line_region_range.begin_idx];
        const Arachne::ExtrusionJunction &junction_to = subject[line_region_range.begin_idx + 1];

        const Point interpolated_start_pt = lerp(junction_from.p, junction_to.p, line_region_range.begin_t);
        const coord_t interpolated_start_w = lerp(junction_from.w, junction_to.w, line_region_range.begin_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_start_pt, interpolated_start_w,
                                             junction_from.perimeter_index);
    }

    for (size_t line_idx = line_region_range.begin_idx + 1; line_idx <= line_region_range.end_idx; ++line_idx)
    {
        extrusion_out.junctions.emplace_back(subject[line_idx]);
    }

    if (line_region_range.end_t == 0.)
    {
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx]);
    }
    else if (line_region_range.end_t == 1.)
    {
        assert(line_region_range.end_idx <= subject.size());
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx + 1]);
    }
    else
    {
        assert(line_region_range.end_idx <= subject.size());
        const Arachne::ExtrusionJunction &junction_from = subject[line_region_range.end_idx];
        const Arachne::ExtrusionJunction &junction_to = subject[line_region_range.end_idx + 1];

        const Point interpolated_end_pt = lerp(junction_from.p, junction_to.p, line_region_range.end_t);
        const coord_t interpolated_end_w = lerp(junction_from.w, junction_to.w, line_region_range.end_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_end_pt, interpolated_end_w, junction_from.perimeter_index);
    }

    return {extrusion_out, line_region_range.clip_idx};
}

ExtrusionSegments create_extrusion_segments(const LineRegionRanges &line_region_ranges,
                                            const Arachne::ExtrusionLine &subject)
{
    ExtrusionSegments extrusion_segments;
    extrusion_segments.reserve(line_region_ranges.size());
    for (const LineRegionRange &region_range : line_region_ranges)
    {
        extrusion_segments.emplace_back(create_extrusion_segment(region_range, subject));
    }

    return extrusion_segments;
}

PolylineSegments polyline_segmentation(const Polyline &subject, const std::vector<ExPolygons> &expolygons_clips,
                                       const size_t default_clip_idx)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), expolygons_clips,
                                                                     default_clip_idx);
    if (line_region_ranges.empty())
    {
        return {PolylineSegment{subject, default_clip_idx}};
    }
    else if (line_region_ranges.size() == 1)
    {
        return {PolylineSegment{subject, line_region_ranges.front().clip_idx}};
    }

    return create_polyline_segments(line_region_ranges, subject);
}

PolylineSegments polygon_segmentation(const Polygon &subject, const std::vector<ExPolygons> &expolygons_clips,
                                      const size_t default_clip_idx)
{
    return polyline_segmentation(to_polyline(subject), expolygons_clips, default_clip_idx);
}

ExtrusionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject,
                                         const std::vector<ExPolygons> &expolygons_clips, const size_t default_clip_idx)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), expolygons_clips,
                                                                     default_clip_idx);
    if (line_region_ranges.empty())
    {
        return {ExtrusionSegment{subject, default_clip_idx}};
    }
    else if (line_region_ranges.size() == 1)
    {
        return {ExtrusionSegment{subject, line_region_ranges.front().clip_idx}};
    }

    return create_extrusion_segments(line_region_ranges, subject);
}

inline std::vector<ExPolygons> to_expolygons_clips(const PerimeterRegions &perimeter_regions_clips)
{
    std::vector<ExPolygons> expolygons_clips;
    expolygons_clips.reserve(perimeter_regions_clips.size());
    for (const PerimeterRegion &perimeter_region_clip : perimeter_regions_clips)
    {
        expolygons_clips.emplace_back(perimeter_region_clip.expolygons);
    }

    return expolygons_clips;
}

PolylineRegionSegments polyline_segmentation(const Polyline &subject, const PrintRegionConfig &base_config,
                                             const PerimeterRegions &perimeter_regions_clips)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject),
                                                                     to_expolygons_clips(perimeter_regions_clips));
    if (line_region_ranges.empty())
    {
        return {PolylineRegionSegment{subject, base_config}};
    }
    else if (line_region_ranges.size() == 1)
    {
        size_t clip_idx = line_region_ranges.front().clip_idx;
        const PrintRegionConfig &config = clip_idx == 0 ? base_config
                                                        : perimeter_regions_clips[clip_idx - 1].region->config();
        return {PolylineRegionSegment{subject, config}};
    }

    PolylineRegionSegments segments_out;
    for (PolylineSegment &segment : create_polyline_segments(line_region_ranges, subject))
    {
        const PrintRegionConfig &config = segment.clip_idx == 0
                                              ? base_config
                                              : perimeter_regions_clips[segment.clip_idx - 1].region->config();
        segments_out.emplace_back(std::move(segment.polyline), config);
    }

    return segments_out;
}

PolylineRegionSegments polygon_segmentation(const Polygon &subject, const PrintRegionConfig &base_config,
                                            const PerimeterRegions &perimeter_regions_clips)
{
    return polyline_segmentation(to_polyline(subject), base_config, perimeter_regions_clips);
}

ExtrusionRegionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject,
                                               const PrintRegionConfig &base_config,
                                               const PerimeterRegions &perimeter_regions_clips)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject),
                                                                     to_expolygons_clips(perimeter_regions_clips));
    if (line_region_ranges.empty())
    {
        return {ExtrusionRegionSegment{subject, base_config}};
    }
    else if (line_region_ranges.size() == 1)
    {
        size_t clip_idx = line_region_ranges.front().clip_idx;
        const PrintRegionConfig &config = clip_idx == 0 ? base_config
                                                        : perimeter_regions_clips[clip_idx - 1].region->config();
        return {ExtrusionRegionSegment{subject, config}};
    }

    ExtrusionRegionSegments segments_out;
    for (ExtrusionSegment &segment : create_extrusion_segments(line_region_ranges, subject))
    {
        const PrintRegionConfig &config = segment.clip_idx == 0
                                              ? base_config
                                              : perimeter_regions_clips[segment.clip_idx - 1].region->config();
        segments_out.emplace_back(std::move(segment.extrusion), config);
    }

    return segments_out;
}

AthenaExtrusionSegment create_extrusion_segment(const LineRegionRange &line_region_range,
                                                const Athena::ExtrusionLine &subject)
{
    // When we call this function, we split ExtrusionLine into at least two segments, so none of those segments are closed.
    Athena::ExtrusionLine extrusion_out(subject.inset_idx, subject.is_odd);
    if (line_region_range.begin_t == 0.)
    {
        extrusion_out.junctions.emplace_back(subject[line_region_range.begin_idx]);
    }
    else
    {
        assert(line_region_range.begin_idx <= subject.size());
        const Athena::ExtrusionJunction &junction_from = subject[line_region_range.begin_idx];
        const Athena::ExtrusionJunction &junction_to = subject[line_region_range.begin_idx + 1];

        const Point interpolated_start_pt = lerp(junction_from.p, junction_to.p, line_region_range.begin_t);
        const coord_t interpolated_start_w = lerp(junction_from.w, junction_to.w, line_region_range.begin_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_start_pt, interpolated_start_w,
                                             junction_from.perimeter_index);
    }

    for (size_t line_idx = line_region_range.begin_idx + 1; line_idx <= line_region_range.end_idx; ++line_idx)
    {
        extrusion_out.junctions.emplace_back(subject[line_idx]);
    }

    if (line_region_range.end_t == 0.)
    {
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx]);
    }
    else if (line_region_range.end_t == 1.)
    {
        assert(line_region_range.end_idx <= subject.size());
        extrusion_out.junctions.emplace_back(subject[line_region_range.end_idx + 1]);
    }
    else
    {
        assert(line_region_range.end_idx <= subject.size());
        const Athena::ExtrusionJunction &junction_from = subject[line_region_range.end_idx];
        const Athena::ExtrusionJunction &junction_to = subject[line_region_range.end_idx + 1];

        const Point interpolated_end_pt = lerp(junction_from.p, junction_to.p, line_region_range.end_t);
        const coord_t interpolated_end_w = lerp(junction_from.w, junction_to.w, line_region_range.end_t);

        assert(junction_from.perimeter_index == junction_to.perimeter_index);
        extrusion_out.junctions.emplace_back(interpolated_end_pt, interpolated_end_w, junction_from.perimeter_index);
    }

    return {extrusion_out, line_region_range.clip_idx};
}

AthenaExtrusionSegments create_extrusion_segments(const LineRegionRanges &line_region_ranges,
                                                  const Athena::ExtrusionLine &subject)
{
    AthenaExtrusionSegments extrusion_segments;
    extrusion_segments.reserve(line_region_ranges.size());
    for (const LineRegionRange &region_range : line_region_ranges)
    {
        extrusion_segments.emplace_back(create_extrusion_segment(region_range, subject));
    }

    return extrusion_segments;
}

AthenaExtrusionSegments extrusion_segmentation(const Athena::ExtrusionLine &subject,
                                               const std::vector<ExPolygons> &expolygons_clips,
                                               const size_t default_clip_idx)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject), expolygons_clips,
                                                                     default_clip_idx);
    if (line_region_ranges.empty())
    {
        return {AthenaExtrusionSegment{subject, default_clip_idx}};
    }
    else if (line_region_ranges.size() == 1)
    {
        return {AthenaExtrusionSegment{subject, line_region_ranges.front().clip_idx}};
    }

    return create_extrusion_segments(line_region_ranges, subject);
}

AthenaExtrusionRegionSegments extrusion_segmentation(const Athena::ExtrusionLine &subject,
                                                     const PrintRegionConfig &base_config,
                                                     const PerimeterRegions &perimeter_regions_clips)
{
    const LineRegionRanges line_region_ranges = subject_segmentation(subject_to_zpath(subject),
                                                                     to_expolygons_clips(perimeter_regions_clips));
    if (line_region_ranges.empty())
    {
        return {AthenaExtrusionRegionSegment{subject, base_config}};
    }
    else if (line_region_ranges.size() == 1)
    {
        size_t clip_idx = line_region_ranges.front().clip_idx;
        const PrintRegionConfig &config = clip_idx == 0 ? base_config
                                                        : perimeter_regions_clips[clip_idx - 1].region->config();
        return {AthenaExtrusionRegionSegment{subject, config}};
    }

    AthenaExtrusionRegionSegments segments_out;
    for (AthenaExtrusionSegment &segment : create_extrusion_segments(line_region_ranges, subject))
    {
        const PrintRegionConfig &config = segment.clip_idx == 0
                                              ? base_config
                                              : perimeter_regions_clips[segment.clip_idx - 1].region->config();
        segments_out.emplace_back(std::move(segment.extrusion), config);
    }

    return segments_out;
}

} // namespace Slic3r::Algorithm::LineSegmentation
