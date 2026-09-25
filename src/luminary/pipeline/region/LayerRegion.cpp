///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2014 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <string>
#include <map>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <unordered_set> // std::unordered_set, which the precompiled header had been supplying
#include <utility>
#include <vector>
#include <cstddef>

#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/fill/contract/FillBase.hpp"
#include "luminary/toolpath/flow/Flow.hpp"
#include "luminary/layer/model/Layer.hpp"
#include "luminary/geometry/bridging/BridgeDetector.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/geometry/transform/Geometry.hpp"
#include "luminary/walls/perimeter/PerimeterGenerator.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/geometry/surface/Surface.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/diagnostics/SVG.hpp"
#include "luminary/geometry/region_expansion/RegionExpansion.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntity.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/slice/mm_segmentation/MultiMaterialSegmentation.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/geometry/surface/SurfaceCollection.hpp"
#include "luminary/core/containers/Sizing.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/layer/region/LayerRegion.hpp"

namespace Luminary
{

Flow LayerRegion::flow(FlowRole role) const
{
    return this->flow(role, m_layer->height);
}

Flow LayerRegion::flow(FlowRole role, double layer_height) const
{
    return m_region->flow(*m_layer->object(), role, layer_height, m_layer->id() == 0);
}

Flow LayerRegion::bridging_flow(FlowRole role) const
{
    const PrintRegion &region = this->region();
    const PrintRegionConfig &region_config = region.config();
    const PrintObject &print_object = *this->layer()->object();

    if (region_config.bridge_extrusion_width.value > 0)
    {
        auto nozzle_diameter = float(print_object.print()->config().nozzle_diameter.get_at(region.extruder(role) - 1));
        Flow bridge_flow = Flow::new_from_config_width(role, region_config.bridge_extrusion_width, nozzle_diameter,
                                                       float(m_layer->height),
                                                       print_object.config().extrusion_width_percent_of_nozzle.value);
        return bridge_flow.with_flow_ratio(region_config.bridge_flow_ratio);
    }
    return this->flow(role).with_flow_ratio(region_config.bridge_flow_ratio);
}

// Fill in layerm->fill_surfaces by trimming the layerm->slices by layerm->fill_expolygons.
void LayerRegion::slices_to_fill_surfaces_clipped()
{
    // Collect polygons per surface type.
    std::array<std::vector<const Surface *>, size_t(stCount)> by_surface;
    for (const Surface &surface : this->slices())
        by_surface[size_t(surface.surface_type)].emplace_back(&surface);
    // Trim surfaces by the fill_boundaries.
    m_fill_surfaces.surfaces.clear();
    for (size_t surface_type = 0; surface_type < size_t(stCount); ++surface_type)
    {
        const std::vector<const Surface *> &this_surfaces = by_surface[surface_type];
        if (!this_surfaces.empty())
            m_fill_surfaces.append(intersection_ex(this_surfaces, this->fill_expolygons()), SurfaceType(surface_type));
    }
}

void LayerRegion::remove_narrow_fill_surfaces()
{
    const float min_half_w = float(scale_(this->flow(frPerimeter).width() * 0.25));
    const double z = (this->layer() != nullptr) ? this->layer()->print_z : 0.0;
    Surfaces &surfaces = m_fill_surfaces.surfaces;
    int removed = 0;
    surfaces.erase(std::remove_if(surfaces.begin(), surfaces.end(),
                                  [min_half_w, z, &removed](const Surface &s)
                                  {
                                      // Only filter sparse fill slivers - bridges, solid, and
                                      // top/bottom surfaces can be legitimately narrow
                                      if (s.surface_type != stInternal)
                                          return false;
                                      ExPolygons opened = opening_ex(ExPolygons{s.expolygon}, min_half_w);
                                      if (opened.empty())
                                      {
                                          if (Luminary::debug_enabled(Luminary::DBG_FILL))
                                          {
                                              double a = std::abs(s.expolygon.area()) * 1e-12;
                                              BoundingBox bb = get_extents(s.expolygon);
                                              dbg_log(Luminary::DBG_FILL, z, "SLIVER",
                                                      "REMOVED type=%d area=%8.6fmm2 "
                                                      "pts=%zu bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                                                      (int) s.surface_type, a, s.expolygon.contour.points.size(),
                                                      unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()),
                                                      unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()));
                                          }
                                          removed++;
                                          return true;
                                      }
                                      return false;
                                  }),
                   surfaces.end());
    if (Luminary::debug_enabled(Luminary::DBG_FILL) && removed > 0)
        dbg_log(Luminary::DBG_FILL, z, "SLIVER", "TOTAL removed=%d remaining=%zu min_width=%.4fmm", removed,
                surfaces.size(), unscaled<double>(min_half_w) * 2.0);
}

