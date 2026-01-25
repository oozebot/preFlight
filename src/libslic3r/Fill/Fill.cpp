///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2016 Sakari Kapanen @Flannelhead
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Mark Hindess
///|/ Copyright (c) 2011 Michael Moon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <oneapi/tbb/scalable_allocator.h>
#include <boost/container/vector.hpp>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>
#include <functional>
#include <cassert>
#include <cinttypes>
#include <cstdlib>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"
// for Arachne based infills
#include "../PerimeterGenerator.hpp"
#include "../Athena/WallToolPaths.hpp"
#include "../Athena/utils/ExtrusionLine.hpp"
#include "../Athena/PerimeterOrder.hpp"
#include "FillBase.hpp"
#include "FillRectilinear.hpp"
#include "FillLightning.hpp"
#include "FillEnsuring.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r
{
namespace FillAdaptive
{
struct Octree;
} // namespace FillAdaptive
namespace FillLightning
{
class Generator;
} // namespace FillLightning

//static constexpr const float NarrowInfillAreaThresholdMM = 3.f;

struct SurfaceFillParams
{
    // Zero based extruder ID.
    unsigned int extruder = 0;
    // Infill pattern, adjusted for the density etc.
    InfillPattern pattern = InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    coordf_t spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t overlap = 0.;
    // Angle as provided by the region config, in radians.
    float angle = 0.f;
    // Is bridging used for this fill? Bridging parameters may be used even if this->flow.bridge() is not set.
    bool bridge;
    // Non-negative for a bridge.
    float bridge_angle = 0.f;

    // FillParams
    float density = 0.f;
    // Don't adjust spacing to fill the space evenly.
    //    bool        	dont_adjust = false;
    // Length of the infill anchor along the perimeter line.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float anchor_length = 1000.f;
    float anchor_length_max = 1000.f;

    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
    Flow flow;

    // For the output
    ExtrusionRole extrusion_role{ExtrusionRole::None};

    // Various print settings?

    // Index of this entry in a linear vector.
    size_t idx = 0;

    bool operator<(const SurfaceFillParams &rhs) const
    {
#define RETURN_COMPARE_NON_EQUAL(KEY) \
    if (this->KEY < rhs.KEY)          \
        return true;                  \
    if (this->KEY > rhs.KEY)          \
        return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) \
    if (TYPE(this->KEY) < TYPE(rhs.KEY))          \
        return true;                              \
    if (TYPE(this->KEY) > TYPE(rhs.KEY))          \
        return false;

        // Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
        if (this->bridge_angle > rhs.bridge_angle)
            return true;
        if (this->bridge_angle < rhs.bridge_angle)
            return false;

        // TopSolidInfill must be processed first so it claims its area, then all other surfaces
        // (SolidInfill, sparse infill, etc.) get trimmed to avoid overlap.
        if (this->extrusion_role == ExtrusionRole::TopSolidInfill &&
            rhs.extrusion_role != ExtrusionRole::TopSolidInfill)
            return true; // TopSolidInfill goes first before everything
        if (this->extrusion_role != ExtrusionRole::TopSolidInfill &&
            rhs.extrusion_role == ExtrusionRole::TopSolidInfill)
            return false; // Everything else goes after TopSolidInfill

        RETURN_COMPARE_NON_EQUAL(extruder);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
        RETURN_COMPARE_NON_EQUAL(spacing);
        RETURN_COMPARE_NON_EQUAL(overlap);
        RETURN_COMPARE_NON_EQUAL(angle);
        RETURN_COMPARE_NON_EQUAL(density);
        //		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
        RETURN_COMPARE_NON_EQUAL(anchor_length);
        RETURN_COMPARE_NON_EQUAL(anchor_length_max);
        RETURN_COMPARE_NON_EQUAL(flow.width());
        RETURN_COMPARE_NON_EQUAL(flow.height());
        RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
        return this->extrusion_role.lower(rhs.extrusion_role);
    }

    bool operator==(const SurfaceFillParams &rhs) const
    {
        return this->extruder == rhs.extruder && this->pattern == rhs.pattern && this->spacing == rhs.spacing &&
               this->overlap == rhs.overlap && this->angle == rhs.angle && this->bridge == rhs.bridge &&
               //				this->bridge_angle 		== rhs.bridge_angle		&&
               this->density == rhs.density &&
               //				this->dont_adjust   	== rhs.dont_adjust 		&&
               this->anchor_length == rhs.anchor_length && this->anchor_length_max == rhs.anchor_length_max &&
               this->flow == rhs.flow && this->extrusion_role == rhs.extrusion_role;
    }
};

struct SurfaceFill
{
    SurfaceFill(const SurfaceFillParams &params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params)
    {
    }

    size_t region_id;
    Surface surface;
    ExPolygons expolygons;
    SurfaceFillParams params;
};

static inline bool fill_type_monotonic(InfillPattern pattern)
{
    return pattern == ipMonotonic || pattern == ipMonotonicLines;
}

