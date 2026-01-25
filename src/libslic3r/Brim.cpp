///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <boost/thread/lock_guard.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <mutex>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstddef>

#include "ClipperUtils.hpp"
#include "ClipperZUtils.hpp"
#include "EdgeGrid.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "ShortestPath.hpp"
#include "libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"

#if defined(BRIM_DEBUG_TO_SVG)
#include "SVG.hpp"
#endif

namespace Slic3r
{

static void append_and_translate(ExPolygons &dst, const ExPolygons &src, const PrintInstance &instance)
{
    size_t dst_idx = dst.size();
    expolygons_append(dst, src);
    for (; dst_idx < dst.size(); ++dst_idx)
        dst[dst_idx].translate(instance.shift.x(), instance.shift.y());
}

static void append_and_translate(Polygons &dst, const Polygons &src, const PrintInstance &instance)
{
    size_t dst_idx = dst.size();
    polygons_append(dst, src);
    for (; dst_idx < dst.size(); ++dst_idx)
        dst[dst_idx].translate(instance.shift.x(), instance.shift.y());
}

static float max_brim_width(const SpanOfConstPtrs<PrintObject> &objects)
{
    assert(!objects.empty());
    return float(std::accumulate(objects.begin(), objects.end(), 0.,
                                 [](double partial_result, const PrintObject *object)
                                 {
                                     return std::max(partial_result, object->config().brim_type == btNoBrim
                                                                         ? 0.
                                                                         : object->config().brim_width.value);
                                 }));
}

// Generate mouse ears at sharp corners
// Ported from SuperSlicer via OrcaSlicer
// Credits: SuperSlicer (@supermerill), OrcaSlicer (@Noisyfox, @SoftFever)
static ExPolygons make_brim_ears(ExPolygons &obj_expoly, coord_t size_ear, coord_t ear_detection_length,
                                 coordf_t brim_ears_max_angle, bool is_outer_brim)
{
    ExPolygons mouse_ears_ex;
    if (size_ear <= 0)
    {
        return mouse_ears_ex;
    }
    // Detect places to put ears
    const coordf_t angle_threshold = (180 - brim_ears_max_angle) * PI / 180.0;
    Points pt_ears;
    for (ExPolygon &poly : obj_expoly)
    {
        Polygon decimated_polygon = poly.contour;
        if (ear_detection_length > 0)
        {
            // decimate polygon
            Points points = poly.contour.points;
            points.push_back(points.front());
            points = MultiPoint::douglas_peucker(points, ear_detection_length);
            if (points.size() > 4)
            { // don't decimate if it's going to be below 4 points, as it's surely enough to fill everything anyway
                points.erase(points.end() - 1);
                decimated_polygon.points = points;
            }
        }

        append(pt_ears, is_outer_brim ? decimated_polygon.convex_points(angle_threshold)
                                      : decimated_polygon.concave_points(angle_threshold));
    }

    // Then add ears
    // create ear pattern
    Polygon point_round;
    for (size_t i = 0; i < POLY_SIDE_COUNT; i++)
    {
        double angle = (2.0 * PI * i) / POLY_SIDE_COUNT;
        point_round.points.emplace_back(size_ear * cos(angle), size_ear * sin(angle));
    }

    // create ears
    for (Point &pt : pt_ears)
    {
        mouse_ears_ex.emplace_back();
        mouse_ears_ex.back().contour = point_round;
        mouse_ears_ex.back().contour.translate(pt);
    }

    return mouse_ears_ex;
}

struct PaintedEarResult
{
    ExPolygons standard_ears; // Ears clipped normally (for <= 0% overlap)
    ExPolygons overlap_ears;  // Ears with positive overlap (uncliped)
};

static PaintedEarResult make_brim_ears_painted(const PrintObject *object, const Print &print, float brim_separation,
                                               const ExPolygons &outer_brim_expoly,
                                               const ExPolygons &bottom_layer_expolygons)
{
    PaintedEarResult result;
    const BrimPoints &brim_ear_points = object->model_object()->brim_points;

    if (brim_ear_points.empty())
    {
        return result;
    }

    const Geometry::Transformation &trsf = object->model_object()->instances[0]->get_transformation();
    Transform3d model_trsf = trsf.get_matrix_no_offset();
    const Point &center_offset = object->center_offset();
    model_trsf = model_trsf.pretranslate(
        Vec3d(-unscale<double>(center_offset.x()), -unscale<double>(center_offset.y()), 0));

    const Flow flow = print.brim_flow();
    const double flowWidth = flow.spacing();
    const float scaled_flow_spacing = flow.scaled_spacing();

    // Use the actual calculated width from first layer external perimeter
    float external_perimeter_width = flow.width(); // Default fallback
    if (!object->layers().empty() && !object->layers().front()->regions().empty())
    {
        // Get the actual first layer external perimeter flow width
        external_perimeter_width = object->layers().front()->regions().front()->flow(frExternalPerimeter).width();
    }

    // Returns the hole polygon if ear is inside a hole, nullptr otherwise
    auto find_containing_hole = [&bottom_layer_expolygons](const Point &ear_center) -> const Polygon *
    {
        // The ear_center is already in object-local coordinates (via model_trsf)
        // The bottom_layer_expolygons are also in object-local coordinates
        // So we can check directly without any instance transforms
        for (const ExPolygon &expoly : bottom_layer_expolygons)
        {
            // First check if point is inside the outer contour
            if (expoly.contour.contains(ear_center))
            {
                // Now check if it's inside any hole
                for (const Polygon &hole : expoly.holes)
                {
                    if (hole.points.empty())
                        continue;

                    BoundingBox hole_bbox(hole.points);
                    // Check if point is in bounding box first (quick reject)
                    if (!hole_bbox.contains(ear_center))
                        continue;

                    // Use Clipper2's point-in-polygon (handles CW/CCW correctly)
                    Clipper2Lib::Path64 hole_path = Slic3rPoints_to_ClipperPath(hole.points);
                    Clipper2Lib::Point64 ear_pt(ear_center.x(), ear_center.y());
                    Clipper2Lib::PointInPolygonResult pip_result = Clipper2Lib::PointInPolygon(ear_pt, hole_path);
                    int pip_result_int = (pip_result == Clipper2Lib::PointInPolygonResult::IsInside) ? 1
                                         : (pip_result == Clipper2Lib::PointInPolygonResult::IsOn)   ? -1
                                                                                                     : 0;

                    // WORKAROUND: If in bbox but PointInPolygon=0, use bbox with margin as fallback
                    // This handles cases where highly tessellated holes cause PointInPolygon to fail
                    if (pip_result_int == 0)
                    {
                        // Shrink bbox by small margin (1mm) and check again
                        BoundingBox shrunk_bbox = hole_bbox;
                        shrunk_bbox.min += Point(scale_(1.0), scale_(1.0));
                        shrunk_bbox.max -= Point(scale_(1.0), scale_(1.0));
                        if (shrunk_bbox.contains(ear_center))
                            return &hole;
                    }
                    else
                    {
                        // pip_result: 1 = inside, -1 = on boundary
                        return &hole; // Found it - ear is inside this hole
                    }
                }
            }
        }
        return nullptr; // Not inside any hole
    };

    // Create ears at each manually-placed point
    for (const auto &pt : brim_ear_points)
    {
        Vec3f world_pos = pt.transform(trsf.get_matrix());
        if (world_pos.z() > 0.01f)
            continue; // Skip points not on first layer

        // The default behavior (0%) already includes some overlap (~20% of perimeter width)
        // So we need to adjust relative to that default:
        //   0% = default overlap behavior (already overlaps ~20%)
        //   +100% = add one full perimeter width MORE overlap
        //   -100% = reduce overlap by one full perimeter width (may create gap)
        //
        // The default overlap appears to be approximately 0.2 * external_perimeter_width
        // based on visual inspection. So at 0%, we want overlap_distance = 0 (no change from default)
        float overlap_distance = scale_((pt.overlap_percent / 100.0f) * external_perimeter_width);

        // For clipping, we need the model perimeter offset by the overlap distance
        // Positive overlap = negative offset (INTO the model)
        // Negative overlap = positive offset (AWAY from model)
        float clip_offset = -overlap_distance;

        // Calculate half perimeter width for clipping calculation
        float half_perimeter_width = external_perimeter_width / 2.0f;

        // Get the original model contour (need to reverse the brim_separation offset from outer_brim_expoly)
        // outer_brim_expoly = model + brim_separation
        // So: model = outer_brim_expoly - brim_separation
        Polygons model_contour = offset(to_polygons(outer_brim_expoly), -brim_separation, JoinType::Square);

        // For true 0% overlap, the brim should touch the external perimeter exactly
        // The external perimeter's outer edge is at: model + (external_perimeter_width / 2)
        // So for 0% overlap, we need to clip at this position

        // Calculate where the external perimeter's outer edge actually is
        Polygons external_perimeter_outer_edge = offset(model_contour, scale_(half_perimeter_width), JoinType::Square);

        // Now apply the user's overlap adjustment
        // At 0%, we clip at the external perimeter's outer edge (no overlap)
        // Positive values move the clip boundary inward (creating overlap)
        // Negative values move the clip boundary outward (creating a gap)
        Polygons contour_for_this_ear = offset(external_perimeter_outer_edge, clip_offset, JoinType::Square);

        // Create full circular ear at user-specified diameter (head_front_radius is in mm)
        float ear_radius_mm = pt.head_front_radius;
        coord_t ear_radius_scaled = scale_(ear_radius_mm);

        // Create circular ear pattern
        Polygon point_round;
        for (size_t i = 0; i < POLY_SIDE_COUNT; i++)
        {
            double angle = (2.0 * PI * i) / POLY_SIDE_COUNT;
            point_round.points.emplace_back(ear_radius_scaled * cos(angle), ear_radius_scaled * sin(angle));
        }

        // Transform to model coordinates
        Vec3f pos = pt.transform(model_trsf);
        int32_t pt_x = scale_(pos.x());
        int32_t pt_y = scale_(pos.y());
        Point ear_center(pt_x, pt_y);
        point_round.translate(ear_center);

        // Check if circle intersects ANY holes - if so, generate inner brims
        Polygons circle_as_polygons{point_round};

        for (const ExPolygon &expoly : bottom_layer_expolygons)
        {
            for (const Polygon &hole : expoly.holes)
            {
                // Check if circle intersects this hole
                Polygons hole_intersection = intersection(circle_as_polygons, Polygons{hole});
                if (hole_intersection.empty())
                    continue; // No intersection with this hole

                // This ear intersects THIS HOLE - generate inner brim logic
                // Create the brim zone by shrinking the hole
                Polygons hole_brim_boundary = offset(hole, -brim_separation, JoinType::Square);

                if (hole_brim_boundary.empty())
                    continue; // Hole too small, skip this hole

                if (pt.overlap_percent > 0)
                {
                    // Positive overlap: ear can extend into the hole wall
                    // For a hole, the perimeter is printed ON the hole boundary
                    // Perimeter inner edge (toward center, where brim touches) = hole + half_perimeter
                    // At 0% overlap: clip at perimeter inner edge = hole + half_perimeter
                    // At 100% overlap: clip at hole - half_perimeter (full perimeter width into wall)

                    // Calculate the overlap clip boundary
                    // Start from hole, offset by (half_perimeter - overlap_distance)
                    float hole_offset = scale_(half_perimeter_width) - overlap_distance;
                    Polygons hole_overlap_boundary = offset(hole, hole_offset, JoinType::Square);

                    // Clip the ear against the overlap boundary
                    ExPolygons full_ear;
                    if (hole_overlap_boundary.empty())
                    {
                        // Fallback if offset collapsed the hole entirely
                        full_ear = intersection_ex(ExPolygons{ExPolygon(point_round)}, hole_brim_boundary);
                    }
                    else
                    {
                        full_ear = intersection_ex(ExPolygons{ExPolygon(point_round)}, hole_overlap_boundary);
                    }

                    // Everything goes to overlap_ears (it's all overlap-protected)
                    append(result.overlap_ears, union_ex(full_ear));
                }
                else
                {
                    // Zero or negative overlap
                    // For a hole, the perimeter is printed ON the hole boundary
                    // Perimeter inner edge (toward center) = hole + half_perimeter
                    // This is the edge we want to touch at 0% overlap
                    Polygons hole_perimeter_inner_edge = offset(hole, scale_(half_perimeter_width), JoinType::Square);

                    // Apply overlap adjustment
                    // For negative overlap: we want a gap toward center
                    // overlap_distance is negative, so -overlap_distance is positive = grows hole = creates gap
                    float hole_clip_offset = -overlap_distance;

                    Polygons hole_clip_boundary;
                    if (hole_perimeter_inner_edge.empty())
                    {
                        hole_clip_boundary = hole_brim_boundary;
                    }
                    else if (std::abs(hole_clip_offset) < 1.0f)
                    {
                        // For very small clip offsets (including 0), use perimeter inner edge directly
                        hole_clip_boundary = hole_perimeter_inner_edge;
                    }
                    else
                    {
                        hole_clip_boundary = offset(hole_perimeter_inner_edge, hole_clip_offset, JoinType::Square);
                    }

                    ExPolygons clipped = intersection_ex(ExPolygons{ExPolygon(point_round)}, hole_clip_boundary);
                    append(result.standard_ears, clipped);
                }
            } // End loop over holes
        } // End loop over ExPolygons

        // Don't skip outer logic - check if circle intersects outer contours too
        bool intersects_outer = false;
        for (const ExPolygon &expoly : bottom_layer_expolygons)
        {
            Polygons outer_intersection = intersection(circle_as_polygons, Polygons{expoly.contour});
            if (!outer_intersection.empty())
            {
                intersects_outer = true;
                break;
            }
        }

        if (!intersects_outer)
        {
            // Circle doesn't intersect any outer boundaries, skip outer brim generation
            continue;
        }

        if (pt.overlap_percent > 0)
        {
            // For positive overlap, we need special handling
            // Create the full ear shape
            ExPolygon full_ear(point_round);

            // For positive overlap, clip against the brim separation boundary (outer edge)
            // but NOT against the model itself - we want to keep the overlap
            ExPolygons clipped_ear = diff_ex(ExPolygons{full_ear}, to_polygons(outer_brim_expoly));

            // Now add the overlap region - the part that goes INTO the model
            // This is the intersection of the ear with the band between brim_separation and the overlap limit
            ExPolygons overlap_region = intersection_ex(ExPolygons{full_ear}, to_polygons(outer_brim_expoly));
            overlap_region = diff_ex(overlap_region, contour_for_this_ear);

            // Combine and add to overlap_ears collection
            append(clipped_ear, overlap_region);
            append(result.overlap_ears, union_ex(clipped_ear));
        }
        else
        {
            // For zero or negative overlap, use standard clipping
            ExPolygons clipped_ear = diff_ex(ExPolygons{ExPolygon(point_round)}, contour_for_this_ear);
            append(result.standard_ears, clipped_ear);
        }
    }

    return result;
}

// Returns ExPolygons of the bottom layer of the print object after elephant foot compensation.
static ExPolygons get_print_object_bottom_layer_expolygons(const PrintObject &print_object)
{
    ExPolygons ex_polygons;
    for (LayerRegion *region : print_object.layers().front()->regions())
        Slic3r::append(ex_polygons, closing_ex(region->slices().surfaces, float(SCALED_EPSILON)));
    return ex_polygons;
}

// Returns ExPolygons of bottom layer for every print object in Print after elephant foot compensation.
static std::vector<ExPolygons> get_print_bottom_layers_expolygons(const Print &print)
{
    std::vector<ExPolygons> bottom_layers_expolygons;
    bottom_layers_expolygons.reserve(print.objects().size());
    for (const PrintObject *object : print.objects())
        bottom_layers_expolygons.emplace_back(get_print_object_bottom_layer_expolygons(*object));

    return bottom_layers_expolygons;
}

static ConstPrintObjectPtrs get_top_level_objects_with_brim(const Print &print,
                                                            const std::vector<ExPolygons> &bottom_layers_expolygons)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    Polygons islands;
    ConstPrintObjectPtrs island_to_object;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
    {
        const PrintObject *object = print.objects()[print_object_idx];
        Polygons islands_object;
        islands_object.reserve(bottom_layers_expolygons[print_object_idx].size());
        for (const ExPolygon &ex_poly : bottom_layers_expolygons[print_object_idx])
            islands_object.emplace_back(ex_poly.contour);

        islands.reserve(islands.size() + object->instances().size() * islands_object.size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : islands_object)
            {
                islands.emplace_back(poly);
                islands.back().translate(instance.shift);
                island_to_object.emplace_back(object);
            }
    }
    assert(islands.size() == island_to_object.size());