// Produce perimeter extrusions, gap fill extrusions and fill polygons for input slices.
void LayerRegion::make_perimeters(
    // Input slices for which the perimeters, gap fills and fill expolygons are to be generated.
    const SurfaceCollection &slices,
    // Configuration regions that will be applied to parts of created perimeters.
    const PerimeterRegions &perimeter_regions,
    // Ranges of perimeter extrusions and gap fill extrusions per suface, referencing
    // newly created extrusions stored at this LayerRegion.
    std::vector<std::pair<ExtrusionRange, ExtrusionRange>> &perimeter_and_gapfill_ranges,
    // All fill areas produced for all input slices above.
    ExPolygons &fill_expolygons,
    // Ranges of fill areas above per input slice.
    std::vector<ExPolygonRange> &fill_expolygons_ranges)
{
    m_perimeters.clear();
    m_perimeters_unsplit.clear();
    m_thin_fills.clear();

    perimeter_and_gapfill_ranges.reserve(perimeter_and_gapfill_ranges.size() + slices.size());
    // There may be more expolygons produced per slice, thus this reserve is conservative.
    fill_expolygons.reserve(fill_expolygons.size() + slices.size());
    fill_expolygons_ranges.reserve(fill_expolygons_ranges.size() + slices.size());

    const PrintConfig &print_config = this->layer()->object()->print()->config();
    const PrintRegionConfig &region_config = this->region().config();
    // This needs to be in sync with PrintObject::_slice() slicing_mode_normal_below_layer!
    bool spiral_vase = print_config.spiral_vase &&
                       // The layer id counts raft layers as well.
                       (this->layer()->id() >= size_t(region_config.bottom_solid_layers.value) &&
                        this->layer()->print_z >= region_config.bottom_solid_min_thickness - EPSILON);

    // Cummulative sum of polygons over all the regions.
    const ExPolygons *lower_slices = this->layer()->lower_layer ? &this->layer()->lower_layer->lslices : nullptr;
    const ExPolygons *upper_slices = this->layer()->upper_layer ? &this->layer()->upper_layer->lslices : nullptr;

    PerimeterGenerator::Parameters params(this->layer()->height, int(this->layer()->id()), this->layer(),
                                          this->flow(frPerimeter), this->flow(frExternalPerimeter),
                                          this->bridging_flow(frPerimeter), this->flow(frSolidInfill), region_config,
                                          this->layer()->object()->config(), print_config, perimeter_regions,
                                          spiral_vase);

    // Cache for offsetted lower_slices
    Polygons lower_layer_polygons_cache;

    for (const Surface &surface : slices)
    {
        auto perimeters_begin = uint32_t(m_perimeters.size());
        auto gap_fills_begin = uint32_t(m_thin_fills.size());
        auto fill_expolygons_begin = uint32_t(fill_expolygons.size());
        // Athena: preFlight's fixed-width perimeter generator (replaces Classic)
        // Arachne: Variable-width perimeter generator (requires non-spiral vase mode)
        // Spiral vase always uses Athena (requires fixed-width single perimeter)
        if (this->layer()->object()->config().perimeter_generator.value == PerimeterGeneratorType::Arachne &&
            !spiral_vase)
            PerimeterGenerator::process_arachne(
                // input:
                params, surface, lower_slices, upper_slices, lower_layer_polygons_cache,
                // output:
                m_perimeters, m_thin_fills, fill_expolygons);
        else
            PerimeterGenerator::process_athena(
                // input:
                params, surface, lower_slices, upper_slices, lower_layer_polygons_cache,
                // output:
                m_perimeters, m_thin_fills, fill_expolygons);
        perimeter_and_gapfill_ranges.emplace_back(ExtrusionRange{perimeters_begin, uint32_t(m_perimeters.size())},
                                                  ExtrusionRange{gap_fills_begin, uint32_t(m_thin_fills.size())});
        fill_expolygons_ranges.emplace_back(ExtrusionRange{fill_expolygons_begin, uint32_t(fill_expolygons.size())});
    }

    // Calculate and store the number of interlocking shells for this layer region.
    // Serpentine replaces the perimeter structure entirely, so no interlocking
    // shells exist even if interlocking is also ticked.
    const int num_interlocking_shells = region_config.interlock_perimeters_enabled.value &&
                                                !region_config.serpentine_enabled.value
                                            ? region_config.interlock_perimeter_count.value
                                            : 0;
    this->set_num_interlocking_shells(num_interlocking_shells);
}

// Extract surfaces of given type from surfaces, extract fill (layer) thickness of one of the surfaces.
static ExPolygons fill_surfaces_extract_expolygons(Surfaces &surfaces, std::initializer_list<SurfaceType> surface_types,
                                                   double &thickness)
{
    // The set gives an O(1) type test, so the filter is O(n) rather than the O(n*m) a std::find()
    // over the type list inside the loop would cost. Called once per layer.

    // Convert surface_types to set for O(1) lookup instead of O(m) linear search
    std::unordered_set<SurfaceType> types_set(surface_types.begin(), surface_types.end());

    size_t cnt = 0;
    for (const Surface &surface : surfaces)
        if (types_set.count(surface.surface_type))
        { // O(1) lookup
            ++cnt;
            thickness = surface.thickness;
        }
    if (cnt == 0)
        return {};

    ExPolygons out;
    out.reserve(cnt);
    for (Surface &surface : surfaces)
        if (types_set.count(surface.surface_type)) // O(1) lookup
            out.emplace_back(std::move(surface.expolygon));
    return out;
}

// Cache for detecting bridge orientation and merging regions with overlapping expansions.
struct Bridge
{
    ExPolygon expolygon;
    uint32_t group_id;
    std::vector<Algorithm::RegionExpansionEx>::const_iterator bridge_expansion_begin;
};

// Group the bridge surfaces by overlaps.
uint32_t group_id(std::vector<Bridge> &bridges, uint32_t src_id)
{
    uint32_t group_id = bridges[src_id].group_id;
    while (group_id != src_id)
    {
        src_id = group_id;
        group_id = bridges[src_id].group_id;
    }
    bridges[src_id].group_id = group_id;
    return group_id;
};