std::vector<SurfaceFill> group_fills(const Layer &layer)
{
    std::vector<SurfaceFill> surface_fills;

    // First pass: Check if merge is enabled in config and collect top solid surface polygons per region.
    // We always collect top solid polygons (needed for both merge and concentric fallback).
    bool config_allows_merge = false;
    std::vector<Polygons> region_top_solid_polygons(layer.regions().size());

    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];

        // Check config (only need to check once, assume all regions have same setting)
        if (region_id == 0)
            config_allows_merge = layerm.region().config().merge_top_solid_infills;

        // Always collect top solid surface polygons for this region
        // (needed for merge check and for concentric fallback when merge is disabled)
        for (const Surface &surface : layerm.fill_surfaces())
        {
            if (surface.is_top())
            {
                Polygons polys = to_polygons(surface.expolygon);
                append(region_top_solid_polygons[region_id], polys);
            }
        }
    }

    // Check if an internal solid surface is spatially adjacent to (touching/overlapping) any top solid surface
    // in the same region. Returns true only when merge is enabled.
    auto is_surface_adjacent_to_top_solid = [&](const Surface &surface, size_t region_id) -> bool
    {
        if (!config_allows_merge || surface.surface_type != stInternalSolid)
            return false;

        const Polygons &top_solid_polys = region_top_solid_polygons[region_id];
        if (top_solid_polys.empty())
            return false;

        // Check if this internal solid surface intersects with any top solid surface
        Polygons internal_polys = to_polygons(surface.expolygon);
        Polygons intersection_result = intersection(internal_polys, top_solid_polys);
        return !intersection_result.empty();
    };

    // Used to determine if internal solid should use concentric pattern when merge is disabled
    auto is_internal_solid_touching_top = [&](const Surface &surface, size_t region_id) -> bool
    {
        if (surface.surface_type != stInternalSolid)
            return false;

        const Polygons &top_solid_polys = region_top_solid_polygons[region_id];
        if (top_solid_polys.empty())
            return false;

        Polygons internal_polys = to_polygons(surface.expolygon);
        Polygons intersection_result = intersection(internal_polys, top_solid_polys);
        return !intersection_result.empty();
    };

    // Check if a solid surface is too narrow for good rectilinear fill.
    // Uses medial axis to find the MAXIMUM width anywhere in the surface.
    // If max width < threshold, the surface is considered narrow.
    auto is_surface_narrow = [](const Surface &surface, const Flow &flow, float threshold_multiplier) -> bool
    {
        if (!surface.is_solid())
            return false;

        // Calculate the threshold width in scaled coordinates
        const coordf_t threshold_width = flow.scaled_width() * threshold_multiplier;

        // Use a small min_width and very large max_width to get the full medial axis
        const double min_width = flow.scaled_width() * 0.5; // Half extrusion width minimum
        const double max_width = 1e10;                      // Very large to capture all skeleton edges

        ThickPolylines polylines;
        surface.expolygon.medial_axis(min_width, max_width, &polylines);

        // If no medial axis, fall back to bounding box check.
        // Medial axis can fail for simple rectangular shapes due to Voronoi edge filtering.
        // Don't skip large surfaces just because medial axis computation failed.
        if (polylines.empty())
        {
            // Use bounding box minimum dimension as a simpler narrowness check
            const BoundingBox bbox = get_extents(surface.expolygon);
            const coordf_t min_dim = std::min(bbox.size().x(), bbox.size().y());
            // If the minimum dimension is larger than the threshold, it's not narrow
            return min_dim < threshold_width;
        }

        // Find the maximum width anywhere in the medial axis
        coordf_t max_found_width = 0;
        for (const ThickPolyline &tp : polylines)
        {
            for (coordf_t w : tp.width)
            {
                if (w > max_found_width)
                    max_found_width = w;
            }
        }

        // If the maximum width is less than threshold, the surface is narrow
        return max_found_width < threshold_width;
    };

    // Determine if this layer is part of the "top solid layers" group by checking if stTop exists
    // on this layer or any layer above. Also calculate distance to the top layer.
    bool layer_has_top_solid = false;
    int distance_to_top = -1; // -1 means not part of top solid group
    {
        // Check if THIS layer has stTop surfaces
        for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
        {
            for (const Surface &surface : layer.regions()[region_id]->fill_surfaces())
            {
                if (surface.is_top())
                {
                    layer_has_top_solid = true;
                    distance_to_top = 0;
                    break;
                }
            }
            if (layer_has_top_solid)
                break;
        }

        // If no stTop on this layer, check layers above to find distance to top
        if (!layer_has_top_solid)
        {
            const Layer *check_layer = layer.upper_layer;
            int dist = 1;
            while (check_layer != nullptr)
            {
                bool found_top = false;
                for (size_t region_id = 0; region_id < check_layer->regions().size(); ++region_id)
                {
                    for (const Surface &surface : check_layer->regions()[region_id]->fill_surfaces())
                    {
                        if (surface.is_top())
                        {
                            found_top = true;
                            distance_to_top = dist;
                            break;
                        }
                    }
                    if (found_top)
                        break;
                }
                if (found_top)
                    break;
                check_layer = check_layer->upper_layer;
                dist++;
                // Limit search to reasonable range (e.g., 20 layers)
                if (dist > 20)
                    break;
            }
        }
    }

    // Fill in a map of a region & surface to SurfaceFillParams.
    std::set<SurfaceFillParams> set_surface_params;
    std::vector<std::vector<const SurfaceFillParams *>> region_to_surface_params(
        layer.regions().size(), std::vector<const SurfaceFillParams *>());
    SurfaceFillParams params;
    bool has_internal_voids = false;
    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];
        region_to_surface_params[region_id].assign(layerm.fill_surfaces().size(), nullptr);
        for (const Surface &surface : layerm.fill_surfaces())
            if (surface.surface_type == stInternalVoid)
                has_internal_voids = true;
            else
            {
                const PrintRegionConfig &region_config = layerm.region().config();
                FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill
                                                           : (surface.is_solid() ? frSolidInfill : frInfill);
                bool is_bridge = layer.id() > 0 && surface.is_bridge();
                params.extruder = layerm.region().extruder(extrusion_role);
                params.pattern = region_config.fill_pattern.value;
                params.density = float(region_config.fill_density);

                if (surface.is_solid())
                {
                    params.density = 100.f;
                    //FIXME for non-thick bridges, shall we allow a bottom surface pattern?
                    // Use top fill pattern for actual top surfaces and internal solid surfaces that
                    // are spatially adjacent to (touching) top solid surfaces
                    if (is_bridge)
                    {
                        params.pattern = ipMonotonic;
                    }
                    else if (surface.is_top() || is_surface_adjacent_to_top_solid(surface, region_id))
                    {
                        // Top surface, or internal solid adjacent to top when merge is enabled
                        params.pattern = region_config.top_fill_pattern.value;
                    }
                    else if (!config_allows_merge && is_internal_solid_touching_top(surface, region_id))
                    {
                        // When merge is disabled but internal solid is touching top solid,
                        // use concentric pattern for visual distinction
                        params.pattern = ipConcentric;
                    }
                    else if (surface.is_external())
                    {
                        // External bottom surface
                        params.pattern = surface.is_top() ? region_config.top_fill_pattern.value
                                                          : region_config.bottom_fill_pattern.value;
                    }
                    else
                    {
                        // Internal solid: use user-selected solid fill pattern
                        params.pattern = region_config.solid_fill_pattern.value;
                    }

                    // When Athena perimeters converge, a narrow sliver may be created that should be covered
                    // by perimeters but gets classified as a fill surface. Skip these very narrow surfaces
                    // for stTop and stBottom only (internal solid may legitimately need filling).
                    // Threshold: 1.5× extrusion width - if narrower, perimeters should cover it.
                    // This always applies regardless of narrow_solid_infill_concentric setting.
                    if (surface.surface_type == stTop || surface.surface_type == stBottom)
                    {
                        const Flow solid_flow = layerm.flow(frSolidInfill);
                        const float skip_threshold = 1.5f; // Very narrow - skip entirely

                        if (is_surface_narrow(surface, solid_flow, skip_threshold))
                        {
                            continue; // Skip this surface entirely - too narrow to fill
                        }
                    }

                    // After pattern is selected, check if this is a narrow solid surface
                    // that should use concentric instead of the configured pattern.
                    // Applies to stInternalSolid, stTop, and stBottom - does NOT affect bridge infill.
                    if ((surface.surface_type == stInternalSolid || surface.surface_type == stTop ||
                         surface.surface_type == stBottom) &&
                        params.pattern != ipConcentric && region_config.narrow_solid_infill_concentric.value)
                    {
                        const Flow solid_flow = layerm.flow(frSolidInfill);
                        const float threshold = float(region_config.narrow_solid_infill_threshold.value);

                        if (is_surface_narrow(surface, solid_flow, threshold))
                        {
                            params.pattern = ipConcentric;
                        }
                    }
                }
                else if (params.density <= 0)
                {
                    // Even at 0% infill density, we need stInternal surfaces to define the sparse boundaries
                    // where interlocking perimeters should be generated. Check if this region has interlocking enabled.
                    const bool has_interlocking = region_config.interlock_perimeters_enabled &&
                                                  region_config.interlock_perimeter_count > 0;

                    // Only process stInternal surfaces at 0% density if interlocking is active
                    if (!(has_interlocking && surface.surface_type == stInternal))
                    {
                        continue;
                    }
                    // If we reach here: interlocking is enabled, surface is stInternal, density is 0%
                    // Allow it through so interlocking code can find the sparse regions
                    // Set a tiny non-zero density to avoid division-by-zero in downstream calculations
                    // This surface will be removed after interlocking processing, so the tiny value won't affect output
                    params.density = 0.001f; // Effectively zero, but prevents div-by-zero
                }

                if (is_bridge)
                {
                    params.extrusion_role = ExtrusionRole::BridgeInfill;
                }
                else
                {
                    if (surface.is_solid())
                    {
                        if (surface.is_top())
                        {
                            params.extrusion_role = ExtrusionRole::TopSolidInfill;
                        }
                        else if (surface.surface_type == stSolidOverBridge)
                        {
                            params.extrusion_role = ExtrusionRole::InfillOverBridge;
                        }
                        else
                        {
                            // Only use TopSolidInfill role for internal solid surfaces that are
                            // spatially adjacent to (touching) actual top solid surfaces
                            if (is_surface_adjacent_to_top_solid(surface, region_id))
                            {
                                params.extrusion_role = ExtrusionRole::TopSolidInfill;
                            }
                            else
                            {
                                params.extrusion_role = ExtrusionRole::SolidInfill;
                            }
                        }
                    }
                    else
                    {
                        params.extrusion_role = ExtrusionRole::InternalInfill;
                    }
                }
                params.bridge_angle = float(surface.bridge_angle);
                params.angle = float(Geometry::deg2rad(region_config.fill_angle.value));

                // Visible surfaces (stTop, stBottom) use fill_angle directly.
                // Internal solid layers alternate 90° based on distance from the visible surface.
                // This ensures the user-configured fill_angle appears on visible surfaces,
                // with proper cross-hatching on layers below/above.
                if (surface.is_solid() && !is_bridge)
                {
                    if (surface.is_top() || surface.is_bottom())
                    {
                        // Visible top/bottom: use fill_angle directly (no rotation)
                        // params.angle already set correctly
                    }
                    else if (surface.surface_type == stInternalSolid)
                    {
                        // Internal solid: calculate rotation based on distance from visible surface
                        if (distance_to_top >= 0)
                        {
                            // Part of top solid layers group - alternate based on distance from top
                            // distance_to_top=0 means same layer as stTop, distance_to_top=1 means one layer below, etc.
                            if (distance_to_top % 2 == 1)
                            {
                                params.angle += float(M_PI / 2.0); // Odd distance: rotate 90°
                            }
                        }
                        else
                        {
                            // Part of bottom solid layers group - alternate based on layer_id
                            // layer_id=0 is stBottom (visible), layer_id=1 should be rotated, etc.
                            if (layer.id() % 2 == 1)
                            {
                                params.angle += float(M_PI / 2.0); // Odd layer: rotate 90°
                            }
                        }
                    }
                }

                // Calculate the actual flow we'll be using for this infill.
                params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
                params.flow = params.bridge ?
                                            // Always enable thick bridges for internal bridges.
                                  layerm.bridging_flow(extrusion_role, surface.is_bridge() && !surface.is_external())
                                            : layerm.flow(extrusion_role,
                                                          (surface.thickness == -1) ? layer.height : surface.thickness);

                // Calculate flow spacing for infill pattern generation.
                // Treat near-solid density (>= 99.9999%) like solid for spacing purposes to avoid
                // underextrusion when fill surface thickness differs from layer height.
                if (surface.is_solid() || is_bridge || params.density >= 99.9999f)
                {
                    if (is_bridge)
                    {
                        float bridge_diameter = params.flow.width(); // For bridges, width == height == diameter

                        // Line-to-line spacing (bridge_infill_overlap setting)
                        float line_overlap_percent;
                        if (region_config.bridge_infill_overlap.percent)
                        {
                            line_overlap_percent = float(region_config.bridge_infill_overlap.value);
                        }
                        else
                        {
                            line_overlap_percent = float(region_config.bridge_infill_overlap.value) / bridge_diameter *
                                                   100.0f;
                        }
                        line_overlap_percent = std::clamp(line_overlap_percent, -100.0f, 80.0f);
                        params.spacing = bridge_diameter * (1.0f - line_overlap_percent / 100.0f);
                    }
                    else
                    {
                        params.spacing = params.flow.spacing();
                    }
                    // Only apply overlap and anchor settings for actual solid/bridge, not high-density sparse
                    if (surface.is_solid() || is_bridge)
                    {
                        // Overlap = 0 because bridge surface geometry is already adjusted in LayerRegion.cpp
                        // by expand_bridges_for_overlap() which runs AFTER the merge logic completes.
                        // This ensures: merge first, then expand for overlap on final geometry.
                        params.overlap = 0.0f;
                        // Don't limit anchor length for solid or bridging infill.
                        params.anchor_length = 1000.f;
                        params.anchor_length_max = 1000.f;
                    }
                }
                else
                {
                    // Internal infill. Calculating infill line spacing independent of the current layer height and 1st layer status,
                    // so that internall infill will be aligned over all layers of the current region.
                    params.spacing = layerm.region()
                                         .flow(*layer.object(), frInfill, layer.object()->config().layer_height, false)
                                         .spacing();
                    // When fill surface thickness differs from layer height, rescale width to maintain
                    // requested density with the rounded rectangle extrusion model.
                    params.flow = params.flow.with_spacing(params.spacing);

                    // When interlocking perimeters are enabled, infill anchors create overlaps and conflicts.
                    // Interlocking perimeters provide their own bonding to real perimeters via P/P overlap.
                    // Override anchor_length to 0 when interlocking is active (user's settings preserved in UI).
                    // IMPORTANT: We only set anchor_length to 0, NOT anchor_length_max. Setting anchor_length_max
                    // to 0 causes dont_connect() to return true, which disables zigzag patterns entirely.
                    // We want to disable perimeter anchoring but still allow infill-to-infill zigzag connections.
                    const bool has_interlocking = region_config.interlock_perimeters_enabled &&
                                                  layerm.num_interlocking_shells() > 0;

                    // Anchor a sparse infill to inner perimeters with the following anchor length:
                    params.anchor_length = has_interlocking ? 0.0f : float(region_config.infill_anchor);
                    if (!has_interlocking && region_config.infill_anchor.percent)
                        params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
                    params.anchor_length_max = float(region_config.infill_anchor_max);
                    if (region_config.infill_anchor_max.percent)
                        params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
                    params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
                }

                auto it_params = set_surface_params.find(params);
                if (it_params == set_surface_params.end())
                    it_params = set_surface_params.insert(it_params, params);
                region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()] = &(
                    *it_params);
            }
    }

    surface_fills.reserve(set_surface_params.size());
    for (const SurfaceFillParams &params : set_surface_params)
    {
        const_cast<SurfaceFillParams &>(params).idx = surface_fills.size();
        surface_fills.emplace_back(params);
    }

    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];
        for (const Surface &surface : layerm.fill_surfaces())
            if (surface.surface_type != stInternalVoid)
            {
                const SurfaceFillParams *params =
                    region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()];
                if (params != nullptr)
                {
                    SurfaceFill &fill = surface_fills[params->idx];

                    if (fill.region_id == size_t(-1))
                    {
                        fill.region_id = region_id;
                        fill.surface = surface;
                        fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
                    }
                    else
                        fill.expolygons.emplace_back(surface.expolygon);
                }
            }
    }

    {
        Polygons all_polygons;
        for (SurfaceFill &fill : surface_fills)
            if (!fill.expolygons.empty())
            {
                if (fill.expolygons.size() > 1 || !all_polygons.empty())
                {
                    Polygons polys = to_polygons(std::move(fill.expolygons));
                    // Make a union of polygons, use a safety offset, subtract the preceding polygons.
                    // Bridges are processed first (see SurfaceFill::operator<())

                    // When trimming solid infill, add clearance to prevent overlap after both surfaces
                    // expand by outer_offset during fill generation. Without clearance, edge-to-edge
                    // surfaces both expand toward the shared boundary and overlap by ~1 bead width.
                    Polygons trim_polygons = all_polygons;
                    if (!all_polygons.empty() && fill.params.extrusion_role == ExtrusionRole::SolidInfill &&
                        fill.params.density > 0.99f)
                    {
                        // Add 25% bead width clearance when trimming non-adjacent solid infill
                        // This prevents overlap when both TopSolid and InternalSolid expand during fill
                        const float clearance = float(fill.params.flow.width() * 0.25);
                        trim_polygons = offset(all_polygons, scale_(clearance));
                    }

                    fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys)
                                                           : diff_ex(polys, trim_polygons, ApplySafetyOffset::Yes);
                    append(all_polygons, std::move(polys));
                }
                else if (&fill != &surface_fills.back())
                    append(all_polygons, to_polygons(fill.expolygons));
            }
    }

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids)
    {
        // Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
        int region_internal_infill = -1;
        int region_solid_infill = -1;
        int region_some_infill = -1;
        for (SurfaceFill &surface_fill : surface_fills)
            if (!surface_fill.expolygons.empty())
            {
                distance_between_surfaces = std::max(distance_between_surfaces,
                                                     surface_fill.params.flow.scaled_spacing());
                append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons,
                       to_polygons(surface_fill.expolygons));
                if (surface_fill.surface.surface_type == stInternalSolid)
                    region_internal_infill = (int) surface_fill.region_id;
                if (surface_fill.surface.is_solid())
                    region_solid_infill = (int) surface_fill.region_id;
                if (surface_fill.surface.surface_type != stInternalVoid)
                    region_some_infill = (int) surface_fill.region_id;
            }
        if (!voids.empty() && !surfaces_polygons.empty())
        {
            // First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
            voids = diff(voids, surfaces_polygons);
            // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
            Polygons collapsed = diff(surfaces_polygons,
                                      opening(surfaces_polygons, float(distance_between_surfaces / 2),
                                              float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
            //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
            // added if two offsetted void regions merge.
            // polygons_append(voids, collapsed);
            ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids,
                                                    ApplySafetyOffset::Yes);
            // Now find an internal infill SurfaceFill to add these extrusions to.
            SurfaceFill *internal_solid_fill = nullptr;
            unsigned int region_id = 0;
            if (region_internal_infill != -1)
                region_id = region_internal_infill;
            else if (region_solid_infill != -1)
                region_id = region_solid_infill;
            else if (region_some_infill != -1)
                region_id = region_some_infill;
            const LayerRegion &layerm = *layer.regions()[region_id];
            for (SurfaceFill &surface_fill : surface_fills)
                if (surface_fill.surface.surface_type == stInternalSolid &&
                    std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON)
                {
                    internal_solid_fill = &surface_fill;
                    break;
                }
            if (internal_solid_fill == nullptr)
            {
                // Produce another solid fill.
                params.extruder = layerm.region().extruder(frSolidInfill);
                params.pattern = layerm.region().config().solid_fill_pattern.value;
                params.density = 100.f;
                params.extrusion_role = ExtrusionRole::InternalInfill;
                params.angle = float(Geometry::deg2rad(layerm.region().config().fill_angle.value));
                // calculate the actual flow we'll be using for this infill
                params.flow = layerm.flow(frSolidInfill);
                params.spacing = params.flow.spacing();
                surface_fills.emplace_back(params);
                surface_fills.back().surface.surface_type = stInternalSolid;
                surface_fills.back().surface.thickness = layer.height;
                surface_fills.back().expolygons = std::move(extensions);
            }
            else
            {
                append(extensions, std::move(internal_solid_fill->expolygons));
                internal_solid_fill->expolygons = union_ex(extensions);
            }
        }
    }

    // This was forcing ALL stInternalSolid surfaces to use ipEnsuring (Athena-style fill),
    // overriding the user's top_fill_pattern selection (Monotonic, Rectilinear, etc.).
    // ipEnsuring uses WallToolPaths which doesn't adjust spacing like FillRectilinear does,
    // causing gaps when fill runs parallel to the area.
    // Comment out this forced override to respect the user's pattern selection.
    /*
    // Use ipEnsuring pattern for all internal Solids.
    {
        for (size_t surface_fill_id = 0; surface_fill_id < surface_fills.size(); ++surface_fill_id)
            if (SurfaceFill &fill = surface_fills[surface_fill_id];
                    fill.surface.surface_type == stInternalSolid
                    || fill.surface.surface_type == stSolidOverBridge) {
                fill.params.pattern = ipEnsuring;
            }
    }
    */

    return surface_fills;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif

// Infill is now generated and assigned directly to islands in make_fills().
// This function is no longer called and remains only for backward compatibility.
static void insert_fills_into_islands(Layer &layer, uint32_t fill_region_id, uint32_t fill_begin, uint32_t fill_end)
{
    // No-op: Infill assignment now happens during per-island generation
}

void Layer::clear_fills()
{
    for (LayerRegion *layerm : m_regions)
        layerm->m_fills.clear();
    for (LayerSlice &lslice : lslices_ex)
        for (LayerIsland &island : lslice.islands)
            island.fills.clear();

    // Interlocking perimeters are generated in make_fills() based on sparse infill boundaries.
    // When infill changes (perimeter count, solid layers, etc.), sparse boundaries change and
    // interlocking must be regenerated. Remove old interlocking to prevent duplicates on re-slice.
    //
    // Process each region separately (each has its own m_perimeters collection)
    for (size_t region_id = 0; region_id < m_regions.size(); ++region_id)
    {
        LayerRegion *layerm = m_regions[region_id];

        // Iterate islands in reverse order to safely remove without index invalidation issues
        for (LayerSlice &lslice : this->lslices_ex)
        {
            for (auto island_it = lslice.islands.rbegin(); island_it != lslice.islands.rend(); ++island_it)
            {
                LayerIsland &island = *island_it;

                // Only process islands in this region
                if (island.perimeters.region() != region_id)
                    continue;

                // Check if this island has interlocking by examining the last entity in its range
                uint32_t island_begin = *island.perimeters.begin();
                uint32_t island_end = *island.perimeters.end();

                if (island_end <= island_begin)
                    continue; // Empty range

                uint32_t last_index = island_end - 1;
                if (last_index >= layerm->m_perimeters.entities.size())
                    continue; // Invalid index

                ExtrusionEntity *last_entity = layerm->m_perimeters.entities[last_index];

                // Check if this is an interlocking collection
                bool is_interlocking = false;
                if (const ExtrusionEntityCollection *collection = dynamic_cast<const ExtrusionEntityCollection *>(
                        last_entity))
                {
                    if (!collection->entities.empty())
                    {
                        if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop *>(
                                collection->entities.front()))
                        {
                            if (!loop->paths.empty() &&
                                loop->paths.front().role() == ExtrusionRole::InterlockingPerimeter)
                            {
                                is_interlocking = true;
                            }
                        }
                    }
                }

                if (!is_interlocking)
                    continue; // No interlocking in this island

                // Remove the interlocking collection
                delete last_entity;
                layerm->m_perimeters.entities.erase(layerm->m_perimeters.entities.begin() + last_index);

                // Shrink this island's range by 1 (remove the interlocking)
                island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(island_begin, island_end - 1));

                // Shift all other islands' ranges back by 1 (inverse of insertion shift)
                for (LayerSlice &other_lslice : this->lslices_ex)
                {
                    for (LayerIsland &other_island : other_lslice.islands)
                    {
                        if (&other_island == &island)
                            continue; // Skip this island
                        if (other_island.perimeters.region() != region_id)
                            continue; // Different region

                        uint32_t other_begin = *other_island.perimeters.begin();
                        uint32_t other_end = *other_island.perimeters.end();

                        // Only shift ranges that reference indices > last_index
                        if (other_begin > last_index)
                        {
                            other_island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(other_begin - 1,
                                                                                                    other_end - 1));
                        }
                        else if (other_end > last_index)
                        {
                            // Range spans the removed index - shouldn't happen, but handle gracefully
                            other_island.perimeters = LayerExtrusionRange(region_id,
                                                                          ExtrusionRange(other_begin, other_end - 1));
                        }
                    }
                }
            }
        }
    }
}

