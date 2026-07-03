///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2021 Ilya @xorza
///|/ Copyright (c) Slic3r 2015 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "PerimeterGenerator.hpp"
#include "Serpentine.hpp"
#include "PreciseWalls.hpp"
#include "Arachne/WallToolPaths.hpp"
#include "Arachne/utils/ExtrusionLine.hpp"
#include "Arachne/PerimeterOrder.hpp"
#include "Athena/WallToolPaths.hpp"
#include "Athena/utils/ExtrusionLine.hpp"
#include "Athena/PerimeterOrder.hpp"
#include "Athena/utils/PolygonsPointIndex.hpp"

#include <ankerl/unordered_dense.h>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <tuple>

#include "AABBTreeLines.hpp"
#include "BoundingBox.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Feature/FuzzySkin/FuzzySkin.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"
#include "ShortestPath.hpp"
#include "Surface.hpp"
#include "Geometry/ConvexHull.hpp"
#include "Arachne/PerimeterOrder.hpp"
#include "Arachne/WallToolPaths.hpp"
#include "Arachne/utils/ExtrusionLine.hpp"
#include "Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r.h"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "Print.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Print.hpp"
#include "Fill/FillBase.hpp"
#include <cstdio>

//#define ARACHNE_DEBUG

#ifdef ARACHNE_DEBUG
#include "SVG.hpp"
#include "Utils.hpp"
#endif

