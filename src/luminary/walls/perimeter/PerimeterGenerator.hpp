///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2015 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/core/Prelude.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntityCollection.hpp"
#include "luminary/toolpath/flow/Flow.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/geometry/surface/SurfaceCollection.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntity.hpp"
#include "luminary/toolpath/extrusion/ExtrusionRole.hpp"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary
{
class ExtrusionEntityCollection;
class Layer;
class LayerRegion;
class Surface;
class PrintRegion;
struct ThickPolyline;

struct PerimeterRegion
{
    const PrintRegion *region;
    ExPolygons expolygons;
    BoundingBox bbox;

    explicit PerimeterRegion(const LayerRegion &layer_region);

    // Allows creating a PerimeterRegion directly from a PrintRegion and expolygons,
    // used when adding painted fuzzy skin areas as PerimeterRegions.
    PerimeterRegion(const PrintRegion *print_region, ExPolygons &&expolys)
        : region(print_region), expolygons(std::move(expolys)), bbox(get_extents(expolygons))
    {
    }

    // If there is any incompatibility, we don't need to create separate LayerRegions.
    // Because it is enough to split perimeters by PerimeterRegions.
    static bool has_compatible_perimeter_regions(const PrintRegionConfig &config,
                                                 const PrintRegionConfig &other_config);

    static void merge_compatible_perimeter_regions(std::vector<PerimeterRegion> &perimeter_regions);
};

using PerimeterRegions = std::vector<PerimeterRegion>;

} // namespace Luminary

namespace Luminary::PerimeterGenerator
{

struct Parameters
{
    Parameters(double layer_height, int layer_id, const Layer *layer, Flow perimeter_flow, Flow ext_perimeter_flow,
               Flow overhang_flow, Flow solid_infill_flow, const PrintRegionConfig &config,
               const PrintObjectConfig &object_config, const PrintConfig &print_config,
               const PerimeterRegions &perimeter_regions, const bool spiral_vase)
        : layer_height(layer_height)
        , layer_id(layer_id)
        , layer(layer)
        , perimeter_flow(perimeter_flow)
        , ext_perimeter_flow(ext_perimeter_flow)
        , overhang_flow(overhang_flow)
        , solid_infill_flow(solid_infill_flow)
        , config(config)
        , object_config(object_config)
        , print_config(print_config)
        , perimeter_regions(perimeter_regions)
        , spiral_vase(spiral_vase)
        , scaled_resolution(scaled<double>(object_config.gcode_resolution.value))
        , mm3_per_mm(perimeter_flow.mm3_per_mm())
        , ext_mm3_per_mm(ext_perimeter_flow.mm3_per_mm())
        , mm3_per_mm_overhang(overhang_flow.mm3_per_mm())
    {
    }

    // Input parameters
    double layer_height;
    int layer_id;
    const Layer *layer;
    Flow perimeter_flow;
    Flow ext_perimeter_flow;
    Flow overhang_flow;
    Flow solid_infill_flow;
    const PrintRegionConfig &config;
    const PrintObjectConfig &object_config;
    const PrintConfig &print_config;
    const PerimeterRegions &perimeter_regions;

    // Derived parameters
    bool spiral_vase;
    double scaled_resolution;
    double ext_mm3_per_mm;
    double mm3_per_mm;
    double mm3_per_mm_overhang;

private:
    Parameters() = delete;
};

void process_arachne(
    // Inputs:
    const Parameters &params, const Surface &surface, const ExPolygons *lower_slices, const ExPolygons *upper_slices,
    // Cache:
    Polygons &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection &out_gap_fill,
    // Infills without the gap fills
    ExPolygons &out_fill_expolygons);

void process_athena(
    // Inputs:
    const Parameters &params, const Surface &surface, const ExPolygons *lower_slices, const ExPolygons *upper_slices,
    // Cache:
    Polygons &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection &out_gap_fill,
    // Infills without the gap fills
    ExPolygons &out_fill_expolygons,
    // Serpentine is refused for the sub-bead material between the pieces of a split island, and a
    // split island's pieces are generated one level deep: a piece that splits again prints as walls.
    bool serpentine_allowed = true, int serpentine_piece_depth = 0);

// nominal_mm3_per_mm is the volumetric baseline for the flow-hold ratio; 0 derives it from
// `flow`. Pass the configured feature flow when `flow` itself has already been width-adjusted
// (fills at solver spacing), so the ratio measures against what the feature speed was tuned for.
ExtrusionMultiPath thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role,
                                                const Flow &flow, float tolerance, float merge_tolerance,
                                                const std::optional<uint32_t> &perimeter_index = std::nullopt,
                                                double nominal_mm3_per_mm = 0.);

} // namespace Luminary::PerimeterGenerator