void Layer::make_fills(FillAdaptive::Octree *adaptive_fill_octree, FillAdaptive::Octree *support_fill_octree,
                       FillLightning::Generator *lightning_generator)
{
    this->clear_fills();

    std::vector<SurfaceFill> surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox = this->object()->bounding_box();
    const auto resolution = this->object()->print()->config().gcode_resolution.value;
    const auto perimeter_generator = this->object()->config().perimeter_generator;

    // Process each region to add interlocking shells at sparse boundaries

    for (size_t region_id = 0; region_id < m_regions.size(); ++region_id)
    {
        LayerRegion *layerm = m_regions[region_id];

        if (!layerm->region().config().interlock_perimeters_enabled)
            continue;

        // Get the interlocking count directly - no percentage calculation needed!
        const int num_interlocking_shells = layerm->num_interlocking_shells();

        if (num_interlocking_shells <= 0)
            continue;

        // When interlocking is enabled, extract sparse infill and bridge infill for interlocking perimeters.
        //
        // ARCHITECTURAL NOTE: discover_vertical_shells() creates TWO types of stInternalSolid:
        // 1. Top/bottom solid support (expanding layers under stTop/above stBottom) - must PRESERVE
        // 2. Vertical shell rings (perimeter-to-sparse gap fill) - should EXTRACT for interlocking
        //
        // We use detection heuristics to distinguish these types:
        // - Proximity to stTop/stBottom surfaces (within N layers)
        // - Area change ratio between adjacent layers (expanding/contracting = solid support)
        // - Conservative fallback (when uncertain, preserve to avoid gaps in solid support)
        //
        // We extract:
        // - stInternal (sparse infill) - primary target for interlocking
        // - stInternalBridge/stBottomBridge - interlocking provides better support than bridge infill
        // - stInternalSolid (ONLY vertical shell rings detected via heuristics)
        //
        // We preserve:
        // - stInternalSolid (top/bottom solid support detected via heuristics)
        // - stTop/stBottom - external surfaces

        // Interlocking perimeters must be generated separately for each island to ensure proper island ordering
        // and prevent duplicate printing. This follows the same architectural pattern as regular perimeters and infill:
        // 1. Extract geometry per-island (using spatial intersection with island.boundary)
        // 2. Generate extrusions for each island independently
        // 3. Assign to islands directly (no global collections)
        //
        // This ensures each island prints completely (perimeters + interlocking + infill) before moving to the next,
        // maintaining proper print order and preventing the bug where all islands reference the same unified collection.

        // Iterate through all islands to generate interlocking for each independently
        int island_counter = 0; // Track island index for debug

        for (LayerSlice &lslice : this->lslices_ex)
        {
            for (LayerIsland &island : lslice.islands)
            {
                // Only process islands belonging to this region
                if (island.perimeters.region() != region_id)
                    continue;

                island_counter++; // Increment for each island processed

                // With the upstream fix in PrintObject.cpp (lines 1827-1841), vertical shell rings are no longer
                // created when interlocking is enabled. Therefore, any stInternalSolid that exists is legitimate
                // solid support (projected from stTop/stBottom surfaces) and should be preserved.
                //
                // We only need to extract:
                // - stInternal (sparse infill) → for interlocking perimeters
                // And preserve:
                // - stInternalSolid (all legitimate solid support)
                // - stInternalBridge/stBottomBridge (bridge infill - generates normally)
                // - stTop/stBottom (external surfaces)
                //
                // CRITICAL: Extract sparse regions for THIS ISLAND ONLY using spatial intersection with island.boundary.
                // This prevents mixing sparse regions from different islands into one unified collection.
                ExPolygons sparse_regions;
                ExPolygons solid_regions; // Collect all solid areas to subtract from sparse

                // Process each surface type - extract only regions that intersect with THIS island
                for (size_t sf_idx = 0; sf_idx < surface_fills.size(); sf_idx++)
                {
                    SurfaceFill &sf = surface_fills[sf_idx];

                    if (sf.region_id != region_id)
                        continue;

                    if (sf.surface.surface_type == stInternal)
                    {
                        // Extract sparse infill for THIS island using spatial intersection
                        ExPolygons island_sparse = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(sparse_regions, island_sparse);
                    }
                    else if (sf.surface.surface_type == stInternalBridge || sf.surface.surface_type == stBottomBridge)
                    {
                        // Bridge infill (stInternalBridge, stBottomBridge) has specific properties that shouldn't be replaced.
                        // Collect bridge areas to subtract from interlocking sparse regions to avoid overlap.
                        // Extract only bridge areas that intersect with THIS island
                        ExPolygons island_bridge = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(solid_regions, island_bridge);
                    }
                    else if (sf.surface.surface_type == stInternalSolid)
                    {
                        // Preserve all stInternalSolid - upstream fix prevents vertical shell rings
                        // Only legitimate solid support (projected from stTop/stBottom) and bridge anchors exist
                        // Collect solid areas to subtract from interlocking sparse regions to avoid overlap
                        // Extract only solid areas that intersect with THIS island
                        ExPolygons island_solid = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(solid_regions, island_solid);
                        continue; // Keep as solid infill
                    }
                }

                // Bridge infill (stInternalBridge) should be subtracted from sparse to avoid overlaps.
                // Bridge support anchors (stInternalSolid created by bridge_over_infill) are NOT created when interlocking
                // is enabled, so we don't need to worry about them here.
                if (!solid_regions.empty())
                {
                    sparse_regions = diff_ex(sparse_regions, solid_regions);
                }

                // Surface extraction complete - sparse_regions contains only stInternal (sparse infill) for THIS island
                // All other surface types (stInternalSolid, stInternalBridge, stTop, stBottom) are preserved

                // group_fills() can fragment sparse regions via diff_ex() when subtracting solid surfaces (e.g., top layer transition beads).
                // Fragmented regions create jagged boundaries with excess vertices, causing Athena to fragment toolpaths.
                // Merge fragments back into unified regions for clean, smooth interlocking perimeter generation.
                // NOTE: This now merges fragments WITHIN this island only, not across islands.
                if (sparse_regions.size() > 1)
                {
                    // Union all fragments with a small safety offset to merge adjacent pieces
                    sparse_regions = union_safety_offset_ex(to_polygons(sparse_regions));
                }

                // The INTERLOCK-SUPPRESS-BRIDGE-SOLID logic in PrintObject.cpp can create degenerate
                // polygons with zero area (collapsed lines/points). These cause Athena to generate
                // malformed toolpaths (single diagonal lines instead of proper shells).
                // Filter them out before interlocking generation.
                const double min_area_threshold = scale_(scale_(0.1)); // 0.1 mm² minimum
                size_t before_filter = sparse_regions.size();
                sparse_regions.erase(std::remove_if(sparse_regions.begin(), sparse_regions.end(),
                                                    [min_area_threshold](const ExPolygon &ep)
                                                    { return ep.area() < min_area_threshold; }),
                                     sparse_regions.end());

                // Skip this island if no sparse regions remain
                if (sparse_regions.empty())
                {
                    continue;
                }

                // Get flow parameters for shell width calculation
                const Flow perimeter_flow = layerm->flow(frPerimeter);
                const coord_t perimeter_scaled_width = perimeter_flow.scaled_width();

                // Check minimum area before processing (5mm² minimum)
                const coord_t min_area = scale_(scale_(5.0));
                double total_area = 0;
                for (const ExPolygon &ex : sparse_regions)
                {
                    total_area += ex.area();
                }

                if (total_area < min_area)
                {
                    continue;
                }

                // The sparse_regions boundary already has infill/perimeter overlap baked in from upstream
                // (PerimeterGenerator.cpp). When interlocking is enabled, we want P/P overlap to control
                // the overlap between innermost normal perimeter and outermost interlocking instead.
                // So we adjust by (pp_overlap - infill_overlap) to replace infill_overlap with pp_overlap.

                // Get P/P overlap amount (what we want for outer interlocking)
                // Must match what the perimeter generator actually uses:
                // - Arachne: hardcoded overlap of (1 - 0.25*PI) ≈ 21.46%
                // - Athena: uses perimeter_perimeter_overlap setting
                coord_t pp_overlap_amount = 0;
                if (perimeter_generator == PerimeterGeneratorType::Arachne)
                {
                    // Arachne uses hardcoded overlap: (1 - 0.25*PI) ≈ 21.46% of perimeter width
                    constexpr double arachne_overlap_percent = (1.0 - 0.25 * M_PI); // ≈ 0.2146
                    pp_overlap_amount = coord_t(perimeter_scaled_width * arachne_overlap_percent);
                }
                else
                {
                    // Athena uses the perimeter_perimeter_overlap setting
                    const ConfigOptionFloatOrPercent &pp_overlap = layerm->region().config().perimeter_perimeter_overlap;
                    if (pp_overlap.percent)
                    {
                        pp_overlap_amount = coord_t(perimeter_scaled_width * (pp_overlap.value / 100.0));
                    }
                    else
                    {
                        pp_overlap_amount = coord_t(scale_(pp_overlap.value));
                    }
                }

                // Get infill/perimeter overlap amount (what was already applied upstream)
                const ConfigOptionFloatOrPercent &infill_overlap = layerm->region().config().infill_overlap;
                coord_t infill_overlap_amount = 0;
                if (infill_overlap.percent)
                {
                    infill_overlap_amount = coord_t(perimeter_scaled_width * (infill_overlap.value / 100.0));
                }
                else
                {
                    infill_overlap_amount = coord_t(scale_(infill_overlap.value));
                }

                // Net adjustment: replace infill_overlap with pp_overlap
                // Positive = expand outward, negative = shrink inward
                coord_t overlap_adjustment = pp_overlap_amount - infill_overlap_amount;

                // Before expanding sparse for P/P overlap, extract any stTop surfaces to avoid conflicts.
                // If we expand into top areas, it creates a ring of incorrectly classified top surfaces
                // around the perimeter that shifts interlocking inward.
                // CRITICAL: Extract only top surfaces that intersect with THIS island
                ExPolygons top_surfaces;
                for (const SurfaceFill &sf : surface_fills)
                {
                    if (sf.region_id == region_id && sf.surface.surface_type == stTop)
                    {
                        ExPolygons island_tops = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(top_surfaces, island_tops);
                    }
                }

                // Save original sparse_regions before adjustment (needed for consumed area calculation)
                // The original has infill_overlap boundary from PerimeterGenerator
                ExPolygons original_sparse_regions = sparse_regions;

                // Adjust sparse regions: replace infill_overlap with pp_overlap for outer interlocking
                ExPolygons expanded_sparse = offset_ex(sparse_regions, float(overlap_adjustment));

                // Ensure expanded sparse doesn't overlap with top surfaces
                if (!top_surfaces.empty())
                {
                    expanded_sparse = diff_ex(expanded_sparse, top_surfaces);
                }

                // Use expanded sparse regions for interlocking generation
                sparse_regions = expanded_sparse;

                if (sparse_regions.empty())
                {
                    continue;
                }

                // Define spacing for BOTH layer types upfront for symmetry
                const bool is_odd_layer = (this->id() % 2 == 1);

                // Interlocking shell-to-shell overlap fraction.
                // Zero overlap preserves the interlocking pattern geometry.
                // Bead fattening for bonding is handled by interlock_perimeter_overlap in GCode.cpp.
                constexpr double INTERLOCKING_OVERLAP_FRACTION = 0.0;

                // External spacing for each layer type
                const coord_t odd_external_spacing = coord_t(
                    perimeter_scaled_width * (1.0 - INTERLOCKING_OVERLAP_FRACTION)); // 0% overlap (adjacent/touching)
                const coord_t even_external_spacing = coord_t(
                    perimeter_scaled_width * (2.0 - INTERLOCKING_OVERLAP_FRACTION)); // -100% overlap (full gap)

                // Current layer's external spacing (alternates between layers)
                const coord_t external_spacing = is_odd_layer ? odd_external_spacing : even_external_spacing;

                // Internal spacing (middle shells): gapped
                const coord_t internal_spacing = coord_t(
                    perimeter_scaled_width *
                    (2.0 - INTERLOCKING_OVERLAP_FRACTION)); // -100% overlap (full gap between shells)

                // Innermost spacing: mirrors the opposite layer's external spacing for symmetry
                // Odd layers get even layer's external spacing (gapped)
                // Even layers get odd layer's external spacing (adjacent)
                // This creates interlocking on BOTH outer and inner edges
                const coord_t innermost_spacing = is_odd_layer ? even_external_spacing : odd_external_spacing;

                // Handle edge case: reduce shells if space is too constrained
                BoundingBox sparse_bbox = get_extents(sparse_regions);
                const coord_t min_dimension = std::min(sparse_bbox.size().x(), sparse_bbox.size().y());
                const coord_t estimated_shell_width = num_interlocking_shells * perimeter_scaled_width * 2;

                int actual_shells = num_interlocking_shells;
                if (min_dimension < estimated_shell_width)
                {
                    actual_shells = min_dimension / (perimeter_scaled_width * 2);
                    if (actual_shells <= 0)
                        continue;
                }

                // The input polygons have coarse discretization from slicing (circles become ~8-32 vertex polygons).
                // Athena uses Voronoi which inherently smooths, but our offset approach preserves vertices.
                // Apply triple offset (morphological close+open) to smooth small irregularities,
                // then simplify to clean up artifacts. This is what Athena does in WallToolPaths.cpp:585-594.
                {
                    const float epsilon = float(scale_(0.05)); // 50 micron smoothing
                    ExPolygons smoothed;
                    for (const ExPolygon &ep : sparse_regions)
                    {
                        // Triple offset: shrink, expand 2x, shrink (smooths corners and small features)
                        ExPolygons step1 = offset_ex(ep, -epsilon);
                        ExPolygons step2 = offset_ex(step1, epsilon * 2);
                        ExPolygons step3 = offset_ex(step2, -epsilon);
                        append(smoothed, step3);
                    }
                    // Simplify to remove unnecessary vertices (10 micron tolerance)
                    sparse_regions = expolygons_simplify(smoothed, scale_(0.01));
                }

                // This approach bypasses Athena's skeleton-based generation which causes pinching.
                //
                // ITERATIVE ALGORITHM (like vector drawing tools):
                // 1. Start with original shape (sparse_regions)
                // 2. For each shell:
                //    a. Offset boundary inward by step_offset
                //    b. Offset holes outward by step_offset
                //    c. Clip where they overlap
                //    d. Create shell from clipped result
                //    e. USE CLIPPED RESULT as input for next iteration
                //
                // INTERLOCKING PATTERN:
                // ODD layers:  oo o o o o  (merged at outer, gapped toward inner)
                //   - Shell 0: 1.0 flow, Shell 1: 50% str, rest: 100% str
                //   - Spacing: half_width, adjacent, gapped, gapped, ...
                // EVEN layers: o o o o oo  (gapped at outer, merged at inner)
                //   - Shell N-1: 1.0 flow, Shell N-2: 50% str, rest: 100% str
                //   - Spacing: half_width, gapped, ..., gapped, adjacent

                // NOTE: Flow adjustments are handled in GCode generation (preFlight.GCode.cpp)
                // Fill.cpp only handles geometry - all shells use base flow (1.0)

                // Create an ExtrusionEntityCollection to hold all interlocking loops
                ExtrusionEntityCollection interlocking_collection;

                // Spacing constants (with overlap compensation)
                const coord_t half_width = perimeter_scaled_width / 2;

                // Position first shell with visual outer edge at sparse boundary (center at half_width).
                // No overlap from interlocking side - let the perimeter's built-in overlap do the bonding.
                const coord_t first_shell_offset = half_width;
                const coord_t gapped_spacing = coord_t(perimeter_scaled_width *
                                                       (2.0 - INTERLOCKING_OVERLAP_FRACTION)); // -100% overlap (full gap)
                const coord_t adjacent_spacing = coord_t(perimeter_scaled_width *
                                                         (1.0 - INTERLOCKING_OVERLAP_FRACTION)); // 0% overlap (touching)

                // Build list of (step_offset, flow_ratio) for each shell
                // step_offset is INCREMENTAL (from previous shell), not cumulative
                // flow_ratio is always 1.0 - actual flow handled dynamically in GCode
                std::vector<std::pair<coord_t, double>> shell_specs;

                for (int shell_idx = 0; shell_idx < actual_shells; ++shell_idx)
                {
                    coord_t step_offset;

                    if (shell_idx == 0)
                    {
                        step_offset = first_shell_offset; // Overlap with innermost normal perimeter
                    }
                    else if (shell_idx == 1)
                    {
                        step_offset = is_odd_layer ? adjacent_spacing : gapped_spacing;
                    }
                    else if (shell_idx == actual_shells - 1 && actual_shells > 2)
                    {
                        step_offset = is_odd_layer ? gapped_spacing : adjacent_spacing;
                    }
                    else
                    {
                        step_offset = gapped_spacing;
                    }

                    shell_specs.push_back({step_offset, 1.0}); // Flow handled in GCode
                }

                // Generate each shell using ITERATIVE geometric offsets
                const bool prefer_clockwise = this->object()->print()->config().prefer_clockwise_movements;

                // PROBLEM: Default generation order is depth-first:
                //   Shell 0: Contour A, Hole 1, Hole 2, Contour B, Hole 3...
                //   Shell 1: Contour A, Hole 1, Hole 2, Contour B, Hole 3...
                // This causes excessive travel between unrelated regions.
                //
                // SOLUTION: Collect all loops with metadata, build containment tree, traverse depth-first
                // so each region completes all its shells before moving to the next region:
                //   Contour A: Shell 0, Shell 1, Shell 2...
                //   Hole 1: Shell 0, Shell 1, Shell 2...
                //   etc.

                // Structure to hold loop with metadata
                struct LoopNode
                {
                    ExtrusionLoop loop;
                    Polygon polygon; // For containment testing
                    size_t shell_idx;
                    bool is_hole;
                    std::vector<size_t> children;
                    size_t parent = SIZE_MAX;
                };
                std::vector<LoopNode> all_loops;

                // Modified create_loop to collect instead of append
                auto collect_loop = [&](const Polygon &poly, double flow_ratio, size_t shell_idx, bool is_hole)
                {
                    if (poly.size() < 3)
                        return;

                    ExtrusionFlow shell_flow(perimeter_flow.mm3_per_mm() * flow_ratio, perimeter_flow.width(),
                                             perimeter_flow.height());
                    ExtrusionAttributes attribs(ExtrusionRole::InterlockingPerimeter, shell_flow);
                    attribs.perimeter_index = static_cast<uint16_t>(shell_idx);

                    ExtrusionPath path(attribs);
                    for (const Point &pt : poly.points)
                    {
                        path.polyline.append(pt);
                    }
                    if (path.polyline.first_point() != path.polyline.last_point())
                    {
                        path.polyline.append(path.polyline.first_point());
                    }

                    ExtrusionPaths paths;
                    paths.push_back(std::move(path));
                    ExtrusionLoop loop(std::move(paths));

                    // Orient based on user preference
                    const bool is_cw = loop.is_clockwise();
                    bool should_reverse;
                    if (is_hole)
                    {
                        should_reverse = prefer_clockwise ? is_cw : !is_cw;
                    }
                    else
                    {
                        should_reverse = prefer_clockwise ? !is_cw : is_cw;
                    }
                    if (should_reverse)
                    {
                        loop.reverse_loop();
                    }

                    LoopNode node;
                    node.loop = std::move(loop);
                    node.polygon = poly;
                    node.shell_idx = shell_idx;
                    node.is_hole = is_hole;
                    all_loops.push_back(std::move(node));
                };

                // Generate all loops (depth-first order initially)
                ExPolygons last_shell_area;
                ExPolygons current_regions = sparse_regions;

                for (size_t shell_idx = 0; shell_idx < shell_specs.size(); ++shell_idx)
                {
                    coord_t step_offset = shell_specs[shell_idx].first;
                    double flow_ratio = shell_specs[shell_idx].second;

                    ExPolygons next_regions;

                    for (const ExPolygon &current : current_regions)
                    {
                        Polygons boundary_offset = offset(current.contour, -float(step_offset));
                        Polygons holes_offset;
                        if (!current.holes.empty())
                        {
                            holes_offset = offset(current.holes, -float(step_offset));
                        }

                        ExPolygons clipped_boundary;
                        if (!boundary_offset.empty())
                        {
                            if (holes_offset.empty())
                            {
                                clipped_boundary = union_ex(boundary_offset);
                            }
                            else
                            {
                                Polygons expanded_holes_positive = holes_offset;
                                for (Polygon &p : expanded_holes_positive)
                                {
                                    p.reverse();
                                }
                                clipped_boundary = diff_ex(boundary_offset, expanded_holes_positive);
                            }

                            for (const ExPolygon &ep : clipped_boundary)
                            {
                                collect_loop(ep.contour, flow_ratio, shell_idx, false);
                                for (const Polygon &hole : ep.holes)
                                {
                                    Polygon hole_as_contour = hole;
                                    hole_as_contour.reverse();
                                    collect_loop(hole_as_contour, flow_ratio, shell_idx, true);
                                }
                            }
                        }

                        if (!clipped_boundary.empty())
                        {
                            append(next_regions, clipped_boundary);
                        }
                    }

                    current_regions = std::move(next_regions);
                    if (shell_idx == shell_specs.size() - 1)
                    {
                        last_shell_area = current_regions;
                    }
                }

                // Build containment tree: for each loop, find smallest containing parent OF SAME TYPE
                // Critical: only match holes with holes, contours with contours
                // Otherwise outer boundary would be detected as parent of hole shells
                for (size_t i = 0; i < all_loops.size(); ++i)
                {
                    Point test_point = all_loops[i].polygon.points.empty() ? Point(0, 0)
                                                                           : all_loops[i].polygon.points.front();
                    double smallest_area = std::numeric_limits<double>::max();
                    size_t best_parent = SIZE_MAX;

                    for (size_t j = 0; j < all_loops.size(); ++j)
                    {
                        if (i == j)
                            continue;
                        // Only consider same-type loops as potential parents
                        if (all_loops[j].is_hole != all_loops[i].is_hole)
                            continue;
                        if (all_loops[j].polygon.contains(test_point))
                        {
                            double area = std::abs(all_loops[j].polygon.area());
                            if (area < smallest_area)
                            {
                                smallest_area = area;
                                best_parent = j;
                            }
                        }
                    }
                    all_loops[i].parent = best_parent;
                    if (best_parent != SIZE_MAX)
                        all_loops[best_parent].children.push_back(i);
                }

                // Find root nodes (no parent), separated by type
                // Contour roots first (outer boundary), then hole roots
                // This ensures interlocking starts at outer boundary where normal perimeters ended
                std::vector<size_t> contour_roots;
                std::vector<size_t> hole_roots;
                for (size_t i = 0; i < all_loops.size(); ++i)
                {
                    if (all_loops[i].parent == SIZE_MAX)
                    {
                        if (all_loops[i].is_hole)
                            hole_roots.push_back(i);
                        else
                            contour_roots.push_back(i);
                    }
                }

                // Get last position for nearest-neighbor ordering
                Point last_pos = Point::Zero();

                // Collect a subtree into a vector (for potential reversal)
                std::function<void(size_t, std::vector<size_t> &)> collect_subtree =
                    [&](size_t idx, std::vector<size_t> &collected)
                {
                    collected.push_back(idx);
                    for (size_t child_idx : all_loops[idx].children)
                    {
                        collect_subtree(child_idx, collected);
                    }
                };

                // Output a list of loop indices, updating last_pos
                auto output_loops = [&](const std::vector<size_t> &indices)
                {
                    for (size_t idx : indices)
                    {
                        interlocking_collection.append(std::move(all_loops[idx].loop));
                        if (!all_loops[idx].polygon.points.empty())
                            last_pos = all_loops[idx].polygon.points.back();
                    }
                };

                // Process a subtree: collect all nodes, reverse if hole region, then output
                // Contour regions: outside-in (shell 0 near boundary → shell N toward center)
                // Hole regions: inside-out (shell 0 near hole → shell N away from hole)
                // Since containment gives outside-in order, we reverse for holes
                auto process_subtree = [&](size_t root_idx)
                {
                    std::vector<size_t> subtree;
                    collect_subtree(root_idx, subtree);

                    // Check if this is a hole region (root node is a hole)
                    bool is_hole_region = all_loops[root_idx].is_hole;

                    if (is_hole_region)
                    {
                        // Reverse for inside-out order (closest to hole first)
                        std::reverse(subtree.begin(), subtree.end());
                    }

                    output_loops(subtree);
                };

                // Helper to process a list of roots by nearest neighbor
                auto process_roots_nearest_neighbor = [&](std::vector<size_t> &roots)
                {
                    std::vector<bool> root_used(roots.size(), false);
                    for (size_t n = 0; n < roots.size(); ++n)
                    {
                        double best_dist = std::numeric_limits<double>::max();
                        size_t best_idx = 0;
                        for (size_t r = 0; r < roots.size(); ++r)
                        {
                            if (root_used[r])
                                continue;
                            Point root_start = all_loops[roots[r]].polygon.points.empty()
                                                   ? Point(0, 0)
                                                   : all_loops[roots[r]].polygon.points.front();
                            double dist = (root_start - last_pos).cast<double>().squaredNorm();
                            if (dist < best_dist)
                            {
                                best_dist = dist;
                                best_idx = r;
                            }
                        }
                        root_used[best_idx] = true;
                        process_subtree(roots[best_idx]);
                    }
                };

                // Process contour roots first (outer boundary - nozzle is already there after normal perimeters)
                // Then process hole roots (nearest neighbor from where we ended)
                process_roots_nearest_neighbor(contour_roots);
                process_roots_nearest_neighbor(hole_roots);

                // Calculate inner_area from the last shell result (for infill boundary)
                // last_shell_area represents the area after the innermost shell center was placed.
                // Offset inward by (half_width - infill_overlap) to create the infill boundary:
                //   - At 0% overlap: offset = half_width (infill starts at inner edge of shell)
                //   - At 100% overlap: offset = half_width - width = -half_width (infill extends to outer edge of shell)
                // This controls overlap between innermost INTERLOCKING shell and sparse INFILL,
                // which is separate from the outer boundary overlap (perimeter-to-interlocking).
                const coord_t infill_boundary_offset = half_width - infill_overlap_amount;
                Polygons inner_area;
                for (const ExPolygon &last_shell : last_shell_area)
                {
                    // Offset contour inward to create sparse infill boundary
                    Polygons inward = offset(last_shell.contour, -float(infill_boundary_offset));
                    if (last_shell.holes.empty())
                    {
                        append(inner_area, inward);
                    }
                    else
                    {
                        // Offset holes outward to create exclusion zones around hole-interlocking perimeters
                        // Note: Holes have CW orientation, so NEGATIVE offset expands them into material area
                        Polygons expanded_holes = offset(last_shell.holes, -float(infill_boundary_offset));
                        // Subtract expanded holes from the inward-offset contour
                        Polygons result = diff(inward, expanded_holes);
                        append(inner_area, result);
                    }
                }

                // Interlocking perimeters are actual perimeters, not infill. They should be in m_perimeters
                // to enable proper Layer Context API queries and processing.
                // CRITICAL: Perimeters structure is Collection-of-Collections, must wrap in ExtrusionEntityCollection
                if (!interlocking_collection.empty())
                {
                    // Wrap interlocking perimeters in collection (perimeters require nested collections)
                    ExtrusionEntityCollection *perimeter_collection = new ExtrusionEntityCollection();
                    perimeter_collection->no_sort = true; // Preserve the order we set via perimeter_index
                    perimeter_collection->entities = std::move(interlocking_collection.entities);

                    // CRITICAL: Islands have non-contiguous indices in m_perimeters. If we APPEND the interlocking
                    // collection to the end and extend this island's range, we'll include other islands' perimeters.
                    //
                    // Example WITHOUT interlocking:
                    //   Island A: range [0, 1), perimeters at index 0
                    //   Island B: range [1, 2), perimeters at index 1
                    //   Island C: range [2, 3), perimeters at index 2
                    //
                    // If we APPEND Island A's interlocking to end (index 3) and set range to [0, 4):
                    //   Island A: range [0, 4) ← WRONG! Includes indices 1, 2, 3 (other islands' perimeters)
                    //
                    // SOLUTION: INSERT at the correct position (right after this island's existing perimeters)
                    // and shift all subsequent island ranges.

                    uint32_t old_begin = *island.perimeters.begin();
                    uint32_t old_end = *island.perimeters.end(); // One past last index

                    // Insert the interlocking collection right after this island's existing perimeters
                    // This shifts all subsequent indices by 1
                    auto insert_pos = layerm->m_perimeters.entities.begin() + old_end;
                    layerm->m_perimeters.entities.insert(insert_pos, perimeter_collection);

                    // Update THIS island's range to include the newly inserted interlocking
                    island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(old_begin, old_end + 1));

                    // Update ALL other islands' ranges on this layer that reference indices >= old_end
                    // because the insertion shifted their indices by 1
                    for (LayerSlice &other_lslice : this->lslices_ex)
                    {
                        for (LayerIsland &other_island : other_lslice.islands)
                        {
                            // Skip this island (already updated above)
                            if (&other_island == &island)
                                continue;

                            // Only update islands in the same region (they share the same m_perimeters)
                            if (other_island.perimeters.region() != region_id)
                                continue;

                            // If this island's range starts at or after the insertion point, shift it by 1
                            uint32_t other_begin = *other_island.perimeters.begin();
                            uint32_t other_end = *other_island.perimeters.end();

                            if (other_begin >= old_end)
                            {
                                // Shift the entire range by 1
                                other_island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(other_begin + 1,
                                                                                                        other_end + 1));
                            }
                        }
                    }
                }

                // inner_area was calculated above in INTERLOCK-GEOMETRIC-OFFSET
                // Use union_ex() to properly pair holes with their parent contours.
                ExPolygons updated_sparse_regions = union_ex(inner_area);

                // The interlocking perimeters have consumed the outer area of sparse regions for THIS island.
                // Update the stInternal surface_fill entries to remove the consumed area.
                // CRITICAL: We must update the global surface_fills by subtracting THIS island's consumed area.
                // Each island's interlocking consumption must be subtracted from the global sparse regions.
                // We do NOT touch stInternalSolid - it's preserved for top/bottom solid layers.

                // Calculate the area consumed by interlocking for THIS island
                // Use ORIGINAL sparse regions (with infill_overlap boundary from PerimeterGenerator)
                // not the adjusted sparse_regions (with pp_overlap boundary).
                // This ensures we subtract the entire interlocking zone from infill, including
                // the gap between infill_overlap and pp_overlap boundaries.
                ExPolygons consumed_by_interlocking = diff_ex(original_sparse_regions, updated_sparse_regions);

                // Subtract the consumed area from the global sparse surface_fills
                // This updates the sparse infill boundaries for THIS island
                for (SurfaceFill &sf : surface_fills)
                {
                    if (sf.region_id == region_id && sf.surface.surface_type == stInternal)
                    {
                        // Subtract the area consumed by THIS island's interlocking
                        sf.expolygons = diff_ex(sf.expolygons, consumed_by_interlocking);
                    }
                    // Note: stInternalSolid is NOT modified - preserved for solid layer settings
                }
            } // End island loop
        } // End lslice loop
    }

    // After interlocking has consumed sparse regions, remove any stInternal surfaces with ~0% density (0.001%).
    // These were only needed for interlocking boundary detection and should not proceed to infill generation.
    surface_fills.erase(std::remove_if(surface_fills.begin(), surface_fills.end(), [](const SurfaceFill &sf)
                                       { return sf.surface.surface_type == stInternal && sf.params.density < 0.01f; }),
                        surface_fills.end());

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRun = 0;
        export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun++).c_str(),
                                  surface_fills);
    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    size_t first_object_layer_id = this->object()->get_layer(0)->id();

    // Process each island first (matches interlocking perimeter architecture).
    // Each island's infill is generated and filled completely before moving to the next island,
    // eliminating chaotic back-and-forth travel caused by global generation + spatial division.

    for (LayerSlice &lslice : this->lslices_ex)
    {
        for (LayerIsland &island : lslice.islands)
        {
            const uint32_t region_id = island.perimeters.region();
            LayerRegion *layerm = this->get_region(region_id);

            // Process each surface type for THIS island
            for (SurfaceFill &surface_fill : surface_fills)
            {
                // Only process surfaces that match this island's region
                if (surface_fill.region_id != region_id)
                    continue;

                // Intersect surface fill with island boundary
                ExPolygons island_expolygons = intersection_ex(surface_fill.expolygons, ExPolygons{island.boundary});

                if (island_expolygons.empty())
                    continue;

                // Create the filler object for this surface type
                std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
                f->set_bounding_box(bbox);
                // Layer ID is used for orienting the infill in alternating directions.
                // Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
                // from raft.
                f->layer_id = this->id() - first_object_layer_id;
                f->z = this->print_z;
                f->angle = surface_fill.params.angle;
                f->overlap = surface_fill.params.overlap;
                f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree
                                                                                       : adaptive_fill_octree;
                f->print_config = &this->object()->print()->config();
                f->print_object_config = &this->object()->config();

                if (surface_fill.params.pattern == ipLightning)
                    dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

                if (surface_fill.params.pattern == ipEnsuring)
                {
                    auto *fill_ensuring = dynamic_cast<FillEnsuring *>(f.get());
                    assert(fill_ensuring != nullptr);
                    fill_ensuring->print_region_config = &m_regions[surface_fill.region_id]->region().config();
                }

                // calculate flow spacing for infill pattern generation
                bool using_internal_flow = !surface_fill.surface.is_solid() && !surface_fill.params.bridge;
                double link_max_length = 0.;
                if (!surface_fill.params.bridge)
                {
#if 0
                    link_max_length = layerm->region().config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//                    printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
                    if (surface_fill.params.density > 80.) // 80%
                        link_max_length = 3. * f->spacing;
#endif
                }

                // Maximum length of the perimeter segment linking two infill lines.
                f->link_max_length = (coord_t) scale_(link_max_length);
                // Used by the concentric infill pattern to clip the loops to create extrusion paths.
                f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) *
                                           LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

                // apply half spacing using this flow's own spacing and generate infill
                FillParams params;
                params.density = float(0.01 * surface_fill.params.density);

                // At exactly 50% density, distance = min_spacing / 0.5 = min_spacing * 2.0 (exact integer multiple)
                // This creates perfectly aligned coordinates that trigger geometric degeneracies in Clipper2.
                // Treat 50.0% as 49.9% to avoid the exact 2x multiplier.
                // Applies to: ipConcentric (9) and ipEnsuring/Athena (20) which use heavy Clipper2 operations.
                if ((surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipEnsuring) &&
                    std::abs(params.density - 0.5f) < 0.0001f)
                {
                    params.density = 0.499f;
                }

                params.dont_adjust = false; //  surface_fill.params.dont_adjust;
                params.anchor_length = surface_fill.params.anchor_length;
                params.anchor_length_max = surface_fill.params.anchor_length_max;
                params.resolution = resolution;
                params.use_advanced_perimeters = ((perimeter_generator == PerimeterGeneratorType::Arachne ||
                                                   perimeter_generator == PerimeterGeneratorType::Athena) &&
                                                  surface_fill.params.pattern == ipConcentric) ||
                                                 surface_fill.params.pattern == ipEnsuring;
                params.perimeter_generator = perimeter_generator;
                params.layer_height = layerm->layer()->height;
                params.prefer_clockwise_movements = this->object()->print()->config().prefer_clockwise_movements;

                // Track fill range for this island and surface type
                uint32_t fill_begin = uint32_t(layerm->m_fills.entities.size());

                // An island may have multiple disconnected sparse regions (e.g., separated by interlocking).
                // Fill each ExPolygon completely before moving to the next to prevent chaotic jumping.

                // Create ONE collection for all fills in this island
                ExtrusionEntityCollection *eec = new ExtrusionEntityCollection();
                eec->no_sort = true; // Prevent re-sorting that would destroy region-by-region order

                // Initialize to perimeter endpoint - that's where the nozzle is when infill starts
                Point last_fill_pos = Point(0, 0);
                bool have_last_pos = false;

                // Get the last perimeter's endpoint as the initial starting position
                // For closed perimeter loops, first_point == last_point, which is where the nozzle
                // finishes after printing the perimeter (the seam position)
                if (!island.perimeters.empty())
                {
                    uint32_t last_perim_idx = *island.perimeters.end() - 1;
                    if (last_perim_idx < layerm->m_perimeters.entities.size())
                    {
                        const ExtrusionEntity *last_perim = layerm->m_perimeters.entities[last_perim_idx];
                        if (last_perim != nullptr)
                        {
                            last_fill_pos = last_perim->last_point();
                            have_last_pos = true;
                        }
                    }
                }

                // Process each disconnected region (ExPolygon) in the island
                for (ExPolygon &expoly : island_expolygons)
                {
                    // Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
                    f->spacing = surface_fill.params.spacing;
                    // For bridges: use original flow width (bridge diameter) so boundary offset is independent of line spacing
                    // For non-bridges: use spacing (normal behavior)
                    f->bounding_width = surface_fill.params.bridge ? surface_fill.params.flow.width()
                                                                   : surface_fill.params.spacing;

                    // Use last fill endpoint if available, otherwise use perimeter endpoint or centroid
                    if (have_last_pos)
                    {
                        params.start_near = last_fill_pos;
                    }
                    else
                    {
                        params.start_near = expoly.contour.centroid();
                    }

                    surface_fill.surface.expolygon = std::move(expoly);
                    Polylines polylines;
                    ThickPolylines thick_polylines;
                    try
                    {
                        if (params.use_advanced_perimeters)
                            thick_polylines = f->fill_surface_advanced(&surface_fill.surface, params);
                        else
                            polylines = f->fill_surface(&surface_fill.surface, params);
                    }
                    catch (InfillFailedException &)
                    {
                    }

                    if (!polylines.empty())
                    {
                        last_fill_pos = polylines.back().last_point();
                        have_last_pos = true;
                    }
                    else if (!thick_polylines.empty())
                    {
                        last_fill_pos = thick_polylines.back().last_point();
                        have_last_pos = true;
                    }

                    // Process this region's infill immediately - fill it completely
                    if (!polylines.empty() || !thick_polylines.empty())
                    {
                        // calculate actual flow from spacing
                        double flow_mm3_per_mm = surface_fill.params.flow.mm3_per_mm();
                        double flow_width = surface_fill.params.flow.width();
                        // with_spacing() uses rounded rectangle formula which is wrong for bridges
                        // (bridges use circular cross-section). Skip width recalculation for bridges.
                        if (using_internal_flow || surface_fill.params.bridge)
                        {
                            // For internal flow: ignore slight spacing variations
                            // For bridges: width is already correct (circular cross-section)
                        }
                        else
                        {
                            Flow new_flow = surface_fill.params.flow.with_spacing(float(f->spacing));
                            flow_mm3_per_mm = new_flow.mm3_per_mm();
                            flow_width = new_flow.width();
                        }

                        if (params.use_advanced_perimeters)
                        {
                            for (const ThickPolyline &thick_polyline : thick_polylines)
                            {
                                Flow new_flow = surface_fill.params.bridge
                                                    ? surface_fill.params.flow
                                                    : surface_fill.params.flow.with_spacing(float(f->spacing));

                                ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(
                                    thick_polyline, surface_fill.params.extrusion_role, new_flow, scaled<float>(0.05),
                                    float(SCALED_EPSILON));
                                if (!multi_path.empty())
                                {
                                    if (multi_path.paths.front().first_point() == multi_path.paths.back().last_point())
                                        eec->entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                                    else
                                        eec->entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                                }
                            }
                        }
                        else
                        {
                            // Add this region's polylines to the collection (already optimized by fill_surface)
                            extrusion_entities_append_paths(
                                eec->entities, std::move(polylines),
                                ExtrusionAttributes{surface_fill.params.extrusion_role,
                                                    ExtrusionFlow{flow_mm3_per_mm, float(flow_width),
                                                                  surface_fill.params.flow.height()},
                                                    f->is_self_crossing()},
                                !params.prefer_clockwise_movements);
                        }
                    }
                }

                // Add the collection to the layer (if it has any fills)
                if (!eec->empty())
                    layerm->m_fills.entities.push_back(eec);
                else
                    delete eec;

                uint32_t fill_end = uint32_t(layerm->m_fills.entities.size());

                // Direct assignment to THIS island (no spatial search needed)
                if (fill_end > fill_begin)
                {
                    island.add_fill_range(LayerExtrusionRange{region_id, {fill_begin, fill_end}});
                }
            }
        }
    }

    for (LayerSlice &lslice : this->lslices_ex)
        for (LayerIsland &island : lslice.islands)
        {
            if (!island.thin_fills.empty())
            {
                // Copy thin fills into fills packed as a collection.
                // Fills are always stored as collections, the rest of the pipeline (wipe into infill, G-code generator) relies on it.
                LayerRegion &layerm = *this->get_region(island.perimeters.region());
                ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
                layerm.m_fills.entities.push_back(&collection);
                collection.entities.reserve(island.thin_fills.size());
                for (uint32_t fill_id : island.thin_fills)
                    collection.entities.push_back(layerm.thin_fills().entities[fill_id]->clone());
                island.add_fill_range(
                    {island.perimeters.region(),
                     {uint32_t(layerm.m_fills.entities.size() - 1), uint32_t(layerm.m_fills.entities.size())}});
            }
            // Sort the fills by region ID.
            std::sort(island.fills.begin(), island.fills.end(), [](auto &l, auto &r)
                      { return l.region() < r.region() || (l.region() == r.region() && *l.begin() < *r.begin()); });
            // Compress continuous fill ranges of the same region.
            {
                size_t k = 0;
                for (size_t i = 0; i < island.fills.size();)
                {
                    uint32_t region_id = island.fills[i].region();
                    uint32_t begin = *island.fills[i].begin();
                    uint32_t end = *island.fills[i].end();
                    size_t j = i + 1;
                    for (; j < island.fills.size() && island.fills[j].region() == region_id &&
                           *island.fills[j].begin() == end;
                         ++j)
                        end = *island.fills[j].end();
                    island.fills[k++] = {region_id, {begin, end}};
                    i = j;
                }
                island.fills.erase(island.fills.begin() + k, island.fills.end());
            }
        }