namespace Slic3r
{

ExtrusionMultiPath PerimeterGenerator::thick_polyline_to_multi_path(const ThickPolyline &thick_polyline,
                                                                    ExtrusionRole role, const Flow &flow,
                                                                    const float tolerance, const float merge_tolerance,
                                                                    const std::optional<uint32_t> &perimeter_index)
{
    ExtrusionMultiPath multi_path;
    ExtrusionPath path(role);
    ThickLines lines = thick_polyline.thicklines();

    for (int i = 0; i < (int) lines.size(); ++i)
    {
        const ThickLine &line = lines[i];
        assert(line.a_width >= SCALED_EPSILON && line.b_width >= SCALED_EPSILON);

        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON)
        {
            // The line is so tiny that we don't care about its width when we connect it to another line.
            if (!path.empty())
                path.polyline.points.back() = line.b; // If the variable path is non-empty, connect this tiny line to it.
            else if (i + 1 <
                     (int) lines.size()) // If there is at least one following line, connect this tiny line to it.
                lines[i + 1].a = line.a;
            else if (!multi_path.paths.empty())
                multi_path.paths.back().polyline.points.back() =
                    line.b; // Connect this tiny line to the last finished path.

            // If any of the above isn't satisfied, then remove this tiny line.
            continue;
        }

        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance)
        {
            const auto segments = (unsigned int) ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j)
                {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len))
                                     .cast<coord_t>());

                    coordf_t w = line.a_width + (j * seg_len) * (line.b_width - line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);

                assert(pp.size() == segments + 1u);
                assert(width.size() == segments * 2);
            }

            // Problem: Original code performed erase() + repeated insert() operations:
            //   lines.erase(lines.begin() + i);                    // O(n)
            //   for (j in segments)
            //       lines.insert(lines.begin() + i + j, ...);      // O(n) each = O(n²) total
            //
            // Solution: Build new vector with corrected elements, swap in place
            // Performance: Changes O(n²) to O(n), potential 10-100x speedup for thick lines
            // Risk: Low - preserves exact same element ordering and semantics
            {
                ThickLines new_lines;
                // Reserve exact capacity: original_size - 1 (erased line) + segments (new lines)
                new_lines.reserve(lines.size() - 1 + segments);

                // Copy all lines before position i (unchanged)
                new_lines.insert(new_lines.end(), lines.begin(), lines.begin() + i);

                // Insert the segmented lines at position i
                for (size_t j = 0; j < segments; ++j)
                {
                    ThickLine new_line(pp[j], pp[j + 1]);
                    new_line.a_width = width[2 * j];
                    new_line.b_width = width[2 * j + 1];
                    new_lines.push_back(new_line);
                }

                // Copy all lines after position i (which was the erased line)
                new_lines.insert(new_lines.end(), lines.begin() + i + 1, lines.end());

                // Swap the new vector into place - O(1) operation
                lines.swap(new_lines);
            }

            --i;
            continue;
        }

        const double w = fmax(line.a_width, line.b_width);

        // Filter out beads too thin to extrude. Two constraints:
        // 1. Flow formula: spacing = width - height × 0.2146 must be positive
        // 2. Nozzle floor: width must be >= nozzle_diameter / 3 for printability
        float min_safe_width = std::max(flow.height() * 0.2146f, flow.nozzle_diameter() / 3.0f);
        if (w <= 0 || unscale<float>(w) < min_safe_width)
        {
            continue; // Skip this line entirely
        }

        // ThickLine.a_width and b_width are EXTRUSION WIDTHS, not spacing values.
        // The old code was converting from spacing to width, which added extra width and broke our exact width enforcement.
        // For Arachne/Athena paths, we use the width directly. For classic/bridge paths, use the old behavior.
        const Flow new_flow = (role.is_bridge() && flow.bridge()) ? flow : flow.with_width(unscale<float>(w));
        if (path.empty())
        {
            // Convert from spacing to extrusion width based on the extrusion model
            // of a square extrusion ended with semi circles.
            path = {perimeter_index.has_value()
                        ? ExtrusionAttributes{path.role(), new_flow, static_cast<uint16_t>(*perimeter_index)}
                        : ExtrusionAttributes{path.role(), new_flow}};
            path.polyline.append(line.a);
            path.polyline.append(line.b);
#ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
#endif
        }
        else
        {
            assert(path.width() >= EPSILON);
            thickness_delta = scaled<double>(fabs(path.width() - new_flow.width()));
            if (thickness_delta <= merge_tolerance)
            {
                // the width difference between this line and the current flow
                // (of the previous line) width is within the accepted tolerance
                path.polyline.append(line.b);
            }
            else
            {
                // we need to initialize a new line
                multi_path.paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                --i;
            }
        }
    }
    if (path.polyline.is_valid())
        multi_path.paths.emplace_back(std::move(path));
    return multi_path;
}
static ClipperZUtils::ZPaths clip_extrusion(const ClipperZUtils::ZPath &subject, const ClipperZUtils::ZPaths &clip,
                                            Clipper2Lib::ClipType clipType)
{
    Clipper2Lib::Clipper64 clipper;

    // Set Z callback to interpolate extrusion line width at intersections
    clipper.SetZCallback(
        [](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top, const Clipper2Lib::Point64 &e2bot,
           const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
        {
            // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
            // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are
            // harmless to the result of the clipping operation,
            // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
            assert(e1bot.z >= 0 && e1top.z >= 0);
            assert(e2bot.z >= 0 && e2top.z >= 0);
            assert((e1bot.z == 0) == (e1top.z == 0));
            assert((e2bot.z == 0) == (e2top.z == 0));

            // Start & end points of the clipped polyline (extrusion path with a non-zero width).
            Clipper2Lib::Point64 start = e1bot;
            Clipper2Lib::Point64 end = e1top;
            if (start.z <= 0 && end.z <= 0)
            {
                start = e2bot;
                end = e2top;
            }

            if (start.z <= 0 && end.z <= 0)
            {
                // Self intersection on the source contour.
                assert(start.z == 0 && end.z == 0);
                pt.z = 0;
            }
            else
            {
                // Interpolate extrusion line width.
                assert(start.z > 0 && end.z > 0);

                // Cast to double BEFORE squaring to prevent int64 overflow.
                // Clipper2 coordinates can span ~4 billion units (nanometers) across a build plate;
                // squaring that difference (~16 * 10^18) overflows int64 (max 9.2 * 10^18),
                // producing NaN in sqrt, which casts to INT64_MIN and corrupts the width.
                double dx = double(end.x - start.x);
                double dy = double(end.y - start.y);
                double length_sqr = dx * dx + dy * dy;
                if (length_sqr < 1.0)
                {
                    pt.z = start.z;
                }
                else
                {
                    double dpx = double(pt.x - start.x);
                    double dpy = double(pt.y - start.y);
                    double dist_sqr = dpx * dpx + dpy * dpy;
                    double t = std::sqrt(dist_sqr / length_sqr);
                    pt.z = start.z + int64_t((end.z - start.z) * t);
                }
            }
        });

    // Convert ZPaths to Paths64 with Z preserved
    Clipper2Lib::Path64 subject_path = ClipperZUtils::zpath_to_path64(subject);
    clipper.AddOpenSubject({subject_path});

    Clipper2Lib::Paths64 clip_paths = ClipperZUtils::zpaths_to_paths64(clip);
    clipper.AddClip(clip_paths);

    ClipperZUtils::ZPaths clipped_paths;
    {
        Clipper2Lib::PolyTree64 clipped_polytree;
        Clipper2Lib::Paths64 open_paths; // REQUIRED for AddOpenSubject results!
        clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, clipped_polytree, open_paths);

        // Convert results back to ZPaths - Z values are set by callback
        clipped_paths = ClipperZUtils::paths64_to_zpaths(open_paths);
    }

    // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
    // For those vertices, we must assign value based on the subject.
    // This happens only in sporadic cases.
    for (ClipperZUtils::ZPath &path : clipped_paths)
        for (ClipperZUtils::ZPoint &c_pt : path)
            if (c_pt.z == 0)
            {
                // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                if (subject.size() <= 2)
                    continue;

                const Point pt(c_pt.x, c_pt.y);
                Point projected_pt_min;
                auto it_min = subject.begin();
                auto dist_sqr_min = std::numeric_limits<double>::max();
                Point prev(subject.front().x, subject.front().y);
                for (auto it = std::next(subject.begin()); it != subject.end(); ++it)
                {
                    Point curr(it->x, it->y);
                    Point projected_pt;
                    if (double dist_sqr = line_alg::distance_to_squared(Line(prev, curr), pt, &projected_pt);
                        dist_sqr < dist_sqr_min)
                    {
                        dist_sqr_min = dist_sqr;
                        projected_pt_min = projected_pt;
                        it_min = std::prev(it);
                    }
                    prev = curr;
                }

                assert(dist_sqr_min <= SCALED_EPSILON);
                assert(std::next(it_min) != subject.end());

                const Point pt_a(it_min->x, it_min->y);
                const Point pt_b(std::next(it_min)->x, std::next(it_min)->y);
                const double line_len = (pt_b - pt_a).cast<double>().norm();
                // Degenerate edge guard: same div-by-zero -> NaN -> INT64_MIN issue as the Z callback above.
                if (line_len < SCALED_EPSILON)
                {
                    c_pt.z = it_min->z;
                }
                else
                {
                    const double dist = (projected_pt_min - pt_a).cast<double>().norm();
                    c_pt.z = coord_t(double(it_min->z) + (dist / line_len) * double(std::next(it_min)->z - it_min->z));
                }
            }

    assert(
        [&clipped_paths = std::as_const(clipped_paths)]() -> bool
        {
            for (const ClipperZUtils::ZPath &path : clipped_paths)
                for (const ClipperZUtils::ZPoint &pt : path)
                    if (pt.z <= 0)
                        return false;
            return true;
        }());

    return clipped_paths;
}
static ExtrusionEntityCollection traverse_extrusions(const PerimeterGenerator::Parameters &params,
                                                     const Polygons &lower_slices_polygons_cache,
                                                     const Polygons &lower_slices_raw,
                                                     Arachne::PerimeterOrder::PerimeterExtrusions &pg_extrusions)
{
    using namespace Slic3r::Feature::FuzzySkin;

    ExtrusionEntityCollection extrusion_coll;
    for (Arachne::PerimeterOrder::PerimeterExtrusion &pg_extrusion : pg_extrusions)
    {
        Arachne::ExtrusionLine extrusion = pg_extrusion.extrusion;
        if (extrusion.empty())
            continue;

        const bool is_external = extrusion.inset_idx == 0;
        ExtrusionRole role_normal = is_external ? ExtrusionRole::ExternalPerimeter : ExtrusionRole::Perimeter;
        ExtrusionRole role_overhang = role_normal | ExtrusionRoleModifier::Bridge;

        // Visibility checks (fuzzy_skin_on_top, fuzzy_skin_first_layer) are now done per-segment
        // inside apply_fuzzy_skin, using the segment's midpoint for accurate detection.
        extrusion = apply_fuzzy_skin(extrusion, params.config, params.perimeter_regions, params.layer_id,
                                     pg_extrusion.extrusion.inset_idx,
                                     !pg_extrusion.extrusion.is_closed || pg_extrusion.is_contour(), params.layer,
                                     &lower_slices_raw, params.ext_perimeter_flow.scaled_width());

        // This prevents the artificial split at the "3 o'clock" position by ensuring the first
        // junction is at the rear of the object (minimum Y), which is a common seam preference.
        if (extrusion.is_closed && extrusion.junctions.size() > 2)
        {
            // Find junction with minimum Y (rear of object)
            auto min_y_it = std::min_element(extrusion.junctions.begin(), extrusion.junctions.end() - 1,
                                             [](const Arachne::ExtrusionJunction &a,
                                                const Arachne::ExtrusionJunction &b) { return a.p.y() < b.p.y(); });

            if (min_y_it != extrusion.junctions.begin() && min_y_it != extrusion.junctions.end() - 1)
            {
                // Rotate so min_y junction becomes first
                // Note: For closed loops, last junction equals first, so we exclude it from rotation
                std::rotate(extrusion.junctions.begin(), min_y_it, extrusion.junctions.end() - 1);
                // Update the last junction to match the new first junction (maintain closure)
                extrusion.junctions.back() = extrusion.junctions.front();
            }
        }

        ExtrusionPaths paths;
        // detect overhanging/bridging perimeters
        bool taking_overhang_path = params.config.overhangs && params.layer_id > params.object_config.raft_layers &&
                                    !(params.object_config.support_material &&
                                      params.object_config.support_material_contact_distance.value == stcgNoGap &&
                                      !params.object_config.support_material_bridge_no_gap);
        if (taking_overhang_path)
        {
            ClipperZUtils::ZPath extrusion_path;
            extrusion_path.reserve(extrusion.size());
            BoundingBox extrusion_path_bbox;
            for (const Arachne::ExtrusionJunction &ej : extrusion.junctions)
            {
                extrusion_path.emplace_back(ej.p.x(), ej.p.y(), ej.w);
                extrusion_path_bbox.merge(Point{ej.p.x(), ej.p.y()});
            }

            ClipperZUtils::ZPaths lower_slices_paths;
            lower_slices_paths.reserve(lower_slices_polygons_cache.size());
            {
                Points clipped;
                extrusion_path_bbox.offset(SCALED_EPSILON);
                for (const Polygon &poly : lower_slices_polygons_cache)
                {
                    clipped.clear();
                    ClipperUtils::clip_clipper_polygon_with_subject_bbox(poly.points, extrusion_path_bbox, clipped);
                    if (!clipped.empty())
                    {
                        lower_slices_paths.emplace_back();
                        ClipperZUtils::ZPath &out = lower_slices_paths.back();
                        out.reserve(clipped.size());
                        for (const Point &pt : clipped)
                            out.emplace_back(pt.x(), pt.y(), 0);
                    }
                }
            }

            // get non-overhang paths by intersecting this loop with the grown lower slices
            ClipperZUtils::ZPaths intersection_result = clip_extrusion(extrusion_path, lower_slices_paths,
                                                                       Clipper2Lib::ClipType::Intersection);
            ClipperZUtils::ZPaths difference_result = clip_extrusion(extrusion_path, lower_slices_paths,
                                                                     Clipper2Lib::ClipType::Difference);
            extrusion_paths_append(paths, intersection_result, role_normal,
                                   is_external ? params.ext_perimeter_flow : params.perimeter_flow,
                                   extrusion.inset_idx);

            // get overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(paths, difference_result, role_overhang, params.overhang_flow, extrusion.inset_idx);

            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            // Arachne sometimes creates extrusion with zero-length (just two same endpoints);
            if (!paths.empty())
            {
                Point start_point = paths.front().first_point();
                if (!extrusion.is_closed)
                {
                    // Especially for open extrusion, we need to select a starting point that is at the start
                    // or the end of the extrusions to make one continuous line. Also, we prefer a non-overhang
                    // starting point.
                    struct PointInfo
                    {
                        size_t occurrence = 0;
                        bool is_overhang = false;
                    };
                    ankerl::unordered_dense::map<Point, PointInfo, PointHash> point_occurrence;
                    for (const ExtrusionPath &path : paths)
                    {
                        ++point_occurrence[path.polyline.first_point()].occurrence;
                        ++point_occurrence[path.polyline.last_point()].occurrence;
                        if (path.role().is_bridge())
                        {
                            point_occurrence[path.polyline.first_point()].is_overhang = true;
                            point_occurrence[path.polyline.last_point()].is_overhang = true;
                        }
                    }

                    // Prefer non-overhang point as a starting point.
                    for (const std::pair<Point, PointInfo> &pt : point_occurrence)
                        if (pt.second.occurrence == 1)
                        {
                            start_point = pt.first;
                            if (!pt.second.is_overhang)
                            {
                                start_point = pt.first;
                                break;
                            }
                        }
                }

                chain_and_reorder_extrusion_paths(paths, &start_point);
            }
        }
        else
        {
            extrusion_paths_append(paths, extrusion, role_normal,
                                   is_external ? params.ext_perimeter_flow : params.perimeter_flow,
                                   extrusion.inset_idx);
        }

        // When top_surface_flow_reduction is enabled, split paths at visibility boundaries and
        // apply reduced flow to visible segments. Uses interval-based sampling per config setting.
        if (is_external && params.config.top_surface_flow_reduction.value > 0 && params.layer != nullptr)
        {
            const double flow_multiplier = 1.0 - (params.config.top_surface_flow_reduction.value / 100.0);
            const coord_t check_diameter = params.ext_perimeter_flow.scaled_width() * 4;

            // Get visibility detection interval from config
            double sample_interval;
            switch (params.config.top_surface_visibility_detection.value)
            {
            case TopSurfaceVisibilityDetection::tsvdPrecise:
                sample_interval = 1.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdStandard:
                sample_interval = 2.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdRelaxed:
                sample_interval = 4.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdMinimal:
                sample_interval = 8.0;
                break;
            default:
                sample_interval = 2.0;
                break;
            }

            // Lambda to check point visibility
            auto point_is_visible = [&](const Point &pt) -> bool
            {
                return params.layer->is_visible_from_top_or_bottom(pt, check_diameter, true, false);
            };

            // Lambda to find exact visibility boundary using binary search
            auto find_visibility_boundary = [&](const Point &p1, const Point &p2) -> Point
            {
                Point visible_pt = p1;
                Point hidden_pt = p2;
                if (point_is_visible(p1))
                    std::swap(visible_pt, hidden_pt);

                // Binary search for boundary
                for (int i = 0; i < 14; ++i)
                {
                    Point mid((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
                    if (point_is_visible(mid))
                        hidden_pt = mid;
                    else
                        visible_pt = mid;
                }
                return Point((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
            };

            ExtrusionPaths new_paths;
            for (ExtrusionPath &path : paths)
            {
                // Only process external perimeter paths (not overhang/bridge paths)
                if (path.role() != ExtrusionRole::ExternalPerimeter || path.polyline.size() < 2)
                {
                    new_paths.push_back(std::move(path));
                    continue;
                }

                // Walk along polyline checking visibility at intervals
                const Points &pts = path.polyline.points;
                bool current_visible = point_is_visible(pts[0]);
                Points current_segment;
                current_segment.push_back(pts[0]);
                Point last_known_pt = pts[0];

                for (size_t i = 1; i < pts.size(); ++i)
                {
                    const Point &prev_pt = pts[i - 1];
                    const Point &curr_pt = pts[i];
                    double seg_len = unscale<double>((curr_pt - prev_pt).cast<double>().norm());

                    if (seg_len <= sample_interval)
                    {
                        // Short segment - just check endpoint
                        bool end_visible = point_is_visible(curr_pt);
                        if (end_visible != current_visible)
                        {
                            Point boundary = find_visibility_boundary(last_known_pt, curr_pt);
                            current_segment.push_back(boundary);
                            // Save current segment with appropriate flow
                            Polyline seg_poly;
                            seg_poly.points = std::move(current_segment);
                            ExtrusionPath seg_path(seg_poly, path.attributes());
                            if (current_visible)
                                seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                            new_paths.push_back(std::move(seg_path));
                            // Start new segment
                            current_segment.clear();
                            current_segment.push_back(boundary);
                            current_visible = end_visible;
                        }
                        current_segment.push_back(curr_pt);
                        last_known_pt = curr_pt;
                    }
                    else
                    {
                        // Long segment - sample at intervals
                        Vec2d direction = (curr_pt - prev_pt).cast<double>();
                        Vec2d dir_unit = direction / direction.norm();

                        double distance_along = sample_interval;
                        while (distance_along < seg_len)
                        {
                            Point sample_pt(prev_pt.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                                            prev_pt.y() + coord_t(dir_unit.y() * scaled(distance_along)));
                            bool sample_visible = point_is_visible(sample_pt);

                            if (sample_visible != current_visible)
                            {
                                Point boundary = find_visibility_boundary(last_known_pt, sample_pt);
                                current_segment.push_back(boundary);
                                Polyline seg_poly;
                                seg_poly.points = std::move(current_segment);
                                ExtrusionPath seg_path(seg_poly, path.attributes());
                                if (current_visible)
                                    seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                                new_paths.push_back(std::move(seg_path));
                                current_segment.clear();
                                current_segment.push_back(boundary);
                                current_visible = sample_visible;
                            }
                            last_known_pt = sample_pt;
                            distance_along += sample_interval;
                        }
                        // Check endpoint
                        bool end_visible = point_is_visible(curr_pt);
                        if (end_visible != current_visible)
                        {
                            Point boundary = find_visibility_boundary(last_known_pt, curr_pt);
                            current_segment.push_back(boundary);
                            Polyline seg_poly;
                            seg_poly.points = std::move(current_segment);
                            ExtrusionPath seg_path(seg_poly, path.attributes());
                            if (current_visible)
                                seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                            new_paths.push_back(std::move(seg_path));
                            current_segment.clear();
                            current_segment.push_back(boundary);
                            current_visible = end_visible;
                        }
                        current_segment.push_back(curr_pt);
                        last_known_pt = curr_pt;
                    }
                }

                // Add final segment
                if (current_segment.size() >= 2)
                {
                    Polyline seg_poly;
                    seg_poly.points = std::move(current_segment);
                    ExtrusionPath seg_path(seg_poly, path.attributes());
                    if (current_visible)
                        seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                    new_paths.push_back(std::move(seg_path));
                }
            }
            paths = std::move(new_paths);
        }

        // Append paths to collection.
        if (!paths.empty())
        {
            // Stamp feature_id from PerimeterOrder's group assignment onto every path
            for (ExtrusionPath &path : paths)
                path.set_feature_id(pg_extrusion.group_id);

            if (extrusion.is_closed)
            {
                ExtrusionLoop extrusion_loop(std::move(paths));
                // Restore the orientation of the extrusion loop.
                if (pg_extrusion.is_contour() == extrusion_loop.is_clockwise())
                    extrusion_loop.reverse_loop();

                for (auto it = std::next(extrusion_loop.paths.begin()); it != extrusion_loop.paths.end(); ++it)
                {
                    assert(it->polyline.points.size() >= 2);
                    assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
                }
                assert(extrusion_loop.paths.front().first_point() == extrusion_loop.paths.back().last_point());

                extrusion_coll.append(std::move(extrusion_loop));
            }
            else
            {
                // Because we are processing one ExtrusionLine all ExtrusionPaths should form one connected path.
                // But there is possibility that due to numerical issue there is poss
                assert(
                    [&paths = std::as_const(paths)]() -> bool
                    {
                        for (auto it = std::next(paths.begin()); it != paths.end(); ++it)
                            if (std::prev(it)->polyline.last_point() != it->polyline.first_point())
                                return false;
                        return true;
                    }());
                ExtrusionMultiPath multi_path;
                multi_path.paths.emplace_back(std::move(paths.front()));

                for (auto it_path = std::next(paths.begin()); it_path != paths.end(); ++it_path)
                {
                    if (multi_path.paths.back().last_point() != it_path->first_point())
                    {
                        extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
                        multi_path = ExtrusionMultiPath();
                    }
                    multi_path.paths.emplace_back(std::move(*it_path));
                }

                extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
            }
        }
    }

    return extrusion_coll;
}

static ExtrusionEntityCollection traverse_extrusions(const PerimeterGenerator::Parameters &params,
                                                     const Polygons &lower_slices_polygons_cache,
                                                     const Polygons &lower_slices_raw,
                                                     Athena::PerimeterOrder::PerimeterExtrusions &pg_extrusions)
{
    using namespace Slic3r::Feature::FuzzySkin;

    ExtrusionEntityCollection extrusion_coll;
    for (Athena::PerimeterOrder::PerimeterExtrusion &pg_extrusion : pg_extrusions)
    {
        Athena::ExtrusionLine extrusion = pg_extrusion.extrusion;
        if (extrusion.empty())
            continue;

        const bool is_external = extrusion.inset_idx == 0;
        ExtrusionRole role_normal = is_external ? ExtrusionRole::ExternalPerimeter : ExtrusionRole::Perimeter;
        ExtrusionRole role_overhang = role_normal | ExtrusionRoleModifier::Bridge;

        // Visibility checks (fuzzy_skin_on_top, fuzzy_skin_first_layer) are now done per-segment
        // inside apply_fuzzy_skin, using the segment's midpoint for accurate detection.
        extrusion = apply_fuzzy_skin(extrusion, params.config, params.perimeter_regions, params.layer_id,
                                     pg_extrusion.extrusion.inset_idx,
                                     !pg_extrusion.extrusion.is_closed || pg_extrusion.is_contour(), params.layer,
                                     &lower_slices_raw, params.ext_perimeter_flow.scaled_width());

        // This prevents the artificial split at the "3 o'clock" position by ensuring the first
        // junction is at the rear of the object (minimum Y), which is a common seam preference.
        if (extrusion.is_closed && extrusion.junctions.size() > 2)
        {
            // Find junction with minimum Y (rear of object)
            auto min_y_it = std::min_element(extrusion.junctions.begin(), extrusion.junctions.end() - 1,
                                             [](const Athena::ExtrusionJunction &a, const Athena::ExtrusionJunction &b)
                                             { return a.p.y() < b.p.y(); });

            if (min_y_it != extrusion.junctions.begin() && min_y_it != extrusion.junctions.end() - 1)
            {
                // Rotate so min_y junction becomes first
                // Note: For closed loops, last junction equals first, so we exclude it from rotation
                std::rotate(extrusion.junctions.begin(), min_y_it, extrusion.junctions.end() - 1);
                // Update the last junction to match the new first junction (maintain closure)
                extrusion.junctions.back() = extrusion.junctions.front();
            }
        }

        ExtrusionPaths paths;
        // detect overhanging/bridging perimeters
        if (params.config.overhangs && params.layer_id > params.object_config.raft_layers &&
            !(params.object_config.support_material &&
              params.object_config.support_material_contact_distance.value == stcgNoGap &&
              !params.object_config.support_material_bridge_no_gap))
        {
            ClipperZUtils::ZPath extrusion_path;
            extrusion_path.reserve(extrusion.size());
            BoundingBox extrusion_path_bbox;
            for (const Athena::ExtrusionJunction &ej : extrusion.junctions)
            {
                extrusion_path.emplace_back(ej.p.x(), ej.p.y(), ej.w);
                extrusion_path_bbox.merge(Point{ej.p.x(), ej.p.y()});
            }

            ClipperZUtils::ZPaths lower_slices_paths;
            lower_slices_paths.reserve(lower_slices_polygons_cache.size());
            {
                Points clipped;
                extrusion_path_bbox.offset(SCALED_EPSILON);
                for (const Polygon &poly : lower_slices_polygons_cache)
                {
                    clipped.clear();
                    ClipperUtils::clip_clipper_polygon_with_subject_bbox(poly.points, extrusion_path_bbox, clipped);
                    if (!clipped.empty())
                    {
                        lower_slices_paths.emplace_back();
                        ClipperZUtils::ZPath &out = lower_slices_paths.back();
                        out.reserve(clipped.size());
                        for (const Point &pt : clipped)
                            out.emplace_back(pt.x(), pt.y(), 0);
                    }
                }
            }

            // get non-overhang paths by intersecting this loop with the grown lower slices
            Athena::extrusion_paths_append(
                paths, clip_extrusion(extrusion_path, lower_slices_paths, Clipper2Lib::ClipType::Intersection),
                role_normal, is_external ? params.ext_perimeter_flow : params.perimeter_flow, extrusion.inset_idx);

            // get overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            Athena::extrusion_paths_append(paths,
                                           clip_extrusion(extrusion_path, lower_slices_paths,
                                                          Clipper2Lib::ClipType::Difference),
                                           role_overhang, params.overhang_flow, extrusion.inset_idx);

            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            // Athena sometimes creates extrusion with zero-length (just two same endpoints);
            if (!paths.empty())
            {
                Point start_point = paths.front().first_point();
                if (!extrusion.is_closed)
                {
                    // Especially for open extrusion, we need to select a starting point that is at the start
                    // or the end of the extrusions to make one continuous line. Also, we prefer a non-overhang
                    // starting point.
                    struct PointInfo
                    {
                        size_t occurrence = 0;
                        bool is_overhang = false;
                    };
                    ankerl::unordered_dense::map<Point, PointInfo, PointHash> point_occurrence;
                    for (const ExtrusionPath &path : paths)
                    {
                        ++point_occurrence[path.polyline.first_point()].occurrence;
                        ++point_occurrence[path.polyline.last_point()].occurrence;
                        if (path.role().is_bridge())
                        {
                            point_occurrence[path.polyline.first_point()].is_overhang = true;
                            point_occurrence[path.polyline.last_point()].is_overhang = true;
                        }
                    }

                    // Prefer non-overhang point as a starting point.
                    for (const std::pair<Point, PointInfo> &pt : point_occurrence)
                        if (pt.second.occurrence == 1)
                        {
                            start_point = pt.first;
                            if (!pt.second.is_overhang)
                            {
                                start_point = pt.first;
                                break;
                            }
                        }
                }

                chain_and_reorder_extrusion_paths(paths, &start_point);
            }
        }
        else
        {
            Athena::extrusion_paths_append(paths, extrusion, role_normal,
                                           is_external ? params.ext_perimeter_flow : params.perimeter_flow,
                                           extrusion.inset_idx);
        }

        // When top_surface_flow_reduction is enabled, split paths at visibility boundaries and
        // apply reduced flow to visible segments. Uses interval-based sampling per config setting.
        if (is_external && params.config.top_surface_flow_reduction.value > 0 && params.layer != nullptr)
        {
            const double flow_multiplier = 1.0 - (params.config.top_surface_flow_reduction.value / 100.0);
            const coord_t check_diameter = params.ext_perimeter_flow.scaled_width() * 4;

            // Get visibility detection interval from config
            double sample_interval;
            switch (params.config.top_surface_visibility_detection.value)
            {
            case TopSurfaceVisibilityDetection::tsvdPrecise:
                sample_interval = 1.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdStandard:
                sample_interval = 2.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdRelaxed:
                sample_interval = 4.0;
                break;
            case TopSurfaceVisibilityDetection::tsvdMinimal:
                sample_interval = 8.0;
                break;
            default:
                sample_interval = 2.0;
                break;
            }

            // Lambda to check point visibility
            auto point_is_visible = [&](const Point &pt) -> bool
            {
                return params.layer->is_visible_from_top_or_bottom(pt, check_diameter, true, false);
            };

            // Lambda to find exact visibility boundary using binary search
            auto find_visibility_boundary = [&](const Point &p1, const Point &p2) -> Point
            {
                Point visible_pt = p1;
                Point hidden_pt = p2;
                if (point_is_visible(p1))
                    std::swap(visible_pt, hidden_pt);

                // Binary search for boundary
                for (int i = 0; i < 14; ++i)
                {
                    Point mid((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
                    if (point_is_visible(mid))
                        hidden_pt = mid;
                    else
                        visible_pt = mid;
                }
                return Point((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
            };

            ExtrusionPaths new_paths;
            for (ExtrusionPath &path : paths)
            {
                // Only process external perimeter paths (not overhang/bridge paths)
                if (path.role() != ExtrusionRole::ExternalPerimeter || path.polyline.size() < 2)
                {
                    new_paths.push_back(std::move(path));
                    continue;
                }

                // Walk along polyline checking visibility at intervals
                const Points &pts = path.polyline.points;
                bool current_visible = point_is_visible(pts[0]);
                Points current_segment;
                current_segment.push_back(pts[0]);
                Point last_known_pt = pts[0];

                for (size_t i = 1; i < pts.size(); ++i)
                {
                    const Point &prev_pt = pts[i - 1];
                    const Point &curr_pt = pts[i];
                    double seg_len = unscale<double>((curr_pt - prev_pt).cast<double>().norm());

                    if (seg_len <= sample_interval)
                    {
                        // Short segment - just check endpoint
                        bool end_visible = point_is_visible(curr_pt);
                        if (end_visible != current_visible)
                        {
                            Point boundary = find_visibility_boundary(last_known_pt, curr_pt);
                            current_segment.push_back(boundary);
                            // Save current segment with appropriate flow
                            Polyline seg_poly;
                            seg_poly.points = std::move(current_segment);
                            ExtrusionPath seg_path(seg_poly, path.attributes());
                            if (current_visible)
                                seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                            new_paths.push_back(std::move(seg_path));
                            // Start new segment
                            current_segment.clear();
                            current_segment.push_back(boundary);
                            current_visible = end_visible;
                        }
                        current_segment.push_back(curr_pt);
                        last_known_pt = curr_pt;
                    }
                    else
                    {
                        // Long segment - sample at intervals
                        Vec2d direction = (curr_pt - prev_pt).cast<double>();
                        Vec2d dir_unit = direction / direction.norm();

                        double distance_along = sample_interval;
                        while (distance_along < seg_len)
                        {
                            Point sample_pt(prev_pt.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                                            prev_pt.y() + coord_t(dir_unit.y() * scaled(distance_along)));
                            bool sample_visible = point_is_visible(sample_pt);

                            if (sample_visible != current_visible)
                            {
                                Point boundary = find_visibility_boundary(last_known_pt, sample_pt);
                                current_segment.push_back(boundary);
                                Polyline seg_poly;
                                seg_poly.points = std::move(current_segment);
                                ExtrusionPath seg_path(seg_poly, path.attributes());
                                if (current_visible)
                                    seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                                new_paths.push_back(std::move(seg_path));
                                current_segment.clear();
                                current_segment.push_back(boundary);
                                current_visible = sample_visible;
                            }
                            last_known_pt = sample_pt;
                            distance_along += sample_interval;
                        }
                        // Check endpoint
                        bool end_visible = point_is_visible(curr_pt);
                        if (end_visible != current_visible)
                        {
                            Point boundary = find_visibility_boundary(last_known_pt, curr_pt);
                            current_segment.push_back(boundary);
                            Polyline seg_poly;
                            seg_poly.points = std::move(current_segment);
                            ExtrusionPath seg_path(seg_poly, path.attributes());
                            if (current_visible)
                                seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                            new_paths.push_back(std::move(seg_path));
                            current_segment.clear();
                            current_segment.push_back(boundary);
                            current_visible = end_visible;
                        }
                        current_segment.push_back(curr_pt);
                        last_known_pt = curr_pt;
                    }
                }

                // Add final segment
                if (current_segment.size() >= 2)
                {
                    Polyline seg_poly;
                    seg_poly.points = std::move(current_segment);
                    ExtrusionPath seg_path(seg_poly, path.attributes());
                    if (current_visible)
                        seg_path.set_mm3_per_mm(seg_path.mm3_per_mm() * flow_multiplier);
                    new_paths.push_back(std::move(seg_path));
                }
            }
            paths = std::move(new_paths);
        }

        // Append paths to collection.
        if (!paths.empty())
        {
            // Stamp feature_id from PerimeterOrder's group assignment onto every path
            for (ExtrusionPath &path : paths)
                path.set_feature_id(pg_extrusion.group_id);

            if (extrusion.is_closed)
            {
                ExtrusionLoop extrusion_loop(std::move(paths));
                // Restore the orientation of the extrusion loop.
                if (pg_extrusion.is_contour() == extrusion_loop.is_clockwise())
                    extrusion_loop.reverse_loop();

                for (auto it = std::next(extrusion_loop.paths.begin()); it != extrusion_loop.paths.end(); ++it)
                {
                    assert(it->polyline.points.size() >= 2);
                    assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
                }
                assert(extrusion_loop.paths.front().first_point() == extrusion_loop.paths.back().last_point());

                extrusion_coll.append(std::move(extrusion_loop));
            }
            else
            {
                // Because we are processing one ExtrusionLine all ExtrusionPaths should form one connected path.
                // But there is possibility that due to numerical issue there is poss
                assert(
                    [&paths = std::as_const(paths)]() -> bool
                    {
                        for (auto it = std::next(paths.begin()); it != paths.end(); ++it)
                            if (std::prev(it)->polyline.last_point() != it->polyline.first_point())
                                return false;
                        return true;
                    }());
                ExtrusionMultiPath multi_path;
                multi_path.paths.emplace_back(std::move(paths.front()));

                for (auto it_path = std::next(paths.begin()); it_path != paths.end(); ++it_path)
                {
                    if (multi_path.paths.back().last_point() != it_path->first_point())
                    {
                        extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
                        multi_path = ExtrusionMultiPath();
                    }
                    multi_path.paths.emplace_back(std::move(*it_path));
                }

                extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
            }
        }
    }

    return extrusion_coll;
}

#ifdef ARACHNE_DEBUG
static void export_perimeters_to_svg(const std::string &path, const Polygons &contours,
                                     const Arachne::Perimeters &perimeters, const ExPolygons &infill_area)
{
    coordf_t stroke_width = scale_(0.03);
    BoundingBox bbox = get_extents(contours);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw(infill_area, "cyan");

    for (const Arachne::Perimeter &perimeter : perimeters)
        for (const Arachne::ExtrusionLine &extrusion_line : perimeter)
        {
            ThickPolyline thick_polyline = to_thick_polyline(extrusion_line);
            svg.draw({thick_polyline}, "green", "blue", stroke_width);
        }

    for (const Line &line : to_lines(contours))
        svg.draw(line, "red", stroke_width);
}
#endif

// find out if paths touch - at least one point of one path is within limit distance of second path
bool paths_touch(const ExtrusionPath &path_one, const ExtrusionPath &path_two, double limit_distance)
{
    AABBTreeLines::LinesDistancer<Line> lines_two{path_two.as_polyline().lines()};
    for (size_t pt_idx = 0; pt_idx < path_one.polyline.size(); pt_idx++)
    {
        if (lines_two.distance_from_lines<false>(path_one.polyline.points[pt_idx]) < limit_distance)
        {
            return true;
        }
    }
    AABBTreeLines::LinesDistancer<Line> lines_one{path_one.as_polyline().lines()};
    for (size_t pt_idx = 0; pt_idx < path_two.polyline.size(); pt_idx++)
    {
        if (lines_one.distance_from_lines<false>(path_two.polyline.points[pt_idx]) < limit_distance)
        {
            return true;
        }
    }
    return false;
}

Polylines reconnect_polylines(const Polylines &polylines, double limit_distance)
{
    if (polylines.empty())
        return polylines;

    std::unordered_map<size_t, Polyline> connected;
    connected.reserve(polylines.size());
    for (size_t i = 0; i < polylines.size(); i++)
    {
        if (!polylines[i].empty())
        {
            connected.emplace(i, polylines[i]);
        }
    }

    // Original code performed find() then at() on same key, causing redundant hash computations.
    // This fix caches the iterator from find() and reuses it, reducing lookups by 50%.
    //
    // Performance impact: 1.5-3× faster for this function (eliminates redundant hash lookups).
    // Context: Connects nearby polyline endpoints during perimeter generation. Called per layer.
    // Note: Code uses unordered_map (O(1) lookups), not map (O(log n)), so improvement is
    // smaller than originally estimated, but still worthwhile.

    // Pre-compute squared distance to avoid repeated multiplications
    const double limit_distance_sq = limit_distance * limit_distance;

    for (size_t a = 0; a < polylines.size(); a++)
    {
        // Cache iterator instead of double lookup (find + at)
        auto it_a = connected.find(a);
        if (it_a == connected.end())
        {
            continue;
        }
        Polyline &base = it_a->second; // Use cached iterator - no second lookup

        for (size_t b = a + 1; b < polylines.size(); b++)
        {
            // Cache iterator for 'b' as well
            auto it_b = connected.find(b);
            if (it_b == connected.end())
            {
                continue;
            }
            Polyline &next = it_b->second; // Use cached iterator

            // Check all 4 connection possibilities using pre-computed squared distance
            if ((base.last_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance_sq)
            {
                base.append(std::move(next));
                connected.erase(it_b); // Use iterator directly for O(1) erase
            }
            else if ((base.last_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance_sq)
            {
                base.points.insert(base.points.end(), next.points.rbegin(), next.points.rend());
                connected.erase(it_b);
            }
            else if ((base.first_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance_sq)
            {
                next.append(std::move(base));
                base = std::move(next);
                base.reverse();
                connected.erase(it_b);
            }
            else if ((base.first_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance_sq)
            {
                base.reverse();
                base.append(std::move(next));
                base.reverse();
                connected.erase(it_b);
            }
        }
    }

    // Pre-allocate result vector to avoid reallocations
    Polylines result;
    result.reserve(connected.size());

    for (auto &ext : connected)
    {
        result.push_back(std::move(ext.second));
    }

    return result;
}

ExtrusionPaths sort_extra_perimeters(const ExtrusionPaths &extra_perims, int index_of_first_unanchored,
                                     double extrusion_spacing)
{
    if (extra_perims.empty())
        return {};

    // Original code used iterative dependency resolution with O(n³) complexity in Phase 2.
    // This fix replaces it with proper topological sort while preserving travel optimization.
    //
    // Performance impact: Changes O(n³ log n) to O(n²) - 10-100× speedup for many extra perimeters.
    // Context: Sorts extra perimeter paths by touch dependencies for correct print order.

    const size_t n = extra_perims.size();

    // Phase 1: Build dependency graph - O(n²) [unavoidable without spatial index]
    std::vector<std::unordered_set<size_t>> dependencies(n);

    for (size_t path_idx = 0; path_idx < n; path_idx++)
    {
        for (size_t prev_path_idx = 0; prev_path_idx < path_idx; prev_path_idx++)
        {
            if (paths_touch(extra_perims[path_idx], extra_perims[prev_path_idx], extrusion_spacing * 1.5f))
            {
                // path_idx depends on prev_path_idx (must print after)
                dependencies[path_idx].insert(prev_path_idx);
            }
        }
    }

    // Phase 2: Initialize dependency state
    // Mark anchored paths as having no dependencies (already processed/anchored)
    for (int i = 0; i < index_of_first_unanchored; i++)
    {
        dependencies[i].clear(); // Anchored paths have no dependencies to wait for
    }

    // Original code had O(n³) iterative dependency resolution here (lines 978-1001).
    // This has been removed because Phase 3 below already implements correct
    // topological sort by checking dependencies.empty() and updating as paths
    // are consumed. The O(n³) phase was unnecessary complexity.

    // Phase 3: Greedy path selection with travel distance optimization - O(n²)
    // This implements topological sort while minimizing travel distance
    Point current_point = extra_perims.begin()->first_point();

    ExtrusionPaths sorted_paths{};
    size_t null_idx = size_t(-1);
    size_t next_idx = null_idx;
    bool reverse = false;
    while (true)
    {
        if (next_idx == null_idx)
        { // find next pidx to print
            double dist = std::numeric_limits<double>::max();
            for (size_t path_idx = 0; path_idx < extra_perims.size(); path_idx++)
            {
                if (!dependencies[path_idx].empty())
                    continue;
                const auto &path = extra_perims[path_idx];
                double dist_a = (path.first_point() - current_point).cast<double>().squaredNorm();
                if (dist_a < dist)
                {
                    dist = dist_a;
                    next_idx = path_idx;
                    reverse = false;
                }
                double dist_b = (path.last_point() - current_point).cast<double>().squaredNorm();
                if (dist_b < dist)
                {
                    dist = dist_b;
                    next_idx = path_idx;
                    reverse = true;
                }
            }
            if (next_idx == null_idx)
            {
                break;
            }
        }
        else
        {
            // we have valid next_idx, add it to the sorted paths, update dependencies, update current point and potentialy set new next_idx
            ExtrusionPath path = extra_perims[next_idx];
            if (reverse)
            {
                path.reverse();
            }
            sorted_paths.push_back(path);
            assert(dependencies[next_idx].empty());
            dependencies[next_idx].insert(null_idx);
            current_point = sorted_paths.back().last_point();
            for (size_t path_idx = 0; path_idx < extra_perims.size(); path_idx++)
            {
                dependencies[path_idx].erase(next_idx);
            }
            double dist = std::numeric_limits<double>::max();
            next_idx = null_idx;

            for (size_t path_idx = next_idx + 1; path_idx < extra_perims.size(); path_idx++)
            {
                if (!dependencies[path_idx].empty())
                {
                    continue;
                }
                const ExtrusionPath &next_path = extra_perims[path_idx];
                double dist_a = (next_path.first_point() - current_point).cast<double>().squaredNorm();
                if (dist_a < dist)
                {
                    dist = dist_a;
                    next_idx = path_idx;
                    reverse = false;
                }
                double dist_b = (next_path.last_point() - current_point).cast<double>().squaredNorm();
                if (dist_b < dist)
                {
                    dist = dist_b;
                    next_idx = path_idx;
                    reverse = true;
                }
            }
            if (dist > scaled(5.0))
            {
                next_idx = null_idx;
            }
        }
    }

    ExtrusionPaths reconnected;
    reconnected.reserve(sorted_paths.size());
    for (const ExtrusionPath &path : sorted_paths)
    {
        if (!reconnected.empty() &&
            (reconnected.back().last_point() - path.first_point()).cast<double>().squaredNorm() <
                extrusion_spacing * extrusion_spacing * 4.0)
        {
            reconnected.back().polyline.points.insert(reconnected.back().polyline.points.end(),
                                                      path.polyline.points.begin(), path.polyline.points.end());
        }
        else
        {
            reconnected.push_back(path);
        }
    }

    // Phase 4: Reconnect close paths and filter short ones
    ExtrusionPaths filtered;
    filtered.reserve(reconnected.size());
    for (ExtrusionPath &p : reconnected)
    {
        if (p.length() > 3 * extrusion_spacing)
        {
            filtered.push_back(p);
        }
    }

    return filtered;
}

#define EXTRA_PERIMETER_OFFSET_PARAMETERS JoinType::Square, 0.
// #define EXTRA_PERIM_DEBUG_FILES
// Function will generate extra perimeters clipped over nonbridgeable areas of the provided surface and returns both the new perimeters and
// Polygons filled by those clipped perimeters
std::tuple<std::vector<ExtrusionPaths>, Polygons> generate_extra_perimeters_over_overhangs(
    ExPolygons infill_area, const Polygons &lower_slices_polygons, int perimeter_count, const Flow &overhang_flow,
    double scaled_resolution, const PrintObjectConfig &object_config, const PrintConfig &print_config)
{
    coord_t anchors_size = std::min(coord_t(scale_(EXTERNAL_INFILL_MARGIN)),
                                    overhang_flow.scaled_spacing() * (perimeter_count + 1));

    BoundingBox infill_area_bb = get_extents(infill_area).inflated(SCALED_EPSILON);
    Polygons optimized_lower_slices = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_slices_polygons,
                                                                                            infill_area_bb);
    Polygons overhangs = diff(infill_area, optimized_lower_slices);

    if (overhangs.empty())
    {
        return {};
    }

    AABBTreeLines::LinesDistancer<Line> lower_layer_aabb_tree{to_lines(optimized_lower_slices)};
    Polygons anchors = intersection(infill_area, optimized_lower_slices);
    Polygons inset_anchors = diff(anchors, expand(overhangs, anchors_size + 0.1 * overhang_flow.scaled_width(),
                                                  EXTRA_PERIMETER_OFFSET_PARAMETERS));
    Polygons inset_overhang_area = diff(infill_area, inset_anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
    {
        BoundingBox bbox = get_extents(inset_overhang_area);
        bbox.offset(scale_(1.));
        ::Slic3r::SVG svg(debug_out_path("inset_overhang_area").c_str(), bbox);
        for (const Line &line : to_lines(inset_anchors))
            svg.draw(line, "purple", scale_(0.25));
        for (const Line &line : to_lines(inset_overhang_area))
            svg.draw(line, "red", scale_(0.15));
        svg.Close();
    }
#endif

    Polygons inset_overhang_area_left_unfilled;

    std::vector<ExtrusionPaths> extra_perims; // overhang region -> extrusion paths
    for (const ExPolygon &overhang : union_ex(to_expolygons(inset_overhang_area)))
    {
        Polygons overhang_to_cover = to_polygons(overhang);
        Polygons expanded_overhang_to_cover = expand(overhang_to_cover, 1.1 * overhang_flow.scaled_spacing());
        Polygons shrinked_overhang_to_cover = shrink(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing());

        Polygons real_overhang = intersection(overhang_to_cover, overhangs);
        if (real_overhang.empty())
        {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), overhang_to_cover.begin(),
                                                     overhang_to_cover.end());
            continue;
        }
        ExtrusionPaths &overhang_region = extra_perims.emplace_back();

        Polygons anchoring = intersection(expanded_overhang_to_cover, inset_anchors);
        Polygons perimeter_polygon = offset(union_(expand(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing()),
                                                   anchoring),
                                            -overhang_flow.scaled_spacing() * 0.6);

        Polygon anchoring_convex_hull = Geometry::convex_hull(anchoring);
        double unbridgeable_area = area(diff(real_overhang, {anchoring_convex_hull}));

        auto [dir, unsupp_dist] = detect_bridging_direction(real_overhang, anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
        {
            BoundingBox bbox = get_extents(anchoring_convex_hull);
            bbox.offset(scale_(1.));
            ::Slic3r::SVG svg(debug_out_path("bridge_check").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon))
                svg.draw(line, "purple", scale_(0.25));
            for (const Line &line : to_lines(real_overhang))
                svg.draw(line, "red", scale_(0.20));
            for (const Line &line : to_lines(anchoring_convex_hull))
                svg.draw(line, "green", scale_(0.15));
            for (const Line &line : to_lines(anchoring))
                svg.draw(line, "yellow", scale_(0.10));
            for (const Line &line : to_lines(diff_ex(perimeter_polygon, {anchoring_convex_hull})))
                svg.draw(line, "black", scale_(0.10));
            for (const Line &line :
                 to_lines(diff_pl(to_polylines(diff(real_overhang, anchors)), expand(anchors, float(SCALED_EPSILON)))))
                svg.draw(line, "blue", scale_(0.30));
            svg.Close();
        }
#endif

        if (unbridgeable_area < 0.2 * area(real_overhang) && unsupp_dist < total_length(real_overhang) * 0.2)
        {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), overhang_to_cover.begin(),
                                                     overhang_to_cover.end());
            perimeter_polygon.clear();
        }
        else
        {
            //  fill the overhang with perimeters
            int continuation_loops = 2;
            int overhang_iters = 0;
            double prev_prev_area = -1;
            size_t oscillation_start_size = 0; // overhang_region size when oscillation first detected
            bool oscillating = false;
            while (continuation_loops >= 0)
            {
                // Safety cap: maximum concentric overhang perimeters per region
                if (++overhang_iters > 50)
                    break;
                auto prev = perimeter_polygon;
                // prepare next perimeter lines
                Polylines perimeter = intersection_pl(to_polylines(perimeter_polygon), shrinked_overhang_to_cover);

                // do not add the perimeter to result yet, first check if perimeter_polygon is not empty after shrinking - this would mean
                //  that the polygon was possibly too small for full perimeter loop and in that case try gap fill first
                perimeter_polygon = union_(perimeter_polygon, anchoring);
                perimeter_polygon = intersection(offset(perimeter_polygon, -overhang_flow.scaled_spacing()),
                                                 expanded_overhang_to_cover);

                if (perimeter_polygon.empty())
                { // fill possible gaps of single extrusion width
                    Polygons shrinked = intersection(offset(prev, -0.3 * overhang_flow.scaled_spacing()),
                                                     expanded_overhang_to_cover);
                    if (!shrinked.empty())
                        extrusion_paths_append(overhang_region,
                                               reconnect_polylines(perimeter, overhang_flow.scaled_spacing()),
                                               ExtrusionAttributes{ExtrusionRole::OverhangPerimeter, overhang_flow});

                    Polylines fills;
                    ExPolygons gap = shrinked.empty() ? offset_ex(prev, overhang_flow.scaled_spacing() * 0.5)
                                                      : to_expolygons(shrinked);

                    for (const ExPolygon &ep : gap)
                    {
                        ep.medial_axis(0.75 * overhang_flow.scaled_width(), 3.0 * overhang_flow.scaled_spacing(),
                                       &fills);
                    }
                    if (!fills.empty())
                    {
                        fills = intersection_pl(fills, shrinked_overhang_to_cover);
                        extrusion_paths_append(overhang_region,
                                               reconnect_polylines(fills, overhang_flow.scaled_spacing()),
                                               ExtrusionAttributes{ExtrusionRole::OverhangPerimeter, overhang_flow});
                    }
                    break;
                }
                else
                {
                    extrusion_paths_append(overhang_region,
                                           reconnect_polylines(perimeter, overhang_flow.scaled_spacing()),
                                           ExtrusionAttributes{ExtrusionRole::OverhangPerimeter, overhang_flow});
                }

                if (intersection(perimeter_polygon, real_overhang).empty())
                {
                    continuation_loops--;
                }

                // Detect oscillation: the union-with-anchoring + inset cycle can
                // ping-pong between two polygon sizes. Compare area with two
                // iterations ago to catch this steady-state cycle.
                // When detected, record the current size of overhang_region so
                // we can discard the junk perimeters emitted during oscillation.
                double curr_area = std::abs(area(perimeter_polygon));
                if (prev_prev_area >= 0 && curr_area > 0 && std::abs(prev_prev_area - curr_area) / curr_area < 0.05)
                {
                    if (!oscillating)
                    {
                        oscillating = true;
                        oscillation_start_size = overhang_region.size();
                    }
                    continuation_loops--;
                }
                prev_prev_area = std::abs(area(prev));

                if (prev == perimeter_polygon)
                {
#ifdef EXTRA_PERIM_DEBUG_FILES
                    BoundingBox bbox = get_extents(perimeter_polygon);
                    bbox.offset(scale_(5.));
                    ::Slic3r::SVG svg(debug_out_path("perimeter_polygon").c_str(), bbox);
                    for (const Line &line : to_lines(perimeter_polygon))
                        svg.draw(line, "blue", scale_(0.25));
                    for (const Line &line : to_lines(overhang_to_cover))
                        svg.draw(line, "red", scale_(0.20));
                    for (const Line &line : to_lines(real_overhang))
                        svg.draw(line, "green", scale_(0.15));
                    for (const Line &line : to_lines(anchoring))
                        svg.draw(line, "yellow", scale_(0.10));
                    svg.Close();
#endif
                    break;
                }
            }

            // If oscillation was detected, the geometry is unsuitable for
            // concentric overhang fill. Discard all extra perimeters for this
            // region and let normal perimeter/infill processing handle it.
            if (oscillating)
            {
                extra_perims.pop_back();
                inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(),
                                                         overhang_to_cover.begin(), overhang_to_cover.end());
                continue;
            }

            perimeter_polygon = expand(perimeter_polygon, 0.5 * overhang_flow.scaled_spacing());
            perimeter_polygon = union_(perimeter_polygon, anchoring);
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), perimeter_polygon.begin(),
                                                     perimeter_polygon.end());

#ifdef EXTRA_PERIM_DEBUG_FILES
            BoundingBox bbox = get_extents(inset_overhang_area);
            bbox.offset(scale_(2.));
            ::Slic3r::SVG svg(debug_out_path("pre_final").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon))
                svg.draw(line, "blue", scale_(0.05));
            for (const Line &line : to_lines(anchoring))
                svg.draw(line, "green", scale_(0.05));
            for (const Line &line : to_lines(overhang_to_cover))
                svg.draw(line, "yellow", scale_(0.05));
            for (const Line &line : to_lines(inset_overhang_area_left_unfilled))
                svg.draw(line, "red", scale_(0.05));
            svg.Close();
#endif
            overhang_region.erase(std::remove_if(overhang_region.begin(), overhang_region.end(),
                                                 [](const ExtrusionPath &p) { return p.empty(); }),
                                  overhang_region.end());

            if (!overhang_region.empty())
            {
                // there is a special case, where the first (or last) generated overhang perimeter eats all anchor space.
                // When this happens, the first overhang perimeter is also a closed loop, and needs special check
                // instead of the following simple is_anchored lambda, which checks only the first and last point (not very useful on closed
                // polyline)
                bool first_overhang_is_closed_and_anchored =
                    (overhang_region.front().first_point() == overhang_region.front().last_point() &&
                     !intersection_pl(overhang_region.front().polyline, optimized_lower_slices).empty());

                auto is_anchored = [&lower_layer_aabb_tree](const ExtrusionPath &path)
                {
                    return lower_layer_aabb_tree.distance_from_lines<true>(path.first_point()) <= 0 ||
                           lower_layer_aabb_tree.distance_from_lines<true>(path.last_point()) <= 0;
                };
                if (!first_overhang_is_closed_and_anchored)
                {
                    std::reverse(overhang_region.begin(), overhang_region.end());
                }
                else
                {
                    size_t min_dist_idx = 0;
                    double min_dist = std::numeric_limits<double>::max();
                    for (size_t i = 0; i < overhang_region.front().polyline.size(); i++)
                    {
                        Point p = overhang_region.front().polyline[i];
                        if (double d = lower_layer_aabb_tree.distance_from_lines<true>(p) < min_dist)
                        {
                            min_dist = d;
                            min_dist_idx = i;
                        }
                    }
                    std::rotate(overhang_region.front().polyline.begin(),
                                overhang_region.front().polyline.begin() + min_dist_idx,
                                overhang_region.front().polyline.end());
                }
                auto first_unanchored = std::stable_partition(overhang_region.begin(), overhang_region.end(),
                                                              is_anchored);
                int index_of_first_unanchored = first_unanchored - overhang_region.begin();
                overhang_region = sort_extra_perimeters(overhang_region, index_of_first_unanchored,
                                                        overhang_flow.scaled_spacing());
            }
        }
    }

#ifdef EXTRA_PERIM_DEBUG_FILES
    BoundingBox bbox = get_extents(inset_overhang_area);
    bbox.offset(scale_(2.));
    ::Slic3r::SVG svg(debug_out_path(("final" + std::to_string(rand())).c_str()).c_str(), bbox);
    for (const Line &line : to_lines(inset_overhang_area_left_unfilled))
        svg.draw(line, "blue", scale_(0.05));
    for (const Line &line : to_lines(inset_overhang_area))
        svg.draw(line, "green", scale_(0.05));
    for (const Line &line : to_lines(diff(inset_overhang_area, inset_overhang_area_left_unfilled)))
        svg.draw(line, "yellow", scale_(0.05));
    svg.Close();
#endif

    inset_overhang_area_left_unfilled = union_(inset_overhang_area_left_unfilled);

    return {extra_perims, diff(inset_overhang_area, inset_overhang_area_left_unfilled)};
}

// Thanks, Cura developers, for implementing an algorithm for generating perimeters with variable width (Arachne) that is based on the paper
// "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling"
void PerimeterGenerator::process_arachne(
    // Inputs:
    const Parameters &params, const Surface &surface, const ExPolygons *lower_slices, const ExPolygons *upper_slices,
    // Cache:
    Polygons &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection & /* out_gap_fill */,
    // Infills without the gap fills
    ExPolygons &out_fill_expolygons)
{
    // other perimeters
    coord_t perimeter_width = params.perimeter_flow.scaled_width();
    coord_t perimeter_spacing = params.perimeter_flow.scaled_spacing();
    // external perimeters
    coord_t ext_perimeter_width = params.ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing = params.ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2 = scaled<coord_t>(
        0.5f * (params.ext_perimeter_flow.spacing() + params.perimeter_flow.spacing()));
    // solid infill
    coord_t solid_infill_spacing = params.solid_infill_flow.scaled_spacing();

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty())
    {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used
        // in the current layer
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder - 1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter / 2)));
    }

    Polygons lower_slices_raw;
    if (lower_slices != nullptr)
    {
        lower_slices_raw = to_polygons(*lower_slices);
    }

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    // detect how many perimeters must be generated for this island
    int loop_number = params.config.perimeters + surface.extra_perimeters - 1; // 0-indexed loops
    if (loop_number > 0 &&
        ((params.config.top_one_perimeter_type == TopOnePerimeterType::TopmostOnly && upper_slices == nullptr) ||
         (params.config.only_one_perimeter_first_layer && params.layer_id == 0)))
        loop_number = 0;

    // Calculate how many inner loops remain when TopSurfaces is selected.
    const int inner_loop_number = (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces &&
                                   upper_slices != nullptr)
                                      ? loop_number - 1
                                      : -1;

    // Set one perimeter when TopSurfaces is selected.
    if (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces)
        loop_number = 0;

    ExPolygons last = offset_ex(surface.expolygon.simplify_p(params.scaled_resolution),
                                -float(ext_perimeter_width / 2. - ext_perimeter_spacing / 2.));
    Polygons last_p = to_polygons(last);
    Arachne::WallToolPaths wall_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(loop_number + 1),
                                           0, params.layer_height, params.object_config, params.print_config);
    Arachne::Perimeters perimeters = wall_tool_paths.getToolPaths();
    ExPolygons infill_contour = union_ex(wall_tool_paths.getInnerContour());
    infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);

    // Check if there are some remaining perimeters to generate (the number of perimeters
    // is greater than one together with enabled the single perimeter on top surface feature).
    if (inner_loop_number >= 0)
    {
        assert(upper_slices != nullptr);

        // Infill contour bounding box.
        BoundingBox infill_contour_bbox = get_extents(infill_contour);
        infill_contour_bbox.offset(SCALED_EPSILON);

        // Get top ExPolygons from current infill contour.
        const Polygons upper_slices_clipped =
            ClipperUtils::clip_clipper_polygons_with_subject_bbox(*upper_slices, infill_contour_bbox);
        ExPolygons top_expolygons = diff_ex(infill_contour, upper_slices_clipped);

        if (!top_expolygons.empty())
        {
            if (lower_slices != nullptr)
            {
                const float bridge_offset = float(std::max<coord_t>(ext_perimeter_spacing, perimeter_width));
                const Polygons lower_slices_clipped =
                    ClipperUtils::clip_clipper_polygons_with_subject_bbox(*lower_slices, infill_contour_bbox);
                const ExPolygons current_slices_bridges = offset_ex(diff_ex(top_expolygons, lower_slices_clipped),
                                                                    bridge_offset);

                // Remove bridges from top surface polygons.
                top_expolygons = diff_ex(top_expolygons, current_slices_bridges);
            }

            // Filter out areas that are too thin and expand top surface polygons a bit to hide the wall line.
            const float top_surface_min_width = std::max<float>(float(ext_perimeter_spacing) / 4.f +
                                                                    scaled<float>(0.00001),
                                                                float(perimeter_width) / 4.f);
            top_expolygons = offset2_ex(top_expolygons, -top_surface_min_width,
                                        top_surface_min_width + float(perimeter_width));

            // Get the not-top ExPolygons (including bridges) from current slices and expanded real top ExPolygons (without bridges).
            const ExPolygons not_top_expolygons = diff_ex(infill_contour, top_expolygons);

            // Get final top ExPolygons.
            top_expolygons = intersection_ex(top_expolygons, infill_contour);

            const Polygons not_top_polygons = to_polygons(not_top_expolygons);
            Arachne::WallToolPaths inner_wall_tool_paths(not_top_polygons, perimeter_spacing, perimeter_spacing,
                                                         coord_t(inner_loop_number + 1), 0, params.layer_height,
                                                         params.object_config, params.print_config);
            Arachne::Perimeters inner_perimeters = inner_wall_tool_paths.getToolPaths();

            // Recalculate indexes of inner perimeters before merging them.
            if (!perimeters.empty())
            {
                for (Arachne::VariableWidthLines &inner_perimeter : inner_perimeters)
                {
                    if (inner_perimeter.empty())
                        continue;

                    for (Arachne::ExtrusionLine &el : inner_perimeter)
                        ++el.inset_idx;
                }
            }

            perimeters.insert(perimeters.end(), inner_perimeters.begin(), inner_perimeters.end());
            infill_contour = union_ex(top_expolygons, inner_wall_tool_paths.getInnerContour());
            infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);
        }
        else
        {
            // There is no top surface ExPolygon, so we call Arachne again with parameters
            // like when the single perimeter feature is disabled.
            Arachne::WallToolPaths no_single_perimeter_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing,
                                                                  coord_t(inner_loop_number + 2), 0,
                                                                  params.layer_height, params.object_config,
                                                                  params.print_config);
            perimeters = no_single_perimeter_tool_paths.getToolPaths();
            infill_contour = union_ex(no_single_perimeter_tool_paths.getInnerContour());
            infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);
        }
    }

    loop_number = int(perimeters.size()) - 1;