    ClipperZUtils::ZPaths islands_clip;
    islands_clip.reserve(islands.size());
    for (const Polygon &poly : islands)
    {
        islands_clip.emplace_back();
        ClipperZUtils::ZPath &island_clip = islands_clip.back();
        island_clip.reserve(poly.points.size());
        int island_idx = int(&poly - &islands.front());
        // The Z coordinate carries index of the island used to get the pointer to the object.
        for (const Point &pt : poly.points)
            island_clip.emplace_back(pt.x(), pt.y(), island_idx + 1);
    }

    // Init Clipper
    Clipper2Lib::Clipper64 clipper;
    // Set Z callback to preserve island indices at intersections
    clipper.SetZCallback(
        [](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top, const Clipper2Lib::Point64 &e2bot,
           const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
        {
            // At intersection, use the non-zero Z value from either edge
            pt.z = (e1bot.z != 0) ? e1bot.z : (e1top.z != 0) ? e1top.z : (e2bot.z != 0) ? e2bot.z : e2top.z;
        });

    // Convert ZPaths to Paths64 with Z preserved
    Clipper2Lib::Paths64 islands_paths = ClipperZUtils::zpaths_to_paths64(islands_clip);
    clipper.AddSubject(islands_paths);

    // Execute union operation to construct polytree
    Clipper2Lib::PolyTree64 islands_polytree;
    //FIXME likely pftNonZero or ptfPositive would be better. Why are we using ptfEvenOdd for Unions?
    clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::EvenOdd, islands_polytree);

