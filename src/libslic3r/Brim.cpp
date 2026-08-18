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
#include "libslic3r/DebugOutput.hpp"
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
#include "libslic3r/Surface.hpp"
#include "libslic3r/Fill/FillConcentric.hpp"
#include "libslic3r/PerimeterGenerator.hpp"

#if defined(BRIM_DEBUG_TO_SVG)
#include "SVG.hpp"
#endif

// Inner brim debug logging
// #define INNER_BRIM_DEBUG
#ifdef INNER_BRIM_DEBUG
#include <cstdio>
#define BRIM_LOG(...) fprintf(stderr, "[BRIM] " __VA_ARGS__)
#else
#define BRIM_LOG(...) ((void) 0)
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
    float result = 0.f;
    for (const PrintObject *object : objects)
    {
        // Painted mouse ears use per-point radius, not brim_width
        if (object->config().brim_type == btPainted)
        {
            for (const auto &pt : object->model_object()->brim_points)
                result = std::max(result, pt.head_front_radius);
        }
        else
        {
            result = std::max(result, (float) object->config().brim_width.value);
        }
    }
    return result;
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
    ExPolygons ears; // All painted ears, each individually clipped at its own overlap level
};

static PaintedEarResult make_brim_ears_painted(const PrintObject *object, const Print &print, float brim_separation,
                                               const ExPolygons &outer_brim_expoly,
                                               const ExPolygons &bottom_layer_expolygons)
{
    PaintedEarResult result;
    const BrimPoints &brim_ear_points = object->model_object()->brim_points;

    if (brim_ear_points.empty())
        return result;

    const Geometry::Transformation &trsf = object->model_object()->instances[0]->get_transformation();
    Transform3d model_trsf = trsf.get_matrix_no_offset();
    const Point &center_offset = object->center_offset();
    model_trsf = model_trsf.pretranslate(
        Vec3d(-unscale<double>(center_offset.x()), -unscale<double>(center_offset.y()), 0));

    const Flow flow = print.brim_flow();

    // Use the actual calculated width from first layer external perimeter
    float external_perimeter_width = flow.width(); // Default fallback
    if (!object->layers().empty() && !object->layers().front()->regions().empty())
    {
        // Get the actual first layer external perimeter flow width
        external_perimeter_width = object->layers().front()->regions().front()->flow(frExternalPerimeter).width();
    }

    // Create ears at each manually-placed point
    for (size_t ear_i = 0; ear_i < brim_ear_points.size(); ++ear_i)
    {
        const auto &pt = brim_ear_points[ear_i];
        Vec3f world_pos = pt.transform(trsf.get_matrix());
        if (world_pos.z() > 0.01f)
            continue; // Skip points not on first layer

        float overlap_distance = scale_((pt.overlap_percent / 100.0f) * external_perimeter_width);
        float clip_offset = -overlap_distance;
        float half_perimeter_width = external_perimeter_width / 2.0f;

        Polygons model_contour = offset(to_polygons(outer_brim_expoly), -brim_separation, JoinType::Square);
        Polygons external_perimeter_outer_edge = offset(model_contour, scale_(half_perimeter_width), JoinType::Square);
        Polygons contour_for_this_ear = offset(external_perimeter_outer_edge, clip_offset, JoinType::Square);

        // Create full circular ear at user-specified diameter
        float ear_radius_mm = pt.head_front_radius;
        coord_t ear_radius_scaled = scale_(ear_radius_mm);

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

        // Unified ear clipping: offset ALL solid boundaries by the overlap-adjusted amount,
        // then diff the circle against the expanded solid. This handles both outside contours
        // AND inside holes with a single operation - all boundaries get the same gap/overlap.
        //
        // offset_ex(ExPolygons, +delta) expands contours outward and shrinks holes inward.
        // At 0% overlap: clip_amount = half_perimeter_width → ear starts at perimeter outer edge
        // At -100%: clip_amount = half_pw + full_pw → 1.5 perimeter widths gap
        // At +100%: clip_amount = half_pw - full_pw → ear overlaps into perimeter by half pw
        float clip_amount = scale_(half_perimeter_width) - overlap_distance;
        ExPolygons clip_solid = offset_ex(bottom_layer_expolygons, clip_amount, JoinType::Square);
        ExPolygons clipped_ear = diff_ex(ExPolygons{ExPolygon(point_round)}, clip_solid);
        BRIM_LOG("  ear at (%.2f,%.2f) radius=%.2f overlap=%d%% clip_amount=%.3f clipped=%zu\n",
                 unscale<double>(ear_center.x()), unscale<double>(ear_center.y()), ear_radius_mm,
                 (int) pt.overlap_percent, unscale<double>(clip_amount), clipped_ear.size());
        append(result.ears, clipped_ear);
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
    ExPolygons clippable;    // Painted mouse ear areas (each individually clipped at its own overlap level)
    ExPolygons auto_ears;    // Auto mouse ear areas (btEar) - clipped, with separation from perimeter
    ExPolygons regular_brim; // Regular brim ring areas (btOuterOnly, btOuterAndInner)
    // Painted ears pre-clipped by make_brim_ears_painted. NOT subject to no_brim_area diff
    // (which would kill positive overlap). Cross-object clipping applied separately.
    ExPolygons painted_preclipped;
    // Cross-object no-brim areas (all objects' solid footprints) for clipping painted ears.
    ExPolygons cross_object_solid;
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
    ExPolygons auto_ears_area;    // Will become result.auto_ears (btEar auto ears)
    ExPolygons regular_brim_area; // Will become result.regular_brim
    // Per-instance painted ears and solid footprints for cross-instance clipping.
    // Using per-instance (not per-object) so instances of the same object clip against each other.
    struct InstanceBrim
    {
        ExPolygons ears;
        ExPolygons solid;
    };
    std::vector<InstanceBrim> per_instance_brim;

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
        ExPolygons auto_ears_area_object;
        ExPolygons regular_brim_area_object;

        ExPolygons no_brim_area_object;

        // Painted ears: generate once per object with ALL contours combined.
        // This ensures each ear is clipped against the full model boundary, not just one ExPolygon.
        if (use_painted_brim_ears && has_outer_brim && is_top_outer_brim)
        {
            ExPolygons all_outer_brim_expolys;
            for (const ExPolygon &ep : bottom_layers_expolygons[print_object_idx])
            {
                Polygons cp{ep.contour};
                append(all_outer_brim_expolys, offset_ex(cp, brim_separation, JoinType::Square));
            }
            // Clip against own object geometry (in object-local coordinates).
            PaintedEarResult painted_ears = make_brim_ears_painted(object, print, brim_separation,
                                                                   all_outer_brim_expolys,
                                                                   bottom_layers_expolygons[print_object_idx]);
            // Store per-instance ears and solid for cross-instance clipping
            for (const PrintInstance &instance : object->instances())
            {
                InstanceBrim ib;
                append_and_translate(ib.ears, painted_ears.ears, instance);
                append_and_translate(ib.solid, bottom_layers_expolygons[print_object_idx], instance);
                per_instance_brim.push_back(std::move(ib));
            }
        }

        for (const ExPolygon &ex_poly : bottom_layers_expolygons[print_object_idx])
        {
            if (has_outer_brim && is_top_outer_brim)
            {
                Polygons contour_polygons{ex_poly.contour};
                ExPolygons outer_brim_expoly = offset_ex(contour_polygons, brim_separation, JoinType::Square);
                if (use_painted_brim_ears)
                {
                    // Already handled above - skip
                }
                else if (use_auto_brim_ears)
                {
                    // Auto-generate: detect sharp corners - routed separately for perimeter separation
                    coord_t size_ear = brim_width;
                    append(auto_ears_area_object,
                           diff_ex(make_brim_ears(outer_brim_expoly, size_ear, ear_detection_length,
                                                  brim_ears_max_angle, true),
                                   outer_brim_expoly));
                }
                else
                {
                    // Regular brim - routed separately for standard loop generation
                    append(regular_brim_area_object,
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

            // For painted ears: skip no_brim_area entirely. Each ear is individually clipped
            // by the unified offset_ex/diff_ex in make_brim_ears_painted, which correctly handles
            // overlap percentages. Adding to no_brim_area would clip away positive overlap.
            // For all other brim types: add contour to no_brim_area for proper clipping.
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
            append_and_translate(auto_ears_area, auto_ears_area_object, instance);
            append_and_translate(regular_brim_area, regular_brim_area_object, instance);
            append_and_translate(no_brim_area, no_brim_area_object, instance);
        }
    }

    BrimAreas result;

    // When brim ears are used, brim_area can contain hundreds of separate ExPolygons (one per ear).
    // This causes severe performance issues in subsequent Clipper2 operations (30+ seconds).
    // Union merges overlapping ears and consolidates fragments into a unified polygon set.
    brim_area = union_ex(brim_area);
    auto_ears_area = union_ex(auto_ears_area);
    result.clippable = diff_ex(brim_area, no_brim_area);
    result.auto_ears = diff_ex(auto_ears_area, no_brim_area);
    result.regular_brim = diff_ex(regular_brim_area, no_brim_area);

    // Painted ears: clip each instance's ears against OTHER instances' solid footprints.
    // Must skip self (j != i) to preserve own-object overlap setting.
    // O(N^2) for N instances - acceptable for typical counts (1-20).
    if (!per_instance_brim.empty())
    {
        const float full_pw = scale_(1.5f * print.brim_flow().width());
        ExPolygons all_painted;
        for (size_t i = 0; i < per_instance_brim.size(); ++i)
        {
            if (per_instance_brim[i].ears.empty())
                continue;

            ExPolygons other_solid;
            for (size_t j = 0; j < per_instance_brim.size(); ++j)
                if (j != i)
                    append(other_solid, per_instance_brim[j].solid);

            ExPolygons ears = union_ex(per_instance_brim[i].ears);
            if (!other_solid.empty())
            {
                ExPolygons cross_clip = offset_ex(other_solid, full_pw, JoinType::Square);
                ears = diff_ex(ears, cross_clip);
            }
            append(all_painted, std::move(ears));
        }
        if (!all_painted.empty())
            result.painted_preclipped = union_ex(all_painted);
    }

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
    BRIM_LOG("has_polygons_nothing_inside: %zu islands, %zu marked as nothing_inside\n", has_nothing_inside.size(),
             std::count(has_nothing_inside.begin(), has_nothing_inside.end(), true));
    for (size_t i = 0; i < has_nothing_inside.size(); ++i)
        if (has_nothing_inside[i])
            BRIM_LOG("  island %zu: nothing_inside=true\n", i);
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

    BRIM_LOG("inner_brim_area: %zu objects, no_brim_offset=%.4f\n", print.objects().size(),
             unscale<double>(no_brim_offset));

    // polygon_idx must correspond to idx generated inside has_polygons_nothing_inside()
    size_t polygon_idx = 0;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
    {
        const PrintObject *object = print.objects()[print_object_idx];
        const BrimType brim_type = object->config().brim_type.value;
        const float brim_separation = scale_(object->config().brim_separation.value);
        const float brim_width = scale_(object->config().brim_width.value);
        const bool top_outer_brim = top_level_objects_idx.find(object->id().id) != top_level_objects_idx.end();

        BRIM_LOG(
            "  object %zu: brim_type=%d, brim_sep=%.3f, brim_width=%.3f, top_outer=%d, expolys=%zu, instances=%zu\n",
            print_object_idx, (int) brim_type, unscale<double>(brim_separation), unscale<double>(brim_width),
            (int) top_outer_brim, bottom_layers_expolygons[print_object_idx].size(), object->instances().size());

        ExPolygons brim_area_innermost_object;
        ExPolygons brim_area_object;
        ExPolygons no_brim_area_object;
        Polygons holes_reversed_object;
        for (const ExPolygon &ex_poly : bottom_layers_expolygons[print_object_idx])
        {
            {
                auto bb = get_extents(ex_poly);
                BRIM_LOG("    expolygon: contour pts=%zu, holes=%zu, bbox=[%.2f,%.2f]-[%.2f,%.2f]\n",
                         ex_poly.contour.points.size(), ex_poly.holes.size(), unscale<double>(bb.min.x()),
                         unscale<double>(bb.min.y()), unscale<double>(bb.max.x()), unscale<double>(bb.max.y()));
            }

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
                        bool nothing_inside = has_nothing_inside[polygon_idx + hole_idx];
                        auto hbb = get_extents(hole);
                        BRIM_LOG(
                            "      hole %zu (poly_idx=%zu): pts=%zu, nothing_inside=%d, bbox=[%.2f,%.2f]-[%.2f,%.2f]\n",
                            hole_idx, polygon_idx + hole_idx, hole.points.size(), (int) nothing_inside,
                            unscale<double>(hbb.min.x()), unscale<double>(hbb.min.y()), unscale<double>(hbb.max.x()),
                            unscale<double>(hbb.max.y()));
                        if (nothing_inside)
                            append(brim_area_innermost_object, shrink_ex({hole}, brim_separation, JoinType::Square));
                        else
                            append(brim_area_object,
                                   diff_ex(shrink_ex({hole}, brim_separation, JoinType::Square),
                                           shrink_ex({hole}, brim_width + brim_separation, JoinType::Square)));
                    }

                polygon_idx += ex_poly.holes.size(); // Increase idx for every hole of the ExPolygon.
            }

            if (brim_type == BrimType::btInnerOnly || brim_type == BrimType::btPainted)
                append(no_brim_area_object,
                       diff_ex(offset(ex_poly.contour, no_brim_offset, JoinType::Square), ex_poly_holes_reversed));

            if (brim_type == BrimType::btOuterOnly || brim_type == BrimType::btPainted)
                append(no_brim_area_object,
                       diff_ex(ex_poly.contour, shrink_ex(ex_poly_holes_reversed, no_brim_offset, JoinType::Square)));

            append(holes_reversed_object, ex_poly_holes_reversed);
        }
        append(no_brim_area_object,
               offset_ex(bottom_layers_expolygons[print_object_idx], brim_separation, JoinType::Square));

        BRIM_LOG("  object %zu result: innermost=%zu, brim_area=%zu, no_brim_area=%zu, holes_reversed=%zu\n",
                 print_object_idx, brim_area_innermost_object.size(), brim_area_object.size(),
                 no_brim_area_object.size(), holes_reversed_object.size());

        for (const PrintInstance &instance : object->instances())
        {
            append_and_translate(brim_area_innermost[print_object_idx], brim_area_innermost_object, instance);
            append_and_translate(brim_area, brim_area_object, instance);
            append_and_translate(no_brim_area, no_brim_area_object, instance);
            append_and_translate(holes_reversed, holes_reversed_object, instance);
        }
    }
    assert(polygon_idx == has_nothing_inside.size());

    BRIM_LOG("inner_brim_area: total brim_area=%zu, no_brim_area=%zu, holes_reversed=%zu\n", brim_area.size(),
             no_brim_area.size(), holes_reversed.size());

    ExPolygons brim_area_innermost_merged;
    // Append all innermost brim areas.
    std::vector<InnerBrimExPolygons> brim_area_out;
    for (size_t print_object_idx = 0; print_object_idx < print.objects().size(); ++print_object_idx)
        if (const double brim_width = print.objects()[print_object_idx]->config().brim_width.value;
            !brim_area_innermost[print_object_idx].empty())
        {
            BRIM_LOG("  innermost brim for object %zu: %zu expolygons, brim_width=%.2f\n", print_object_idx,
                     brim_area_innermost[print_object_idx].size(), brim_width);
            append(brim_area_innermost_merged, brim_area_innermost[print_object_idx]);
            brim_area_out.push_back(
                {std::move(brim_area_innermost[print_object_idx]), InnerBrimType::INNERMOST, brim_width});
        }

    // Append all normal brim areas.
    // Use the max brim_width across all objects for the NORMAL group bound
    double max_brim_width = 0.;
    for (const PrintObject *obj : print.objects())
        max_brim_width = std::max(max_brim_width, obj->config().brim_width.value);

    brim_area = union_ex(brim_area);
    ExPolygons normal_brim = diff_ex(intersection_ex(to_polygons(std::move(brim_area)), holes_reversed), no_brim_area);
    BRIM_LOG("  normal brim area (after intersect+diff): %zu expolygons, max_brim_width=%.2f\n", normal_brim.size(),
             max_brim_width);
    for (size_t i = 0; i < normal_brim.size(); ++i)
    {
        auto bb = get_extents(normal_brim[i]);
        BRIM_LOG("    normal_brim[%zu]: contour pts=%zu, holes=%zu, area=%.4f, bbox=[%.2f,%.2f]-[%.2f,%.2f]\n", i,
                 normal_brim[i].contour.points.size(), normal_brim[i].holes.size(),
                 unscale<double>(unscale<double>(normal_brim[i].area())), unscale<double>(bb.min.x()),
                 unscale<double>(bb.min.y()), unscale<double>(bb.max.x()), unscale<double>(bb.max.y()));
    }
    brim_area_out.push_back({std::move(normal_brim), InnerBrimType::NORMAL, max_brim_width});

    // Cut out a huge brim areas that overflows into the INNERMOST holes.
    brim_area_out.back().brim_area = diff_ex(brim_area_out.back().brim_area, brim_area_innermost_merged);

    BRIM_LOG("inner_brim_area: returning %zu brim groups\n", brim_area_out.size());
    for (size_t i = 0; i < brim_area_out.size(); ++i)
        BRIM_LOG("  group[%zu]: type=%s, expolygons=%zu, brim_width=%.2f\n", i,
                 brim_area_out[i].type == InnerBrimType::INNERMOST ? "INNERMOST" : "NORMAL",
                 brim_area_out[i].brim_area.size(), brim_area_out[i].brim_width);
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

// Inner brim generation using shrink-from-boundary + clip approach.
// Mirrors the outer brim's expand-from-boundary algorithm:
//   Outer: expand islands outward each iteration, shrink by 0.5*spacing for centerline, clip to brim area.
//   Inner: shrink hole boundaries inward each iteration, expand by 0.5*spacing for centerline, clip to brim area.
// This produces clean concentric loops that match outer brim loop count.
static void make_inner_brim(const Print &print, const ConstPrintObjectPtrs &top_level_objects_with_brim,
                            const std::vector<ExPolygons> &bottom_layers_expolygons, ExtrusionEntityCollection &brim)
{
    assert(print.objects().size() == bottom_layers_expolygons.size());
    const auto scaled_resolution = scaled<double>(print.config().gcode_resolution.value);
    Flow flow = print.brim_flow();

    // Step 1: Compute inner brim areas = within brim_width of any solid boundary inside a hole.
    // This covers both hole walls (brim going inward) AND inner feature contours (brim going outward).
    Polygons all_holes_reversed;
    ExPolygons all_solid_areas;
    ExPolygons all_solid_areas_expanded;
    bool any_inner_brim = false;
    for (size_t obj_idx = 0; obj_idx < print.objects().size(); ++obj_idx)
    {
        const PrintObject *object = print.objects()[obj_idx];
        const BrimType bt = object->config().brim_type.value;
        const bool has_inner = (bt == BrimType::btInnerOnly || bt == BrimType::btOuterAndInner ||
                                bt == BrimType::btEar || bt == BrimType::btPainted);
        // Check quantized brim_width (not config value) - if brim_width rounds to 0 loops, skip
        if (!has_inner || size_t(floor(object->config().brim_width.value / flow.spacing())) == 0)
            continue;
        any_inner_brim = true;
        const float brim_sep = scale_(object->config().brim_separation.value);
        // Quantize brim width to fit whole beads, matching outer brim's floor() behavior.
        // This prevents Athena from squeezing in an extra compressed loop.
        const auto num_loops = size_t(floor(object->config().brim_width.value / flow.spacing()));
        const float brim_width = float(num_loops) * float(flow.scaled_spacing());

        for (const ExPolygon &ex_poly : bottom_layers_expolygons[obj_idx])
        {
            Polygons holes = ex_poly.holes;
            polygons_reverse(holes);
            ExPolygons solid_sep = offset_ex(ExPolygon(ex_poly), brim_sep, JoinType::Square);
            ExPolygons solid_exp = offset_ex(ExPolygon(ex_poly), brim_sep + brim_width, JoinType::Square);
            for (const PrintInstance &instance : object->instances())
            {
                append_and_translate(all_holes_reversed, holes, instance);
                append_and_translate(all_solid_areas, solid_sep, instance);
                append_and_translate(all_solid_areas_expanded, solid_exp, instance);
            }
        }
    }

    if (!any_inner_brim)
        return;

    // Check if any object uses ear brim - these need the original inner_brim_area() logic
    // which properly clips inner brim to ear circles.
    bool any_ear_brim = false;
    for (const PrintObject *obj : print.objects())
    {
        const BrimType bt = obj->config().brim_type.value;
        if (bt == BrimType::btEar || bt == BrimType::btPainted)
            any_ear_brim = true;
    }

    ExPolygons brim_regions;
    if (any_ear_brim)
    {
        // For ear brim types, use the original inner_brim_area() which properly scopes
        // inner brim to holes within ear circles.
        std::vector<InnerBrimExPolygons> inner_brims_ex = inner_brim_area(print, top_level_objects_with_brim,
                                                                          bottom_layers_expolygons,
                                                                          float(flow.scaled_spacing()));
        for (const auto &ib : inner_brims_ex)
            append(brim_regions, ib.brim_area);
    }
    else
    {
        // For regular inner brim, compute brim_width rings around all solid boundaries inside holes.
        // This covers both hole walls AND inner feature contours.
        ExPolygons brim_band = diff_ex(all_solid_areas_expanded, all_solid_areas);
        ExPolygons holes_as_ex;
        for (const Polygon &h : all_holes_reversed)
            holes_as_ex.emplace_back(ExPolygon(h));
        brim_regions = union_ex(intersection_ex(brim_band, holes_as_ex));
    }

    for (const PrintObject *obj : print.objects())
        BRIM_LOG("  object brim_type=%d, brim_width=%.2f\n", (int) obj->config().brim_type.value,
                 obj->config().brim_width.value);
    BRIM_LOG("make_inner_brim: any_ear_brim=%d, brim_regions=%zu\n", (int) any_ear_brim, brim_regions.size());
    if (brim_regions.empty())
    {
        BRIM_LOG("make_inner_brim: no brim regions, returning early\n");
        return;
    }

    // Sort regions largest-first so wall-adjacent loops (which adhere to the object)
    // print before smaller disconnected regions.
    std::sort(brim_regions.begin(), brim_regions.end(),
              [](const ExPolygon &a, const ExPolygon &b) { return std::abs(a.area()) > std::abs(b.area()); });

    BRIM_LOG("make_inner_brim: %zu brim regions (brim_width rings around all solid boundaries inside holes)\n",
             brim_regions.size());
    for (size_t i = 0; i < brim_regions.size(); ++i)
    {
        auto bb = get_extents(brim_regions[i]);
        BRIM_LOG("  region[%zu]: area=%.2f, holes=%zu, bbox=[%.2f,%.2f]-[%.2f,%.2f]\n", i,
                 unscale<double>(unscale<double>(std::abs(brim_regions[i].area()))), brim_regions[i].holes.size(),
                 unscale<double>(bb.min.x()), unscale<double>(bb.min.y()), unscale<double>(bb.max.x()),
                 unscale<double>(bb.max.y()));
    }

    const size_t inner_brim_start = brim.entities.size();

    // Step 3: Fill inner brim areas using FillConcentric with Athena.
    // Same approach as painted mouse ears - Athena handles arbitrary ExPolygon regions,
    // nested holes, and shared boundaries correctly without manual loop management.
    assert(!print.objects().empty());
    if (print.objects().empty())
        return;
    const PrintObjectConfig &obj_config = print.objects().front()->config();

    FillConcentric fill_concentric;
    fill_concentric.spacing = flow.spacing();
    fill_concentric.overlap = 0;
    fill_concentric.bounding_width = 0;
    fill_concentric.loop_clipping = 0;
    fill_concentric.print_config = &print.config();
    fill_concentric.print_object_config = &obj_config;

    FillParams fill_params;
    fill_params.density = 1.0f;
    fill_params.dont_adjust = false;
    fill_params.use_advanced_perimeters = true;
    fill_params.perimeter_generator = PerimeterGeneratorType::Athena;
    fill_params.layer_height = print.skirt_first_layer_height();
    fill_params.prefer_clockwise_movements = print.config().prefer_clockwise_movements;

    for (const ExPolygon &brim_area : brim_regions)
    {
        // Inset by half-spacing so the first loop doesn't overlap the perimeter
        ExPolygons inset = offset_ex(ExPolygons{brim_area}, -0.5f * float(flow.scaled_spacing()), JoinType::Square);
        if (inset.empty())
            continue;

        for (const ExPolygon &area : inset)
        {
            fill_params.start_near = area.contour.points.front();
            Surface surface(stBottom, area);
            ThickPolylines thick_polylines = fill_concentric.fill_surface_advanced(&surface, fill_params);

            // Collect this region's loops, then reverse so outermost is first.
            // Athena generates inside-out; we want outside-in so the GCode export's
            // nearest-neighbor chaining (which starts near the wall) picks adjacent loops.
            std::vector<ExtrusionEntity *> region_entities;
            Flow brim_loop_flow = flow.with_spacing(float(fill_concentric.spacing));
            // Minimum path length to keep - skip tiny gap-fill fragments that won't adhere
            const double min_brim_length = flow.scaled_spacing() * 3.0;
            for (const ThickPolyline &tp : thick_polylines)
            {
                ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(
                    tp, ExtrusionRole::Skirt, brim_loop_flow, scaled<float>(0.05), float(SCALED_EPSILON));
                if (!multi_path.empty() && multi_path.length() >= min_brim_length)
                {
                    // Use ExtrusionLoop for closed paths (proper seam handling),
                    // ExtrusionMultiPath for open paths.
                    // Athena may leave a small gap, so use tolerance instead of exact match.
                    const double close_tolerance = flow.scaled_spacing() * 0.5;
                    if ((multi_path.paths.front().first_point() - multi_path.paths.back().last_point())
                            .cast<double>()
                            .norm() < close_tolerance)
                        region_entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                    else
                        region_entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                }
            }
            // Athena's output is spatially coherent - each path is adjacent to the previous.
            // Reversing gives outside-in order while preserving spatial adjacency.
            BRIM_LOG("  region fill: %zu entities from %zu thick_polylines\n", region_entities.size(),
                     thick_polylines.size());
            append(brim.entities, std::move(region_entities));
        }
    }

    BRIM_LOG("make_inner_brim: done with Athena concentric fill, brim entities=%zu (%zu inner)\n", brim.entities.size(),
             brim.entities.size() - inner_brim_start);
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

    // The first support layer carries the whole support tower, so the outer brim yields to it: the
    // regular brim and all ear regions are clipped away from the support footprint (with the
    // object's brim separation as clearance) before any brim line is generated. Inner brim is
    // generated separately and is not clipped here.
    ExPolygons support_keepout;
    for (const PrintObject *object : print.objects())
    {
        if (object->support_layers().empty())
            continue;
        const SupportLayer *first_support_layer = object->support_layers().front();
        // Only a support layer that sits on the bed counts as the flange.
        if (first_support_layer->bottom_z() > EPSILON || first_support_layer->support_islands.empty())
            continue;
        const ExPolygons flange = offset_ex(first_support_layer->support_islands,
                                            float(scale_(object->config().brim_separation.value)), JoinType::Square);
        for (const PrintInstance &instance : object->instances())
            append_and_translate(support_keepout, flange, instance);
    }
    if (!support_keepout.empty())
    {
        support_keepout = union_ex(support_keepout);
        if (debug_enabled(DBG_SUPPORT))
            dbg_log(DBG_SUPPORT, 0., "SUPPORT-BRIM-KEEPOUT", "polys=%zu area=%.1f", support_keepout.size(),
                    area(to_polygons(support_keepout)) * SCALING_FACTOR * SCALING_FACTOR);
        brim_areas.regular_brim = diff_ex(brim_areas.regular_brim, support_keepout);
        brim_areas.auto_ears = diff_ex(brim_areas.auto_ears, support_keepout);
        brim_areas.clippable = diff_ex(brim_areas.clippable, support_keepout);
        brim_areas.painted_preclipped = diff_ex(brim_areas.painted_preclipped, support_keepout);
    }

    // Build islands_area from all brim region types
    ExPolygons islands_area_ex = brim_areas.regular_brim;
    append(islands_area_ex, brim_areas.auto_ears);
    append(islands_area_ex, brim_areas.clippable);
    append(islands_area_ex, brim_areas.painted_preclipped);

    islands_area = to_polygons(islands_area_ex);

    ExtrusionEntityCollection brim;

    // --- Regular brims: expand-outward + clip algorithm ---
    // Loops grow outward from object islands and are clipped to the brim area.
    Polylines all_loops;
    if (!brim_areas.regular_brim.empty())
    {
        Polygons regular_clip_area = to_polygons(brim_areas.regular_brim);
        Polygons loops;
        Polygons regular_islands = islands; // Copy so mouse ears still have original islands
        size_t num_loops = size_t(floor(max_brim_width(print.objects()) / flow.spacing()));
        for (size_t i = 0; i < num_loops; ++i)
        {
            try_cancel();
            regular_islands = expand(regular_islands, float(flow.scaled_spacing()), JoinType::Square);
            for (Polygon &poly : regular_islands)
                poly.douglas_peucker(scaled_resolution);
            polygons_append(loops, shrink(regular_islands, 0.5f * float(flow.scaled_spacing())));
        }
        loops = union_pt_chained_outside_in(loops);

        // Clip loops against the regular brim area and convert to polylines
        std::vector<Polylines> loops_pl_by_levels;
        {
            Polylines loops_pl = to_polylines(loops);
            loops_pl_by_levels.assign(loops_pl.size(), Polylines());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, loops_pl.size()),
                              [&loops_pl_by_levels, &loops_pl,
                               &regular_clip_area](const tbb::blocked_range<size_t> &range)
                              {
                                  for (size_t i = range.begin(); i < range.end(); ++i)
                                  {
                                      loops_pl_by_levels[i] = chain_polylines(
                                          intersection_pl({std::move(loops_pl[i])}, regular_clip_area));
                                  }
                              });
        }
        for (Polylines &polylines : loops_pl_by_levels)
            append(all_loops, std::move(polylines));
        loops_pl_by_levels.clear();
        optimize_polylines_by_reversing(&all_loops);
    }

    // --- Mouse ears: concentric fill via Athena perimeter generator ---
    // Uses FillConcentric with Athena WallToolPaths for proper gap filling at ear centers.
    // Auto ears get half-spacing inset to prevent overlap with external perimeter.
    // Painted ears start at raw boundary - each ear was individually clipped at its own overlap level.
    ExPolygons all_ear_areas = brim_areas.auto_ears;
    append(all_ear_areas, brim_areas.clippable);
    append(all_ear_areas, brim_areas.painted_preclipped);
    if (!all_ear_areas.empty())
    {
        const size_t auto_ears_count = brim_areas.auto_ears.size();

        // Sort ears by nearest-neighbor to minimize travel between them.
        // Start from the skirt's end position when available (draft shield generates skirt first).
        std::vector<bool> is_auto_ear(all_ear_areas.size(), false);
        for (size_t i = 0; i < auto_ears_count; ++i)
            is_auto_ear[i] = true;
        Points ear_centroids;
        ear_centroids.reserve(all_ear_areas.size());
        for (const ExPolygon &ep : all_ear_areas)
            ear_centroids.push_back(ep.contour.centroid());
        const Point *start_near = nullptr;
        Point skirt_end;
        if (!print.skirt().empty())
        {
            skirt_end = print.skirt().last_point();
            start_near = &skirt_end;
        }
        std::vector<size_t> ear_order = chain_points(ear_centroids, start_near);

        // Get a print object config for the perimeter generator
        const PrintObjectConfig &obj_config = print.objects().front()->config();

        // Configure FillConcentric with Athena
        FillConcentric fill_concentric;
        fill_concentric.spacing = flow.spacing();
        fill_concentric.overlap = 0;
        fill_concentric.bounding_width = 0;
        fill_concentric.loop_clipping = 0;
        fill_concentric.print_config = &print.config();
        fill_concentric.print_object_config = &obj_config;

        FillParams fill_params;
        fill_params.density = 1.0f;
        fill_params.dont_adjust = false;
        fill_params.use_advanced_perimeters = true;
        fill_params.perimeter_generator = PerimeterGeneratorType::Athena;
        fill_params.layer_height = print.skirt_first_layer_height();
        fill_params.prefer_clockwise_movements = print.config().prefer_clockwise_movements;

        for (size_t ordered_idx : ear_order)
        {
            const ExPolygon &brim_area = all_ear_areas[ordered_idx];
            try_cancel();

            // Auto ears: inset by half-spacing so the first loop doesn't overlap the external perimeter.
            // Painted ears: use raw boundary - the clip boundary already accounts for overlap.
            ExPolygon ear_shape;
            if (is_auto_ear[ordered_idx])
            {
                ExPolygons inset = offset_ex(ExPolygons{brim_area}, -0.5f * float(flow.scaled_spacing()),
                                             JoinType::Square);
                if (inset.empty())
                    continue;
                ear_shape = std::move(inset.front());
            }
            else
            {
                ear_shape = brim_area;
            }

            fill_params.start_near = ear_shape.contour.centroid();

            Surface surface(stBottom, ear_shape);
            ThickPolylines thick_polylines = fill_concentric.fill_surface_advanced(&surface, fill_params);

            // Convert ThickPolylines to ExtrusionLoop/ExtrusionMultiPath entities
            Flow ear_flow = flow.with_spacing(float(fill_concentric.spacing));
            for (const ThickPolyline &tp : thick_polylines)
            {
                ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(
                    tp, ExtrusionRole::Skirt, ear_flow, scaled<float>(0.05), float(SCALED_EPSILON));
                if (!multi_path.empty())
                {
                    // Tolerance-based check for closed loops - Athena may leave sub-spacing gaps
                    const double close_tol = flow.scaled_spacing() * 0.5;
                    if ((multi_path.paths.front().first_point() - multi_path.paths.back().last_point())
                            .cast<double>()
                            .norm() < close_tol)
                        brim.entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                    else
                        brim.entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                }
            }
        }
    }

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