#ifdef ARACHNE_DEBUG
    {
        static int iRun = 0;
        export_perimeters_to_svg(debug_out_path("arachne-perimeters-%d-%d.svg", params.layer_id, iRun++),
                                 to_polygons(last), perimeters, union_ex(wallToolPaths.getInnerContour()));
    }
#endif

    // All closed ExtrusionLine should have the same the first and the last point.
    // But in rare cases, Arachne produce ExtrusionLine marked as closed but without
    // equal the first and the last point.
    assert(
        [&perimeters = std::as_const(perimeters)]() -> bool
        {
            for (const Arachne::Perimeter &perimeter : perimeters)
                for (const Arachne::ExtrusionLine &el : perimeter)
                    if (el.is_closed && el.junctions.front().p != el.junctions.back().p)
                        return false;
            return true;
        }());

    Arachne::PerimeterOrder::PerimeterExtrusions ordered_extrusions =
        Arachne::PerimeterOrder::ordered_perimeter_extrusions(perimeters, params.config.external_perimeters_first);

    ExtrusionEntityCollection extrusion_coll = traverse_extrusions(params, lower_slices_polygons_cache,
                                                                   lower_slices_raw, ordered_extrusions);
    if (!extrusion_coll.empty())
        out_loops.append(extrusion_coll);

    const coord_t spacing = (perimeters.size() == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
    if (offset_ex(infill_contour, -float(spacing / 2.)).empty())
        infill_contour.clear(); // Infill region is too small, so let's filter it out.

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset = (loop_number < 0)    ? 0
                    : (loop_number == 0) ?
                                         // one loop
                        ext_perimeter_spacing
                                         :
                                         // two or more loops?
                        perimeter_spacing;

    inset = coord_t(scale_(params.config.get_abs_value("infill_overlap", unscale<double>(inset))));
    Polygons pp;
    for (ExPolygon &ex : infill_contour)
        ex.simplify_p(params.scaled_resolution, &pp);
    // Clip simplified polygons against original contour (see process_athena)
    pp = intersection(pp, to_polygons(infill_contour));
    // collapse too narrow infill areas
    const auto min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    // append infill areas to fill_surfaces
    ExPolygons infill_areas = offset2_ex(union_ex(pp), float(-min_perimeter_infill_spacing / 2.),
                                         float(inset + min_perimeter_infill_spacing / 2.));

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers)
    {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters,
              filled_area] = generate_extra_perimeters_over_overhangs(infill_areas, lower_slices_polygons_cache,
                                                                      loop_number + 1, params.overhang_flow,
                                                                      params.scaled_resolution, params.object_config,
                                                                      params.print_config);
        if (!extra_perimeters.empty())
        {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection &>(
                *out_loops.entities.back());
            ExtrusionEntitiesPtr old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters)
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }

    append(out_fill_expolygons, std::move(infill_areas));
}

PerimeterRegion::PerimeterRegion(const LayerRegion &layer_region) : region(&layer_region.region())
{
    this->expolygons = to_expolygons(layer_region.slices().surfaces);
    this->bbox = get_extents(this->expolygons);
}