    // Just read the Z value directly from the polygon points
    std::unordered_set<size_t> processed_objects_idx;
    ConstPrintObjectPtrs top_level_objects_with_brim;
    for (size_t i = 0; i < islands_polytree.Count(); ++i)
    {
        const Clipper2Lib::PolyPath64 *child = islands_polytree[i];
        if (!child->Polygon().empty())
        {
            // With USINGZ, Z values are preserved - find first non-zero Z
            for (const auto &pt : child->Polygon())
            {
                if (pt.z != 0)
                {
                    size_t obj_idx = pt.z - 1;
                    if (processed_objects_idx.find(island_to_object[obj_idx]->id().id) == processed_objects_idx.end())
                    {
                        top_level_objects_with_brim.emplace_back(island_to_object[obj_idx]);
                        processed_objects_idx.insert(island_to_object[obj_idx]->id().id);
                        break;
                    }
                }
            }
        }
    }
    return top_level_objects_with_brim;
}

static Polygons top_level_outer_brim_islands(const ConstPrintObjectPtrs &top_level_objects_with_brim,
                                             const double scaled_resolution)
{
    Polygons islands;
    for (const PrintObject *object : top_level_objects_with_brim)
    {
        if (!object->has_brim())
            continue;

        //FIXME how about the brim type?
        auto brim_separation = float(scale_(object->config().brim_separation.value));
        Polygons islands_object;
        for (const ExPolygon &ex_poly : get_print_object_bottom_layer_expolygons(*object))
        {
            Polygons contour_offset = offset(ex_poly.contour, brim_separation, JoinType::Square);
            for (Polygon &poly : contour_offset)
                poly.douglas_peucker(scaled_resolution);

            polygons_append(islands_object, std::move(contour_offset));
        }

        for (const PrintInstance &instance : object->instances())
            append_and_translate(islands, islands_object, instance);
    }
    return islands;
}