std::vector<Bridge> get_grouped_bridges(ExPolygons &&bridge_expolygons,
                                        const std::vector<Algorithm::RegionExpansionEx> &bridge_expansions)
{
    using namespace Algorithm;

    std::vector<Bridge> result;
    {
        result.reserve(bridge_expansions.size());
        uint32_t group_id = 0;
        using std::move_iterator;
        for (ExPolygon &expolygon : bridge_expolygons)
            result.push_back({std::move(expolygon), group_id++, bridge_expansions.end()});
    }

    // Detect overlaps of bridge anchors inside their respective shell regions.
    // bridge_expansions are sorted by boundary id and source id.
    for (auto expansion_iterator = bridge_expansions.begin(); expansion_iterator != bridge_expansions.end();)
    {
        auto boundary_region_begin = expansion_iterator;
        auto boundary_region_end = std::find_if(next(expansion_iterator), bridge_expansions.end(),
                                                [&](const RegionExpansionEx &expansion)
                                                { return expansion.boundary_id != expansion_iterator->boundary_id; });

        // Cache of bboxes per expansion boundary.
        std::vector<BoundingBox> bounding_boxes;
        bounding_boxes.reserve(std::distance(boundary_region_begin, boundary_region_end));
        std::transform(boundary_region_begin, boundary_region_end, std::back_inserter(bounding_boxes),
                       [](const RegionExpansionEx &expansion) { return get_extents(expansion.expolygon.contour); });

        // For each bridge anchor of the current source:
        for (; expansion_iterator != boundary_region_end; ++expansion_iterator)
        {
            auto candidate_iterator = std::next(expansion_iterator);
            for (; candidate_iterator != boundary_region_end; ++candidate_iterator)
            {
                const BoundingBox &current_bounding_box{bounding_boxes[expansion_iterator - boundary_region_begin]};
                const BoundingBox &candidate_bounding_box{bounding_boxes[candidate_iterator - boundary_region_begin]};
                if (expansion_iterator->src_id != candidate_iterator->src_id &&
                    current_bounding_box.overlap(candidate_bounding_box)
                    // One may ignore holes, they are irrelevant for intersection test.
                    &&
                    !intersection(expansion_iterator->expolygon.contour, candidate_iterator->expolygon.contour).empty())
                {
                    // The two bridge regions intersect. Give them the same (lower) group id.
                    uint32_t id = group_id(result, expansion_iterator->src_id);
                    uint32_t id2 = group_id(result, candidate_iterator->src_id);
                    if (id < id2)
                        result[id2].group_id = id;
                    else
                        result[id].group_id = id2;
                }
            }
        }
    }
    return result;
}

Surfaces merge_bridges(std::vector<Bridge> &bridges, const std::vector<Algorithm::RegionExpansionEx> &bridge_expansions,
                       const float closing_radius, const bool use_air_floating = false,
                       const ExPolygons *lower_slices = nullptr, const double dbg_print_z = 0.)
{
    for (auto it = bridge_expansions.begin(); it != bridge_expansions.end();)
    {
        bridges[it->src_id].bridge_expansion_begin = it;
        uint32_t src_id = it->src_id;
        for (++it; it != bridge_expansions.end() && it->src_id == src_id; ++it)
            ;
    }

    Surfaces result;
    // The air-support reference is loop-invariant and the lower layer can be large; expand it
    // once for every bridge group on this layer region.
    std::optional<Polygons> lower_expanded_storage;
    const Polygons *lower_expanded = nullptr;
    if (use_air_floating && lower_slices != nullptr)
    {
        lower_expanded_storage = expand(to_polygons(*lower_slices), float(SCALED_EPSILON));
        lower_expanded = &*lower_expanded_storage;
    }
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++bridge_id)
    {
        if (group_id(bridges, bridge_id) == bridge_id)
        {
            // Head of the group.
            Polygons bridge_group;
            Polygons expansions;
            for (uint32_t bridge_id2 = bridge_id; bridge_id2 < uint32_t(bridges.size()); ++bridge_id2)
            {
                if (group_id(bridges, bridge_id2) == bridge_id)
                {
                    append(bridge_group, to_polygons(std::move(bridges[bridge_id2].expolygon)));
                    auto it_bridge_expansion = bridges[bridge_id2].bridge_expansion_begin;
                    assert(it_bridge_expansion == bridge_expansions.end() || it_bridge_expansion->src_id == bridge_id2);
                    for (; it_bridge_expansion != bridge_expansions.end() && it_bridge_expansion->src_id == bridge_id2;
                         ++it_bridge_expansion)
                        append(expansions, to_polygons(it_bridge_expansion->expolygon));
                }
            }
            append(bridge_group, expansions);

            // The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
            // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
            // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
            ExPolygons merged_bridges = closing_ex(bridge_group, closing_radius);
            // without safety offset, artifacts are generated
            // union_safety_offset_ex(acc)

            // Loop-invariant of the direction vote below (only read without air input).
            const bool air_input = use_air_floating && lower_expanded != nullptr;
            const Polygons expansions_expanded = air_input ? Polygons{} : expand(expansions, float(SCALED_EPSILON));

            for (ExPolygon &bridge_expolygon : merged_bridges)
            {
                // Floating edges drive the direction vote. With interlocking the expansions can
                // claim essentially the whole boundary (the shell band leaves no fill surfaces to
                // stop at), reducing the floating set to orientation noise, so there the floating
                // edges come from slice truth instead: boundary segments over air per the lower
                // slices.
                const Lines lines{air_input ? to_lines(diff_pl(to_polylines(bridge_expolygon), *lower_expanded))
                                            : to_lines(diff_pl(to_polylines(bridge_expolygon), expansions_expanded))};
                auto [bridging_dir, unsupported_dist] = detect_bridging_direction(lines, to_polygons(bridge_expolygon));
                Surface surface{stBottomBridge, std::move(bridge_expolygon)};
                surface.bridge_angle = M_PI + std::atan2(bridging_dir.y(), bridging_dir.x());
                if (Luminary::debug_enabled(Luminary::DBG_FILL))
                {
                    double floating_len = 0;
                    for (const Line &l : lines)
                        floating_len += unscaled<double>(l.length());
                    double boundary_len = unscaled<double>(surface.expolygon.contour.length());
                    for (const Polygon &h : surface.expolygon.holes)
                        boundary_len += unscaled<double>(h.length());
                    const BoundingBox bb = get_extents(surface.expolygon);
                    dbg_log(Luminary::DBG_FILL, dbg_print_z, "FILL",
                            "BRIDGE_DIR src=merge input=%s area=%.2fmm2 boundary=%.2fmm floating=%.2fmm "
                            "floating_segs=%zu angle=%.3frad unsupported=%.2f bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                            air_input ? "air" : "expansion", std::abs(surface.expolygon.area()) * 1e-12, boundary_len,
                            floating_len, lines.size(), surface.bridge_angle, unsupported_dist,
                            unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()), unscaled<double>(bb.max.x()),
                            unscaled<double>(bb.max.y()));
                    // Shadow comparison on the paths still using expansion-based floating edges:
                    // the air-based candidate is logged next to the production angle, so classic
                    // slices provide the equivalence evidence.
                    if (!air_input && lower_slices != nullptr)
                    {
                        const Lines air_lines{
                            to_lines(diff_pl(to_polylines(surface.expolygon),
                                             expand(to_polygons(*lower_slices), float(SCALED_EPSILON))))};
                        double air_len = 0;
                        for (const Line &l : air_lines)
                            air_len += unscaled<double>(l.length());
                        auto [cand_dir, cand_unsup] = detect_bridging_direction(air_lines,
                                                                                to_polygons(surface.expolygon));
                        dbg_log(Luminary::DBG_FILL, dbg_print_z, "FILL",
                                "BRIDGE_DIR_CAND air=%.2fmm air_segs=%zu cand_angle=%.3frad cand_unsupported=%.2f "
                                "bbox=(%.2f,%.2f)-(%.2f,%.2f)",
                                air_len, air_lines.size(), M_PI + std::atan2(cand_dir.y(), cand_dir.x()), cand_unsup,
                                unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()),
                                unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()));
                    }
                }
                result.push_back(std::move(surface));
            }
        }
    }
    return result;
}