bool PerimeterRegion::has_compatible_perimeter_regions(const PrintRegionConfig &config,
                                                       const PrintRegionConfig &other_config)
{
    return config.fuzzy_skin == other_config.fuzzy_skin &&
           config.fuzzy_skin_thickness == other_config.fuzzy_skin_thickness &&
           config.fuzzy_skin_point_dist == other_config.fuzzy_skin_point_dist;
}

void PerimeterRegion::merge_compatible_perimeter_regions(PerimeterRegions &perimeter_regions)
{
    if (perimeter_regions.size() <= 1)
    {
        return;
    }

    PerimeterRegions perimeter_regions_merged;
    for (auto it_curr_region = perimeter_regions.begin(); it_curr_region != perimeter_regions.end();)
    {
        PerimeterRegion current_merge = *it_curr_region;
        auto it_next_region = std::next(it_curr_region);
        for (; it_next_region != perimeter_regions.end() &&
               has_compatible_perimeter_regions(it_next_region->region->config(), it_curr_region->region->config());
             ++it_next_region)
        {
            Slic3r::append(current_merge.expolygons, std::move(it_next_region->expolygons));
            current_merge.bbox.merge(it_next_region->bbox);
        }

        if (std::distance(it_curr_region, it_next_region) > 1)
        {
            current_merge.expolygons = union_ex(current_merge.expolygons);
        }

        perimeter_regions_merged.emplace_back(std::move(current_merge));
        it_curr_region = it_next_region;
    }

    perimeter_regions = perimeter_regions_merged;
}

} // namespace Slic3r

// Athena maintains the fixed-width behavior (current repo behavior)
// while Arachne will be modified to provide true variable-width perimeters

namespace Slic3r
{

// True when either perimeter or interlock debug output is requested; gates the
// computation in the debug helpers below. Each dbg_log call still enforces its
// exact category, so --debug interlock yields only [INTERLOCK] lines and
// --debug perimeters only [PERIM]; [WKT] geometry dumps appear under either.
static inline bool pg_dbg_active()
{
    return Slic3r::debug_enabled(Slic3r::DBG_PERIMETERS | Slic3r::DBG_INTERLOCK);
}

// ===================== PERIMETER DEBUG HELPERS =====================
static void dbg_perim_contours(const char *phase, double z, int layer_id, const ExPolygons &contours, const char *label)
{
    if (!pg_dbg_active() || contours.empty())
        return;
    double total_area = 0;
    for (const ExPolygon &ep : contours)
        total_area += std::abs(ep.area()) * 1e-12;
    BoundingBox bb = get_extents(contours);
    dbg_log(Slic3r::DBG_PERIMETERS, z, "PERIM", "%s %s ep=%zu area=%8.4fmm2 bbox=(%.2f,%.2f)-(%.2f,%.2f)", phase, label,
            contours.size(), total_area, unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()),
            unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()));
    for (size_t i = 0; i < contours.size(); i++)
    {
        const ExPolygon &ep = contours[i];
        double a = std::abs(ep.area()) * 1e-12;
        BoundingBox epbb = get_extents(ep);
        dbg_log(Slic3r::DBG_PERIMETERS, z, "PERIM",
                "  %s %s [%zu] area=%8.4fmm2 holes=%zu pts=%zu "
                "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                phase, label, i, a, ep.holes.size(), ep.contour.points.size(), unscaled<double>(epbb.min.x()),
                unscaled<double>(epbb.min.y()), unscaled<double>(epbb.max.x()), unscaled<double>(epbb.max.y()));
    }
}

static void dbg_perim_loops(double z, int layer_id, const Athena::Perimeters &perimeters, coord_t ext_perimeter_width,
                            coord_t perimeter_width)
{
    if (!pg_dbg_active())
        return;
    int total_loops = 0;
    for (const auto &perim_set : perimeters)
    {
        for (const auto &el : perim_set)
        {
            if (el.junctions.empty())
                continue;
            total_loops++;
            // Compute bounding box from junctions
            Point pmin = el.junctions.front().p, pmax = pmin;
            coord_t min_w = el.junctions.front().w, max_w = min_w;
            for (const auto &j : el.junctions)
            {
                pmin.x() = std::min(pmin.x(), j.p.x());
                pmin.y() = std::min(pmin.y(), j.p.y());
                pmax.x() = std::max(pmax.x(), j.p.x());
                pmax.y() = std::max(pmax.y(), j.p.y());
                min_w = std::min(min_w, j.w);
                max_w = std::max(max_w, j.w);
            }
            dbg_log(Slic3r::DBG_PERIMETERS, z, "PERIM",
                    "LOOP inset=%zu closed=%d pts=%zu w=%.4f-%.4fmm "
                    "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                    el.inset_idx, (int) el.is_closed, el.junctions.size(), unscaled<double>(min_w),
                    unscaled<double>(max_w), unscaled<double>(pmin.x()), unscaled<double>(pmin.y()),
                    unscaled<double>(pmax.x()), unscaled<double>(pmax.y()));
        }
    }
    dbg_log(Slic3r::DBG_PERIMETERS, z, "PERIM", "LOOPS_TOTAL: %d loops, ext_w=%.4fmm perim_w=%.4fmm", total_loops,
            unscaled<double>(ext_perimeter_width), unscaled<double>(perimeter_width));
}

static void dbg_perim_overlap(double z, int layer_id, int loop_number, coord_t spacing, coord_t inset_before,
                              coord_t inset_after, coord_t min_perim_infill_spacing)
{
    if (!pg_dbg_active())
        return;
    dbg_log(Slic3r::DBG_PERIMETERS, z, "PERIM",
            "OVERLAP loops=%d spacing=%.4fmm inset_base=%.4fmm "
            "overlap=%.4fmm min_perim_infill_spacing=%.4fmm",
            loop_number + 1, unscaled<double>(spacing), unscaled<double>(inset_before), unscaled<double>(inset_after),
            unscaled<double>(min_perim_infill_spacing));
}
// ===================== INTERLOCK DEBUG HELPERS =====================
static void dbg_il_regions(double z, const char *phase, const ExPolygons &regions, const char *label)
{
    if (!pg_dbg_active())
        return;
    double total_area = 0;
    for (const ExPolygon &ep : regions)
        total_area += std::abs(ep.area()) * 1e-12;
    if (regions.empty())
    {
        dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK", "%s %s EMPTY", phase, label);
        return;
    }
    BoundingBox bb = get_extents(regions);
    dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK", "%s %s ep=%zu area=%8.4fmm2 bbox=(%.2f,%.2f)-(%.2f,%.2f)", phase,
            label, regions.size(), total_area, unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()),
            unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()));
    for (size_t i = 0; i < regions.size(); i++)
    {
        const ExPolygon &ep = regions[i];
        double a = std::abs(ep.area()) * 1e-12;
        BoundingBox epbb = get_extents(ep);
        dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK",
                "  %s %s [%zu] area=%8.4fmm2 holes=%zu pts=%zu "
                "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                phase, label, i, a, ep.holes.size(), ep.contour.points.size(), unscaled<double>(epbb.min.x()),
                unscaled<double>(epbb.min.y()), unscaled<double>(epbb.max.x()), unscaled<double>(epbb.max.y()));
    }
}

static void dbg_il_params(double z, int layer_id, bool is_odd, int actual_shells, int requested_shells,
                          coord_t perimeter_width, coord_t base_w, coord_t main_w, coord_t boundary_w,
                          coord_t il_external, coord_t il_internal, coord_t il_innermost, coord_t overlap_amount,
                          coord_t perim_to_il_overlap)
{
    if (!pg_dbg_active())
        return;
    dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK",
            "PARAMS layer=%d odd=%d shells=%d/%d perim_w=%.4fmm "
            "base_w=%.4f main_w=%.4f boundary_w=%.4f il_ext=%.4f il_int=%.4f il_inner=%.4f "
            "overlap=%.4f p2il_overlap=%.4fmm",
            layer_id, (int) is_odd, actual_shells, requested_shells, unscaled<double>(perimeter_width),
            unscaled<double>(base_w), unscaled<double>(main_w), unscaled<double>(boundary_w),
            unscaled<double>(il_external), unscaled<double>(il_internal), unscaled<double>(il_innermost),
            unscaled<double>(overlap_amount), unscaled<double>(perim_to_il_overlap));
}

// Geometry debug (--debug perimeters/interlock): emit ExPolygons/polylines as WKT in mm, so a layer can be pasted into
// shapely/QGIS/PostGIS (shapely.wkt.loads) for inspection. Generic - not interlocking-specific.
static void dbg_wkt_expolys(double z, const char *label, const ExPolygons &eps)
{
    if (!pg_dbg_active() || eps.empty())
        return;
    auto pt = [](const Point &p)
    {
        return std::to_string(unscaled<double>(p.x())) + " " + std::to_string(unscaled<double>(p.y()));
    };
    auto ring = [&](const Polygon &poly)
    {
        std::string r = "(";
        for (size_t k = 0; k < poly.points.size(); ++k)
            r += (k ? "," : "") + pt(poly.points[k]);
        if (!poly.points.empty())
            r += "," + pt(poly.points.front()); // close the ring
        return r + ")";
    };
    std::string s = "MULTIPOLYGON(";
    for (size_t i = 0; i < eps.size(); ++i)
    {
        s += (i ? ",(" : "(") + ring(eps[i].contour);
        for (const Polygon &h : eps[i].holes)
            s += "," + ring(h);
        s += ")";
    }
    s += ")";
    dbg_log(Slic3r::DBG_PERIMETERS | Slic3r::DBG_INTERLOCK, z, "WKT", "%s %s", label, s.c_str());
}

static void dbg_wkt_polyline(double z, const char *label, size_t inset, bool odd,
                             const std::vector<Athena::ExtrusionJunction> &js)
{
    if (!pg_dbg_active() || js.size() < 2)
        return;
    std::string s = "LINESTRING(";
    for (size_t k = 0; k < js.size(); ++k)
        s += (k ? "," : "") + std::to_string(unscaled<double>(js[k].p.x())) + " " +
             std::to_string(unscaled<double>(js[k].p.y()));
    s += ")";
    dbg_log(Slic3r::DBG_PERIMETERS | Slic3r::DBG_INTERLOCK, z, "WKT", "%s inset=%zu odd=%d %s", label, inset, (int) odd,
            s.c_str());
}

static void dbg_il_athena_shells(double z, const char *label, const std::vector<Athena::VariableWidthLines> &il_paths,
                                 int shells)
{
    if (!pg_dbg_active())
        return;
    for (size_t inset_idx = 0; inset_idx < il_paths.size() && inset_idx < size_t(shells); ++inset_idx)
    {
        int closed_count = 0, open_count = 0, odd_count = 0, small_count = 0, empty_count = 0;
        for (const Athena::ExtrusionLine &line : il_paths[inset_idx])
        {
            if (line.empty() || line.size() < 2)
            {
                empty_count++;
                continue;
            }
            if (line.is_odd)
                odd_count++;
            if (!line.is_closed)
                open_count++;
            else
                closed_count++;
            Polygon poly;
            for (const Athena::ExtrusionJunction &j : line.junctions)
                poly.points.push_back(j.p);
            if (poly.size() < 3)
                small_count++;
        }
        dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK",
                "ATHENA_SHELL %s inset=%zu lines=%zu closed=%d open=%d odd=%d "
                "small=%d empty=%d",
                label, inset_idx, il_paths[inset_idx].size(), closed_count, open_count, odd_count, small_count,
                empty_count);
    }
}

static void dbg_il_collect_result(double z, const char *label, const ExtrusionEntityCollection &coll)
{
    if (!pg_dbg_active())
        return;
    int loops = 0, paths = 0;
    double total_len = 0;
    for (const ExtrusionEntity *ee : coll.entities)
    {
        if (dynamic_cast<const ExtrusionLoop *>(ee))
            loops++;
        else if (auto *pp = dynamic_cast<const ExtrusionPath *>(ee))
        {
            paths++;
            total_len += unscaled<double>(pp->polyline.length());
        }
    }
    dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK", "COLLECTED %s entities=%zu loops=%d paths=%d path_len=%.2fmm", label,
            coll.entities.size(), loops, paths, total_len);
}

static void dbg_il_inner_contour(double z, const Polygons &athena_inner, const Polygons &geometric_inner,
                                 const ExPolygons &final_contour)
{
    if (!pg_dbg_active())
        return;
    double athena_area = 0, geo_area = 0, final_area = 0;
    for (const Polygon &p : athena_inner)
        athena_area += std::abs(p.area()) * 1e-12;
    for (const Polygon &p : geometric_inner)
        geo_area += std::abs(p.area()) * 1e-12;
    for (const ExPolygon &ep : final_contour)
        final_area += std::abs(ep.area()) * 1e-12;
    dbg_log(Slic3r::DBG_INTERLOCK, z, "INTERLOCK",
            "INNER_CONTOUR athena_polys=%zu area=%.4fmm2 "
            "geo_polys=%zu area=%.4fmm2 final_ep=%zu area=%.4fmm2",
            athena_inner.size(), athena_area, geometric_inner.size(), geo_area, final_contour.size(), final_area);
}
// ===================== END PERIMETER DEBUG HELPERS =====================

// ===================== END INTERLOCK DEBUG HELPERS =====================

// preFlight: portions of `probe` that lose coverage within m_top layers above
// or m_bot layers below (i.e. are visible from outside the solid range).
// Layer::id() is raft-offset, so the index into the object's layer vector is
// corrected before walking.
//
// min_exposure > 0 drops each PER-LAYER exposure step narrower than twice
// that radius before accumulating: a slope retreats by one thin ring per
// layer while a true flat face appears as one wide step, so this separates
// faces (kept) from slopes, chamfers, thread flanks and noise (dropped).
static ExPolygons visible_within_solid_range(const Layer *layer, const ExPolygons &probe, int m_top, int m_bot,
                                             float min_exposure = 0.f)
{
    auto significant = [min_exposure](ExPolygons &&exposed) -> ExPolygons
    {
        return min_exposure > 0.f ? opening_ex(exposed, min_exposure) : std::move(exposed);
    };

    ExPolygons vis;
    const auto &all_layers = layer->object()->layers();
    const size_t raft = layer->object()->slicing_parameters().raft_layers();
    const size_t cur_idx = size_t(layer->id()) >= raft ? size_t(layer->id()) - raft : 0;
    if (m_top > 0)
    {
        ExPolygons covered = probe;
        for (int k = 1; k <= m_top && !covered.empty() && (cur_idx + k) < all_layers.size(); ++k)
        {
            append(vis, significant(diff_ex(covered, all_layers[cur_idx + k]->lslices)));
            covered = intersection_ex(covered, all_layers[cur_idx + k]->lslices);
        }
        // Ran out of layers = top of object, everything remaining is visible
        if (!covered.empty() && (cur_idx + m_top) >= all_layers.size())
            append(vis, significant(std::move(covered)));
    }
    if (m_bot > 0)
    {
        ExPolygons covered = probe;
        for (int k = 1; k <= m_bot && !covered.empty() && cur_idx >= static_cast<size_t>(k); ++k)
        {
            append(vis, significant(diff_ex(covered, all_layers[cur_idx - k]->lslices)));
            covered = intersection_ex(covered, all_layers[cur_idx - k]->lslices);
        }
        // Ran out of layers = bottom of object
        if (!covered.empty() && cur_idx < static_cast<size_t>(m_bot))
            append(vis, significant(std::move(covered)));
    }
    return vis;
}