struct BrimAreas
{
    ExPolygons clippable;         // Areas that will be clipped by no_brim_area
    ExPolygons overlap_protected; // Areas with positive overlap that bypass clipping
};

static BrimAreas top_level_outer_brim_area(const Print &print, const ConstPrintObjectPtrs &top_level_objects_with_brim,
                                           const std::vector<ExPolygons> &bottom_layers_expolygons,
                                           const float no_brim_offset)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    std::unordered_set<size_t> top_level_objects_idx;
    top_level_objects_idx.reserve(top_level_objects_with_brim.size());
    for (const PrintObject *object : top_level_objects_with_brim)
        top_level_objects_idx.insert(object->id().id);

    ExPolygons brim_area;         // Will become result.clippable
    ExPolygons overlap_protected; // Will become result.overlap_protected
    ExPolygons no_brim_area;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
    {
        const PrintObject *object = print.objects()[print_object_idx];
        const BrimType brim_type = object->config().brim_type.value;
        const float brim_separation = scale_(object->config().brim_separation.value);
        const float brim_width = scale_(object->config().brim_width.value);
        const bool is_top_outer_brim = top_level_objects_idx.find(object->id().id) != top_level_objects_idx.end();
        const bool use_auto_brim_ears = brim_type == BrimType::btEar;
        const bool use_painted_brim_ears = brim_type == BrimType::btPainted;
        const bool use_brim_ears = use_auto_brim_ears || use_painted_brim_ears;
        const bool has_inner_brim = brim_type == btInnerOnly || brim_type == btOuterAndInner || use_brim_ears;
        const bool has_outer_brim = brim_type == btOuterOnly || brim_type == btOuterAndInner || use_brim_ears;
        const coord_t ear_detection_length = scale_(object->config().brim_ears_detection_length.value);
        const coordf_t brim_ears_max_angle = object->config().brim_ears_max_angle.value;
        const double flow_width = print.brim_flow().spacing();

        ExPolygons brim_area_object;
        ExPolygons overlap_protected_object;
        ExPolygons no_brim_area_object;
        for (const ExPolygon &ex_poly : bottom_layers_expolygons[print_object_idx])
        {
            if (has_outer_brim && is_top_outer_brim)
            {
                Polygons contour_polygons{ex_poly.contour};
                ExPolygons outer_brim_expoly = offset_ex(contour_polygons, brim_separation, JoinType::Square);
                if (use_painted_brim_ears)
                {
                    // Manual placement: use user-placed points with their specified sizes
                    PaintedEarResult painted_ears = make_brim_ears_painted(object, print, brim_separation,
                                                                           outer_brim_expoly,
                                                                           bottom_layers_expolygons[print_object_idx]);
                    append(brim_area_object, painted_ears.standard_ears);
                    append(overlap_protected_object, painted_ears.overlap_ears);
                }
                else if (use_auto_brim_ears)
                {
                    // Auto-generate: detect sharp corners
                    coord_t size_ear = brim_width;
                    append(brim_area_object, diff_ex(make_brim_ears(outer_brim_expoly, size_ear, ear_detection_length,
                                                                    brim_ears_max_angle, true),
                                                     outer_brim_expoly));
                }
                else
                {
                    // Regular brim
                    append(brim_area_object,
                           diff_ex(offset(ex_poly.contour, brim_width + brim_separation, JoinType::Square),
                                   outer_brim_expoly));
                }
            }

            // After 7ff76d07684858fd937ef2f5d863f105a10f798e offset and shrink don't work with CW polygons (holes), so let's make it CCW.
            Polygons ex_poly_holes_reversed = ex_poly.holes;
            polygons_reverse(ex_poly_holes_reversed);
            if (!has_inner_brim)
                append(no_brim_area_object, shrink_ex(ex_poly_holes_reversed, no_brim_offset, JoinType::Square));

            if (!has_outer_brim)
                append(no_brim_area_object,
                       diff_ex(offset(ex_poly.contour, no_brim_offset, JoinType::Square), ex_poly_holes_reversed));

            // For painted ears, don't add ANY no-brim restrictions (they handle their own clipping/overlap)
            if (!use_painted_brim_ears)
            {
                if (has_inner_brim || has_outer_brim)
                    append(no_brim_area_object,
                           offset_ex(ExPolygon(ex_poly.contour), brim_separation, JoinType::Square));
                no_brim_area_object.emplace_back(ex_poly.contour);
            }
        }

        for (const PrintInstance &instance : object->instances())
        {
            append_and_translate(brim_area, brim_area_object, instance);
            append_and_translate(overlap_protected, overlap_protected_object, instance);
            append_and_translate(no_brim_area, no_brim_area_object, instance);
        }
    }

    BrimAreas result;

    // When brim ears are used, brim_area can contain hundreds of separate ExPolygons (one per ear).
    // This causes severe performance issues in subsequent Clipper2 operations (30+ seconds).
    // Union merges overlapping ears and consolidates fragments into a unified polygon set.
    brim_area = union_ex(brim_area);
    overlap_protected = union_ex(overlap_protected); // Also merge overlap-protected ears

    result.clippable = diff_ex(brim_area, no_brim_area); // Standard clipping for normal areas
    result.overlap_protected = overlap_protected;        // No clipping for overlap areas

    return result;
}