struct ExpansionResult
{
    Algorithm::WaveSeeds anchors;
    std::vector<Algorithm::RegionExpansionEx> expansions;
};

ExpansionResult expand_expolygons(const ExPolygons &expolygons, std::vector<ExpansionZone> &expansion_zones)
{
    using namespace Algorithm;
    WaveSeeds bridge_anchors;
    std::vector<RegionExpansionEx> bridge_expansions;

    unsigned processed_bridges_count = 0;
    for (ExpansionZone &expansion_zone : expansion_zones)
    {
        WaveSeeds seeds{
            wave_seeds(expolygons, expansion_zone.expolygons, expansion_zone.parameters.tiny_expansion, true)};
        std::vector<RegionExpansionEx> expansions{
            propagate_waves_ex(seeds, expansion_zone.expolygons, expansion_zone.parameters)};

        for (WaveSeed &seed : seeds)
            seed.boundary += processed_bridges_count;
        for (RegionExpansionEx &expansion : expansions)
            expansion.boundary_id += processed_bridges_count;

        expansion_zone.expanded_into = !expansions.empty();

        append(bridge_anchors, std::move(seeds));
        append(bridge_expansions, std::move(expansions));

        processed_bridges_count += expansion_zone.expolygons.size();
    }
    return {bridge_anchors, bridge_expansions};
}

// This function clips bridge surfaces to stay within fill_expolygons.
// It does NOT apply overlap expansion - that is handled by expand_bridges_for_overlap()
// which runs AFTER the merge logic completes.
//
// Why clip first: Bridges must stay within fill bounds for proper merge detection.
// The BRIDGE-ABSORB-ADJACENT-SOLID logic needs un-expanded geometry to correctly
// identify which solid regions are adjacent to bridges.
void apply_bridge_overlap_compensation(Surfaces &bridges, const LayerRegion *layer_region,
                                       const ExPolygons &fill_expolygons)
{
    if (bridges.empty())
        return;

    Surfaces adjusted_bridges;
    adjusted_bridges.reserve(bridges.size());

    // Partial bridge polygons can be narrower than fill_expolygons because
    // overhang detection (diff of current vs lower layer) misses perimeter
    // overlap zones at the sides. Grow the bridge to fill_expolygons boundary
    // so the fill algorithm has the full area to work with.
    const float perimeter_expand = float(scale_(layer_region->flow(frPerimeter).width()));

    for (Surface &s : bridges)
    {
        ExPolygons clipped = intersection_ex(ExPolygons{s.expolygon}, fill_expolygons);
        if (!clipped.empty())
            clipped = intersection_ex(offset_ex(clipped, perimeter_expand), fill_expolygons);

        for (ExPolygon &ep : clipped)
        {
            if (ep.area() > 0)
                adjusted_bridges.emplace_back(s, std::move(ep));
        }
    }
    bridges = std::move(adjusted_bridges);
}

// This function expands bridge surfaces based on bridge_infill_perimeter_overlap.
// Call it after the bridge absorb-adjacent-solid merge has completed.
// The expansion allows bridge infill to overlap with perimeters by the desired amount.
void expand_bridges_for_overlap(Surfaces &bridges, const LayerRegion *layer_region, const ExPolygons &fill_expolygons)
{
    if (bridges.empty())
        return;

    const auto &region_config = layer_region->region().config();

    // Get infill_overlap as absolute value (matching PerimeterGenerator's calculation)
    const float solid_infill_spacing = layer_region->flow(frSolidInfill).spacing();
    const float perimeter_width = layer_region->flow(frPerimeter).width();
    const float infill_overlap_base = perimeter_width / 2.0f + solid_infill_spacing / 2.0f;

    float infill_overlap_abs;
    if (region_config.infill_overlap.percent)
    {
        infill_overlap_abs = infill_overlap_base * float(region_config.infill_overlap.value) / 100.0f;
    }
    else
    {
        infill_overlap_abs = float(region_config.infill_overlap.value);
    }

    // Get bridge_infill_perimeter_overlap as absolute value
    float bridge_overlap_abs;
    if (region_config.bridge_infill_perimeter_overlap.percent)
    {
        bridge_overlap_abs = perimeter_width * float(region_config.bridge_infill_perimeter_overlap.value) / 100.0f;
    }
    else
    {
        bridge_overlap_abs = float(region_config.bridge_infill_perimeter_overlap.value);
    }

    // Calculate compensation: how much to expand/shrink surfaces
    // Positive = bridge wants MORE overlap, expand surfaces
    // Negative = bridge wants LESS overlap, shrink surfaces
    float compensation = bridge_overlap_abs - infill_overlap_abs;

    // Below one micron the bridges keep their size but are still clipped to the fill area:
    // the absorb-adjacent-solid closing above is unclipped and can cover small holes or run
    // past the fill boundary, and this clip is what returns them to the containment bound.
    // A surface the clip leaves whole (area unchanged) is kept as it was, so layers where the
    // clip is a no-op keep their original polygons.
    const bool no_compensation = std::abs(compensation) < 0.001f;

    // Create clip boundary: fill_expolygons expanded by compensation
    // This defines the maximum extent bridges can expand to
    ExPolygons bridge_clip_boundary = no_compensation ? fill_expolygons
                                                      : offset_ex(fill_expolygons, scale_(compensation));

    Surfaces adjusted_bridges;
    adjusted_bridges.reserve(bridges.size());
    double clipped_area = 0;

    for (Surface &s : bridges)
    {
        ExPolygons adjusted;

        if (compensation > 0 && !no_compensation)
        {
            // Positive: expand surfaces to get more overlap with perimeter
            ExPolygons expanded = offset_ex(ExPolygons{s.expolygon}, scale_(compensation));
            // Clip to the expanded boundary
            adjusted = intersection_ex(expanded, bridge_clip_boundary);
        }
        else
        {
            // Negative or none: clip to the boundary (surfaces shrink or stay)
            adjusted = intersection_ex(ExPolygons{s.expolygon}, bridge_clip_boundary);
        }

        if (no_compensation)
        {
            double area_after = 0;
            for (const ExPolygon &ep : adjusted)
                area_after += std::abs(ep.area());
            const double lost = std::abs(s.expolygon.area()) - area_after;
            // Anything under 0.001 mm2 is clipper rounding, not geometry outside the fill area.
            if (lost < 1e9)
            {
                adjusted_bridges.push_back(std::move(s));
                continue;
            }
            clipped_area += lost;
        }

        for (ExPolygon &ep : adjusted)
        {
            if (ep.area() > 0)
                adjusted_bridges.emplace_back(s, std::move(ep));
        }
    }
    if (clipped_area > 0)
    {
        DBG_COUNT("BRIDGE_OVERLAP_CLIP");
        dbg_log(Luminary::DBG_FILL, layer_region->layer()->print_z, "FILL",
                "BRIDGE_OVERLAP_CLIP layer=%zu lost=%.4fmm2 compensation=%.4fmm", layer_region->layer()->id(),
                clipped_area * 1e-12, double(compensation));
    }
    bridges = std::move(adjusted_bridges);
}

// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params,
// detect bridges.
// Trim "shells" by the expanded bridges.
Surfaces expand_bridges_detect_orientations(Surfaces &surfaces, std::vector<ExpansionZone> &expansion_zones,
                                            const float closing_radius, const bool use_air_floating,
                                            const ExPolygons *lower_slices, const double dbg_print_z)
{
    using namespace Luminary::Algorithm;

    double thickness;
    ExPolygons bridge_expolygons = fill_surfaces_extract_expolygons(surfaces, {stBottomBridge}, thickness);
    if (bridge_expolygons.empty())
        return {};

    // Calculate bridge anchors and their expansions in their respective shell region.
    ExpansionResult expansion_result{expand_expolygons(bridge_expolygons, expansion_zones)};

    std::vector<Bridge> bridges{get_grouped_bridges(std::move(bridge_expolygons), expansion_result.expansions)};
    bridge_expolygons.clear();

    std::sort(expansion_result.anchors.begin(), expansion_result.anchors.end(), Algorithm::lower_by_src_and_boundary);

    // Merge the groups with the same group id, produce surfaces by merging source overhangs with their newly expanded anchors.
    std::sort(expansion_result.expansions.begin(), expansion_result.expansions.end(), [](auto &l, auto &r)
              { return l.src_id < r.src_id || (l.src_id == r.src_id && l.boundary_id < r.boundary_id); });
    Surfaces out{merge_bridges(bridges, expansion_result.expansions, closing_radius, use_air_floating, lower_slices,
                               dbg_print_z)};

    // Clip by the expanded bridges.
    for (ExpansionZone &expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, out);
    return out;
}

Surfaces expand_merge_surfaces(Surfaces &surfaces, SurfaceType surface_type,
                               std::vector<ExpansionZone> &expansion_zones, const float closing_radius,
                               const double bridge_angle)
{
    using namespace Luminary::Algorithm;

    double thickness;
    ExPolygons src = fill_surfaces_extract_expolygons(surfaces, {surface_type}, thickness);
    if (src.empty())
        return {};

    unsigned processed_expolygons_count = 0;
    std::vector<RegionExpansion> expansions;
    for (ExpansionZone &expansion_zone : expansion_zones)
    {
        std::vector<RegionExpansion> zone_expansions = propagate_waves(src, expansion_zone.expolygons,
                                                                       expansion_zone.parameters);
        expansion_zone.expanded_into = !zone_expansions.empty();

        for (RegionExpansion &expansion : zone_expansions)
            expansion.boundary_id += processed_expolygons_count;

        processed_expolygons_count += expansion_zone.expolygons.size();
        append(expansions, std::move(zone_expansions));
    }

    std::vector<ExPolygon> expanded = merge_expansions_into_expolygons(std::move(src), std::move(expansions));
    // The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
    // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
    // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
    expanded = closing_ex(expanded, closing_radius);
    // Trim the zones by the expanded expolygons.
    for (ExpansionZone &expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, expanded);

    Surface templ{surface_type, {}};
    templ.bridge_angle = bridge_angle;
    Surfaces out;
    out.reserve(expanded.size());
    for (auto &expoly : expanded)
        out.emplace_back(templ, std::move(expoly));
    return out;
}