void PerimeterGenerator::process_athena(
    // Inputs:
    const Parameters &params, const Surface &surface, const ExPolygons *lower_slices, const ExPolygons *upper_slices,
    // Cache:
    Polygons &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection &out_loops,
    // Gaps without the thin walls (not implemented for Athena - matches Arachne behavior)
    ExtrusionEntityCollection & /* out_gap_fill */,
    // Infills without the gap fills
    ExPolygons &out_fill_expolygons)
{
    // Widths are fixed; spacing depends on effective perimeter count
    coord_t perimeter_width = params.perimeter_flow.scaled_width();
    coord_t ext_perimeter_width = params.ext_perimeter_flow.scaled_width();
    coord_t solid_infill_spacing = params.solid_infill_flow.scaled_spacing();

    // Detect how many perimeters must be generated for this island
    int loop_number = params.config.perimeters + surface.extra_perimeters - 1; // 0-indexed loops

    // "Perimeters while Interlocking" (per-region, additive). Generate the REDUCED wall count from the
    // island boundary up front, so spacing/overlap are the reduced-count values - byte-identical to plain
    // perimeters=<reduced>. The buried core then gets interlocking on the clean leftover; the extra
    // (full - reduced) walls are added back ONLY where interlocking is suppressed (the additive Athena
    // refill below), so interlocking in one part of an island never reduces the walls in another part.
    const int il_regular_override = params.config.interlock_regular_perimeters.value;
    const bool il_feature_active = il_regular_override > 0 && il_regular_override < params.config.perimeters.value &&
                                   params.config.interlock_perimeters_enabled && !params.config.serpentine_enabled &&
                                   !params.spiral_vase && params.layer != nullptr;
    // Only reduce a surface that will actually get some interlocking; a fully-solid surface (e.g. a
    // top/bottom layer) keeps its full walls in a single pass and is never under-walled. The additive block
    // below restores full walls in the suppressed sub-regions of a partially-buried surface.
    bool il_reduce_walls = false;
    if (il_feature_active)
    {
        const ExPolygons probe{surface.expolygon};
        const ExPolygons vis = visible_within_solid_range(params.layer, probe,
                                                          params.config.interlock_solid_layers_top.value,
                                                          params.config.interlock_solid_layers_bottom.value);
        bool has_il = vis.empty();
        if (!has_il)
        {
            double remaining = 0;
            for (const ExPolygon &ep : diff_ex(probe, union_ex(vis)))
                remaining += std::abs(ep.area());
            has_il = remaining > double(perimeter_width) * double(perimeter_width);
        }
        il_reduce_walls = has_il;
    }
    if (il_reduce_walls)
        loop_number = il_regular_override - 1;

    // Compute spacing using the effective (reduced when PWI-active) perimeter count.
    int effective_perims = il_reduce_walls ? il_regular_override : params.config.perimeters.value;
    coord_t perimeter_spacing = preFlight::PreciseWalls::calculate_perimeter_spacing(
        params.perimeter_flow,
        preFlight::PreciseWalls::get_effective_perimeter_overlap(params.config.perimeter_perimeter_overlap,
                                                                 effective_perims));
    coord_t ext_perimeter_spacing = preFlight::PreciseWalls::calculate_perimeter_spacing(
        params.ext_perimeter_flow,
        preFlight::PreciseWalls::get_effective_perimeter_overlap(params.config.perimeter_perimeter_overlap,
                                                                 effective_perims));
    coord_t ext_perimeter_spacing2 = preFlight::PreciseWalls::calculate_external_spacing(
        params.ext_perimeter_flow, params.perimeter_flow,
        preFlight::PreciseWalls::get_effective_external_overlap(params.config.external_perimeter_overlap,
                                                                effective_perims));

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty())
    {
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder - 1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter / 2)));
    }

    Polygons lower_slices_raw;
    if (lower_slices != nullptr)
    {
        lower_slices_raw = to_polygons(*lower_slices);
    }

    if (loop_number > 0 &&
        ((params.config.top_one_perimeter_type == TopOnePerimeterType::TopmostOnly && upper_slices == nullptr) ||
         (params.config.only_one_perimeter_first_layer && params.layer_id == 0)))
        loop_number = 0;

    // Calculate how many inner loops remain when TopSurfaces is selected.
    const int inner_loop_number = (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces &&
                                   upper_slices != nullptr)
                                      ? loop_number - 1
                                      : -1;

    // Set one perimeter when TopSurfaces is selected.
    if (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces)
        loop_number = 0;

    ExPolygons last = offset_ex(surface.expolygon.simplify_p(params.scaled_resolution),
                                -float(ext_perimeter_width / 2. - ext_perimeter_spacing / 2.));

    Polygons last_p = to_polygons(last);

    // Perimeter compression allows narrower beads where loops converge:
    //   Minimal = 75%, Moderate = 50%, Aggressive = 25% of bead width
    //   Floor = 33% of nozzle diameter (nozzle/3) for printability
    double min_bead_width_factor = 0.25;
    switch (params.config.perimeter_compression.value)
    {
    case PerimeterCompression::pcMinimal:
        min_bead_width_factor = 0.75;
        break;
    case PerimeterCompression::pcModerate:
        min_bead_width_factor = 0.50;
        break;
    case PerimeterCompression::pcAggressive:
        min_bead_width_factor = 0.25;
        break;
    default:
        min_bead_width_factor = 0.25;
        break;
    }

    // Resolve maximum perimeter width (always % of nozzle diameter)
    coord_t max_perimeter_width = 0;
    {
        double nozzle_diam = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder.value - 1);
        double mpw_pct = params.config.max_perimeter_width.value;
        if (mpw_pct > 0)
            max_perimeter_width = scaled<coord_t>(nozzle_diam * mpw_pct * 0.01);
    }

    // preFlight: Convert thin wall precision enum to nanometer snap grid value
    coord_t tw_snap = 10000; // default 0.01mm
    switch (params.config.thin_wall_precision.value)
    {
    case twp001:
        tw_snap = 1000;
        break;
    case twp005:
        tw_snap = 5000;
        break;
    case twp01:
        tw_snap = 10000;
        break;
    case twp05:
        tw_snap = 50000;
        break;
    case twp1:
        tw_snap = 100000;
        break;
    }

    // Serpentine depth-limit hand-off: the band was emitted and the Athena block
    // below owns the interior with exactly one wall plus normal infill. Gates the
    // TopSurfaces and interlocking machinery, which assume Athena owns the island.
    bool serp_band_handoff = false;
    // The band's own no_sort collection, captured on hand-off so the inner Athena
    // wall can be spliced to its front (the inner beads print before the band bead).
    ExtrusionEntityCollection *serp_band_coll = nullptr;
    // Perpendicular depth past which the depth-limit fill may live, so the fill
    // overlaps the band even where the wall was too thin for an Athena bead.
    // Currently always 0 (the depth hand-off bounds the fill at the caps and no
    // path sets it non-zero), so the serp_fill_clip > 0 consumer never fires.
    coord_t serp_fill_clip = 0;
    // The flow the smooth wall renders with: the serpentine bead's flow, so the
    // wall matches the serpentine width (sp_params.flow already inherits the
    // external perimeter flow when serpentine_extrusion_width is 0). External by
    // default for non-serpentine islands.
    Flow serp_wall_flow = params.ext_perimeter_flow;

    // Serpentine fill: replaces both perimeters and infill with a single
    // continuous extrusion.
    if (params.config.serpentine_enabled && !params.spiral_vase)
    {
        const double sp_nozzle = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder.value - 1);
        // First-layer width overrides the explicit serpentine width, matching
        // every other extrusion width in the pipeline; both honor the
        // percent-of-nozzle interpretation flag.
        Flow sp_flow = params.ext_perimeter_flow;
        if (params.layer_id == 0 && params.print_config.first_layer_extrusion_width.value > 0)
            sp_flow = Flow::new_from_config_width(frExternalPerimeter, params.print_config.first_layer_extrusion_width,
                                                  (float) sp_nozzle, (float) params.layer_height,
                                                  params.object_config.extrusion_width_percent_of_nozzle.value);
        else if (params.config.serpentine_extrusion_width.value > 0)
            sp_flow = Flow::new_from_config_width(frExternalPerimeter, params.config.serpentine_extrusion_width,
                                                  (float) sp_nozzle, (float) params.layer_height,
                                                  params.object_config.extrusion_width_percent_of_nozzle.value);

        Serpentine::Params sp_params;
        sp_params.bead_width = scaled<coord_t>((double) sp_flow.width());
        // Overlap shares PreciseWalls semantics: percent values are percent
        // of layer height doubled internally, absolute values are mm. Clamped
        // here once so the generator and the fill inset arithmetic below
        // always agree on the effective value.
        const auto &ov_opt = params.config.serpentine_overlap;
        const double bw_mm = (double) sp_flow.width();
        double ov_mm = ov_opt.percent ? (2.0 * ov_opt.value * 0.01 * params.layer_height) : ov_opt.value;
        ov_mm = std::clamp(ov_mm, -3.0 * bw_mm, 0.45 * bw_mm);
        sp_params.overlap = scaled<coord_t>(ov_mm);
        sp_params.max_bead = scaled<coord_t>(sp_nozzle * params.config.serpentine_max_bead.value * 0.01);
        sp_params.phase_mode = int(params.config.serpentine_ridges.value);
        sp_params.aim = int(params.config.serpentine_aim.value);
        sp_params.flow = sp_flow;
        sp_params.layer_id = params.layer_id;
        sp_params.print_z = (params.layer != nullptr) ? params.layer->print_z : 0.0;

        // Serpentine's boundary walk rides the contour with its bead centerline, so
        // generating on the raw slice leaves the bead's outer edge a half-bead proud
        // and the part prints ~one bead oversize. Inset the outer contour a half-bead,
        // like a normal external perimeter, so the outer bead edge lands on the slice
        // contour, but keep the holes at the true boundary: a whole-island inset also
        // grows every hole, and ring-mode teeth form the bore surface with no boundary
        // bead, so a grown hole leaves the inside a bead short of the real bore.
        // Intersecting the slice (true holes) with the inset outer disc insets only the
        // contour. Every serpentine branch derives its geometry from this.
        // The inset can erase a sub-bead sliver (empty) or split a necked island
        // (size > 1); both fall back to the raw slice. Empty usually fails
        // generate()'s min-dimension gate (Athena then prints it at the right size),
        // but a large-bbox diagonal sliver could clear the gate and print ~half a bead
        // oversize; size > 1 prints serpentine on the raw slice ~half a bead oversize
        // per side. Log both so neither ships silently.
        ExPolygons serp_in = intersection_ex(ExPolygons{surface.expolygon},
                                             offset_ex(ExPolygon(surface.expolygon.contour),
                                                       -0.5f * float(sp_params.bead_width)));
        if (serp_in.size() > 1)
            dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                    "serpentine half-bead inset split the island into %zu pieces (sub-bead neck or hole "
                    "near the edge); using the raw slice, so this island prints ~half a bead oversize",
                    serp_in.size());
        else if (serp_in.empty())
            dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                    "serpentine half-bead inset erased the island (sub-bead sliver); using the raw slice - "
                    "prints ~half a bead oversize if it clears generate()'s min-dimension gate");
        const ExPolygon &serp_island = (serp_in.size() == 1) ? serp_in.front() : surface.expolygon;

        // Solid surfaces: portions of this island within the top/bottom solid
        // range print as ordinary solid infill. Slice-to-slice noise produces
        // hairline slivers in the visibility diffs; the opening drops them so
        // only real exposure (at least a bead wide) triggers shell mode.
        // Only true faces flip a layer into shell mode. The lslices walk
        // proposes candidate exposure regions (the per-step opening already
        // drops the thin rings slopes and noise contribute layer by layer);
        // each candidate must then confirm through the layer visibility API:
        // interior sample discs genuinely visible from above or below within
        // the solid range. Slopes, chamfers and thread flanks fail (their
        // disc neighbors are covered beyond the range, or they are too thin
        // to seat a sample); flat faces pass.
        ExPolygons vis;
        if (!params.config.serpentine_limit_depth && params.config.serpentine_solid_surfaces && params.layer != nullptr)
        {
            // Serpentine needs two solid layers per face (the face layer and
            // the one beneath/above it); the maze provides the rest of the
            // shell. The configured counts act as on/off here (minimum shell
            // thickness has nothing to act on, since covered layers hand the
            // fill machinery no regions at all). The confirmation below keeps
            // the second layer on the same confirmed face footprint.
            const int m_top = params.config.top_solid_layers.value > 0 ? 2 : 0;
            const int m_bot = params.config.bottom_solid_layers.value > 0 ? 2 : 0;
            const float step_open = 1.5f * float(sp_params.bead_width);

            auto face_confirmed = [&](const ExPolygon &cand, bool from_top, int range) -> bool
            {
                if (range <= 0)
                    return false;
                const double inward_d = scale_(1.5);
                const coord_t disc_r = coord_t(scale_(1.0));
                static const double ct[8] = {1., .707, 0., -.707, -1., -.707, 0., .707};
                static const double st[8] = {0., .707, 1., .707, 0., -.707, -1., -.707};
                auto disc_visible = [&](const Point &s) -> bool
                {
                    for (int j = 0; j < 8; ++j)
                    {
                        Point p(s.x() + coord_t(ct[j] * disc_r), s.y() + coord_t(st[j] * disc_r));
                        // points off the island do not count against the disc
                        if (!surface.expolygon.contains(p))
                            continue;
                        int k = from_top ? params.layer->layers_until_visible_from_top(p, range)
                                         : params.layer->layers_until_visible_from_bottom(p, range);
                        if (k > range)
                            return false;
                    }
                    return true;
                };
                // Interior samples: the centroid plus contour quarter points
                // pushed inward. A candidate too thin to seat any sample is
                // not a face.
                size_t n_samples = 0;
                Point centroid = cand.contour.centroid();
                if (cand.contains(centroid))
                {
                    ++n_samples;
                    if (!disc_visible(centroid))
                        return false;
                }
                const Polygon &c = cand.contour;
                for (int q = 0; q < 4 && c.points.size() >= 3; ++q)
                {
                    size_t i = c.points.size() * q / 4;
                    const Point &a = c.points[i];
                    const Point &b = c.points[(i + 1) % c.points.size()];
                    Vec2d t = (b - a).cast<double>();
                    double len = t.norm();
                    if (len < SCALED_EPSILON)
                        continue;
                    t /= len;
                    Point s = a + Point(coord_t(-t.y() * inward_d), coord_t(t.x() * inward_d));
                    if (!cand.contains(s))
                        continue;
                    ++n_samples;
                    if (!disc_visible(s))
                        return false;
                }
                return n_samples > 0;
            };

            ExPolygons cand_top = visible_within_solid_range(params.layer, ExPolygons{surface.expolygon}, m_top, 0,
                                                             step_open);
            ExPolygons cand_bot = visible_within_solid_range(params.layer, ExPolygons{surface.expolygon}, 0, m_bot,
                                                             step_open);
            for (ExPolygon &cand : cand_top)
                if (face_confirmed(cand, true, m_top))
                    vis.push_back(std::move(cand));
            for (ExPolygon &cand : cand_bot)
                if (face_confirmed(cand, false, m_bot))
                    vis.push_back(std::move(cand));
        }

        // A continuous inner perimeter sits one bead inside the band caps, bonded
        // to them by the configured serpentine overlap, so the inner bead and the
        // band weld by the same amount the user set for the pattern itself.
        // Prepended into the band's own per-island collection, it prints first,
        // anchors to the layer below, and gives the band caps (outside) and the
        // fill (inside) one continuous edge to fuse to all the way around, which
        // rectilinear fill cannot. The fill inset is moved in by one bead (less
        // the bond) to make room for it.
        const coord_t serp_inner_bond = sp_params.overlap;
        auto prepend_inner_perimeter = [&](const ExPolygons &perim_region)
        {
            if (out_loops.entities.empty())
                return;
            auto *band_coll = dynamic_cast<ExtrusionEntityCollection *>(out_loops.entities.back());
            if (band_coll == nullptr)
                return;
            const ExtrusionFlow pflow(sp_params.flow.mm3_per_mm(), sp_params.flow.width(), sp_params.flow.height());
            std::vector<ExtrusionEntity *> perims;
            auto make_perim = [&](const Polygon &poly)
            {
                if (poly.points.size() < 3)
                    return;
                ExtrusionPath p(ExtrusionAttributes(ExtrusionRole::Serpentine, pflow));
                for (const Point &pt : poly.points)
                    p.polyline.append(pt);
                p.polyline.append(p.polyline.first_point());
                ExtrusionPaths ps;
                ps.push_back(std::move(p));
                perims.push_back(new ExtrusionLoop(std::move(ps)));
            };
            for (const ExPolygon &ep : perim_region)
            {
                make_perim(ep.contour);
                for (const Polygon &h : ep.holes)
                    make_perim(h);
            }
            if (!perims.empty())
                band_coll->entities.insert(band_coll->entities.begin(), perims.begin(), perims.end());
        };

        // Serpentine overhang detection: split each serpentine path into supported
        // (Serpentine) and overhanging (SerpentineOverhang) spans against the lower
        // slices grown by half a nozzle (the same support envelope the normal
        // perimeter overhang pass uses). A centerline outside that envelope is an
        // overhang. Re-tags the role only; the serpentine flow is kept, so the
        // overhang spans take the overhang speed/fan/color without reflowing.
        const bool serp_detect_overhang = params.config.overhangs &&
                                          params.layer_id > params.object_config.raft_layers &&
                                          !lower_slices_polygons_cache.empty();
        const ExPolygons serp_supported = serp_detect_overhang ? union_ex(lower_slices_polygons_cache) : ExPolygons{};
        auto tag_serpentine_overhangs = [&](ExtrusionEntity *band_entity)
        {
            if (!serp_detect_overhang || band_entity == nullptr)
                return;
            auto *coll = dynamic_cast<ExtrusionEntityCollection *>(band_entity);
            if (coll == nullptr)
                return;
            const double step = double(scale_(0.5)); // overhang split granularity
            auto supported_at = [&](const Point &p)
            {
                for (const ExPolygon &ex : serp_supported)
                    if (ex.contains(p))
                        return true;
                return false;
            };
            auto split_paths = [&](ExtrusionPaths &paths)
            {
                ExtrusionPaths out;
                out.reserve(paths.size());
                for (ExtrusionPath &src : paths)
                {
                    if (!src.role().is_serpentine() || src.polyline.size() < 2)
                    {
                        out.push_back(std::move(src));
                        continue;
                    }
                    const ExtrusionAttributes base_attr = src.attributes();
                    ExtrusionAttributes over_attr = base_attr;
                    over_attr.role = ExtrusionRole::SerpentineOverhang;
                    // Subdivide every segment, classify each step by its midpoint,
                    // then group consecutive same-class steps into sub-paths. The
                    // sub-paths share endpoints, so the tour stays continuous.
                    const Points &pts = src.polyline.points;
                    std::vector<Point> sp;
                    std::vector<char> seg_over;
                    sp.push_back(pts.front());
                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                    {
                        const Vec2d a = pts[i].cast<double>();
                        const Vec2d d = pts[i + 1].cast<double>() - a;
                        const double len = d.norm();
                        const int steps = std::max(1, int(std::ceil(len / step)));
                        for (int k = 1; k <= steps; ++k)
                        {
                            sp.push_back((a + d * (double(k) / steps)).cast<coord_t>());
                            const Point mid = (a + d * ((double(k) - 0.5) / steps)).cast<coord_t>();
                            seg_over.push_back(supported_at(mid) ? char(0) : char(1));
                        }
                    }
                    // Fast path: a fully supported path keeps its original
                    // geometry (Serpentine); a fully overhanging one only flips
                    // role. Only genuinely mixed paths are subdivided.
                    bool mixed = false;
                    for (char c : seg_over)
                        if (c != seg_over.front())
                        {
                            mixed = true;
                            break;
                        }
                    if (seg_over.empty() || !mixed)
                    {
                        if (!seg_over.empty() && seg_over.front())
                        {
                            ExtrusionPath p(over_attr);
                            p.polyline = src.polyline;
                            out.push_back(std::move(p));
                        }
                        else
                            out.push_back(std::move(src));
                        continue;
                    }
                    size_t i = 0;
                    while (i < seg_over.size())
                    {
                        const char ov = seg_over[i];
                        size_t j = i;
                        while (j < seg_over.size() && seg_over[j] == ov)
                            ++j;
                        ExtrusionPath p(ov ? over_attr : base_attr);
                        for (size_t k = i; k <= j; ++k)
                            p.polyline.append(sp[k]);
                        out.push_back(std::move(p));
                        i = j;
                    }
                }
                paths = std::move(out);
            };
            for (ExtrusionEntity *e : coll->entities)
            {
                if (auto *mp = dynamic_cast<ExtrusionMultiPath *>(e))
                    split_paths(mp->paths);
                else if (auto *lp = dynamic_cast<ExtrusionLoop *>(e))
                    split_paths(lp->paths);
            }
        };

        if (params.config.serpentine_limit_depth)
        {
            // Depth-limited mode: run the full serpentine pattern (anchor aim, ring
            // mode, no band corners) but stop every tooth at the configured depth,
            // so the result is the full fan clamped shallow. Athena then prints one
            // perimeter just inside the tooth tips and normal infill beyond. band_clamp
            // stays 0 so generate() runs full mode; depth_clamp does the limiting. The
            // shallowest closable pattern is two beads, so smaller depths floor.
            Serpentine::Params band_params = sp_params;
            const coord_t depth_floor = coord_t(2.0 * sp_params.bead_width);
            const coord_t serp_depth = std::max(scaled<coord_t>(params.config.serpentine_depth.value), depth_floor);
            band_params.band_clamp = 0;
            band_params.depth_clamp = serp_depth;
            // The depth line each tooth set stops on: the region at least serp_depth
            // behind every boundary, so a tooth cap lands on the Athena bead's near edge
            // and adheres by the configured overlap without crossing into it. On an annulus
            // the contour's inward line and a bore's outward line cross once 2*serp_depth
            // exceeds the local wall; a single offset_ex of the holed island returns a
            // non-empty inverted mid-wall ring there, and each tooth set then conforms to
            // the opposite wall's line (outer teeth stop serp_depth from the bore, bore
            // teeth serp_depth from the contour) instead of meeting at the midplane. Build
            // it from per-boundary offsets and difference them so it empties cleanly when
            // the lines cross; with no depth line the rc/2 cap in generate() governs (each
            // tooth half the near wall) and both sets meet there.
            ExPolygon serp_contour_only;
            serp_contour_only.contour = serp_island.contour;
            ExPolygons serp_depth_region = offset_ex(serp_contour_only, -float(serp_depth));
            for (const Polygon &bore : serp_island.holes)
            {
                ExPolygon bore_solid;
                bore_solid.contour = bore;
                bore_solid.contour.make_counter_clockwise();
                serp_depth_region = diff_ex(serp_depth_region, offset_ex(bore_solid, float(serp_depth)));
            }
            band_params.fill_core = std::move(serp_depth_region);
            // On a thin or eccentric wall the two boundaries' depth lines cross and fill_core empties
            // there, so each tooth falls back to a per-ray midplane scalar; the two fans' midplanes do
            // not coincide on an off-centre wall and the anchor bead the teeth bond to pinches out (it
            // fragments into disconnected loops on the thin side). Floor fill_core with a continuous
            // mid-wall ribbon: where the wall is thick the ribbon sits inside fill_core and has no
            // effect; where it is thin the ribbon governs, the teeth conform to it and back their depth
            // off, so a band at least one Athena seat wide survives all the way around and the depth
            // varies to keep it. Empty for a solid island.
            size_t band_samples = 0, band_loops = 0, band_rejected = 0, band_pruned = 0;
            ExPolygons anchor_ribbon = Serpentine::wall_anchor_band(serp_island, sp_params.bead_width,
                                                                    sp_params.overlap, &band_samples, &band_loops,
                                                                    &band_rejected, &band_pruned);
            if (!anchor_ribbon.empty())
                band_params.fill_core = union_ex(band_params.fill_core, anchor_ribbon);
            // An empty band on a holed island (all holes rejected, or the wall pinched to nothing) means
            // the fill_core floor was lost; flag it. A solid island legitimately has no band, so the
            // warning is gated on holes to keep the tripwire meaningful. pruned counts the hole-to-hole
            // projections the bounding-box test skipped (0 on a 1-2 bore part; large on a many-bore part).
            dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                    "wall band: samples=%zu loops=%zu rejected=%zu pruned=%zu%s", band_samples, band_loops,
                    band_rejected, band_pruned,
                    (!serp_island.holes.empty() && anchor_ribbon.empty()) ? " (EMPTY - no floor)" : "");
            // generate() returns the interior band bounded by the smooth tooth-tip line; the holed
            // hand-off uses it so Athena never sees the inter-tooth gaps.
            ExPolygons serp_interior;
            if (band_params.fill_core.empty() && serp_island.holes.empty())
            {
                // Solid island shallower than the requested depth everywhere: no interior
                // beyond the depth to hand to Athena, and depth mode disables the center ring,
                // so the teeth fall back to the scalar clamp, converge, collide and fragment at
                // the center. Run full serpentine instead; its center ring / hub weave the
                // converging tips into the continuous tour. A small pin, or a depth set deeper
                // than the part, hits this. A holed island whose depth lines have crossed also
                // empties fill_core, but it must stay in depth mode (below): the rc/2 cap holds
                // each tooth at half the near wall and the Athena wall bonds both tooth sets.
                dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                        "serpentine island shallower than the %.2fmm depth; running full mode (no depth limit)",
                        unscale<double>(serp_depth));
                if (Serpentine::generate(serp_island, sp_params, out_loops))
                {
                    tag_serpentine_overhangs(out_loops.entities.back());
                    return;
                }
                // generate() failed: fall through to Athena below.
            }
            else if (Serpentine::generate(serp_island, band_params, out_loops, &serp_interior))
            {
                tag_serpentine_overhangs(out_loops.entities.back());
                // generate() appended exactly one no_sort collection for this island;
                // hold it so the Athena wall can be prepended into it below.
                serp_band_coll = out_loops.entities.empty()
                                     ? nullptr
                                     : dynamic_cast<ExtrusionEntityCollection *>(out_loops.entities.back());
                // The smooth wall takes the serpentine bead's width and overlap, not
                // the external perimeter's: override the external flow/width/spacing
                // that drive wall-0 (the only wall here). The spacing is the
                // serpentine bead spacing (width - overlap), so the wall overlaps its
                // serpentine neighbour by exactly the configured serpentine overlap.
                serp_wall_flow = sp_params.flow;
                ext_perimeter_width = sp_params.bead_width;
                ext_perimeter_spacing = std::max<coord_t>(sp_params.bead_width - sp_params.overlap, coord_t(1));
                // Build the region Athena lays its wall + infill on, from the interior the
                // teeth did not cover (bounded by the tooth cap line).
                Polygons cov;
                if (serp_band_coll != nullptr)
                    serp_band_coll->polygons_covered_by_width(cov, float(SCALED_EPSILON));
                ExPolygons core = diff_ex(serp_island, cov);
                ExPolygons opened;
                if (!serp_island.holes.empty())
                {
                    // Holed island (annulus, e.g. a nut): open the full core for a smooth gap-free
                    // centre, then intersect with fill_core (the mid-wall conform band) so the
                    // inter-tooth fingers (all nearer the boundary than the band) are clipped out. The
                    // open runs on the wide core so it never pinches the centre apart; the band only
                    // masks the fingers, it never bounds the smooth centre.
                    ExPolygons core_open = opening_ex(core, 0.5f * float(sp_params.bead_width));
                    // Mask the inter-tooth fingers with fill_core (the mid-wall conform region the
                    // teeth stopped on), not the tip-disc serp_interior: on an eccentric wall the
                    // contour and bore tip discs cross and their difference slivers, fragmenting the
                    // bead. fill_core is continuous by construction (the mid-wall ribbon floors it) and
                    // already sits mid-wall, so the masked seat comes back one continuous loop.
                    opened = intersection_ex(core_open, band_params.fill_core);
                    if (opened.empty() && !core_open.empty())
                    {
                        // fill_core masked the interior away (a degenerate core) while the opened core
                        // still holds a real central region. Seat the wall on the opened core rather
                        // than leave an empty interior, which prints as a void; logged.
                        opened = std::move(core_open);
                        dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                                "serpentine anchor mask empty; interior recovered from the opened core");
                    }
                    // Drop solid specks. Where the knurl floors a tooth the band mask pinches off a
                    // small island (a finger-mouth or floored-tooth bit); on a bottom or solid layer
                    // each would print as a stray solid patch. Keep only the anchor band (the one
                    // large loop the mid-wall ribbon floors continuous) and let areas below min_speck
                    // pass through unfilled. min_speck is the knob; it is tiny against the band, so
                    // the band itself is never dropped.
                    const double min_speck = 8.0 * double(sp_params.bead_width) * double(sp_params.bead_width);
                    size_t n_specks = 0;
                    if (opened.size() > 1)
                    {
                        // Keep the largest loop unconditionally so the drop can never hollow a band that
                        // fragmented (loops>1); drop only the genuine sub-min_speck pockets around it.
                        size_t best = 0;
                        for (size_t i = 1; i < opened.size(); ++i)
                            if (opened[i].area() > opened[best].area())
                                best = i;
                        ExPolygons keep;
                        keep.reserve(opened.size());
                        for (size_t i = 0; i < opened.size(); ++i)
                            if (i == best || opened[i].area() >= min_speck)
                                keep.push_back(std::move(opened[i]));
                            else
                                ++n_specks;
                        opened = std::move(keep);
                    }
                    if (opened.empty())
                        dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                                "serpentine holed interior empty (teeth met or band collapsed); no Athena wall");
                    dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                            "anchor seat: %zu loop(s) specks_dropped=%zu%s", opened.size(), n_specks,
                            anchor_ribbon.empty() ? "" : " (anchor band)");
                }
                else
                {
                    // Solid island: one tooth set, but core grows a pitch-wide finger out to
                    // every shallow ruler slit (a level-1 tooth a fraction of the depth) that the
                    // wall must not follow out to the surface. Separate by depth, not width, since a
                    // width-based open cannot tell them apart (fingers and the deep-cap gaps are both
                    // ~pitch wide): keep only core within cap_band of the depth line. A light 0.5*bw
                    // open then clears slivers without bridging the deep-cap gaps, so the wall conforms
                    // and bonds each convergent deep cap.
                    const float cap_band = 1.0f * float(sp_params.bead_width);
                    ExPolygons deep = intersection_ex(core, offset_ex(serp_island, -(float(serp_depth) - cap_band)));
                    opened = opening_ex(deep, 0.5f * float(sp_params.bead_width));
                    if (opened.empty() && !core.empty())
                    {
                        // Deep band collapsed (over-deep for this solid): fall back to the blanket
                        // 1.5*bw open (erases the fingers; no per-cap conform, but gap-free).
                        opened = opening_ex(core, 1.5f * float(sp_params.bead_width));
                        dbg_log(Slic3r::DBG_SERPENTINE, sp_params.print_z, "SRP",
                                "serpentine deep band too thin; wall uses the blanket smooth this island");
                    }
                }
                // Athena seats wall-0 a half serpentine-spacing inside last. The wall
                // always bonds the serpentine skin to the interior. Positive overlap is
                // honored exactly: grow last out by half the overlap so the wall lands one
                // full serpentine spacing from the caps, overlapping them by exactly the
                // overlap. In spread mode (negative overlap) the spacing inflates by the
                // gap, so the same overlap/2 grow would seat the wall one inflated spacing
                // out and leave the skin gapped from the interior by |overlap|. The
                // requested spread belongs in the infill behind the wall, not at the skin,
                // so the wall bonds at a small positive floor: grow = wall_bond - overlap/2
                // places the wall wall_bond inside the caps regardless of the spacing.
                const coord_t wall_bond = sp_params.overlap >= 0 ? sp_params.overlap
                                                                 : coord_t(0.15 * sp_params.bead_width);
                const float grow = float(wall_bond) - 0.5f * float(sp_params.overlap);
                last = offset_ex(opened.empty() && serp_island.holes.empty() ? core : opened, grow);
                // Solid-island fallback: an empty interior here would print as a void. For a holed
                // island an empty interior means the teeth met at the midplane (nothing to fill), and
                // falling back to the full core would re-admit the gaps the tip line excluded.
                if (serp_island.holes.empty() && last.empty() && !core.empty())
                    last = core;
                last_p = to_polygons(last);
                loop_number = 0;          // exactly one Athena wall behind the band
                serp_band_handoff = true; // gate TopSurfaces / interlocking below
                serp_fill_clip = 0;       // the opened core already bounds the fill at the caps
            }
            // generate() failed: fall through with the original last_p/loop_number, the
            // normal full-perimeter fallback.
        }
        else if (vis.empty())
        {
            // Fully covered (or solid surfaces off): serpentine everything.
            if (Serpentine::generate(serp_island, sp_params, out_loops))
            {
                tag_serpentine_overhangs(out_loops.entities.back());
                return;
            }
        }
        else
        {
            // The boundary keeps the zigzag band at its shortest depth, so
            // the side pattern stays continuous and the band caps give the
            // fill a clean edge to bond into. Behind the band, the visible
            // portions (grown by the standard anchoring margin) become fill
            // surfaces; the covered remainder continues as serpentine
            // sub-islands.
            Serpentine::Params band_params = sp_params;
            // One bead of tooth depth: the minimum that keeps the band a
            // continuous zigzag (emission cutoff 0.5 bw, crowd floor 0.6 bw),
            // and shallow enough that corner teeth floored by the crossing
            // neighbor still meet the fill corner within the bond.
            band_params.band_clamp = coord_t(1.0 * sp_params.bead_width);
            // The fill must always bond into the band caps, even in spread
            // mode (negative overlap): the roof anchors to the band, the
            // requested gaps live inside the pattern only.
            const coord_t fill_bond = std::max(sp_params.overlap, coord_t(0.15 * sp_params.bead_width));
            // A continuous inner perimeter sits one bead inside the band caps,
            // bonded to them at a fixed 10.73% overlap (independent of the user's
            // serpentine overlap, so the structural weld is always solid). It is
            // emitted first, so it anchors to the layer below far better than the
            // discrete teeth, and gives the band caps (outside) and the solid
            // fill (inside) one continuous edge to fuse to all the way around,
            // which rectilinear solid fill cannot. The fill then starts one bead
            // further in, inside this perimeter.
            const coord_t perim_depth = band_params.band_clamp + sp_params.bead_width - serp_inner_bond;
            const float inset = float(perim_depth + sp_params.bead_width / 2 - fill_bond);
            ExPolygons interior = offset_ex(serp_island, -inset);
            if (interior.empty())
            {
                // Too narrow to host band + perimeter + fill: the full pattern
                // covers such an island better than a hollow band would.
                if (Serpentine::generate(serp_island, sp_params, out_loops))
                {
                    tag_serpentine_overhangs(out_loops.entities.back());
                    return;
                }
            }
            else if (Serpentine::generate(serp_island, band_params, out_loops))
            {
                tag_serpentine_overhangs(out_loops.entities.back());
                // Inner perimeter only where the surface is genuinely visible.
                prepend_inner_perimeter(intersection_ex(vis, offset_ex(serp_island, -float(perim_depth))));
                {
                    // The solid zone is the visible area inside the perimeter;
                    // every part of it types solid downstream. It overlaps the
                    // inner perimeter by the standard infill/perimeters overlap
                    // (not the serpentine bond), so the solid face bonds to the
                    // smooth bead exactly like ordinary fill bonds to a perimeter.
                    // The maze claims the covered remainder.
                    const coord_t infill_ov = coord_t(
                        scale_(params.config.get_abs_value("infill_overlap", unscale<double>(ext_perimeter_spacing))));
                    ExPolygons solid_zone = intersection_ex(
                        vis, offset_ex(serp_island, -float(perim_depth + sp_params.bead_width / 2 - infill_ov)));
                    ExPolygons maze_interior = offset_ex(serp_island, -(inset + 0.5f * float(sp_params.bead_width)));
                    ExPolygons maze_zone = opening_ex(
                        diff_ex(maze_interior, offset_ex(solid_zone, float(sp_params.bead_width / 2 - fill_bond))),
                        float(sp_params.bead_width));
                    for (const ExPolygon &sub : maze_zone)
                        if (!Serpentine::generate(sub, sp_params, out_loops))
                            out_fill_expolygons.push_back(sub); // too small or hostile: plain fill takes it
                        else
                            tag_serpentine_overhangs(out_loops.entities.back());
                    append(out_fill_expolygons, std::move(solid_zone));
                    return;
                }
            }
        }
        // Fall through to Athena on failure.
    }

    Athena::WallToolPaths wall_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(loop_number + 1), 0,
                                          params.layer_height, params.object_config, params.print_config,
                                          ext_perimeter_width, perimeter_width, ext_perimeter_spacing2,
                                          perimeter_spacing, 0, params.layer_id, min_bead_width_factor, tw_snap,
                                          max_perimeter_width);
    wall_tool_paths.set_debug_print_z((params.layer != nullptr) ? params.layer->print_z : 0.0);
    Athena::Perimeters perimeters = wall_tool_paths.getToolPaths();
    // Arachne treats widths as "suggestions" and recalculates them. We enforce exact user values.
    // This fixes the core issue where extrusion widths vary from user settings (e.g., 0.5mm -> 0.499mm)
    preFlight::PreciseWalls::enforce_exact_widths(perimeters, ext_perimeter_width, perimeter_width, tw_snap);
    // Skeletal trapezoidation receives spacing and width separately, and inner_contour already
    // accounts for the actual bead widths, so no compensating offset is needed here.
    ExPolygons infill_contour = union_ex(wall_tool_paths.getInnerContour());
    // Athena's skeleton decomposition generates high-vertex-count inner contours.
    // Simplify early so all downstream Clipper2 operations run on reduced geometry.
    infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);

    // Debug: log perimeter loops and inner contour
    const double dbg_z = (params.layer != nullptr) ? params.layer->print_z : 0.0;
    dbg_perim_loops(dbg_z, params.layer_id, perimeters, ext_perimeter_width, perimeter_width);
    dbg_perim_contours("INNER_CONTOUR", dbg_z, params.layer_id, infill_contour, "before_overlap");

    // Check if there are some remaining perimeters to generate (the number of perimeters
    // is greater than one together with enabled the single perimeter on top surface feature).
    // Skipped under the serpentine band hand-off: the wall count is fixed at one there.
    if (inner_loop_number >= 0 && !serp_band_handoff)
    {
        assert(upper_slices != nullptr);

        // Infill contour bounding box.
        BoundingBox infill_contour_bbox = get_extents(infill_contour);
        infill_contour_bbox.offset(SCALED_EPSILON);

        // Get top ExPolygons from current infill contour.
        const Polygons upper_slices_clipped =
            ClipperUtils::clip_clipper_polygons_with_subject_bbox(*upper_slices, infill_contour_bbox);
        ExPolygons top_expolygons = diff_ex(infill_contour, upper_slices_clipped);

        if (!top_expolygons.empty())
        {
            if (lower_slices != nullptr)
            {
                const float bridge_offset = float(std::max<coord_t>(ext_perimeter_spacing, perimeter_width));
                const Polygons lower_slices_clipped =
                    ClipperUtils::clip_clipper_polygons_with_subject_bbox(*lower_slices, infill_contour_bbox);
                const ExPolygons current_slices_bridges = offset_ex(diff_ex(top_expolygons, lower_slices_clipped),
                                                                    bridge_offset);

                // Remove bridges from top surface polygons.
                top_expolygons = diff_ex(top_expolygons, current_slices_bridges);
            }

            // Filter out areas that are too thin and expand top surface polygons a bit to hide the wall line.
            const float top_surface_min_width = std::max<float>(float(ext_perimeter_spacing) / 4.f +
                                                                    scaled<float>(0.00001),
                                                                float(perimeter_width) / 4.f);
            top_expolygons = offset2_ex(top_expolygons, -top_surface_min_width,
                                        top_surface_min_width + float(perimeter_width));

            // Get the not-top ExPolygons (including bridges) from current slices and expanded real top ExPolygons (without bridges).
            const ExPolygons not_top_expolygons = diff_ex(infill_contour, top_expolygons);

            // Get final top ExPolygons.
            top_expolygons = intersection_ex(top_expolygons, infill_contour);

            const Polygons not_top_polygons = to_polygons(not_top_expolygons);
            Athena::WallToolPaths inner_wall_tool_paths(not_top_polygons, perimeter_spacing, perimeter_spacing,
                                                        coord_t(inner_loop_number + 1), 0, params.layer_height,
                                                        params.object_config, params.print_config, perimeter_width,
                                                        perimeter_width, 0, perimeter_spacing, 0, params.layer_id,
                                                        min_bead_width_factor, tw_snap, max_perimeter_width);
            Athena::Perimeters inner_perimeters = inner_wall_tool_paths.getToolPaths();
            preFlight::PreciseWalls::enforce_exact_widths(inner_perimeters, ext_perimeter_width, perimeter_width,
                                                          tw_snap);

            // Recalculate indexes of inner perimeters before merging them.
            if (!perimeters.empty())
            {
                for (Athena::VariableWidthLines &inner_perimeter : inner_perimeters)
                {
                    if (inner_perimeter.empty())
                        continue;

                    for (Athena::ExtrusionLine &el : inner_perimeter)
                        ++el.inset_idx;
                }
            }

            perimeters.insert(perimeters.end(), inner_perimeters.begin(), inner_perimeters.end());
            infill_contour = union_ex(top_expolygons, inner_wall_tool_paths.getInnerContour());
            infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);
        }
        else
        {
            // There is no top surface ExPolygon, so we call Arachne again with parameters
            // like when the single perimeter feature is disabled.
            Athena::WallToolPaths no_single_perimeter_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing,
                                                                 coord_t(inner_loop_number + 2), 0, params.layer_height,
                                                                 params.object_config, params.print_config,
                                                                 ext_perimeter_width, perimeter_width,
                                                                 ext_perimeter_spacing2, perimeter_spacing, 0,
                                                                 params.layer_id, min_bead_width_factor, tw_snap,
                                                                 max_perimeter_width);
            perimeters = no_single_perimeter_tool_paths.getToolPaths();
            preFlight::PreciseWalls::enforce_exact_widths(perimeters, ext_perimeter_width, perimeter_width, tw_snap);
            infill_contour = union_ex(no_single_perimeter_tool_paths.getInnerContour());
            infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);
        }
    }

    loop_number = int(perimeters.size()) - 1;