// Return vector of booleans indicated if polygons from bottom_layers_expolygons contain another polygon or not.
// Every ExPolygon is counted as several Polygons (contour and holes). Contour polygon is always processed before holes.
static std::vector<bool> has_polygons_nothing_inside(const Print &print,
                                                     const std::vector<ExPolygons> &bottom_layers_expolygons)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    Polygons islands;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
    {
        const PrintObject *object = print.objects()[print_object_idx];
        const Polygons islands_object = to_polygons(bottom_layers_expolygons[print_object_idx]);

        islands.reserve(islands.size() + object->instances().size() * islands_object.size());
        for (const PrintInstance &instance : object->instances())
            append_and_translate(islands, islands_object, instance);
    }

    ClipperZUtils::ZPaths islands_clip;
    islands_clip.reserve(islands.size());
    for (const Polygon &poly : islands)
    {
        size_t island_idx = &poly - &islands.front();
        ClipperZUtils::ZPath island_clip;
        for (const Point &pt : poly.points)
            island_clip.emplace_back(pt.x(), pt.y(), island_idx + 1);
        islands_clip.emplace_back(island_clip);
    }

    Clipper2Lib::Clipper64 clipper;
    // Set Z callback to preserve island indices at intersections
    clipper.SetZCallback(
        [](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top, const Clipper2Lib::Point64 &e2bot,
           const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
        {
            // At intersection, use the non-zero Z value from either edge
            pt.z = (e1bot.z != 0) ? e1bot.z : (e1top.z != 0) ? e1top.z : (e2bot.z != 0) ? e2bot.z : e2top.z;
        });

    // Convert ZPaths to Paths64 with Z preserved
    Clipper2Lib::Paths64 islands_paths = ClipperZUtils::zpaths_to_paths64(islands_clip);
    clipper.AddSubject(islands_paths);
    Clipper2Lib::PolyTree64 islands_polytree;
    clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::EvenOdd, islands_polytree);

    std::vector<bool> has_nothing_inside(islands.size());
    std::function<void(const Clipper2Lib::PolyPath64 &)> check_contours =
        [&check_contours, &has_nothing_inside](const Clipper2Lib::PolyPath64 &parent_node) -> void
    {
        // Iterate children using Count/operator[]
        for (size_t i = 0; i < parent_node.Count(); ++i)
            check_contours(*parent_node[i]);

        // With USINGZ, Z values are preserved - just read the island index from Z
        if (parent_node.Count() == 0 && !parent_node.Polygon().empty())
        {
            // Find first non-zero Z to identify the island
            for (const auto &pt : parent_node.Polygon())
            {
                if (pt.z > 0 && size_t(pt.z - 1) < has_nothing_inside.size())
                {
                    has_nothing_inside[pt.z - 1] = true;
                    break;
                }
            }
        }
    };

    check_contours(islands_polytree);
    return has_nothing_inside;
}

// INNERMOST means that ExPolygon doesn't contain any other ExPolygons.
// NORMAL is for other cases.
enum class InnerBrimType
{
    NORMAL,
    INNERMOST
};

struct InnerBrimExPolygons
{
    ExPolygons brim_area;
    InnerBrimType type = InnerBrimType::NORMAL;
    double brim_width = 0.;
};

static std::vector<InnerBrimExPolygons> inner_brim_area(const Print &print,
                                                        const ConstPrintObjectPtrs &top_level_objects_with_brim,
                                                        const std::vector<ExPolygons> &bottom_layers_expolygons,
                                                        const float no_brim_offset)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    std::vector<bool> has_nothing_inside = has_polygons_nothing_inside(print, bottom_layers_expolygons);
    std::unordered_set<size_t> top_level_objects_idx;
    top_level_objects_idx.reserve(top_level_objects_with_brim.size());
    for (const PrintObject *object : top_level_objects_with_brim)
        top_level_objects_idx.insert(object->id().id);

    std::vector<ExPolygons> brim_area_innermost(print.objects().size());
    ExPolygons brim_area;
    ExPolygons no_brim_area;
    Polygons holes_reversed;

    // polygon_idx must correspond to idx generated inside has_polygons_nothing_inside()
    size_t polygon_idx = 0;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
    {
        const PrintObject *object = print.objects()[print_object_idx];
        const BrimType brim_type = object->config().brim_type.value;
        const float brim_separation = scale_(object->config().brim_separation.value);
        const float brim_width = scale_(object->config().brim_width.value);
        const bool top_outer_brim = top_level_objects_idx.find(object->id().id) != top_level_objects_idx.end();

        ExPolygons brim_area_innermost_object;
        ExPolygons brim_area_object;
        ExPolygons no_brim_area_object;
        Polygons holes_reversed_object;
        for (const ExPolygon &ex_poly : bottom_layers_expolygons[print_object_idx])
        {
            if (brim_type == BrimType::btOuterOnly || brim_type == BrimType::btOuterAndInner)
            {
                if (top_outer_brim)
                    no_brim_area_object.emplace_back(ex_poly);
                else
                    append(brim_area_object,
                           diff_ex(offset(ex_poly.contour, brim_width + brim_separation, JoinType::Square),
                                   offset(ex_poly.contour, brim_separation, JoinType::Square)));
            }

            // After 7ff76d07684858fd937ef2f5d863f105a10f798e offset and shrink don't work with CW polygons (holes), so let's make it CCW.
            Polygons ex_poly_holes_reversed = ex_poly.holes;
            polygons_reverse(ex_poly_holes_reversed);
            for ([[maybe_unused]] const PrintInstance &instance : object->instances())
            {
                ++polygon_idx; // Increase idx because of the contour of the ExPolygon.

                if (brim_type == BrimType::btInnerOnly || brim_type == BrimType::btOuterAndInner)
                    for (const Polygon &hole : ex_poly_holes_reversed)
                    {
                        size_t hole_idx = &hole - &ex_poly_holes_reversed.front();
                        if (has_nothing_inside[polygon_idx + hole_idx])
                            append(brim_area_innermost_object, shrink_ex({hole}, brim_separation, JoinType::Square));
                        else
                            append(brim_area_object,
                                   diff_ex(shrink_ex({hole}, brim_separation, JoinType::Square),
                                           shrink_ex({hole}, brim_width + brim_separation, JoinType::Square)));
                    }

                polygon_idx += ex_poly.holes.size(); // Increase idx for every hole of the ExPolygon.
            }

            if (brim_type == BrimType::btInnerOnly || brim_type == BrimType::btNoBrim)
                append(no_brim_area_object,
                       diff_ex(offset(ex_poly.contour, no_brim_offset, JoinType::Square), ex_poly_holes_reversed));

            if (brim_type == BrimType::btOuterOnly || brim_type == BrimType::btNoBrim)
                append(no_brim_area_object,
                       diff_ex(ex_poly.contour, shrink_ex(ex_poly_holes_reversed, no_brim_offset, JoinType::Square)));

            append(holes_reversed_object, ex_poly_holes_reversed);
        }
        append(no_brim_area_object,
               offset_ex(bottom_layers_expolygons[print_object_idx], brim_separation, JoinType::Square));

        for (const PrintInstance &instance : object->instances())
        {
            append_and_translate(brim_area_innermost[print_object_idx], brim_area_innermost_object, instance);
            append_and_translate(brim_area, brim_area_object, instance);
            append_and_translate(no_brim_area, no_brim_area_object, instance);
            append_and_translate(holes_reversed, holes_reversed_object, instance);
        }
    }
    assert(polygon_idx == has_nothing_inside.size());

    ExPolygons brim_area_innermost_merged;
    // Append all innermost brim areas.
    std::vector<InnerBrimExPolygons> brim_area_out;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
        if (const double brim_width = print.objects()[print_object_idx]->config().brim_width.value;
            !brim_area_innermost[print_object_idx].empty())
        {
            append(brim_area_innermost_merged, brim_area_innermost[print_object_idx]);
            brim_area_out.push_back(
                {std::move(brim_area_innermost[print_object_idx]), InnerBrimType::INNERMOST, brim_width});
        }

    // Append all normal brim areas.
    brim_area = union_ex(brim_area);
    brim_area_out.push_back({diff_ex(intersection_ex(to_polygons(std::move(brim_area)), holes_reversed), no_brim_area),
                             InnerBrimType::NORMAL});

    // Cut out a huge brim areas that overflows into the INNERMOST holes.
    brim_area_out.back().brim_area = diff_ex(brim_area_out.back().brim_area, brim_area_innermost_merged);
    return brim_area_out;
}