void LayerRegion::process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered)
{
    using namespace Luminary::Algorithm;

#ifdef PREFLIGHT_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* PREFLIGHT_DEBUG_SLICE_PROCESSING */

    // Width of the perimeters.
    float shell_width = 0;
    float expansion_min = 0;
    if (int num_perimeters = this->region().config().perimeters; num_perimeters > 0)
    {
        Flow external_perimeter_flow = this->flow(frExternalPerimeter);
        Flow perimeter_flow = this->flow(frPerimeter);
        shell_width = 0.5f * external_perimeter_flow.scaled_width() + external_perimeter_flow.scaled_spacing();
        shell_width += perimeter_flow.scaled_spacing() * (num_perimeters - 1);
        expansion_min = perimeter_flow.scaled_spacing();
    }
    else
    {
        // With no perimeters there is no shell to expand the external surfaces over.
        shell_width = float(SCALED_EPSILON);
        expansion_min = float(SCALED_EPSILON);
        ;
    }

    // Scaled expansions of the respective external surfaces.
    float expansion_top = shell_width * sqrt(2.);
    float expansion_bottom = expansion_top;
    // Calculate bridge expansion based on bridge_infill_perimeter_overlap setting.
    // This mirrors how infill_overlap works in PerimeterGenerator:
    // - 0% = edges touch (expansion = shell_width)
    // - 100% = full 1 perimeter overlap (expansion = shell_width + perimeter_spacing)
    // - -100% = full 1 perimeter gap (expansion = shell_width - perimeter_spacing)
    float expansion_bottom_bridge;
    {
        const float perimeter_spacing = this->flow(frPerimeter).scaled_spacing();
        const auto &config = this->region().config();

        // Get overlap as absolute value (percentage of perimeter_spacing)
        float overlap_amount;
        if (config.bridge_infill_perimeter_overlap.percent)
        {
            overlap_amount = perimeter_spacing * float(config.bridge_infill_perimeter_overlap.value) / 100.0f;
        }
        else
        {
            overlap_amount = float(scale_(config.bridge_infill_perimeter_overlap.value));
        }

        // Base expansion for "edges touch" (0%) is shell_width
        // Add overlap amount for the desired overlap/gap
        expansion_bottom_bridge = shell_width + overlap_amount;

        // Ensure we don't go negative (would invert the surface)
        expansion_bottom_bridge = std::max(expansion_bottom_bridge, expansion_min);
    }
    // Expand by waves of expansion_step size (expansion_step is scaled), but with no more steps than max_nr_expansion_steps.
    static constexpr const float expansion_step = scaled<float>(0.1);
    // Don't take more than max_nr_steps for small expansion_step.
    static constexpr const size_t max_nr_expansion_steps = 5;
    // Radius (with added epsilon) to absorb empty regions emering from regularization of ensuring, viz  const float narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
    const float closing_radius = 0.55f * 0.65f * 1.05f * this->flow(frSolidInfill).scaled_spacing();

    // Expand the top / bottom / bridge surfaces into the shell thickness solid infills.
    double layer_thickness;
    ExPolygons shells = union_ex(
        fill_surfaces_extract_expolygons(m_fill_surfaces.surfaces, {stInternalSolid, stBridgeAnchor}, layer_thickness));
    ExPolygons sparse = union_ex(
        fill_surfaces_extract_expolygons(m_fill_surfaces.surfaces, {stInternal}, layer_thickness));
    ExPolygons top_expolygons = union_ex(
        fill_surfaces_extract_expolygons(m_fill_surfaces.surfaces, {stTop}, layer_thickness));
    const auto expansion_params_into_sparse_infill = RegionExpansionParameters::build(expansion_min, expansion_step,
                                                                                      max_nr_expansion_steps);
    const auto expansion_params_into_solid_infill = RegionExpansionParameters::build(expansion_bottom_bridge,
                                                                                     expansion_step,
                                                                                     max_nr_expansion_steps);

    // The normal expansion zones (shells, sparse) are bounded by fill_expolygons which has
    // infill_overlap baked in. For bridges to have independent overlap control, we need
    // zones that extend to the perimeter area.
    //
    // Strategy: Create extended zones for bridge expansion by offsetting the fill areas
    // outward. The extension allows bridges to reach anywhere from -100% to +100% overlap.
    // The actual expansion distance is controlled by expansion_bottom_bridge.
    SurfaceCollection bridges;
    // Most layers carry no bridge surface; the zone unions and offset below exist only to
    // expand bridges, and both expansion paths return nothing without one.
    if (m_fill_surfaces.has(stBottomBridge))
    {
        const float perimeter_spacing = this->flow(frPerimeter).scaled_spacing();
        // Extend zones outward to cover the perimeter overlap range
        // The extension needs to "undo" any infill_overlap and provide room for bridge overlap
        // Using perimeter_spacing * 2 covers the full -100% to +100% range
        const float zone_extension = perimeter_spacing * 2.0f;

        // Create extended zones for bridges by offsetting existing fill areas
        ExPolygons bridge_zone_base = union_ex(shells, sparse);
        if (!top_expolygons.empty())
            bridge_zone_base = union_ex(bridge_zone_base, top_expolygons);
        ExPolygons extended_bridge_zones = offset_ex(bridge_zone_base, zone_extension);

        // Use extended zones for bridge expansion
        const auto bridge_expansion_params = RegionExpansionParameters::build(expansion_bottom_bridge, expansion_step,
                                                                              max_nr_expansion_steps);
        std::vector<ExpansionZone> bridge_expansion_zones{
            ExpansionZone{std::move(extended_bridge_zones), bridge_expansion_params},
        };

        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z;
        const double custom_angle = this->region().config().bridge_angle.value;
        bridges.surfaces = custom_angle > 0
                               ? expand_merge_surfaces(m_fill_surfaces.surfaces, stBottomBridge, bridge_expansion_zones,
                                                       closing_radius, Geometry::deg2rad(custom_angle))
                               : expand_bridges_detect_orientations(
                                     m_fill_surfaces.surfaces, bridge_expansion_zones, closing_radius,
                                     this->region().config().interlock_perimeters_enabled &&
                                         this->num_interlocking_shells() > 0,
                                     lower_layer != nullptr ? &lower_layer->lslices : nullptr, this->layer()->print_z);
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";

        // For counterbore bridges, DON'T override bridge_angle here. Keeping the
        // detected angle preserves natural bridge grouping (same angle = same group,
        // no inter-group overlap). The fill DIRECTION is overridden at fill time in
        // make_fills via Fill::counterbore_fill_angle.

        // Apply bridge overlap compensation to original bridge surfaces
        apply_bridge_overlap_compensation(bridges.surfaces, this, this->fill_expolygons());
    }

    // Subtract bridge areas from the normal fill zones to prevent overlap
    // The bridges were expanded with extended zones, so we need to remove their area
    // from shells/sparse/tops before other surfaces expand into them
    if (!bridges.surfaces.empty())
    {
        Polygons bridge_polys;
        for (const Surface &s : bridges.surfaces)
            polygons_append(bridge_polys, to_polygons(s.expolygon));
        shells = diff_ex(shells, bridge_polys);
        sparse = diff_ex(sparse, bridge_polys);
        top_expolygons = diff_ex(top_expolygons, bridge_polys);
    }

    // Create normal expansion zones for non-bridge surfaces
    std::vector<ExpansionZone> expansion_zones{
        ExpansionZone{std::move(shells), expansion_params_into_solid_infill},
        ExpansionZone{std::move(sparse), expansion_params_into_sparse_infill},
        ExpansionZone{std::move(top_expolygons), expansion_params_into_solid_infill},
    };

    m_fill_surfaces.remove_types({stTop});
    {
        Surface top_templ(stTop, {});
        top_templ.thickness = layer_thickness;
        m_fill_surfaces.append(std::move(expansion_zones.back().expolygons), top_templ);
    }

    expansion_zones.pop_back();

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_bottom, expansion_step,
                                                                        max_nr_expansion_steps);
    Surfaces bottoms = expand_merge_surfaces(m_fill_surfaces.surfaces, stBottom, expansion_zones, closing_radius);

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_top, expansion_step,
                                                                        max_nr_expansion_steps);
    Surfaces tops = expand_merge_surfaces(m_fill_surfaces.surfaces, stTop, expansion_zones, closing_radius);

    //    m_fill_surfaces.remove_types({ stBottomBridge, stBottom, stTop, stInternal, stInternalSolid });
    m_fill_surfaces.clear();
    unsigned zones_expolygons_count = 0;
    for (const ExpansionZone &zone : expansion_zones)
        zones_expolygons_count += zone.expolygons.size();
    reserve_more(m_fill_surfaces.surfaces, zones_expolygons_count + bridges.size() + bottoms.size() + tops.size());
    // Small overhangs (0.2-0.3mm) trigger expansion but are too narrow to actually extrude.
    // Only create stInternalSolid if the region is wide enough to fit solid infill extrusion(s).
    // Use erosion test with solid infill width as threshold: if it can't fit an extrusion, merge to sparse.
    {
        const Flow solid_infill_flow = this->flow(frSolidInfill);
        const float min_width = solid_infill_flow.scaled_width(); // Minimum printable width

        ExPolygons solid_regions;
        ExPolygons unprintable_slivers;

        // Test each expansion region for printable width
        for (ExPolygon &ex : expansion_zones[0].expolygons)
        {
            // Erode by half the solid infill width. If region survives, it's >= min_width.
            ExPolygons eroded = offset2_ex(ExPolygons{ex}, -min_width * 0.5f, +min_width * 0.5f);

            if (eroded.empty())
            {
                // Too narrow to fit a solid infill extrusion, so merge to sparse
                unprintable_slivers.push_back(std::move(ex));
            }
            else
            {
                // Wide enough to print solid infill, so keep as solid
                solid_regions.push_back(std::move(ex));
            }
        }

        // Add printable solid regions as stInternalSolid
        {
            Surface solid_templ(stInternalSolid, {});
            solid_templ.thickness = layer_thickness;
            m_fill_surfaces.append(std::move(solid_regions), solid_templ);
        }

        // Merge unprintable regions into sparse infill (reclassify, don't discard)
        append(expansion_zones[1].expolygons, std::move(unprintable_slivers));

        // Add sparse infill (original + reclassified unprintable regions)
        {
            Surface sparse_templ(stInternal, {});
            sparse_templ.thickness = layer_thickness;
            m_fill_surfaces.append(std::move(expansion_zones[1].expolygons), sparse_templ);
        }
    }

    // Problem: Solid infill next to bridge infill (stBottomBridge) can leave gaps.
    // Solution: ANY solid infill that touches bridge infill gets absorbed INTO the bridge.
    // This handles TRUE overhangs (stBottomBridge), not bridge_over_infill (stInternalBridge).
    if (!bridges.surfaces.empty())
    {
        // Get ALL solid surfaces (stInternalSolid AND stBottom) from m_fill_surfaces
        SurfacesPtr internal_solids = m_fill_surfaces.filter_by_types({stInternalSolid, stBridgeAnchor});

        // Also check bottoms - they're about to be appended and may be adjacent
        ExPolygons bottom_expolys;
        for (const Surface &s : bottoms)
        {
            bottom_expolys.push_back(s.expolygon);
        }

        // Collect all solid expolygons
        ExPolygons solid_expolys;
        for (const Surface *s : internal_solids)
        {
            solid_expolys.push_back(s->expolygon);
        }
        append(solid_expolys, bottom_expolys);

        if (!solid_expolys.empty())
        {
            // Extract bridge polygons
            ExPolygons bridge_expolys;
            for (const Surface &s : bridges.surfaces)
            {
                bridge_expolys.push_back(s.expolygon);
            }

            // Expand bridges by 2x bridge extrusion width to catch adjacent solids
            const float expand_dist = this->bridging_flow(frInfill).scaled_width() * 2.0f;
            ExPolygons expanded_bridges = offset_ex(bridge_expolys, expand_dist);

            // Find adjacent vs non-adjacent solids
            ExPolygons adjacent_solids = intersection_ex(solid_expolys, expanded_bridges);
            ExPolygons non_adjacent_solids = diff_ex(solid_expolys, expanded_bridges);

            if (!adjacent_solids.empty())
            {
                // Merge ALL adjacent solids into bridges
                ExPolygons combined = bridge_expolys;
                combined.insert(combined.end(), adjacent_solids.begin(), adjacent_solids.end());
                combined = union_ex(combined);

                // Apply aggressive closing to fill any gaps
                const float closing_dist = this->flow(frSolidInfill).scaled_width();
                combined = offset_ex(combined, closing_dist);
                combined = offset_ex(combined, -closing_dist);

                // Preserve per-surface bridge angles through the merge.
                // Each merged ExPolygon gets the angle from the original bridge surface
                // it overlaps most with. This keeps counterbore corridors with different
                // angles from collapsing to a single direction.
                Surfaces old_bridges;
                old_bridges.swap(bridges.surfaces);
                for (const ExPolygon &ep : combined)
                {
                    // Find the original bridge surface with the most overlap
                    double best_overlap = 0;
                    double best_angle = old_bridges.empty() ? 0 : old_bridges.front().bridge_angle;
                    for (const Surface &orig : old_bridges)
                    {
                        double overlap = 0;
                        for (const ExPolygon &ov : intersection_ex(ExPolygons{ep}, ExPolygons{orig.expolygon}))
                            overlap += std::abs(ov.area());
                        if (overlap > best_overlap)
                        {
                            best_overlap = overlap;
                            best_angle = orig.bridge_angle;
                        }
                    }
                    Surface s(stBottomBridge, ep);
                    s.bridge_angle = best_angle;
                    bridges.surfaces.push_back(s);
                }

                // Overlap expansion runs after the entire merge block completes
                // (see expand_bridges_for_overlap call after BRIDGE-ABSORB-ADJACENT-SOLID)

                // Figure out which non-adjacent solids were internal vs bottom
                ExPolygons non_adj_internal;
                ExPolygons non_adj_bottom;
                if (!non_adjacent_solids.empty())
                {
                    ExPolygons orig_internal;
                    for (const Surface *s : internal_solids)
                    {
                        orig_internal.push_back(s->expolygon);
                    }
                    non_adj_internal = intersection_ex(non_adjacent_solids, orig_internal);
                    non_adj_bottom = intersection_ex(non_adjacent_solids, bottom_expolys);
                }

                // Remove old stInternalSolid/stBridgeAnchor and add back non-adjacent ones
                m_fill_surfaces.remove_type(stInternalSolid);
                m_fill_surfaces.remove_type(stBridgeAnchor);
                if (!non_adj_internal.empty())
                {
                    Surface solid_templ(stInternalSolid, {});
                    solid_templ.thickness = layer_thickness;
                    m_fill_surfaces.append(std::move(non_adj_internal), solid_templ);
                }

                // Update bottoms to only contain non-adjacent
                if (!non_adj_bottom.empty())
                {
                    double bottom_bridge_angle = bottoms.empty() ? 0 : bottoms.front().bridge_angle;
                    bottoms.clear();
                    for (const ExPolygon &ep : non_adj_bottom)
                    {
                        Surface s(stBottom, ep);
                        s.bridge_angle = bottom_bridge_angle;
                        bottoms.push_back(s);
                    }
                }
                else
                {
                    bottoms.clear();
                }
            }
        }
    }

    // Now that merge logic is complete, expand bridges based on bridge_infill_perimeter_overlap.
    // This ensures overlap expansion happens on the FINAL merged geometry, not before merge.
    expand_bridges_for_overlap(bridges.surfaces, this, this->fill_expolygons());

    m_fill_surfaces.append(std::move(bridges.surfaces));
    m_fill_surfaces.append(std::move(bottoms));
    m_fill_surfaces.append(std::move(tops));