#ifndef NDEBUG
    for (LayerRegion *layerm : m_regions)
        for (const ExtrusionEntity *e : layerm->fills())
            assert(dynamic_cast<const ExtrusionEntityCollection *>(e) != nullptr);
#endif
}

Polylines Layer::generate_sparse_infill_polylines_for_anchoring(FillAdaptive::Octree *adaptive_fill_octree,
                                                                FillAdaptive::Octree *support_fill_octree,
                                                                FillLightning::Generator *lightning_generator) const
{
    std::vector<SurfaceFill> surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox = this->object()->bounding_box();
    const auto resolution = this->object()->print()->config().gcode_resolution.value;

    Polylines sparse_infill_polylines{};

    for (SurfaceFill &surface_fill : surface_fills)
    {
        if (surface_fill.surface.surface_type != stInternal)
        {
            continue;
        }

        switch (surface_fill.params.pattern)
        {
        case ipCount:
            continue;
            break;
        case ipSupportBase:
            continue;
            break;
        case ipEnsuring:
            continue;
            break;
        case ipLightning:
        case ipAdaptiveCubic:
        case ipSupportCubic:
        case ipRectilinear:
        case ipMonotonic:
        case ipMonotonicLines:
        case ipAlignedRectilinear:
        case ipGrid:
        case ipTriangles:
        case ipStars:
        case ipCubic:
        case ipLine:
        case ipConcentric:
        case ipHoneycomb:
        case ip3DHoneycomb:
        case ipGyroid:
        case ipHilbertCurve:
        case ipArchimedeanChords:
        case ipOctagramSpiral:
        case ipZigZag:
            break;
        }

        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id() - this->object()->get_layer(0)->id(); // We need to subtract raft layers.
        f->z = this->print_z;
        f->angle = surface_fill.params.angle;
        f->overlap = surface_fill.params.overlap;
        f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree
                                                                               : adaptive_fill_octree;
        f->print_config = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();

        if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

        // calculate flow spacing for infill pattern generation
        double link_max_length = 0.;
        if (!surface_fill.params.bridge)
        {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t) scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) *
                                   LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density = float(0.01 * surface_fill.params.density);

        // At exactly 50% density, distance = min_spacing / 0.5 = min_spacing * 2.0 (exact integer multiple)
        // This creates perfectly aligned coordinates that trigger geometric degeneracies in Clipper2.
        // Treat 50.0% as 49.9% to avoid the exact 2x multiplier.
        // Applies to: ipConcentric (9) and ipEnsuring/Athena (20) which use heavy Clipper2 operations.
        if ((surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipEnsuring) &&
            std::abs(params.density - 0.5f) < 0.0001f)
        {
            params.density = 0.499f;
        }

        params.dont_adjust = false; //  surface_fill.params.dont_adjust;
        params.anchor_length = surface_fill.params.anchor_length;
        params.anchor_length_max = surface_fill.params.anchor_length_max;
        params.resolution = resolution;
        params.use_advanced_perimeters = false;
        params.layer_height = layerm.layer()->height;

        Point last_fill_pos = Point(0, 0);
        bool have_last_pos = false;

        for (ExPolygon &expoly : surface_fill.expolygons)
        {
            // Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
            f->spacing = surface_fill.params.spacing;
            // For bridges: use original flow width so boundary offset is independent of line spacing
            // For non-bridges: use spacing (normal behavior)
            f->bounding_width = surface_fill.params.bridge ? surface_fill.params.flow.width()
                                                           : surface_fill.params.spacing;

            if (have_last_pos)
            {
                params.start_near = last_fill_pos;
            }
            else
            {
                params.start_near = expoly.contour.centroid();
            }

            surface_fill.surface.expolygon = std::move(expoly);
            try
            {
                Polylines polylines = f->fill_surface(&surface_fill.surface, params);
                if (!polylines.empty())
                {
                    last_fill_pos = polylines.back().last_point();
                    have_last_pos = true;
                }
                sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
            }
            catch (InfillFailedException &)
            {
            }
        }
    }

    return sparse_infill_polylines;
}

// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
    // LayerRegion::slices contains surfaces marked with SurfaceType.
    // Here we want to collect top surfaces extruded with the same extruder.
    // A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

    // First classify regions based on the extruder used.
    struct IroningParams
    {
        int extruder = -1;
        bool just_infill = false;
        // Spacing of the ironing lines, also to calculate the extrusion flow from.
        double line_spacing;
        // Height of the extrusion, to calculate the extrusion flow from.
        double height;
        double speed;
        double angle;

        bool operator<(const IroningParams &rhs) const
        {
            if (this->extruder < rhs.extruder)
                return true;
            if (this->extruder > rhs.extruder)
                return false;
            if (int(this->just_infill) < int(rhs.just_infill))
                return true;
            if (int(this->just_infill) > int(rhs.just_infill))
                return false;
            if (this->line_spacing < rhs.line_spacing)
                return true;
            if (this->line_spacing > rhs.line_spacing)
                return false;
            if (this->height < rhs.height)
                return true;
            if (this->height > rhs.height)
                return false;
            if (this->speed < rhs.speed)
                return true;
            if (this->speed > rhs.speed)
                return false;
            if (this->angle < rhs.angle)
                return true;
            if (this->angle > rhs.angle)
                return false;
            return false;
        }

        bool operator==(const IroningParams &rhs) const
        {
            return this->extruder == rhs.extruder && this->just_infill == rhs.just_infill &&
                   this->line_spacing == rhs.line_spacing && this->height == rhs.height && this->speed == rhs.speed &&
                   this->angle == rhs.angle;
        }

        LayerRegion *layerm;
        uint32_t region_id;

        // IdeaMaker: ironing
        // ironing flowrate (5% percent)
        // ironing speed (10 mm/sec)

        // Kisslicer:
        // iron off, Sweep, Group
        // ironing speed: 15 mm/sec

        // Cura:
        // Pattern (zig-zag / concentric)
        // line spacing (0.1mm)
        // flow: from normal layer height. 10%
        // speed: 20 mm/sec
    };

    std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().layer_height;

    for (uint32_t region_id = 0; region_id < uint32_t(this->regions().size()); ++region_id)
        if (LayerRegion *layerm = this->get_region(region_id); !layerm->slices().empty())
        {
            IroningParams ironing_params;
            const PrintRegionConfig &config = layerm->region().config();
            if (config.ironing && (config.ironing_type == IroningType::AllSolid ||
                                   (config.top_solid_layers > 0 && (config.ironing_type == IroningType::TopSurfaces ||
                                                                    (config.ironing_type == IroningType::TopmostOnly &&
                                                                     layerm->layer()->upper_layer == nullptr)))))
            {
                if (config.perimeter_extruder == config.solid_infill_extruder || config.perimeters == 0)
                {
                    // Iron the whole face.
                    ironing_params.extruder = config.solid_infill_extruder;
                }
                else
                {
                    // Iron just the infill.
                    ironing_params.extruder = config.solid_infill_extruder;
                }
            }
            if (ironing_params.extruder != -1)
            {
                //TODO just_infill is currently not used.
                ironing_params.just_infill = false;
                ironing_params.line_spacing = config.ironing_spacing;
                ironing_params.height = default_layer_height * 0.01 * config.ironing_flowrate;
                ironing_params.speed = config.ironing_speed;
                ironing_params.angle = config.fill_angle * M_PI / 180.;
                ironing_params.layerm = layerm;
                ironing_params.region_id = region_id;
                by_extruder.emplace_back(ironing_params);
            }
        }
    std::sort(by_extruder.begin(), by_extruder.end());

    FillRectilinear fill;
    FillParams fill_params;
    fill.set_bounding_box(this->object()->bounding_box());
    // Layer ID is used for orienting the infill in alternating directions.
    // Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
    // from raft.
    //FIXME ironing does not take fill angle into account. Shall it? Does it matter?
    fill.layer_id = this->id() - this->object()->get_layer(0)->id();
    fill.z = this->print_z;
    fill.overlap = 0;
    fill_params.density = 1.;
    fill_params.monotonic = true;

    for (size_t i = 0; i < by_extruder.size();)
    {
        // Find span of regions equivalent to the ironing operation.
        IroningParams &ironing_params = by_extruder[i];
        size_t j = i;
        for (++j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++j)
            ;

        // Create the ironing extrusions for regions <i, j)
        ExPolygons ironing_areas;
        double nozzle_dmr = this->object()->print()->config().nozzle_diameter.values[ironing_params.extruder - 1];
        if (ironing_params.just_infill)
        {
            //TODO just_infill is currently not used.
            // Just infill.
        }
        else
        {
            // Infill and perimeter.
            // Merge top surfaces with the same ironing parameters.
            Polygons polys;
            Polygons infills;
            for (size_t k = i; k < j; ++k)
            {
                const IroningParams &ironing_params = by_extruder[k];
                const PrintRegionConfig &region_config = ironing_params.layerm->region().config();
                bool iron_everything = region_config.ironing_type == IroningType::AllSolid;
                bool iron_completely = iron_everything;
                if (iron_everything)
                {
                    // Check whether there is any non-solid hole in the regions.
                    bool internal_infill_solid = region_config.fill_density.value > 95.;
                    for (const Surface &surface : ironing_params.layerm->fill_surfaces())
                        if ((!internal_infill_solid && surface.surface_type == stInternal) ||
                            surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid)
                        {
                            // Some fill region is not quite solid. Don't iron over the whole surface.
                            iron_completely = false;
                            break;
                        }
                }
                if (iron_completely)
                {
                    // Iron everything. This is likely only good for solid transparent objects.
                    for (const Surface &surface : ironing_params.layerm->slices())
                        polygons_append(polys, surface.expolygon);
                }
                else
                {
                    for (const Surface &surface : ironing_params.layerm->slices())
                        if (surface.surface_type == stTop || (iron_everything && surface.surface_type == stBottom))
                            // stBottomBridge is not being ironed on purpose, as it would likely destroy the bridges.
                            polygons_append(polys, surface.expolygon);
                }
                if (iron_everything && !iron_completely)
                {
                    // Add solid fill surfaces. This may not be ideal, as one will not iron perimeters touching these
                    // solid fill surfaces, but it is likely better than nothing.
                    for (const Surface &surface : ironing_params.layerm->fill_surfaces())
                        if (surface.surface_type == stInternalSolid)
                            polygons_append(infills, surface.expolygon);
                }
            }

            if (!infills.empty() || j > i + 1)
            {
                // Ironing over more than a single region or over solid internal infill.
                if (!infills.empty())
                    // For IroningType::AllSolid only:
                    // Add solid infill areas for layers, that contain some non-ironable infil (sparse infill, bridge infill).
                    append(polys, std::move(infills));
                polys = union_safety_offset(polys);
            }
            // Trim the top surfaces with half the nozzle diameter.
            ironing_areas = intersection_ex(polys, offset(this->lslices, -float(scale_(0.5 * nozzle_dmr))));
        }

        // Create the filler object.
        fill.spacing = ironing_params.line_spacing;
        fill.angle = float(ironing_params.angle + 0.25 * M_PI);
        fill.link_max_length = (coord_t) scale_(3. * fill.spacing);
        double extrusion_height = ironing_params.height * fill.spacing / nozzle_dmr;
        float extrusion_width = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr),
                                                                                     float(extrusion_height));
        double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas)
        {
            surface_fill.expolygon = std::move(expoly);
            Polylines polylines;
            try
            {
                assert(!fill_params.use_advanced_perimeters);
                polylines = fill.fill_surface(&surface_fill, fill_params);
            }
            catch (InfillFailedException &)
            {
            }
            if (!polylines.empty())
            {
                // Save into layer.
                auto fill_begin = uint32_t(ironing_params.layerm->fills().size());
                ExtrusionEntityCollection *eec = nullptr;
                ironing_params.layerm->m_fills.entities.push_back(eec = new ExtrusionEntityCollection());
                // Don't sort the ironing infill lines as they are monotonicly ordered.
                eec->no_sort = true;
                extrusion_entities_append_paths(eec->entities, std::move(polylines),
                                                ExtrusionAttributes{ExtrusionRole::Ironing,
                                                                    ExtrusionFlow{flow_mm3_per_mm, extrusion_width,
                                                                                  float(extrusion_height)}});
                insert_fills_into_islands(*this, ironing_params.region_id, fill_begin,
                                          uint32_t(ironing_params.layerm->fills().size()));
            }
        }

        // Regions up to j were processed.
        i = j;
    }
}

} // namespace Slic3r