// Flip orientation of open polylines to minimize travel distance.
static void optimize_polylines_by_reversing(Polylines *polylines)
{
    for (size_t poly_idx = 1; poly_idx < polylines->size(); ++poly_idx)
    {
        const Polyline &prev = (*polylines)[poly_idx - 1];
        Polyline &next = (*polylines)[poly_idx];

        if (!next.is_closed())
        {
            double dist_to_start = (next.first_point() - prev.last_point()).cast<double>().norm();
            double dist_to_end = (next.last_point() - prev.last_point()).cast<double>().norm();

            if (dist_to_end < dist_to_start)
                next.reverse();
        }
    }
}

static Polylines connect_brim_lines(Polylines &&polylines, const Polygons &brim_area, float max_connection_length)
{
    if (polylines.empty())
        return {};

    BoundingBox bbox = get_extents(polylines);
    bbox.merge(get_extents(brim_area));

    EdgeGrid::Grid grid(bbox.inflated(SCALED_EPSILON));
    grid.create(brim_area, polylines, coord_t(scale_(10.)));

    struct Visitor
    {
        explicit Visitor(const EdgeGrid::Grid &grid) : grid(grid) {}

        bool operator()(coord_t iy, coord_t ix)
        {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            this->intersect = false;
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                 ++it_contour_and_segment)
            {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, brim_line.a, brim_line.b))
                {
                    this->intersect = true;
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid &grid;
        Line brim_line;
        bool intersect = false;

    } visitor(grid);

    // Connect successive polylines if they are open, their ends are closer than max_connection_length.
    // Remove empty polylines.
    {
        // Skip initial empty lines.
        size_t poly_idx = 0;
        for (; poly_idx < polylines.size() && polylines[poly_idx].empty(); ++poly_idx)
            ;
        size_t end = ++poly_idx;
        double max_connection_length2 = Slic3r::sqr(max_connection_length);
        for (; poly_idx < polylines.size(); ++poly_idx)
        {
            Polyline &next = polylines[poly_idx];
            if (!next.empty())
            {
                Polyline &prev = polylines[end - 1];
                bool connect = false;
                if (!prev.is_closed() && !next.is_closed())
                {
                    double dist2 = (prev.last_point() - next.first_point()).cast<double>().squaredNorm();
                    if (dist2 <= max_connection_length2)
                    {
                        visitor.brim_line.a = prev.last_point();
                        visitor.brim_line.b = next.first_point();
                        // Shrink the connection line to avoid collisions with the brim centerlines.
                        visitor.brim_line.extend(-SCALED_EPSILON);
                        grid.visit_cells_intersecting_line(visitor.brim_line.a, visitor.brim_line.b, visitor);
                        connect = !visitor.intersect;
                    }
                }
                if (connect)
                {
                    append(prev.points, std::move(next.points));
                }
                else
                {
                    if (end < poly_idx)
                        polylines[end] = std::move(next);
                    ++end;
                }
            }
        }
        if (end < polylines.size())
            polylines.erase(polylines.begin() + int(end), polylines.end());
    }

    return std::move(polylines);
}