#ifdef PREFLIGHT_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* PREFLIGHT_DEBUG_SLICE_PROCESSING */
}

void LayerRegion::prepare_fill_surfaces()
{
#ifdef PREFLIGHT_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-initial");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-initial");
#endif /* PREFLIGHT_DEBUG_SLICE_PROCESSING */

    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */

    bool spiral_vase = this->layer()->object()->print()->config().spiral_vase;

    // if no solid layers are requested, turn top/bottom surfaces to internal
    if (!spiral_vase && this->region().config().top_solid_layers == 0)
    {
        for (Surface &surface : m_fill_surfaces)
            if (surface.is_top())
                surface.surface_type = stInternal;
    }
    if (this->region().config().bottom_solid_layers == 0)
    {
        for (Surface &surface : m_fill_surfaces)
            if (surface.is_bottom()) // (surface.surface_type == stBottom)
                surface.surface_type = stInternal;
    }

    // turn too small internal regions into solid regions according to the user setting
    if (!spiral_vase && this->region().config().fill_density.value > 0)
    {
        // When interlocking perimeters are enabled, we need ALL sparse regions to remain as stInternal
        // so they can be extracted for interlocking perimeter generation. If solid_infill_below_area
        // converts sparse regions to stInternalSolid, they won't be available for interlocking.
        // Override the threshold to 0 when interlocking is active (user's setting is preserved in UI).
        const bool has_interlocking = this->region().config().interlock_perimeters_enabled &&
                                      this->num_interlocking_shells() > 0;
        double threshold_area = has_interlocking ? 0.0 : this->region().config().solid_infill_below_area.value;

        // scaling an area requires two calls!
        double min_area = scale_(scale_(threshold_area));
        for (Surface &surface : m_fill_surfaces)
        {
            if (surface.surface_type == stInternal && surface.area() <= min_area)
            {
                surface.surface_type = stInternalSolid;
            }
        }
    }

#ifdef PREFLIGHT_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-final");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-final");
#endif /* PREFLIGHT_DEBUG_SLICE_PROCESSING */
}

double LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss * ss;
}

void LayerRegion::trim_surfaces(const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices())
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    m_slices.set(intersection_ex(this->slices().surfaces, trimming_polygons), stInternal);
}

void LayerRegion::elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step,
                                                  const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices())
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    Polygons tmp = intersection(this->slices().surfaces, trimming_polygons);
    append(tmp,
           diff(this->slices().surfaces, opening(this->slices().surfaces, elephant_foot_compensation_perimeter_step)));
    m_slices.set(union_ex(tmp), stInternal);
}

void LayerRegion::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const Surface &surface : this->slices())
        bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->slices())
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    for (const Surface &surface : this->fill_surfaces())
        svg.draw(surface.expolygon.lines(), surface_type_to_color_name(surface.surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx++).c_str());
}

void LayerRegion::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const Surface &surface : this->fill_surfaces())
        bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->fill_surfaces())
    {
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
        svg.draw_outline(surface.expolygon, "black", "blue", scale_(0.05));
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_fill_surfaces_to_svg(
        debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx++).c_str());
}

} // namespace Luminary