#ifdef ARACHNE_DEBUG
    {
        static int iRun = 0;
        export_perimeters_to_svg(debug_out_path("arachne-perimeters-%d-%d.svg", params.layer_id, iRun++),
                                 to_polygons(last), perimeters, union_ex(wallToolPaths.getInnerContour()));
    }
#endif

    // All closed ExtrusionLine should have the same the first and the last point.
    // But in rare cases, Arachne produce ExtrusionLine marked as closed but without
    // equal the first and the last point.
    assert(
        [&perimeters = std::as_const(perimeters)]() -> bool
        {
            for (const Athena::Perimeter &perimeter : perimeters)
                for (const Athena::ExtrusionLine &el : perimeter)
                    if (el.is_closed && el.junctions.front().p != el.junctions.back().p)
                        return false;
            return true;
        }());

    // ===== Per-region "Perimeters while Interlocking": footprint + additive suppressed-region walls =====
    // The footprint (where interlocking lands vs where it is suppressed) is computed ONCE here and reused
    // by the interlocking block below, so the wall count and the shell placement share one region and
    // cannot disagree. In suppressed sub-regions the extra (full - reduced) walls are regenerated as clean
    // closed Athena loops; the buried core keeps the reduced count plus its interlocking shells.
    ExPolygons il_regions, non_il_regions, bridge_anchor_zone, il_suppressed_core, il_wall_footprint;
    // The reduced-count inner contour, saved so the seam-knit closing below can be re-bounded to it
    // (the closing must never bridge a real interior hole or overrun the external wall).
    ExPolygons il_reduced_inner;
    bool il_have_region = false;

    // Inward depth the interlocking shell stack consumes (outline edge -> innermost shell -> inner edge),
    // mirroring the interlocking block's total_depth. A region narrower than this on both sides has no room
    // for an infill core: the shells cram into it and shatter the regular perimeters (the narrow-arm void).
    // The footprint drops such regions so they keep full perimeters instead of being interlocked.
    coord_t il_core_depth = 0;
    {
        const coord_t il_base_w = perimeter_width;
        const coord_t il_main_w = coord_t(perimeter_width * std::sqrt(2.0));
        constexpr double IL_BOUNDARY_FLOW = (3.0 + 2.0 * 1.41421356) / 4.0;
        const coord_t il_boundary_w = coord_t(perimeter_width * std::sqrt(IL_BOUNDARY_FLOW));
        const coord_t il_boundary_shift = (il_boundary_w - il_base_w) / 2;
        const coord_t il_overlap_reduction = preFlight::PreciseWalls::calculate_perimeter_spacing(
            params.perimeter_flow, params.config.interlock_perimeter_overlap);
        const coord_t il_overlap_amount = perimeter_width - il_overlap_reduction;
        const coord_t il_adjacent_d = (il_base_w + il_main_w) / 2 - il_overlap_amount;
        const coord_t il_gapped_d = 2 * il_adjacent_d;
        const int il_num_shells = params.config.interlock_perimeter_count.value;
        il_core_depth = il_base_w / 2; // ic_bead_width_0/2
        if (il_num_shells >= 2)
            il_core_depth += il_adjacent_d; // ic_external
        if (il_num_shells >= 3)
            il_core_depth += il_gapped_d * (il_num_shells - 3) + (il_gapped_d - il_boundary_shift); // +ic_innermost
        il_core_depth += perimeter_width / 2;
    }

    // pwi_active: true only on the PWI (wall-reduced) path. The visibility split, bridge subtraction and
    // 4.47 opening run on every interlocking island (HEAD behavior). The reclassifications and the
    // core/sliver filters that follow are PWI-only, so with the feature off (interlock_regular_perimeters=0)
    // the footprint is byte-identical to HEAD.
    auto il_compute_footprint = [&](const ExPolygons &contour, bool pwi_active)
    {
        il_regions.clear();
        non_il_regions.clear();
        bridge_anchor_zone.clear();
        ExPolygons visibility_zone = visible_within_solid_range(params.layer, contour,
                                                                params.config.interlock_solid_layers_top.value,
                                                                params.config.interlock_solid_layers_bottom.value);
        if (!visibility_zone.empty())
        {
            visibility_zone = union_ex(visibility_zone);
            il_regions = diff_ex(contour, visibility_zone);
            non_il_regions = intersection_ex(contour, visibility_zone);
        }
        else
            il_regions = contour;
        if (lower_slices != nullptr && !il_regions.empty())
        {
            ExPolygons overhang = diff_ex(contour, *lower_slices);
            if (!overhang.empty())
            {
                coord_t anchor_depth = (il_regular_override > 0 ? coord_t(il_regular_override) * perimeter_width
                                                                : coord_t(loop_number + 1) * perimeter_width) +
                                       ext_perimeter_width / 2;
                ExPolygons overhang_grown = offset_ex(overhang, anchor_depth);
                ExPolygons lower_in_infill = intersection_ex(contour, *lower_slices);
                bridge_anchor_zone = intersection_ex(overhang_grown, lower_in_infill);
                if (!bridge_anchor_zone.empty())
                {
                    il_regions = diff_ex(il_regions, bridge_anchor_zone);
                    // PWI: fold the anchor zone into the suppressed set so the il_regions.empty() early exit
                    // still covers it with full walls + solid fill (otherwise it is in neither region -> void).
                    if (pwi_active)
                    {
                        append(non_il_regions, bridge_anchor_zone);
                        non_il_regions = union_ex(non_il_regions);
                    }
                }
            }
        }
        const coord_t opening = coord_t(perimeter_width * 4.47) / 2;
        if (opening > 0 && !il_regions.empty())
        {
            const ExPolygons before_open = il_regions;
            il_regions = offset_ex(offset_ex(il_regions, -opening), opening);
            // Thin buried features the opening removes are too narrow for interlocking - reclassify them as
            // suppressed (full walls) instead of dropping them, which would leave the area void.
            const ExPolygons opening_removed = diff_ex(before_open, il_regions);
            if (pwi_active && !opening_removed.empty())
            {
                append(non_il_regions, opening_removed);
                non_il_regions = union_ex(non_il_regions);
            }
        }

        // Core-existence filter (per connected component): interlocking only where the full shell pattern
        // leaves room for a FILLABLE infill core. The shells consume il_core_depth inward from the
        // boundary; a component must also leave a core at least a couple beads wide, or the shells cram
        // together and shatter the regular perimeters (the narrow-arm/spoke voids). Components that fail
        // keep full perimeters (moved to non_il); wide regions with a real core are kept whole.
        const coord_t il_core_min = 2 * perimeter_width; // a fillable core, not a sliver
        const coord_t il_core_req = il_core_depth + il_core_min;
        if (pwi_active && il_core_req > 0 && !il_regions.empty())
        {
            ExPolygons wide, narrow;
            for (const ExPolygon &comp : il_regions)
            {
                if (offset_ex(ExPolygons{comp}, -float(il_core_req)).empty())
                    narrow.push_back(comp);
                else
                    wide.push_back(comp);
            }
            if (!narrow.empty())
            {
                il_regions = std::move(wide);
                append(non_il_regions, narrow);
                non_il_regions = union_ex(non_il_regions);
            }
        }

        // Drop thin taper "slope-ring" slivers from non_il (the perimeter<->interlocking seam, not real
        // solid faces) so the additive block never jams an extra wall into them. They are intentionally NOT
        // unioned into il_regions - that fragments il_regions into many holes and breaks the inner-contour
        // offset. Instead the interlocking shells generate over the WHOLE reduced contour and flow across
        // the slivers (no jammed wall, no gap). A bead-or-wider face is a genuine solid surface and stays
        // suppressed. Average width (2*area / boundary length) is offset-free, unlike a morphological
        // opening which fails to collapse a thin closed ring. Only when interlocking actually exists
        // (il_regions non-empty): with no interlocking there is no seam, and a thin FEATURE region would
        // otherwise be wrongly dropped here instead of kept for its full walls.
        if (pwi_active && !il_regions.empty() && !non_il_regions.empty())
        {
            ExPolygons genuine;
            for (const ExPolygon &face : non_il_regions)
            {
                double blen = face.contour.length();
                for (const Polygon &h : face.holes)
                    blen += h.length();
                if (blen > 0.0 && 2.0 * std::abs(face.area()) / blen >= double(perimeter_width))
                    genuine.push_back(face);
            }
            non_il_regions = std::move(genuine);
        }
    };

    // Additive per-region walls. The base stack is already the REDUCED count (il_reduce_walls set
    // loop_number = reduced - 1 before wall generation, so perimeters/infill_contour are byte-identical to
    // plain perimeters=<reduced>). The buried core keeps the reduced stack plus its interlocking shells; the
    // suppressed sub-regions get the extra (full - reduced) walls back as CLOSED concentric loops generated by
    // Athena directly on the suppressed region (reduced_inner INTERSECT non_il) - so they fill that region
    // with the PWI-off nesting and are inherently bounded by the il-interface (no overlap into the buried core).
    if (il_reduce_walls && !perimeters.empty() && !infill_contour.empty())
    {
        const size_t reduced_count = size_t(il_regular_override);
        const size_t full_count = size_t(params.config.perimeters.value);

        // The reduced inner contour IS infill_contour here (getInnerContour of the reduced stack). Saved at
        // function scope so the seam-knit closing can re-bound to it.
        il_reduced_inner = infill_contour;

        il_compute_footprint(il_reduced_inner, /*pwi_active=*/true);
        if (full_count > reduced_count)
        {
            if (!non_il_regions.empty())
            {
                // Feed the SUPPRESSED region itself (reduced_inner INTERSECT non_il) - the correct boundary -
                // through Athena concentric. The (full - reduced) extra walls nest inward from the reduced
                // inner edge as CLOSED loops, filling the suppressed flanks with the PWI-off nesting. Because
                // they are generated INSIDE non_il they are inherently bounded by the il-interface: they fill
                // up to the interlocking and STOP, never riding over the buried core. inset_idx is bumped by
                // reduced_count so the extra loops index contiguously after the base reduced stack.
                const ExPolygons extra_outline_ex = intersection_ex(il_reduced_inner, non_il_regions);
                // Per-PWI-layer geometry dump (debug only) - load the regions/loops in shapely to inspect
                // the band decision on any part.
                const bool wkt_dump = pg_dbg_active();
                if (wkt_dump)
                {
                    dbg_wkt_expolys(dbg_z, "last", last);
                    dbg_wkt_expolys(dbg_z, "non_il", non_il_regions);
                    dbg_wkt_expolys(dbg_z, "il_regions", il_regions);
                    dbg_wkt_expolys(dbg_z, "extra_outline", extra_outline_ex);
                }
                int extra_loops = 0, extra_dropped = 0;
                ExPolygons extra_inner, kept_footprint;
                if (!extra_outline_ex.empty())
                {
                    const Polygons extra_outline = to_polygons(extra_outline_ex);
                    Athena::WallToolPaths extra_wall_tool_paths(extra_outline, perimeter_spacing, perimeter_spacing,
                                                                coord_t(full_count - reduced_count), 0,
                                                                params.layer_height, params.object_config,
                                                                params.print_config, perimeter_width, perimeter_width,
                                                                0, perimeter_spacing, 0, params.layer_id,
                                                                min_bead_width_factor, tw_snap, max_perimeter_width);
                    Athena::Perimeters extra_perimeters = extra_wall_tool_paths.getToolPaths();
                    preFlight::PreciseWalls::enforce_exact_widths(extra_perimeters, ext_perimeter_width,
                                                                  perimeter_width, tw_snap);
                    extra_inner = union_ex(extra_wall_tool_paths.getInnerContour());

                    // Legitimacy by CONNECTIVITY (the user's rule, no magic depth): an additive perimeter is
                    // legitimate only if it chains back to a real island boundary through other perimeters.
                    // Build the merged wall band - every base + additive centerline fattened just past half the
                    // nesting pitch, so adjacent nested loops touch and form one connected component, while a
                    // ring stranded around a buried core (separated by an infill gap) lands in its OWN
                    // component. Keep additive loops whose component reaches the base (already-anchored) stack;
                    // drop the rest. The only quantity is perimeter_spacing (a setting) - no depth threshold,
                    // so it is invariant to nozzle / line width / perimeter count. inset_idx is bumped so kept
                    // loops index contiguously after the base reduced stack. Odd thin-wall lines are always kept.
                    const float merge_r = float(perimeter_spacing) * 0.75f;
                    auto centerline = [](const Athena::ExtrusionLine &ln)
                    {
                        Polyline pl;
                        for (const Athena::ExtrusionJunction &j : ln.junctions)
                            pl.append(j.p);
                        return pl;
                    };
                    Polylines base_cl; // the already-anchored real perimeters (current `perimeters` = base stack)
                    for (const Athena::VariableWidthLines &lvl : perimeters)
                        for (const Athena::ExtrusionLine &ln : lvl)
                            if (ln.junctions.size() >= 2)
                                base_cl.push_back(centerline(ln));
                    Polylines all_cl = base_cl;
                    for (Athena::VariableWidthLines &lvl : extra_perimeters)
                        for (Athena::ExtrusionLine &ln : lvl)
                        {
                            ln.inset_idx += reduced_count;
                            if (ln.junctions.size() >= 2)
                                all_cl.push_back(centerline(ln));
                        }
                    const ExPolygons full_band = base_cl.empty() ? ExPolygons{} : union_ex(offset(all_cl, merge_r));
                    const ExPolygons base_fp = base_cl.empty() ? ExPolygons{} : union_ex(offset(base_cl, merge_r));
                    ExPolygons anchored_band; // the wall-band components that reach the anchored base stack
                    for (const ExPolygon &comp : full_band)
                        if (!intersection_ex(ExPolygons{comp}, base_fp).empty())
                            anchored_band.push_back(comp);
                    auto pt_anchored = [&anchored_band](const Point &p)
                    {
                        for (const ExPolygon &ep : anchored_band)
                            if (ep.contains(p))
                                return true;
                        return false;
                    };
                    for (Athena::VariableWidthLines &level : extra_perimeters)
                    {
                        Athena::VariableWidthLines kept;
                        for (Athena::ExtrusionLine &line : level)
                        {
                            if (line.junctions.empty())
                                continue;
                            bool anchored = line.is_odd; // odd thin-wall centerlines are always legit
                            if (!anchored)
                            {
                                size_t n_in = 0;
                                for (const Athena::ExtrusionJunction &j : line.junctions)
                                    if (pt_anchored(j.p))
                                        ++n_in;
                                anchored = n_in * 2 >= line.junctions.size(); // majority chained to the boundary
                            }
                            if (wkt_dump)
                                dbg_wkt_polyline(dbg_z, anchored ? "loop" : "band", line.inset_idx, line.is_odd,
                                                 line.junctions);
                            if (!anchored)
                            {
                                ++extra_dropped;
                                continue;
                            }
                            ++extra_loops;
                            kept.push_back(std::move(line));
                        }
                        level = std::move(kept);
                    }
                    Athena::WallToolPaths::removeEmptyToolPaths(extra_perimeters);

                    // Footprint of the KEPT loops only (centerlines fattened by half a bead), so the dropped
                    // band area is NOT walled and falls back to fill.
                    Polylines kept_centerlines;
                    for (const Athena::VariableWidthLines &level : extra_perimeters)
                        for (const Athena::ExtrusionLine &line : level)
                        {
                            if (line.junctions.size() < 2)
                                continue;
                            Polyline pl;
                            for (const Athena::ExtrusionJunction &j : line.junctions)
                                pl.append(j.p);
                            kept_centerlines.push_back(std::move(pl));
                        }
                    if (!kept_centerlines.empty())
                        kept_footprint = union_ex(offset(kept_centerlines, float(perimeter_width) / 2.f));

                    perimeters.insert(perimeters.end(), extra_perimeters.begin(), extra_perimeters.end());
                    loop_number = int(perimeters.size()) - 1;
                }

                // Suppressed fill = the suppressed region MINUS the kept additive walls. The dropped band area
                // returns to fill, so interlocking bonds directly to fill there. "Perimeters win": the final
                // fill rebuild re-clips off il_wall_footprint after the seam closing. infill_contour stays the
                // WHOLE reduced inner contour so the interlocking shells run over the whole buried area.
                {
                    const ExPolygons supp = intersection_ex(il_reduced_inner, non_il_regions);
                    if (extra_outline_ex.empty())
                        il_suppressed_core = supp; // no extra walls: the whole suppressed region is fill
                    else if (extra_dropped > 0)
                    {
                        // A band was removed: bound the fill by the KEPT walls so the dropped band area returns
                        // to fill. The constant-offset footprint is acceptable here - the vacated area is a wide
                        // bridge annulus, re-clipped and overlap-padded downstream.
                        il_suppressed_core = kept_footprint.empty() ? supp : diff_ex(supp, kept_footprint);
                        il_wall_footprint = kept_footprint;
                    }
                    else
                    {
                        // Nothing dropped (the common case, incl. every complex flank): use Athena's precise
                        // variable-width inner contour so the fill is pristine - identical to plain concentric.
                        il_suppressed_core = intersection_ex(extra_inner, non_il_regions);
                        il_wall_footprint = diff_ex(supp, il_suppressed_core);
                    }
                }
                if (wkt_dump)
                {
                    dbg_wkt_expolys(dbg_z, "suppressed_core", il_suppressed_core);
                    dbg_wkt_expolys(dbg_z, "wall_footprint", il_wall_footprint);
                }
                if (pg_dbg_active())
                {
                    auto a = [](const ExPolygons &e)
                    {
                        double s = 0;
                        for (const ExPolygon &p : e)
                            s += std::abs(p.area()) * 1e-12;
                        return s;
                    };
                    BoundingBox nb = get_extents(non_il_regions);
                    dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "ILADD",
                            "layer=%d full=%zu reduced=%zu loops=%d band_dropped=%d "
                            "il_reg=%.2f(%zu) non_il=%.2f(%zu) non_il_bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                            params.layer_id, full_count, reduced_count, extra_loops, extra_dropped, a(il_regions),
                            il_regions.size(), a(non_il_regions), non_il_regions.size(), unscaled<double>(nb.min.x()),
                            unscaled<double>(nb.min.y()), unscaled<double>(nb.max.x()), unscaled<double>(nb.max.y()));
                }
            }
            // il_have_region drives the buried-core fill rebuild + seam closing below even on a fully buried
            // island (no suppressed region): there il_suppressed_core / il_wall_footprint stay empty and the
            // walls stay at the reduced count, matching the fully-buried (roller) path.
            il_have_region = true;
        }
    }

    Athena::PerimeterOrder::PerimeterExtrusions ordered_extrusions =
        Athena::PerimeterOrder::ordered_perimeter_extrusions(perimeters, params.config.external_perimeters_first);

    // Render the serpentine band's wall-0 with the serpentine flow (its width),
    // not the external perimeter flow; non-serpentine islands use params unchanged.
    // NOTE: the derived Parameters::ext_mm3_per_mm is NOT recomputed here, so it is
    // stale on this copy. It is currently dead (per-path mm3 comes from the Flow at
    // each construction site), but anyone wiring ext_mm3_per_mm into a flow calc must
    // recompute it from serp_wall_flow.
    Parameters wall_params = params;
    if (serp_band_handoff)
        wall_params.ext_perimeter_flow = serp_wall_flow;
    if (ExtrusionEntityCollection extrusion_coll = traverse_extrusions(wall_params, lower_slices_polygons_cache,
                                                                       lower_slices_raw, ordered_extrusions);
        !extrusion_coll.empty())
    {
        if (serp_band_handoff && serp_band_coll != nullptr)
        {
            // Limit-depth hand-off: the inner Athena wall attaches to the
            // serpentine teeth, so it prints as the Serpentine feature type
            // (overhang spans Athena already detected become Serp. Overhang). Then
            // splice it to the front of the band's no_sort collection so it prints
            // first; clearing the source vector transfers ownership (no double free).
            auto reclass = [](ExtrusionPaths &paths)
            {
                for (ExtrusionPath &p : paths)
                {
                    const ExtrusionRole r = p.role();
                    if (r.is_perimeter() && !r.is_serpentine())
                        p.attributes().role = r.is_bridge() ? ExtrusionRole::SerpentineOverhang
                                                            : ExtrusionRole::Serpentine;
                }
            };
            for (ExtrusionEntity *e : extrusion_coll.entities)
            {
                if (auto *mp = dynamic_cast<ExtrusionMultiPath *>(e))
                    reclass(mp->paths);
                else if (auto *lp = dynamic_cast<ExtrusionLoop *>(e))
                    reclass(lp->paths);
            }
            serp_band_coll->entities.insert(serp_band_coll->entities.begin(), extrusion_coll.entities.begin(),
                                            extrusion_coll.entities.end());
            extrusion_coll.entities.clear();
        }
        else
            out_loops.append(extrusion_coll);
    }

    // Note: Gap fill is intentionally not implemented for Athena (matches Arachne behavior)

    const coord_t spacing = (perimeters.size() == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
    if (offset_ex(infill_contour, -float(spacing / 2.)).empty())
        infill_contour.clear(); // Infill region is too small, so let's filter it out.

    // ===================== INTERLOCKING PERIMETER GENERATION =====================
    // Interlocking shells are true perimeters generated from infill_contour (the area inside
    // regular perimeters). They consume space from infill_contour, and infill fills whatever
    // remains. The Visibility API excludes areas near top/bottom surfaces.
    // Skip on retry: we're here because IL failed with fewer perimeters; with more perimeters
    // the infill contour is even smaller, so IL would fail again. Go straight to infill.
    // Serpentine depth mode reaches here with one Athena wall behind the band and the
    // inner core as infill_contour, so interlocking runs inside it. Full serpentine mode
    // returns above (never reaches here), so interlocking is ignored there as intended.
    if (params.config.interlock_perimeters_enabled && !infill_contour.empty() && params.layer != nullptr)
    {
        const int num_interlocking_shells = params.config.interlock_perimeter_count.value;
        if (num_interlocking_shells > 0)
        {
            // Reuse the per-region footprint computed above when PWI reduced the walls, so the wall count
            // and the shell placement are driven by ONE region. When the override is off the footprint was
            // not computed, so compute it here from infill_contour (same visibility split, bridge
            // anchoring, 4.47 opening and core filter - centralized in il_compute_footprint).
            if (!il_have_region)
                il_compute_footprint(infill_contour, il_reduce_walls);

            dbg_il_regions(dbg_z, "VISIBILITY", infill_contour, "infill_contour");
            dbg_il_regions(dbg_z, "VISIBILITY", il_regions, "il_regions");
            dbg_il_regions(dbg_z, "VISIBILITY", non_il_regions, "non_il_regions");

            if (il_regions.empty())
            {
                // No interlocking lands on this surface. If the per-region additive walls ran, bound the
                // fill by them (il_suppressed_core) so sparse/solid does not overflow the injected
                // perimeters; otherwise leave infill_contour as the whole reduced contour (HEAD behavior).
                if (il_have_region)
                    infill_contour = il_suppressed_core;
                goto skip_interlocking;
            }

            // Always true here (empty case handled above); guard kept to avoid re-indenting ~600 lines.
            if (!il_regions.empty())
            {
                const bool is_odd_layer = (params.layer_id % 2 == 1);

                // Flow-scaled bead widths for interlocking pattern
                const coord_t base_w = perimeter_width;
                const coord_t main_w = coord_t(perimeter_width * sqrt(2.0));
                constexpr double BOUNDARY_FLOW = (3.0 + 2.0 * 1.41421356) / 4.0;
                const coord_t boundary_w = coord_t(perimeter_width * sqrt(BOUNDARY_FLOW));
                const coord_t boundary_shift = (boundary_w - base_w) / 2;

                // Interlocking overlap
                const auto &il_overlap = params.config.interlock_perimeter_overlap;
                const coord_t overlap_reduction =
                    preFlight::PreciseWalls::calculate_perimeter_spacing(params.perimeter_flow, il_overlap);
                const coord_t overlap_amount = perimeter_width - overlap_reduction;

                // Shell spacings
                const coord_t il_adjacent = (base_w + main_w) / 2 - overlap_amount;
                const coord_t il_gapped = 2 * il_adjacent;
                // Compensate for the wider boundary bead on even layers: shell 0 center
                // is boundary_shift further from the outline, so reduce il_external to
                // keep all 200% beads aligned with the odd-layer pattern.
                const coord_t il_external = is_odd_layer ? il_adjacent : (il_gapped - boundary_shift);
                const coord_t il_internal = il_gapped;
                // Mirror: odd innermost (146%) uses even external spacing,
                // even innermost (100%) uses odd external spacing (no shift needed).
                const coord_t il_innermost = is_odd_layer ? (il_gapped - boundary_shift) : il_adjacent;

                // Reduce shell count if space is too narrow
                BoundingBox zone_bbox = get_extents(infill_contour);
                coord_t min_dim = std::min(zone_bbox.size().x(), zone_bbox.size().y());
                int actual_shells = num_interlocking_shells;
                if (min_dim < num_interlocking_shells * perimeter_width * 2)
                {
                    actual_shells = min_dim / (perimeter_width * 2);
                    if (actual_shells <= 0)
                    {
                        // No room for shells. Mirror the il_regions.empty() bailout: bound the fill by the
                        // additive walls so solid/sparse does not overflow the injected perimeters.
                        if (il_have_region)
                            infill_contour = il_suppressed_core;
                        goto skip_interlocking;
                    }
                }

                dbg_il_params(dbg_z, params.layer_id, is_odd_layer, actual_shells, num_interlocking_shells,
                              perimeter_width, base_w, main_w, boundary_w, il_external, il_internal, il_innermost,
                              overlap_amount, perimeter_width - perimeter_spacing);

                {
                    if (infill_contour.empty())
                        goto skip_interlocking;

                    // Index of the current surface's sub-collection in out_loops.
                    // It was just appended, so it's the last one.
                    const size_t current_coll_idx = out_loops.entities.empty() ? 0 : out_loops.entities.size() - 1;

                    // Shell 0 bead width: wider boundary bead on even layers, standard on odd
                    const coord_t il_bead_width_0 = is_odd_layer ? base_w : boundary_w;

                    // Perimeter/perimeter overlap: expand outline so shell 0 overlaps
                    // with the innermost regular perimeter by the same amount that
                    // regular perimeters overlap each other.
                    const coord_t perim_to_il_overlap = perimeter_width - perimeter_spacing;

                    const bool prefer_cw = params.print_config.prefer_clockwise_movements;

                    // Keep only genuine wide visibility faces; drop hairline taper "slope-ring" slivers
                    // (the perimeter<->interlocking seam) so they neither clip the shells nor reach solid
                    // fill - interlocking bonds across that lane, it is never gap-filled. A morphological
                    // opening (offset -w/+w) does not reliably collapse a thin closed annulus, so test
                    // average width (2*area / boundary length) directly: a bead or wider is a real solid
                    // face and stays suppressed; thinner is seam noise the shells flow over.
                    ExPolygons non_il_opened;
                    for (const ExPolygon &face : non_il_regions)
                    {
                        double blen = face.contour.length();
                        for (const Polygon &h : face.holes)
                            blen += h.length();
                        if (blen > 0.0 && 2.0 * std::abs(face.area()) / blen >= double(perimeter_width))
                            non_il_opened.push_back(face);
                    }
                    // Add bridge anchor zone after opening filter so narrow anchor
                    // strips at top/bottom of bridges aren't removed
                    if (!bridge_anchor_zone.empty())
                    {
                        append(non_il_opened, bridge_anchor_zone);
                        non_il_opened = union_ex(non_il_opened);
                    }
                    const bool need_visibility_clip = !non_il_opened.empty();

                    // Helper to create an interlocking ExtrusionLoop from a Polygon
                    auto make_il_loop = [&](ExtrusionEntityCollection &coll, const Polygon &poly, size_t shell_idx)
                    {
                        ExtrusionFlow sf(params.perimeter_flow.mm3_per_mm(), params.perimeter_flow.width(),
                                         params.perimeter_flow.height());
                        ExtrusionAttributes attribs(ExtrusionRole::InterlockingPerimeter, sf);
                        attribs.perimeter_index = static_cast<uint16_t>(shell_idx);
                        ExtrusionPath p(attribs);
                        for (const Point &pt : poly.points)
                            p.polyline.append(pt);
                        if (p.polyline.first_point() != p.polyline.last_point())
                            p.polyline.append(p.polyline.first_point());
                        ExtrusionPaths paths;
                        paths.push_back(std::move(p));
                        ExtrusionLoop loop(std::move(paths));
                        if (prefer_cw ? !loop.is_clockwise() : loop.is_clockwise())
                            loop.reverse_loop();
                        coll.append(std::move(loop));
                    };

                    // Helper to collect shells from Athena output into an ExtrusionEntityCollection.
                    // Handles visibility clipping against non_il_regions.
                    auto collect_shells = [&](ExtrusionEntityCollection &coll,
                                              const std::vector<Athena::VariableWidthLines> &il_paths, int shells)
                    {
                        for (size_t inset_idx = 0; inset_idx < il_paths.size() && inset_idx < size_t(shells);
                             ++inset_idx)
                        {
                            for (const Athena::ExtrusionLine &line : il_paths[inset_idx])
                            {
                                if (line.empty() || line.size() < 2 || line.is_odd || !line.is_closed)
                                    continue;
                                Polygon poly;
                                for (const Athena::ExtrusionJunction &j : line.junctions)
                                    poly.points.push_back(j.p);
                                if (poly.size() < 3)
                                    continue;

                                if (need_visibility_clip)
                                {
                                    Polyline shell_pl;
                                    for (const Point &pt : poly.points)
                                        shell_pl.append(pt);
                                    shell_pl.append(poly.points.front());

                                    double shell_len = unscaled<double>(shell_pl.length());
                                    Polylines clipped = diff_pl(shell_pl, non_il_opened);
                                    if (pg_dbg_active())
                                    {
                                        BoundingBox pbb = get_extents(poly);
                                        if (clipped.empty())
                                        {
                                            dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "INTERLOCK",
                                                    "VIS_CLIP inset=%zu FULLY_CLIPPED "
                                                    "shell_len=%.2fmm bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                                                    inset_idx, shell_len, unscaled<double>(pbb.min.x()),
                                                    unscaled<double>(pbb.min.y()), unscaled<double>(pbb.max.x()),
                                                    unscaled<double>(pbb.max.y()));
                                        }
                                        else
                                        {
                                            double total_clip_len = 0;
                                            for (const Polyline &pl : clipped)
                                                total_clip_len += unscaled<double>(pl.length());
                                            dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "INTERLOCK",
                                                    "VIS_CLIP inset=%zu segs=%zu "
                                                    "shell_len=%.2fmm clip_len=%.2fmm (%.1f%%) "
                                                    "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                                                    inset_idx, clipped.size(), shell_len, total_clip_len,
                                                    (total_clip_len / shell_len) * 100.0, unscaled<double>(pbb.min.x()),
                                                    unscaled<double>(pbb.min.y()), unscaled<double>(pbb.max.x()),
                                                    unscaled<double>(pbb.max.y()));
                                        }
                                    }
                                    if (clipped.empty())
                                        continue;

                                    // Rejoin segments split at the seam point
                                    if (clipped.size() >= 2)
                                    {
                                        const Point &seam = poly.points.front();
                                        const double eps_sq = double(SCALED_EPSILON) * double(SCALED_EPSILON);
                                        bool did_join;
                                        do
                                        {
                                            did_join = false;
                                            for (size_t a = 0; a < clipped.size() && !did_join; ++a)
                                                for (size_t b = a + 1; b < clipped.size() && !did_join; ++b)
                                                {
                                                    bool a_end =
                                                        (clipped[a].last_point() - seam).cast<double>().squaredNorm() <
                                                        eps_sq;
                                                    bool b_start =
                                                        (clipped[b].first_point() - seam).cast<double>().squaredNorm() <
                                                        eps_sq;
                                                    if (a_end && b_start)
                                                    {
                                                        for (size_t pi = 1; pi < clipped[b].size(); ++pi)
                                                            clipped[a].append(clipped[b].points[pi]);
                                                        clipped.erase(clipped.begin() + b);
                                                        did_join = true;
                                                        break;
                                                    }
                                                    bool b_end =
                                                        (clipped[b].last_point() - seam).cast<double>().squaredNorm() <
                                                        eps_sq;
                                                    bool a_start =
                                                        (clipped[a].first_point() - seam).cast<double>().squaredNorm() <
                                                        eps_sq;
                                                    if (b_end && a_start)
                                                    {
                                                        for (size_t pi = 1; pi < clipped[a].size(); ++pi)
                                                            clipped[b].append(clipped[a].points[pi]);
                                                        clipped.erase(clipped.begin() + a);
                                                        did_join = true;
                                                        break;
                                                    }
                                                }
                                        } while (did_join && clipped.size() >= 2);
                                    }

                                    if (clipped.size() == 1 && clipped.front().size() >= 4 &&
                                        clipped.front().first_point() == clipped.front().last_point())
                                    {
                                        Polygon clipped_poly;
                                        const auto &pts = clipped.front().points;
                                        for (size_t ci = 0; ci + 1 < pts.size(); ++ci)
                                            clipped_poly.points.push_back(pts[ci]);
                                        make_il_loop(coll, clipped_poly, inset_idx);
                                        if (pg_dbg_active())
                                            dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "INTERLOCK",
                                                    "VIS_RESULT inset=%zu REJOINED_LOOP", inset_idx);
                                    }
                                    else
                                    {
                                        const double min_segment_len = perimeter_width * 3.0;
                                        for (const Polyline &seg : clipped)
                                        {
                                            double seg_len = unscaled<double>(seg.length());
                                            if (seg.size() < 2 || seg.length() < min_segment_len)
                                            {
                                                if (pg_dbg_active())
                                                    dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "INTERLOCK",
                                                            "VIS_RESULT inset=%zu "
                                                            "DROPPED_SHORT len=%.2fmm min=%.2fmm",
                                                            inset_idx, seg_len,
                                                            unscaled<double>(perimeter_width) * 3.0);
                                                continue;
                                            }
                                            if (pg_dbg_active())
                                                dbg_log(Slic3r::DBG_INTERLOCK, dbg_z, "INTERLOCK",
                                                        "VIS_RESULT inset=%zu "
                                                        "KEPT_PATH len=%.2fmm",
                                                        inset_idx, seg_len);
                                            ExtrusionFlow sf(params.perimeter_flow.mm3_per_mm(),
                                                             params.perimeter_flow.width(),
                                                             params.perimeter_flow.height());
                                            ExtrusionAttributes attribs(ExtrusionRole::InterlockingPerimeter, sf);
                                            attribs.perimeter_index = static_cast<uint16_t>(inset_idx);
                                            ExtrusionPath ep(attribs);
                                            ep.polyline = seg;
                                            coll.append(std::move(ep));
                                        }
                                    }
                                }
                                else
                                {
                                    make_il_loop(coll, poly, inset_idx);
                                }
                            }
                        }
                    };

                    // Generate interlocking shells via Athena.
                    // Two-phase approach:
                    // 1. One unified Athena call on the full contour for inner contour
                    //    computation (determines where infill starts).
                    // 2. Per-ExPolygon Athena calls for entity generation (gives each
                    //    entity a definitive island association for interleaving).
                    // Using per-ExPolygon inner contours can produce gaps at ExPolygon
                    // seams because the skeleton partitions space differently when
                    // processing parts vs the whole.
                    bool any_il_generated = false;

                    struct ExPolyIL
                    {
                        ExPolygon expoly;
                        ExtrusionEntityCollection entities;
                    };
                    std::vector<ExPolyIL> per_expoly_il;

                    // Phase 1: unified Athena for inner contour.
                    // Use consistent (odd-layer) parameters so the inner contour
                    // doesn't oscillate between layers. The narrower outermost bead
                    // produces the most generous inner contour. The ~0.04mm difference
                    // from the actual even-layer boundary bead is within infill_overlap.
                    const coord_t ic_bead_width_0 = base_w;
                    const coord_t ic_external = il_adjacent;
                    const coord_t ic_innermost = il_gapped - boundary_shift;

                    Polygons il_outline = to_polygons(infill_contour);
                    if (perim_to_il_overlap > 0)
                        il_outline = offset(il_outline, float(perim_to_il_overlap));

                    Athena::WallToolPaths il_walls(il_outline, ic_bead_width_0, il_internal, size_t(actual_shells), 0,
                                                   params.layer_height, params.object_config, params.print_config,
                                                   ic_bead_width_0, perimeter_width, ic_external, il_internal,
                                                   ic_innermost, params.layer_id, 1.0, 10000);

                    // Phase 1 must run first so getInnerContour() works.
                    il_walls.generate();

                    // Phase 2: generate entities with real alternating parameters.
                    // Phase 1 used consistent ic_* params for stable inner contour;
                    // Phase 2 uses il_* params for correct alternating bead widths.
                    if (infill_contour.size() <= 1)
                    {
                        // Single ExPolygon: generate entities with alternating params
                        Athena::WallToolPaths il_entity_walls(il_outline, il_bead_width_0, il_internal,
                                                              size_t(actual_shells), 0, params.layer_height,
                                                              params.object_config, params.print_config,
                                                              il_bead_width_0, perimeter_width, il_external,
                                                              il_internal, il_innermost, params.layer_id, 1.0, 10000);
                        const auto &il_paths = il_entity_walls.generate();
                        dbg_il_athena_shells(dbg_z, "unified", il_paths, actual_shells);
                        ExPolyIL ep_il;
                        ep_il.expoly = ExPolygon();
                        collect_shells(ep_il.entities, il_paths, actual_shells);
                        dbg_il_collect_result(dbg_z, "unified", ep_il.entities);
                        if (!ep_il.entities.empty())
                            any_il_generated = true;
                        per_expoly_il.push_back(std::move(ep_il));
                    }
                    else
                    {
                        for (size_t ep_idx = 0; ep_idx < infill_contour.size(); ++ep_idx)
                        {
                            const ExPolygon &expoly = infill_contour[ep_idx];
                            Polygons ep_outline = to_polygons(expoly);
                            if (ep_outline.empty())
                                continue;
                            if (perim_to_il_overlap > 0)
                                ep_outline = offset(ep_outline, float(perim_to_il_overlap));

                            Athena::WallToolPaths ep_walls(ep_outline, il_bead_width_0, il_internal,
                                                           size_t(actual_shells), 0, params.layer_height,
                                                           params.object_config, params.print_config, il_bead_width_0,
                                                           perimeter_width, il_external, il_internal, il_innermost,
                                                           params.layer_id, 1.0, 10000);
                            const auto &ep_paths = ep_walls.generate();

                            if (pg_dbg_active())
                            {
                                char label[64];
                                snprintf(label, sizeof(label), "ep[%zu]", ep_idx);
                                dbg_il_athena_shells(dbg_z, label, ep_paths, actual_shells);
                            }
                            ExPolyIL ep_il;
                            ep_il.expoly = expoly;
                            collect_shells(ep_il.entities, ep_paths, actual_shells);
                            if (pg_dbg_active())
                            {
                                char label[64];
                                snprintf(label, sizeof(label), "ep[%zu]", ep_idx);
                                dbg_il_collect_result(dbg_z, label, ep_il.entities);
                            }
                            if (!ep_il.entities.empty())
                                any_il_generated = true;
                            per_expoly_il.push_back(std::move(ep_il));
                        }
                    }

                    // Interleave interlocking with perimeters across ALL sub-collections
                    // in out_loops. Each sub-collection corresponds to an island/feature group.
                    // Match each interlocking entity to the nearest perimeter (by centroid)
                    // across ALL sub-collections to find which island it belongs to.
                    if (any_il_generated && !out_loops.entities.empty())
                    {
                        auto get_fid = [](const ExtrusionEntity *ee) -> uint16_t
                        {
                            if (auto *loop = dynamic_cast<const ExtrusionLoop *>(ee))
                                if (!loop->paths.empty() && loop->paths.front().attributes().feature_id)
                                    return *loop->paths.front().attributes().feature_id;
                            if (auto *path = dynamic_cast<const ExtrusionPath *>(ee))
                                if (path->attributes().feature_id)
                                    return *path->attributes().feature_id;
                            // Open/odd thin-wall perimeters are emitted as ExtrusionMultiPath; without this they
                            // read as feature 0 and the interleave dumps them after the interlocking instead of
                            // with their region's perimeters.
                            if (auto *mp = dynamic_cast<const ExtrusionMultiPath *>(ee))
                                if (!mp->paths.empty() && mp->paths.front().attributes().feature_id)
                                    return *mp->paths.front().attributes().feature_id;
                            return uint16_t(0);
                        };

                        auto get_inset = [](const ExtrusionEntity *ent) -> uint16_t
                        {
                            if (auto *lp = dynamic_cast<const ExtrusionLoop *>(ent))
                                return lp->paths.empty() ? uint16_t(0)
                                                         : lp->paths.front().attributes().perimeter_index.value_or(0);
                            if (auto *pp = dynamic_cast<const ExtrusionPath *>(ent))
                                return pp->attributes().perimeter_index.value_or(0);
                            return uint16_t(0);
                        };

                        // Strip existing interlocking from the CURRENT sub-collection only.
                        // Prior sub-collections are untouched - they own their interlocking.
                        {
                            auto *cur_coll = dynamic_cast<ExtrusionEntityCollection *>(
                                out_loops.entities[current_coll_idx]);
                            if (cur_coll)
                            {
                                std::vector<ExtrusionEntity *> keep;
                                for (ExtrusionEntity *e : cur_coll->entities)
                                {
                                    if (e->role() == ExtrusionRole::InterlockingPerimeter)
                                    {
                                        ExPolyIL ep_il;
                                        ep_il.expoly = ExPolygon();
                                        ep_il.entities.append(*e);
                                        per_expoly_il.push_back(std::move(ep_il));
                                        delete e;
                                    }
                                    else
                                    {
                                        keep.push_back(e);
                                    }
                                }
                                cur_coll->entities = std::move(keep);
                            }
                        }

                        // Build perimeter reference list from the CURRENT sub-collection only.
                        // Store sampled points on each perimeter for distance-based matching
                        // (centroids fail on concentric geometry where all centers coincide).
                        struct PerimRef
                        {
                            Points sample_pts;
                            uint16_t fid;
                        };
                        std::vector<PerimRef> all_perim_refs;
                        {
                            auto *cur_coll = dynamic_cast<ExtrusionEntityCollection *>(
                                out_loops.entities[current_coll_idx]);
                            if (cur_coll)
                                for (ExtrusionEntity *e : cur_coll->entities)
                                {
                                    Points pts;
                                    e->collect_points(pts);
                                    all_perim_refs.push_back({std::move(pts), get_fid(e)});
                                }
                        }

                        if (all_perim_refs.empty())
                        {
                            // No real perimeters found. Append interlocking to current sub-collection.
                            auto *cur_coll = dynamic_cast<ExtrusionEntityCollection *>(
                                out_loops.entities[current_coll_idx]);
                            if (cur_coll)
                                for (auto &ep_il : per_expoly_il)
                                    for (ExtrusionEntity *ent : ep_il.entities.entities)
                                        cur_coll->append(*ent);
                            goto skip_interleave;
                        }

                        {
                            // Match each interlocking entity to its nearest perimeter using
                            // minimum point-to-point distance on the actual paths.
                            struct ILAssignment
                            {
                                ExtrusionEntity *entity;
                                uint16_t fid;
                                uint16_t inset;
                            };
                            std::vector<ILAssignment> il_assignments;
                            for (auto &ep_il : per_expoly_il)
                            {
                                for (ExtrusionEntity *ent : ep_il.entities.entities)
                                {
                                    Points il_pts;
                                    ent->collect_points(il_pts);
                                    double best_dist = std::numeric_limits<double>::max();
                                    uint16_t best_fid = 0;
                                    for (const auto &pr : all_perim_refs)
                                    {
                                        // Minimum distance between any IL point and any perimeter point
                                        for (const Point &ip : il_pts)
                                            for (const Point &pp : pr.sample_pts)
                                            {
                                                double d = (ip - pp).cast<double>().squaredNorm();
                                                if (d < best_dist)
                                                {
                                                    best_dist = d;
                                                    best_fid = pr.fid;
                                                }
                                            }
                                    }
                                    il_assignments.push_back({ent, best_fid, get_inset(ent)});
                                }
                            }

                            // Rebuild the current sub-collection with interleaved interlocking
                            {
                                auto *coll = dynamic_cast<ExtrusionEntityCollection *>(
                                    out_loops.entities[current_coll_idx]);
                                if (coll && !coll->entities.empty())
                                {
                                    std::vector<ExtrusionEntity *> real_perims;
                                    std::vector<uint16_t> perim_fids;
                                    for (ExtrusionEntity *e : coll->entities)
                                    {
                                        if (e->role() != ExtrusionRole::InterlockingPerimeter)
                                        {
                                            real_perims.push_back(e);
                                            perim_fids.push_back(get_fid(e));
                                        }
                                    }

                                    // Build feature order from perimeter encounter order
                                    std::vector<uint16_t> feature_order;
                                    for (uint16_t fid : perim_fids)
                                    {
                                        if (std::find(feature_order.begin(), feature_order.end(), fid) ==
                                            feature_order.end())
                                            feature_order.push_back(fid);
                                    }

                                    // Rebuild: perimeters then interlocking per feature.
                                    // Interlocking sorted by inset (closest to perimeters first),
                                    // shortest-path chained within each inset for fragments.
                                    std::vector<ExtrusionEntity *> ordered;
                                    for (uint16_t fid : feature_order)
                                    {
                                        for (size_t i = 0; i < real_perims.size(); ++i)
                                            if (perim_fids[i] == fid)
                                                ordered.push_back(real_perims[i]->clone());

                                        // Group interlocking by inset for this feature
                                        std::map<uint16_t, std::vector<ExtrusionEntity *>> il_by_inset;
                                        for (size_t k = 0; k < il_assignments.size(); ++k)
                                            if (il_assignments[k].fid == fid)
                                                il_by_inset[il_assignments[k].inset].push_back(
                                                    il_assignments[k].entity->clone());

                                        // Emit each inset in order; chain within each for travel
                                        Point start = ordered.empty() ? Point::Zero() : ordered.back()->last_point();
                                        for (auto &[inset, il_ptrs] : il_by_inset)
                                        {
                                            chain_and_reorder_extrusion_entities(il_ptrs, &start);
                                            for (auto *e : il_ptrs)
                                            {
                                                ordered.push_back(e);
                                                start = e->last_point();
                                            }
                                        }
                                    }

                                    coll->clear();
                                    coll->entities = std::move(ordered);
                                }
                            }
                        }
                    skip_interleave:;
                    }

                    // Update infill_contour using the UNIFIED Athena inner contour,
                    // supplemented with a geometric offset fallback for corners.
                    // Athena's skeleton-derived inner contour is accurate along straight
                    // walls but curves away in tight corners, leaving gaps. A geometric
                    // offset of the outline follows corners properly. Union both.
                    const Polygons &il_inner = il_walls.getInnerContour();
                    {
                        // Geometric fallback: offset il_outline inward by total shell depth.
                        // This follows corners that Athena's skeleton rounds away from.
                        // Use consistent (Phase 1) parameters for geometric depth
                        coord_t total_depth = ic_bead_width_0 / 2; // outline edge to shell 0 center
                        if (actual_shells >= 2)
                            total_depth += ic_external; // shell 0 center to shell 1 center
                        if (actual_shells >= 3)
                            total_depth += il_internal * (actual_shells - 3) + ic_innermost;
                        total_depth += perimeter_width / 2; // last shell center to inner edge

                        // Use infill_contour (not il_outline) as the base for geometric
                        // offset. il_outline includes perim_to_il_overlap expansion which
                        // would make the geometric inner too large for narrow features.
                        Polygons geometric_inner = offset(to_polygons(infill_contour), -float(total_depth));
                        // Filter out tiny slivers - only keep polygons large enough for infill
                        const double min_area = double(perimeter_width) * double(perimeter_width) * 4.0;
                        geometric_inner.erase(std::remove_if(geometric_inner.begin(), geometric_inner.end(),
                                                             [min_area](const Polygon &p)
                                                             { return std::abs(p.area()) < min_area; }),
                                              geometric_inner.end());

                        // Only use geometric fallback where it extends Athena's inner
                        // contour into corners. Clip to areas adjacent to the inner contour
                        // to prevent creating spurious infill in narrow features where
                        // Athena correctly determined no infill should exist.
                        if (!il_inner.empty() && !geometric_inner.empty())
                        {
                            ExPolygons expanded_inner = offset_ex(union_ex(il_inner), perimeter_width * 2);
                            geometric_inner = to_polygons(intersection_ex(union_ex(geometric_inner), expanded_inner));
                        }
                        else if (il_inner.empty())
                        {
                            geometric_inner.clear(); // no inner contour = no fallback needed
                        }

                        // Union Athena's inner contour with the geometric fallback
                        Polygons combined_inner;
                        append(combined_inner, il_inner);
                        append(combined_inner, geometric_inner);

                        ExPolygons new_inner = intersection_ex(union_ex(combined_inner), il_regions);
                        // The suppressed (non-interlocking) fill area must be bounded by the FULL kept walls,
                        // not the reduced footprint, or solid infill overlaps the additive perimeters. The
                        // per-region additive block stored that full-depth core in il_suppressed_core (it left
                        // infill_contour as the whole reduced contour so the shells could run on it). With the
                        // override off, keep the opened non_il region (HEAD behavior).
                        if (il_have_region)
                            append(new_inner, il_suppressed_core);
                        else
                            append(new_inner, non_il_opened);
                        infill_contour = union_ex(new_inner);
                        // Knit the buried-core and suppressed-core fill pieces into one continuous region and
                        // close the hairline seam holes where they meet (a closing: expand, clip, retract).
                        // Their shared edge comes from two different offsets (shells vs walls) so it never
                        // tiles exactly, leaving sub-bead holes that split the bridge/solid infill.
                        if (il_have_region)
                        {
                            infill_contour = offset_ex(offset_ex(infill_contour, float(perimeter_width)),
                                                       -float(perimeter_width));
                            // A blind closing bridges any throat narrower than two beads, so it would fill a
                            // real interior void (vent slot / bolt hole) and expand fill outward over the
                            // external wall. The reduced inner contour is the largest the fill may ever be
                            // (it carries the real holes and the outer-wall edge), so clip the closing back to
                            // it: the seam knit lives inside it (kept), hole-bridging and outward overrun do
                            // not (rejected).
                            if (!il_reduced_inner.empty())
                                infill_contour = intersection_ex(infill_contour, union_ex(il_reduced_inner));
                            // Perimeters always win: re-clip the fill off the additive wall footprint so the
                            // closing cannot ride it onto an injected wall. The fill stage re-adds the normal
                            // infill-overlap afterward.
                            if (!il_wall_footprint.empty())
                                infill_contour = diff_ex(infill_contour, il_wall_footprint);
                        }
                        infill_contour = expolygons_simplify(infill_contour, params.scaled_resolution);

                        // Diagnostic: the final fill region after interlocking (debug only).
                        if (pg_dbg_active())
                            dbg_wkt_expolys(dbg_z, "infill_contour", infill_contour);

                        dbg_il_inner_contour(dbg_z, il_inner, geometric_inner, infill_contour);
                    }
                }
            }
        }
    }