static void make_inner_brim(const Print &print, const ConstPrintObjectPtrs &top_level_objects_with_brim,
                            const std::vector<ExPolygons> &bottom_layers_expolygons, ExtrusionEntityCollection &brim)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    const auto scaled_resolution = scaled<double>(print.config().gcode_resolution.value);
    Flow flow = print.brim_flow();
    std::vector<InnerBrimExPolygons> inner_brims_ex = inner_brim_area(print, top_level_objects_with_brim,
                                                                      bottom_layers_expolygons,
                                                                      float(flow.scaled_spacing()));
    Polygons loops;
    std::mutex loops_mutex;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, inner_brims_ex.size()),
                      [&inner_brims_ex, &flow, &scaled_resolution, &loops,
                       &loops_mutex](const tbb::blocked_range<size_t> &range)
                      {
                          for (size_t brim_idx = range.begin(); brim_idx < range.end(); ++brim_idx)
                          {
                              const InnerBrimExPolygons &inner_brim_ex = inner_brims_ex[brim_idx];
                              auto num_loops = size_t(floor(inner_brim_ex.brim_width / flow.spacing()));
                              ExPolygons islands_ex = offset_ex(inner_brim_ex.brim_area,
                                                                -0.5f * float(flow.scaled_spacing()), JoinType::Square);
                              for (size_t i = 0; (inner_brim_ex.type == InnerBrimType::INNERMOST ? i < num_loops
                                                                                                 : !islands_ex.empty());
                                   ++i)
                              {
                                  for (ExPolygon &poly_ex : islands_ex)
                                      poly_ex.douglas_peucker(scaled_resolution);

                                  {
                                      boost::lock_guard<std::mutex> lock(loops_mutex);
                                      polygons_append(loops, to_polygons(islands_ex));
                                  }
                                  islands_ex = offset_ex(islands_ex, -float(flow.scaled_spacing()), JoinType::Square);
                              }
                          }
                      }); // end of parallel_for

    loops = union_pt_chained_outside_in(loops);
    std::reverse(loops.begin(), loops.end());
    extrusion_entities_append_loops(brim.entities, std::move(loops),
                                    ExtrusionAttributes{ExtrusionRole::Skirt,
                                                        ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                                      float(print.skirt_first_layer_height())}});
}

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area)
{
    const auto scaled_resolution = scaled<double>(print.config().gcode_resolution.value);
    Flow flow = print.brim_flow();
    std::vector<ExPolygons> bottom_layers_expolygons = get_print_bottom_layers_expolygons(print);
    ConstPrintObjectPtrs top_level_objects_with_brim = get_top_level_objects_with_brim(print, bottom_layers_expolygons);
    Polygons islands = top_level_outer_brim_islands(top_level_objects_with_brim, scaled_resolution);
    BrimAreas brim_areas = top_level_outer_brim_area(print, top_level_objects_with_brim, bottom_layers_expolygons,
                                                     float(flow.scaled_spacing()));
    ExPolygons islands_area_ex = brim_areas.clippable;
    // Overlap protected areas will be added later, after loop generation
    islands_area = to_polygons(islands_area_ex);

    // Instead of generating all loops then intersecting, process each brim area completely
    ExtrusionEntityCollection brim;

    // Combine all brim areas (standard and overlap-protected)
    ExPolygons all_brim_areas = brim_areas.clippable;
    append(all_brim_areas, brim_areas.overlap_protected);

    // Process each brim area independently with concentric loops
    for (const ExPolygon &brim_area : all_brim_areas)
    {
        try_cancel();

        ExPolygons current = {brim_area};

        // Generate concentric loops for this brim area
        while (!current.empty())
        {
            for (const ExPolygon &ex : current)
            {
                if (ex.contour.length() > flow.scaled_spacing())
                { // Skip tiny loops
                    ExtrusionLoop *loop = new ExtrusionLoop();
                    loop->paths.emplace_back(
                        ExtrusionAttributes{ExtrusionRole::Skirt,
                                            ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                          float(print.skirt_first_layer_height())}});
                    loop->paths.back().polyline = ex.contour.split_at_first_point();
                    loop->paths.back().polyline.points.push_back(
                        loop->paths.back().polyline.points.front()); // Close the loop
                    brim.entities.emplace_back(loop);

                    // Also add holes as loops
                    for (const Polygon &hole : ex.holes)
                    {
                        if (hole.length() > flow.scaled_spacing())
                        {
                            ExtrusionLoop *hole_loop = new ExtrusionLoop();
                            hole_loop->paths.emplace_back(
                                ExtrusionAttributes{ExtrusionRole::Skirt,
                                                    ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                                  float(print.skirt_first_layer_height())}});
                            hole_loop->paths.back().polyline = hole.split_at_first_point();
                            hole_loop->paths.back().polyline.points.push_back(
                                hole_loop->paths.back().polyline.points.front());
                            brim.entities.emplace_back(hole_loop);
                        }
                    }
                }
            }

            // Offset inward for next loop
            // JoinType::Round creates arc approximations at every corner, causing exponential vertex growth
            // in concentric loops (100 → 500 → 2500 → 12500 → 19k+ vertices). This causes:
            // 1. Extreme memory usage and processing time
            // 2. Clipper2 fragmentation (1 polygon → 893 fragments)
            // JoinType::Miter maintains vertex count while still producing smooth brims.
            current = offset_ex(current, -float(flow.scaled_spacing()), JoinType::Miter);
        }
    }

    // No need for all_loops - we've directly created the extrusion entities
    Polylines all_loops; // Empty - kept for compatibility with debug code below

#ifdef BRIM_DEBUG_TO_SVG
    static int irun = 0;
    ++irun;

    {
        SVG svg(debug_out_path("brim-%d.svg", irun).c_str(), get_extents(all_loops));
        svg.draw(union_ex(islands), "blue");
        svg.draw(islands_area_ex, "green");
        svg.draw(all_loops, "black", coord_t(scale_(0.1)));
    }
#endif // BRIM_DEBUG_TO_SVG

    all_loops = connect_brim_lines(std::move(all_loops), offset(islands_area_ex, float(SCALED_EPSILON)),
                                   float(flow.scaled_spacing()) * 2.f);

#ifdef BRIM_DEBUG_TO_SVG
    {
        SVG svg(debug_out_path("brim-connected-%d.svg", irun).c_str(), get_extents(all_loops));
        svg.draw(union_ex(islands), "blue");
        svg.draw(islands_area_ex, "green");
        svg.draw(all_loops, "black", coord_t(scale_(0.1)));
    }
#endif // BRIM_DEBUG_TO_SVG

    const bool could_brim_intersects_skirt = std::any_of(print.objects().begin(), print.objects().end(),
                                                         [&print](const PrintObject *object)
                                                         {
                                                             const BrimType &bt = object->config().brim_type;
                                                             return (bt == btOuterOnly || bt == btOuterAndInner) &&
                                                                    print.config().skirt_distance.value <
                                                                        object->config().brim_width;
                                                         });

    const bool draft_shield = print.config().draft_shield != dsDisabled;

    // If there is a possibility that brim intersects skirt, go through loops and split those extrusions
    // The result is either the original Polygon or a list of Polylines
    if (draft_shield && !print.skirt().empty() && could_brim_intersects_skirt)
    {
        // Find the bounding polygons of the skirt
        const Polygons skirt_inners = offset(dynamic_cast<ExtrusionLoop *>(print.skirt().entities.back())->polygon(),
                                             -float(scale_(print.skirt_flow().spacing())) / 2.f, JoinType::Round,
                                             float(scale_(0.1)));
        const Polygons skirt_outers = offset(dynamic_cast<ExtrusionLoop *>(print.skirt().entities.front())->polygon(),
                                             float(scale_(print.skirt_flow().spacing())) / 2.f, JoinType::Round,
                                             float(scale_(0.1)));

        // First calculate the trimming region.
        ClipperZUtils::ZPaths trimming;
        {
            ClipperZUtils::ZPaths input_subject;
            ClipperZUtils::ZPaths input_clip;
            for (const Polygon &poly : skirt_outers)
            {
                input_subject.emplace_back();
                ClipperZUtils::ZPath &out = input_subject.back();
                out.reserve(poly.points.size());
                for (const Point &pt : poly.points)
                    out.emplace_back(pt.x(), pt.y(), 0);
            }
            for (const Polygon &poly : skirt_inners)
            {
                input_clip.emplace_back();
                ClipperZUtils::ZPath &out = input_clip.back();
                out.reserve(poly.points.size());
                for (const Point &pt : poly.points)
                    out.emplace_back(pt.x(), pt.y(), 0);
            }
            // init Clipper
            Clipper2Lib::Clipper64 clipper;
            // No Z callback needed - Z=0 for all paths here

            // Convert ZPaths to Paths64 with Z preserved
            Clipper2Lib::Paths64 subject_paths = ClipperZUtils::zpaths_to_paths64(input_subject);
            Clipper2Lib::Paths64 clip_paths = ClipperZUtils::zpaths_to_paths64(input_clip);

            // add polygons
            clipper.AddSubject(subject_paths);
            clipper.AddClip(clip_paths);
            // perform operation
            Clipper2Lib::Paths64 trimming_paths;
            clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, trimming_paths);

            // Convert result back to ZPaths
            trimming = ClipperZUtils::paths64_to_zpaths(trimming_paths);
        }

        // Second, trim the extrusion loops with the trimming regions.
        ClipperZUtils::ZPaths loops_trimmed;
        {
            // Produce ClipperZUtils::ZPaths from polylines (not necessarily closed).
            ClipperZUtils::ZPaths input_clip;
            for (const Polyline &loop_pl : all_loops)
            {
                input_clip.emplace_back();
                ClipperZUtils::ZPath &out = input_clip.back();
                out.reserve(loop_pl.points.size());
                int64_t loop_idx = &loop_pl - &all_loops.front();
                for (const Point &pt : loop_pl.points)
                    // The Z coordinate carries index of the source loop.
                    out.emplace_back(pt.x(), pt.y(), loop_idx + 1);
            }
            // init Clipper
            Clipper2Lib::Clipper64 clipper;
            // Set Z callback to preserve loop indices at intersections
            clipper.SetZCallback(
                [](const Clipper2Lib::Point64 &e1bot, const Clipper2Lib::Point64 &e1top,
                   const Clipper2Lib::Point64 &e2bot, const Clipper2Lib::Point64 &e2top, Clipper2Lib::Point64 &pt)
                {
                    // At intersection, use the non-zero Z (loop index) from either edge
                    pt.z = (e1bot.z != 0) ? e1bot.z : (e1top.z != 0) ? e1top.z : (e2bot.z != 0) ? e2bot.z : e2top.z;
                });

            // Convert ZPaths to Paths64 with Z preserved
            Clipper2Lib::Paths64 input_paths = ClipperZUtils::zpaths_to_paths64(input_clip);
            Clipper2Lib::Paths64 trimming_paths = ClipperZUtils::zpaths_to_paths64(trimming);

            // add polygons
            clipper.AddOpenSubject(input_paths); // Open paths
            clipper.AddClip(trimming_paths);
            // perform operation - for open subjects, results go to separate output
            Clipper2Lib::PolyTree64 closed_result;
            Clipper2Lib::Paths64 open_result;
            clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, closed_result,
                            open_result);

            // Convert result back to ZPaths - Z values (loop indices) are preserved
            loops_trimmed = ClipperZUtils::paths64_to_zpaths(open_result);
        }

        // Third, produce the extrusions, sorted by the source loop indices.
        {
            std::vector<std::pair<const ClipperZUtils::ZPath *, size_t>> loops_trimmed_order;
            loops_trimmed_order.reserve(loops_trimmed.size());
            for (const ClipperZUtils::ZPath &path : loops_trimmed)
            {
                size_t input_idx = 0;
                for (const ClipperZUtils::ZPoint &pt : path)
                    if (pt.z > 0)
                    {
                        input_idx = (size_t) pt.z;
                        break;
                    }
                assert(input_idx != 0);
                loops_trimmed_order.emplace_back(&path, input_idx);
            }
            std::stable_sort(loops_trimmed_order.begin(), loops_trimmed_order.end(),
                             [](const std::pair<const ClipperZUtils::ZPath *, size_t> &l,
                                const std::pair<const ClipperZUtils::ZPath *, size_t> &r)
                             { return l.second < r.second; });

            Point last_pt(0, 0);
            for (size_t i = 0; i < loops_trimmed_order.size();)
            {
                // Find all pieces that the initial loop was split into.
                size_t j = i + 1;
                for (; j < loops_trimmed_order.size() && loops_trimmed_order[i].second == loops_trimmed_order[j].second;
                     ++j)
                    ;
                const ClipperZUtils::ZPath &first_path = *loops_trimmed_order[i].first;
                if (i + 1 == j && first_path.size() > 3 && first_path.front().x == first_path.back().x &&
                    first_path.front().y == first_path.back().y)
                {
                    auto *loop = new ExtrusionLoop();
                    brim.entities.emplace_back(loop);
                    loop->paths.emplace_back(
                        ExtrusionAttributes{ExtrusionRole::Skirt,
                                            ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                          float(print.skirt_first_layer_height())}});
                    Points &points = loop->paths.front().polyline.points;
                    points.reserve(first_path.size());
                    for (const ClipperZUtils::ZPoint &pt : first_path)
                        points.emplace_back(coord_t(pt.x), coord_t(pt.y));
                    i = j;
                }
                else
                {
                    //FIXME The path chaining here may not be optimal.
                    ExtrusionEntityCollection this_loop_trimmed;
                    this_loop_trimmed.entities.reserve(j - i);
                    for (; i < j; ++i)
                    {
                        this_loop_trimmed.entities.emplace_back(new ExtrusionPath(
                            {ExtrusionRole::Skirt, ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                                 float(print.skirt_first_layer_height())}}));
                        const ClipperZUtils::ZPath &path = *loops_trimmed_order[i].first;
                        Points &points =
                            dynamic_cast<ExtrusionPath *>(this_loop_trimmed.entities.back())->polyline.points;
                        points.reserve(path.size());
                        for (const ClipperZUtils::ZPoint &pt : path)
                            points.emplace_back(coord_t(pt.x), coord_t(pt.y));
                    }
                    chain_and_reorder_extrusion_entities(this_loop_trimmed.entities, &last_pt);
                    brim.entities.reserve(brim.entities.size() + this_loop_trimmed.entities.size());
                    append(brim.entities, std::move(this_loop_trimmed.entities));
                    this_loop_trimmed.entities.clear();
                }
                last_pt = brim.last_point();
            }
        }
    }
    else
    {
        extrusion_entities_append_loops_and_paths(
            brim.entities, std::move(all_loops),
            ExtrusionAttributes{ExtrusionRole::Skirt, ExtrusionFlow{float(flow.mm3_per_mm()), float(flow.width()),
                                                                    float(print.skirt_first_layer_height())}});
    }

    make_inner_brim(print, top_level_objects_with_brim, bottom_layers_expolygons, brim);
    return brim;
}

} // namespace Slic3r