skip_interlocking:
    // ===================== END INTERLOCKING =====================

    // Debug: log infill contour after interlocking consumed space
    dbg_perim_contours("INNER_CONTOUR", dbg_z, params.layer_id, infill_contour, "after_interlocking");

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset = (loop_number < 0)    ? 0
                    : (loop_number == 0) ?
                                         // one loop
                        ext_perimeter_spacing
                                         :
                                         // two or more loops?
                        perimeter_spacing;

    coord_t inset_base = inset; // save pre-overlap value
    inset = coord_t(scale_(params.config.get_abs_value("infill_overlap", unscale<double>(inset))));
    Polygons pp;
    for (ExPolygon &ex : infill_contour)
    {
        // Drop degenerate ExPolygons where the effective gap between contour
        // and holes is too narrow for infill. The simplify/union/offset pipeline
        // converts ExPolygons to raw Polygons, losing contour/hole winding;
        // for nearly-coincident boundaries this produces a full disc that covers
        // the entire perimeter region. Applies to any hole count - interlocking
        // can consume interior space leaving multi-hole thin shells.
        if (!ex.holes.empty())
        {
            double ex_area = std::abs(ex.area());
            BoundingBox ex_bb = ex.contour.bounding_box();
            double max_dim = double(std::max(ex_bb.size().x(), ex_bb.size().y()));
            double effective_gap = (max_dim > 0) ? (ex_area / max_dim) : 0;
            if (effective_gap < double(solid_infill_spacing))
                continue;
        }
        ex.simplify_p(params.scaled_resolution, &pp);
    }
    // Clip simplified polygons against the original contour to prevent
    // simplification-induced overshoot. Douglas-Peucker chord-cuts across
    // concavities, enlarging the polygon. This clips that enlargement while
    // preserving the simplified topology that offset2_ex needs.
    pp = intersection(pp, to_polygons(infill_contour));
    // Debug: log simplification point count
    if (pg_dbg_active())
    {
        size_t pp_pts = 0;
        for (const Polygon &p : pp)
            pp_pts += p.points.size();
        size_t ic_pts = 0;
        for (const ExPolygon &ep : infill_contour)
        {
            ic_pts += ep.contour.points.size();
            for (const Polygon &h : ep.holes)
                ic_pts += h.points.size();
        }
        dbg_log(Slic3r::DBG_PERIMETERS, dbg_z, "PERIM", "SIMPLIFY before=%zu after=%zu resolution=%.4fmm", ic_pts,
                pp_pts, unscaled<double>(params.scaled_resolution));
    }
    // collapse too narrow infill areas
    const auto min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    dbg_perim_overlap(dbg_z, params.layer_id, loop_number, spacing, inset_base, inset, min_perimeter_infill_spacing);
    // append infill areas to fill_surfaces
    ExPolygons infill_areas = offset2_ex(union_ex(pp), float(-min_perimeter_infill_spacing / 2.),
                                         float(inset + min_perimeter_infill_spacing / 2.));
    dbg_perim_contours("INFILL_AREA", dbg_z, params.layer_id, infill_areas, "after_overlap");
    // Debug: compute overshoot - infill area that extends beyond innermost perimeter inner edge
    if (pg_dbg_active() && !infill_areas.empty() && !infill_contour.empty())
    {
        ExPolygons overshoot = diff_ex(infill_areas, infill_contour);
        if (!overshoot.empty())
        {
            double overshoot_area = 0;
            for (const ExPolygon &ep : overshoot)
                overshoot_area += std::abs(ep.area()) * 1e-12;
            BoundingBox obb = get_extents(overshoot);
            dbg_log(Slic3r::DBG_PERIMETERS, dbg_z, "PERIM",
                    "OVERSHOOT ep=%zu area=%8.4fmm2 bbox=(%.2f,%.2f)-(%.2f,%.2f)", overshoot.size(), overshoot_area,
                    unscaled<double>(obb.min.x()), unscaled<double>(obb.min.y()), unscaled<double>(obb.max.x()),
                    unscaled<double>(obb.max.y()));
            for (size_t i = 0; i < overshoot.size(); i++)
            {
                const ExPolygon &ep = overshoot[i];
                double a = std::abs(ep.area()) * 1e-12;
                BoundingBox epbb = get_extents(ep);
                dbg_log(Slic3r::DBG_PERIMETERS, dbg_z, "PERIM",
                        "  OVERSHOOT [%zu] area=%8.4fmm2 pts=%zu "
                        "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                        i, a, ep.contour.points.size(), unscaled<double>(epbb.min.x()), unscaled<double>(epbb.min.y()),
                        unscaled<double>(epbb.max.x()), unscaled<double>(epbb.max.y()));
            }
        }
    }

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers)
    {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters,
              filled_area] = generate_extra_perimeters_over_overhangs(infill_areas, lower_slices_polygons_cache,
                                                                      loop_number + 1, params.overhang_flow,
                                                                      params.scaled_resolution, params.object_config,
                                                                      params.print_config);
        if (!extra_perimeters.empty())
        {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection &>(
                *out_loops.entities.back());
            ExtrusionEntitiesPtr old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters)
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }

    // Depth-limit serpentine: keep the solid/sparse fill from overrunning the band
    // beyond the configured infill/perimeters overlap (see serp_fill_clip).
    if (serp_band_handoff && serp_fill_clip > 0)
        infill_areas = intersection_ex(infill_areas, offset_ex(surface.expolygon, -float(serp_fill_clip)));

    append(out_fill_expolygons, std::move(infill_areas));
}

} // namespace Slic3r
