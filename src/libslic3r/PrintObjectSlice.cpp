///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <boost/log/trivial.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "ClipperUtils.hpp"
#include "ElephantFootCompensation.hpp"
#include "Geometry/ConvexHull.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "ColorDithering.hpp"
#include "ColorMixer.hpp"
#include "FilamentOptics.hpp"
#include "MixedColorPalette.hpp"
#include "TriangleSelector.hpp"
#include <map>
#include <unordered_map>
#include "Print.hpp"
#include "ProgressConfig.hpp"
#include "ShortestPath.hpp"
#include "admesh/stl.h"
#include "libslic3r/Feature/Interlocking/InterlockingGenerator.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r
{

LayerPtrs new_layers(PrintObject *print_object,
                     // Object layers (pairs of bottom/top Z coordinate), without the raft.
                     const std::vector<coordf_t> &object_layers)
{
    LayerPtrs out;
    out.reserve(object_layers.size());
    auto id = int(print_object->slicing_parameters().raft_layers());
    coordf_t zmin = print_object->slicing_parameters().object_print_z_min;
    Layer *prev = nullptr;
    for (size_t i_layer = 0; i_layer < object_layers.size(); i_layer += 2)
    {
        coordf_t lo = object_layers[i_layer];
        coordf_t hi = object_layers[i_layer + 1];
        coordf_t slice_z = 0.5 * (lo + hi);
        // Snap again after adding the raft offset; the boundaries were snapped based at z = 0.
        coordf_t print_z = snap_print_z_whole_mm(hi + zmin);
        Layer *layer = new Layer(id++, print_object, hi - lo, print_z, slice_z);
        out.emplace_back(layer);
        if (prev != nullptr)
        {
            prev->upper_layer = layer;
            layer->lower_layer = prev;
        }
        prev = layer;
    }
    return out;
}

// Slice single triangle mesh.
static std::vector<ExPolygons> slice_volume(const ModelVolume &volume, const std::vector<float> &zs,
                                            const MeshSlicingParamsEx &params,
                                            const std::function<void()> &throw_on_cancel_callback)
{
    std::vector<ExPolygons> layers;
    if (!zs.empty())
    {
        indexed_triangle_set its = volume.mesh().its;
        if (its.indices.size() > 0)
        {
            MeshSlicingParamsEx params2{params};
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.)
                its_flip_triangles(its);
            layers = slice_mesh_ex(its, zs, params2, throw_on_cancel_callback);
            throw_on_cancel_callback();
        }
    }
    return layers;
}

// Slice single triangle mesh.
// Filter the zs not inside the ranges. The ranges are closed at the bottom and open at the top, they are sorted lexicographically and non overlapping.
static std::vector<ExPolygons> slice_volume(const ModelVolume &volume, const std::vector<float> &z,
                                            const std::vector<t_layer_height_range> &ranges,
                                            const MeshSlicingParamsEx &params,
                                            const std::function<void()> &throw_on_cancel_callback)
{
    std::vector<ExPolygons> out;
    if (!z.empty() && !ranges.empty())
    {
        if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second)
        {
            // All layers fit into a single range.
            out = slice_volume(volume, z, params, throw_on_cancel_callback);
        }
        else
        {
            std::vector<float> z_filtered;
            std::vector<std::pair<size_t, size_t>> n_filtered;
            z_filtered.reserve(z.size());
            n_filtered.reserve(2 * ranges.size());
            size_t i = 0;
            for (const t_layer_height_range &range : ranges)
            {
                for (; i < z.size() && z[i] < range.first; ++i)
                    ;
                size_t first = i;
                for (; i < z.size() && z[i] < range.second; ++i)
                    z_filtered.emplace_back(z[i]);
                if (i > first)
                    n_filtered.emplace_back(std::make_pair(first, i));
            }
            if (!n_filtered.empty())
            {
                std::vector<ExPolygons> layers = slice_volume(volume, z_filtered, params, throw_on_cancel_callback);
                out.assign(z.size(), ExPolygons());
                i = 0;
                for (const std::pair<size_t, size_t> &span : n_filtered)
                    for (size_t j = span.first; j < span.second; ++j)
                        out[j] = std::move(layers[i++]);
            }
        }
    }
    return out;
}

struct VolumeSlices
{
    ObjectID volume_id;
    std::vector<ExPolygons> slices;
};

static inline bool model_volume_needs_slicing(const ModelVolume &mv)
{
    ModelVolumeType type = mv.type();
    return type == ModelVolumeType::MODEL_PART || type == ModelVolumeType::NEGATIVE_VOLUME ||
           type == ModelVolumeType::PARAMETER_MODIFIER;
}

// Slice printable volumes, negative volumes and modifier volumes, sorted by ModelVolume::id().
// Apply closing radius.
// Apply positive XY compensation to ModelVolumeType::MODEL_PART and ModelVolumeType::PARAMETER_MODIFIER, not to ModelVolumeType::NEGATIVE_VOLUME.
// Apply contour simplification.
static std::vector<VolumeSlices> slice_volumes_inner(
    const PrintConfig &print_config, const PrintObjectConfig &print_object_config, const Transform3d &object_trafo,
    ModelVolumePtrs model_volumes, const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    const std::vector<float> &zs, const std::function<void()> &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<VolumeSlices> out;
    out.reserve(model_volumes.size());

    std::vector<t_layer_height_range> slicing_ranges;
    if (layer_ranges.size() > 1)
        slicing_ranges.reserve(layer_ranges.size());

    MeshSlicingParamsEx params_base;
    params_base.closing_radius = print_object_config.slice_closing_radius.value;
    params_base.extra_offset = 0;
    params_base.trafo = object_trafo;
    params_base.resolution = print_config.resolution.value;

    switch (print_object_config.slicing_mode.value)
    {
    case SlicingMode::Regular:
        params_base.mode = MeshSlicingParams::SlicingMode::Regular;
        break;
    case SlicingMode::EvenOdd:
        params_base.mode = MeshSlicingParams::SlicingMode::EvenOdd;
        break;
    case SlicingMode::CloseHoles:
        params_base.mode = MeshSlicingParams::SlicingMode::Positive;
        break;
    }

    params_base.mode_below = params_base.mode;

    const size_t num_extruders = print_config.nozzle_diameter.size();
    const bool is_mm_painted = num_extruders > 1 &&
                               std::any_of(model_volumes.cbegin(), model_volumes.cend(), [](const ModelVolume *mv)
                                           { return mv->is_mm_painted() || mv->is_color_mixing_painted(); });
    const auto extra_offset = is_mm_painted ? 0.f
                                            : std::max(0.f, float(print_object_config.xy_size_compensation.value));

    for (const ModelVolume *model_volume : model_volumes)
        if (model_volume_needs_slicing(*model_volume))
        {
            MeshSlicingParamsEx params{params_base};
            if (!model_volume->is_negative_volume())
                params.extra_offset = extra_offset;
            if (layer_ranges.size() == 1)
            {
                if (const PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front();
                    layer_range.has_volume(model_volume->id()))
                {
                    if (model_volume->is_model_part() && print_config.spiral_vase)
                    {
                        auto it = std::find_if(layer_range.volume_regions.begin(), layer_range.volume_regions.end(),
                                               [model_volume](const auto &slice)
                                               { return model_volume == slice.model_volume; });
                        params.mode = MeshSlicingParams::SlicingMode::PositiveLargestContour;
                        // Slice the bottom layers with SlicingMode::Regular.
                        // This needs to be in sync with LayerRegion::make_perimeters() spiral_vase!
                        const PrintRegionConfig &region_config = it->region->config();
                        params.slicing_mode_normal_below_layer = size_t(region_config.bottom_solid_layers.value);
                        for (; params.slicing_mode_normal_below_layer < zs.size() &&
                               zs[params.slicing_mode_normal_below_layer] <
                                   region_config.bottom_solid_min_thickness - EPSILON;
                             ++params.slicing_mode_normal_below_layer)
                            ;
                    }
                    out.push_back(
                        {model_volume->id(), slice_volume(*model_volume, zs, params, throw_on_cancel_callback)});
                }
            }
            else
            {
                assert(!print_config.spiral_vase);
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (!slicing_ranges.empty())
                    out.push_back({model_volume->id(),
                                   slice_volume(*model_volume, zs, slicing_ranges, params, throw_on_cancel_callback)});
            }
            if (!out.empty() && out.back().slices.empty())
                out.pop_back();
        }

    return out;
}

static inline VolumeSlices &volume_slices_find_by_id(std::vector<VolumeSlices> &volume_slices, const ObjectID id)
{
    auto it = lower_bound_by_predicate(volume_slices.begin(), volume_slices.end(),
                                       [id](const VolumeSlices &vs) { return vs.volume_id < id; });
    assert(it != volume_slices.end() && it->volume_id == id);
    return *it;
}

static inline bool overlap_in_xy(const PrintObjectRegions::BoundingBox &l, const PrintObjectRegions::BoundingBox &r)
{
    return !(l.max().x() < r.min().x() || l.min().x() > r.max().x() || l.max().y() < r.min().y() ||
             l.min().y() > r.max().y());
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_first(
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges, double z)
{
    auto it = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
                                       [z](const PrintObjectRegions::LayerRangeRegions &lr)
                                       { return lr.layer_height_range.second < z; });
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    if (z == it->layer_height_range.second)
        if (auto it_next = it; ++it_next != layer_ranges.end() && it_next->layer_height_range.first == z)
            it = it_next;
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    return it;
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_next(
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator it, double z)
{
    for (; it->layer_height_range.second <= z; ++it)
        assert(it != layer_ranges.end());
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z < it->layer_height_range.second);
    return it;
}

static std::vector<std::vector<ExPolygons>> slices_to_regions(ModelVolumePtrs model_volumes,
                                                              const PrintObjectRegions &print_object_regions,
                                                              const std::vector<float> &zs,
                                                              std::vector<VolumeSlices> &&volume_slices,
                                                              const std::function<void()> &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<std::vector<ExPolygons>> slices_by_region(print_object_regions.all_regions.size(),
                                                          std::vector<ExPolygons>(zs.size(), ExPolygons()));

    // First shuffle slices into regions if there is no overlap with another region possible, collect zs of the complex cases.
    std::vector<std::pair<size_t, float>> zs_complex;
    {
        size_t z_idx = 0;
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges)
        {
            for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.first; ++z_idx)
                ;
            if (layer_range.volume_regions.empty())
            {
            }
            else if (layer_range.volume_regions.size() == 1)
            {
                const ModelVolume *model_volume = layer_range.volume_regions.front().model_volume;
                assert(model_volume != nullptr);
                if (model_volume->is_model_part())
                {
                    VolumeSlices &slices_src = volume_slices_find_by_id(volume_slices, model_volume->id());
                    auto &slices_dst =
                        slices_by_region[layer_range.volume_regions.front().region->print_object_region_id()];
                    for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++z_idx)
                        slices_dst[z_idx] = std::move(slices_src.slices[z_idx]);
                }
            }
            else
            {
                zs_complex.reserve(zs.size());
                for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++z_idx)
                {
                    float z = zs[z_idx];
                    int idx_first_printable_region = -1;
                    bool complex = false;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++idx_region)
                    {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z)
                        {
                            if (idx_first_printable_region == -1 && region.model_volume->is_model_part())
                                idx_first_printable_region = idx_region;
                            else if (idx_first_printable_region != -1)
                            {
                                // Test for overlap with some other region.
                                for (int idx_region2 = idx_first_printable_region; idx_region2 < idx_region;
                                     ++idx_region2)
                                {
                                    const PrintObjectRegions::VolumeRegion &region2 =
                                        layer_range.volume_regions[idx_region2];
                                    if (region2.bbox->min().z() <= z && region2.bbox->max().z() >= z &&
                                        overlap_in_xy(*region.bbox, *region2.bbox))
                                    {
                                        complex = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (complex)
                        zs_complex.push_back({z_idx, z});
                    else if (idx_first_printable_region >= 0)
                    {
                        const PrintObjectRegions::VolumeRegion &region =
                            layer_range.volume_regions[idx_first_printable_region];
                        slices_by_region[region.region->print_object_region_id()][z_idx] = std::move(
                            volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]);
                    }
                }
            }
            throw_on_cancel_callback();
        }
    }

    // Second perform region clipping and assignment in parallel.
    if (!zs_complex.empty())
    {
        std::vector<std::vector<VolumeSlices *>> layer_ranges_regions_to_slices(
            print_object_regions.layer_ranges.size(), std::vector<VolumeSlices *>());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges)
        {
            std::vector<VolumeSlices *> &layer_range_regions_to_slices =
                layer_ranges_regions_to_slices[&layer_range - print_object_regions.layer_ranges.data()];
            layer_range_regions_to_slices.reserve(layer_range.volume_regions.size());
            for (const PrintObjectRegions::VolumeRegion &region : layer_range.volume_regions)
                layer_range_regions_to_slices.push_back(
                    &volume_slices_find_by_id(volume_slices, region.model_volume->id()));
        }
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, zs_complex.size()),
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices,
             &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range)
            {
                float z = zs_complex[range.begin()].second;
                auto it_layer_range = layer_range_first(print_object_regions.layer_ranges, z);
                // Per volume_regions slices at this Z height.
                struct RegionSlice
                {
                    ExPolygons expolygons;
                    // Identifier of this region in PrintObjectRegions::all_regions
                    int region_id;
                    ObjectID volume_id;
                    bool operator<(const RegionSlice &rhs) const
                    {
                        bool this_empty = this->region_id < 0 || this->expolygons.empty();
                        bool rhs_empty = rhs.region_id < 0 || rhs.expolygons.empty();
                        // Sort the empty items to the end of the list.
                        // Sort by region_id & volume_id lexicographically.
                        return !this_empty &&
                               (rhs_empty || (this->region_id < rhs.region_id ||
                                              (this->region_id == rhs.region_id && volume_id < volume_id)));
                    }
                };
                std::vector<RegionSlice> temp_slices;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++zs_complex_idx)
                {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices *> &layer_range_regions_to_slices =
                            layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at thiz Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices *&slices : layer_range_regions_to_slices)
                        {
                            const PrintObjectRegions::VolumeRegion &volume_region =
                                layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back(
                                {std::move(slices->slices[z_idx]),
                                 volume_region.region ? volume_region.region->print_object_region_id() : -1,
                                 volume_region.model_volume->id()});
                        }
                    }
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++idx_region)
                        if (!temp_slices[idx_region].expolygons.empty())
                        {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier())
                            {
                                assert(region.parent > -1);
                                bool next_region_same_modifier =
                                    idx_region + 1 < int(temp_slices.size()) &&
                                    layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice = temp_slices[idx_region];
                                ExPolygons source = std::move(this_slice.expolygons);
                                if (parent_slice.expolygons.empty())
                                {
                                    this_slice.expolygons.clear();
                                }
                                else
                                {
                                    this_slice.expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex(parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            }
                            else if (region.model_volume->is_model_part() || region.model_volume->is_negative_volume())
                            {
                                // Clip every non-zero region preceding it.
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++idx_region2)
                                    if (!temp_slices[idx_region2].expolygons.empty())
                                    {
                                        if (const PrintObjectRegions::VolumeRegion &region2 =
                                                layer_range.volume_regions[idx_region2];
                                            !region2.model_volume->is_negative_volume() &&
                                            overlap_in_xy(*region.bbox, *region2.bbox))
                                            temp_slices[idx_region2].expolygons =
                                                diff_ex(temp_slices[idx_region2].expolygons,
                                                        temp_slices[idx_region].expolygons);
                                    }
                            }
                        }
                    // Sort by region_id, push empty slices to the end.
                    std::sort(temp_slices.begin(), temp_slices.end());
                    // Remove the empty slices.
                    temp_slices.erase(std::find_if(temp_slices.begin(), temp_slices.end(), [](const auto &slice)
                                                   { return slice.region_id == -1 || slice.expolygons.empty(); }),
                                      temp_slices.end());
                    // Merge slices and store them to the output.
                    for (int i = 0; i < int(temp_slices.size());)
                    {
                        // Find a range of temp_slices with the same region_id.
                        int j = i;
                        bool merged = false;
                        ExPolygons &expolygons = temp_slices[i].expolygons;
                        for (++j; j < int(temp_slices.size()) && temp_slices[i].region_id == temp_slices[j].region_id;
                             ++j)
                            if (ExPolygons &expolygons2 = temp_slices[j].expolygons; !expolygons2.empty())
                            {
                                if (expolygons.empty())
                                {
                                    expolygons = std::move(expolygons2);
                                }
                                else
                                {
                                    append(expolygons, std::move(expolygons2));
                                    merged = true;
                                }
                            }
                        if (merged)
                            expolygons = closing_ex(expolygons, float(scale_(EPSILON)));
                        slices_by_region[temp_slices[i].region_id][z_idx] = std::move(expolygons);
                        i = j;
                    }
                    throw_on_cancel_callback();
                }
            });
    }

    return slices_by_region;
}

// Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
// was not really functional for a long day and nobody missed it.
// Could we reuse this fixing code one day?
/*
std::string fix_slicing_errors(LayerPtrs &layers, const std::function<void()> &throw_if_canceled)
{
    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(layers.size());
    for (size_t idx_layer = 0; idx_layer < layers.size(); ++ idx_layer)
        if (layers[idx_layer]->slicing_errors)
            buggy_layers.push_back(idx_layer);

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                Layer *layer     = layers[idx_layer];
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                // BOOST_LOG_TRIVIAL(trace) << "Attempting to repair layer" << idx_layer;
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    LayerRegion *layerm = layer->get_region(region_id);
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    for (size_t j = idx_layer + 1; j < layers.size(); ++ j)
                        if (! layers[j]->slicing_errors) {
                            upper_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    for (int j = int(idx_layer) - 1; j >= 0; -- j)
                        if (! layers[j]->slicing_errors) {
                            lower_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    // Collect outer contours and holes from the valid layers above & below.
                    Polygons outer;
                    outer.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) + 
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    size_t num_holes = 0;
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    Polygons holes;
                    holes.reserve(num_holes);
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    layerm->m_slices.set(diff_ex(union_(outer), holes), stInternal);
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    // remove empty layers from bottom
    while (! layers.empty() && (layers.front()->lslices.empty() || layers.front()->empty())) {
        delete layers.front();
        layers.erase(layers.begin());
        layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++ i)
            layers[i]->set_id(layers[i]->id() - 1);
    }

    return buggy_layers.empty() ? "" :
        "The model has overlapping or self-intersecting facets. I tried to repair it, "
        "however you might want to check the results or repair the input file and retry.\n";
}
*/

// Called by make_perimeters()
// 1) Decides Z positions of the layers,
// Counterbore bridge: modify slice geometry to progressively close holes.
// For each painted transition layer, find the bore (large hole) and shaft (small hole).
// The transition starts AT the painted layer (step 0, least fill) and extends upward -
// each subsequent layer gets more corridor fills, progressively closing the bore toward
// the shaft shape. More transition layers = counterbore extends deeper into the object.
void apply_counterbore_bridge_geometry(PrintObject &po)
{
    // Helper: extract a hole from lslices that overlaps the given area
    auto find_hole_overlapping = [](const ExPolygons &lslices, const ExPolygons &overlap_area) -> ExPolygons
    {
        ExPolygons holes;
        for (const ExPolygon &lslice : lslices)
        {
            for (const Polygon &hole : lslice.holes)
            {
                ExPolygon hole_ex(hole);
                hole_ex.contour.make_counter_clockwise();
                if (!intersection_ex(ExPolygons{hole_ex}, overlap_area).empty())
                    holes.push_back(hole_ex);
            }
        }
        return holes.empty() ? ExPolygons{} : union_ex(holes);
    };

    for (size_t layer_id = 0; layer_id < po.layer_count(); ++layer_id)
    {
        Layer *layer = po.get_layer(layer_id);
        if (layer->counterbore_bridge_painted_areas.empty())
            continue;
        if (layer_id == 0)
            continue;

        // Snapshot lslices for layers ABOVE the painted layer (the transition zone).
        int max_transition = 0;
        for (const auto &[gp, ntl] : layer->counterbore_bridge_painted_areas)
            max_transition = std::max(max_transition, std::min(ntl, (int) (po.layer_count() - 1 - layer_id)));
        std::vector<ExPolygons> target_lslices_snap(max_transition);
        for (int s = 0; s < max_transition; ++s)
            target_lslices_snap[s] = po.get_layer(layer_id + 1 + s)->lslices;
        ExPolygons ceiling_lslices_snap = layer->lslices;

        // Process each (ExPolygons, bridge_layers) group independently.
        // Each group may contain multiple ExPolygons (separate holes with the same bridge_layers).
        for (const auto &[group_polys, num_transition_layers] : layer->counterbore_bridge_painted_areas)
        {
            int layers_available = std::min(num_transition_layers, (int) (po.layer_count() - 1 - layer_id));
            if (layers_available <= 0)
                continue;

            // Process each ExPolygon in the group as a separate counterbore hole
            for (const ExPolygon &painted_region : group_polys)
            {
                ExPolygons single_painted = {painted_region};

                // Find bore from the painted layer (large hole).
                ExPolygons bore_poly = find_hole_overlapping(ceiling_lslices_snap, single_painted);
                if (bore_poly.empty())
                    continue;

                // Find shaft from the first layer above (small hole), using snapshot.
                ExPolygons shaft_poly;
                if (!target_lslices_snap.empty())
                    shaft_poly = find_hole_overlapping(target_lslices_snap[0], single_painted);
                if (shaft_poly.empty())
                    continue;

                BoundingBox shaft_bb = get_extents(shaft_poly);
                double shaft_extent = std::max(shaft_bb.size().x(), shaft_bb.size().y());

                // Build shaft-shaped corridors for each step. Instead of rectangular strips,
                // create the convex hull of the shaft polygon shifted in both perpendicular
                // directions. This follows the shaft shape exactly (hexagons, ovals, etc.)
                // so the crescent edges support the perimeters of the shaft layers above.
                struct CorridorInfo
                {
                    ExPolygons corridor;
                    double angle_rad;
                };
                std::vector<CorridorInfo> corridors;

                double smear_dist = shaft_extent * 2.0;

                for (int step = 0; step < layers_available; ++step)
                {
                    double angle_rad = step * M_PI / num_transition_layers;
                    double perp_angle = angle_rad + M_PI / 2.0;
                    coord_t dx = coord_t(cos(perp_angle) * smear_dist);
                    coord_t dy = coord_t(sin(perp_angle) * smear_dist);

                    // Collect all points from shaft + two shifted copies, then convex hull.
                    // Convex hull fills the gap between non-overlapping shifted copies,
                    // creating a continuous shaft-shaped corridor.
                    Points all_pts;
                    for (const ExPolygon &ep : shaft_poly)
                    {
                        for (const Point &p : ep.contour.points)
                        {
                            all_pts.push_back(p);
                            all_pts.push_back(p + Point(dx, dy));
                            all_pts.push_back(p + Point(-dx, -dy));
                        }
                    }
                    Polygon hull = Geometry::convex_hull(all_pts);
                    hull.make_counter_clockwise();
                    corridors.push_back({ExPolygons{ExPolygon(hull)}, angle_rad});
                }

                // Ring = bore - shaft (the gap that needs bridging)
                ExPolygons ring = diff_ex(bore_poly, shaft_poly, ApplySafetyOffset::Yes);
                if (ring.empty())
                    continue;

                // Transition starts AT the painted layer and extends upward.
                // The painted layer is step 0 (least fill), higher steps have more fill.
                for (int step = 0; step < layers_available; ++step)
                {
                    size_t target_layer_id = layer_id + step;
                    Layer *target_layer = po.get_layer(target_layer_id);

                    // Cumulative corridor intersection: step 0 = least fill, step N-1 = most fill.
                    ExPolygons remaining_hole;
                    for (int s = 0; s <= step; ++s)
                    {
                        if (s == 0)
                            remaining_hole = corridors[s].corridor;
                        else
                            remaining_hole = intersection_ex(remaining_hole, corridors[s].corridor);
                    }

                    // Bridge material = ring minus the remaining hole
                    ExPolygons bridge_material = diff_ex(ring, remaining_hole);
                    if (bridge_material.empty())
                        continue;

                    // Carve bore from current lslices (not snapshot) so multiple groups accumulate.
                    // For the painted layer (step 0), bore already exists as a hole so carve is a no-op.
                    ExPolygons carved = diff_ex(target_layer->lslices, bore_poly);
                    target_layer->lslices = union_ex(carved, bridge_material);
                    target_layer->lslice_indices_sorted_by_print_order = chain_expolygons(target_layer->lslices);

                    // Store corridor angle (perpendicular to corridor direction)
                    double bridge_angle = corridors[step].angle_rad + M_PI / 2.0;
                    target_layer->counterbore_bridge_regions.push_back(std::make_pair(bridge_material, bridge_angle));

                    if (target_layer->region_count() > 0)
                    {
                        LayerRegion *layerm = target_layer->get_region(0);
                        ExPolygons carved_surfaces = diff_ex(to_expolygons(layerm->m_slices.surfaces), bore_poly);
                        ExPolygons merged = union_ex(carved_surfaces, bridge_material);
                        layerm->m_slices.set(std::move(merged), stInternal);
                    }
                }
            }
        }
    }
}

// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
void PrintObject::slice()
{
    if (!this->set_started(posSlice))
        return;

    // Calculate absolute progress positions based on total slicing weight
    const float total_weight = g_progress_config.slicing.total_base();
    float accum = 0.0f;

    // Step 1: Prepare layer structure
    // printf("[%lld] SLICING STARTED - Preparing layers\n",
    //     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // fflush(stdout);
    accum += g_progress_config.slicing.prepare_layers;
    m_print->set_status(static_cast<int>((accum / total_weight) * 33.0f), _u8L("Preparing layers"));

    std::vector<coordf_t> layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile));

    // Step 2: Slice volumes
    // printf("[%lld] SLICING - Slicing volumes\n",
    //     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // fflush(stdout);
    accum += g_progress_config.slicing.slice_volumes;
    m_print->set_status(static_cast<int>((accum / total_weight) * 33.0f), _u8L("Slicing volumes"));

    this->slice_volumes();
    m_print->throw_if_canceled();
#if 0
    // Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
    // was not really functional for a long day and nobody missed it.
    // Could we reuse this fixing code one day?

    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(m_layers, [this](){ m_print->throw_if_canceled(); });
    m_print->throw_if_canceled();
    if (! warning.empty())
        BOOST_LOG_TRIVIAL(info) << warning;
#endif
    // Step 3: Process sliced regions
    // printf("[%lld] SLICING - Processing regions\n",
    //     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // fflush(stdout);
    accum += g_progress_config.slicing.process_sliced_regions;
    m_print->set_status(static_cast<int>((accum / total_weight) * 33.0f), _u8L("Processing regions"));

    // Counterbore bridge: project painted faces to layers, then modify slice geometry
    // to progressively close detected counterbore holes. This runs BEFORE lslices_ex
    // and build_up_down_graph so modified geometry is picked up by everything downstream.
    if (this->model_object()->is_counterbore_bridge_painted())
    {
        // Direct projection: iterate painted triangles, find their Z, project onto nearest layer.
        // This works for horizontal faces (overhang ceilings) that the segmentation pipeline misses.
        const Transform3d trafo = this->trafo_centered();
        for (const ModelVolume *mv : this->model_object()->volumes)
        {
            if (!mv->is_model_part() || !mv->is_counterbore_bridge_painted())
                continue;

            const TriangleMesh &mesh = mv->mesh();
            const indexed_triangle_set &its = mesh.its;
            const Transform3d vol_trafo = trafo * mv->get_matrix();

            // Iterate each possible state value (= bridge_layers count).
            // State 1 is legacy ENFORCER from old projects, treated as bridge_layers=2.
            // States 2-9 directly encode bridge_layers.
            for (int state_val = 1; state_val <= 9; ++state_val)
            {
                const int bridge_layers = std::max(state_val, 2);
                const indexed_triangle_set painted_its =
                    mv->counterbore_bridge_facets.get_facets(*mv, static_cast<TriangleStateType>(state_val));
                if (painted_its.indices.empty())
                    continue;

                // Collect per-layer ExPolygons for this bridge_layers group
                std::vector<ExPolygons> per_layer_polys(m_layers.size());

                int painted_tri_count = (int) painted_its.indices.size();
                for (int face_idx = 0; face_idx < painted_tri_count; ++face_idx)
                {
                    const auto &tri = painted_its.indices[face_idx];
                    Vec3d v0 = vol_trafo * painted_its.vertices[tri[0]].cast<double>();
                    Vec3d v1 = vol_trafo * painted_its.vertices[tri[1]].cast<double>();
                    Vec3d v2 = vol_trafo * painted_its.vertices[tri[2]].cast<double>();

                    double z_min = std::min({v0.z(), v1.z(), v2.z()});
                    double z_max = std::max({v0.z(), v1.z(), v2.z()});

                    for (size_t layer_id = 0; layer_id < m_layers.size(); ++layer_id)
                    {
                        Layer *layer = m_layers[layer_id];
                        double lz = layer->print_z;
                        double half_lh = layer->height * 0.5;
                        if (z_min <= lz + half_lh && z_max >= lz - half_lh)
                        {
                            Polygon tri_poly;
                            tri_poly.points.push_back(Point(scaled(v0.x()), scaled(v0.y())));
                            tri_poly.points.push_back(Point(scaled(v1.x()), scaled(v1.y())));
                            tri_poly.points.push_back(Point(scaled(v2.x()), scaled(v2.y())));
                            tri_poly.make_counter_clockwise();
                            if (tri_poly.area() > 0)
                                per_layer_polys[layer_id].emplace_back(ExPolygon(tri_poly));
                        }
                    }
                }

                // Merge and store per-layer painted areas for this bridge_layers group
                for (size_t layer_id = 0; layer_id < m_layers.size(); ++layer_id)
                {
                    if (per_layer_polys[layer_id].empty())
                        continue;
                    ExPolygons merged = offset2_ex(union_ex(per_layer_polys[layer_id]), scaled<float>(0.2),
                                                   scaled<float>(-0.2));
                    if (!merged.empty())
                        m_layers[layer_id]->counterbore_bridge_painted_areas.emplace_back(
                            std::make_pair(std::move(merged), bridge_layers));
                }
            }
        }

        // Now apply the progressive hole-closing geometry modification.
        apply_counterbore_bridge_geometry(*this);
        m_print->throw_if_canceled();
    }
    // Update bounding boxes, back up raw slices of complex models.
    accum += g_progress_config.slicing.process_geometry;
    m_print->set_status(static_cast<int>((accum / total_weight) * 33.0f), _u8L("Processing geometry"));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
                      [this](const tbb::blocked_range<size_t> &range)
                      {
                          for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx)
                          {
                              m_print->throw_if_canceled();
                              Layer &layer = *m_layers[layer_idx];
                              layer.lslices_ex.clear();
                              layer.lslices_ex.reserve(layer.lslices.size());
                              for (const ExPolygon &expoly : layer.lslices)
                                  layer.lslices_ex.push_back({get_extents(expoly)});
                              layer.backup_untyped_slices();
                          }
                      });
    // Interlink the lslices into a Z graph.
    // printf("[%lld] SLICING - Building layer graph\n",
    //     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // fflush(stdout);
    accum += g_progress_config.slicing.build_layer_graph;
    m_print->set_status(static_cast<int>((accum / total_weight) * 33.0f), _u8L("Building layer graph"));
    tbb::parallel_for(tbb::blocked_range<size_t>(1, m_layers.size()),
                      [this](const tbb::blocked_range<size_t> &range)
                      {
                          for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx)
                          {
                              m_print->throw_if_canceled();
                              Layer::build_up_down_graph(*m_layers[layer_idx - 1], *m_layers[layer_idx]);
                          }
                      });
    if (m_layers.empty())
        throw Slic3r::SlicingError(
            "No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n");
    this->set_done(posSlice);
}

template<typename ThrowOnCancel>
void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MM segmentation based on painting in MM segmentation gizmo
    std::vector<std::vector<ExPolygons>> segmentation = multi_material_segmentation_by_painting(print_object,
                                                                                                throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    // Color mixing: resolve painted color mixing regions to physical extruders per layer
    // This reads color_mixing_facets, runs segmentation, then merges resolved extruder
    // assignments into the main segmentation array.
    {
        bool has_color_mixing = false;
        for (const ModelVolume *mv : print_object.model_object()->volumes)
            if (mv->is_model_part() && mv->is_color_mixing_painted())
                has_color_mixing = true;

        // Fast-path: with a single extruder there's nothing to mix. Painted data is preserved
        // in the model but ignored at slice time.
        if (has_color_mixing && print_object.print()->config().nozzle_diameter.size() <= 1)
            has_color_mixing = false;

        if (has_color_mixing)
        {
            const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();

            // Collect which painting states are actually used across all volumes
            std::vector<int> used_states_list;
            for (const ModelVolume *mv : print_object.model_object()->volumes)
            {
                if (!mv->is_model_part())
                    continue;
                const auto &data = mv->color_mixing_facets.get_data();
                for (size_t i = 1; i < data.used_states.size(); ++i)
                    if (data.used_states[i])
                        if (std::find(used_states_list.begin(), used_states_list.end(), (int) i) ==
                            used_states_list.end())
                            used_states_list.push_back((int) i);
            }
            std::sort(used_states_list.begin(), used_states_list.end());

            if (!used_states_list.empty())
            {
                // Build compact remap: original_state → compact_state (1, 2, 3, ...)
                // and reverse map: compact_state → original_state
                std::map<int, int> state_to_compact;
                std::vector<int> compact_to_state; // index 0 = compact state 1's original
                for (size_t i = 0; i < used_states_list.size(); ++i)
                {
                    state_to_compact[used_states_list[i]] = (int) i + 1; // compact states are 1-based
                    compact_to_state.push_back(used_states_list[i]);
                }
                const size_t num_compact_states = used_states_list.size() + 1; // +1 for NONE at 0

                // Remap painted states to compact 1..N for segmentation. Held in a local
                // per-volume map rather than mutated onto mv.color_mixing_facets -- that
                // mutation raced under Print::process's tbb::parallel_for when two
                // PrintObjects of the same ModelObject sliced concurrently.
                std::unordered_map<const ModelVolume *, FacetsAnnotation> remapped;
                for (const ModelVolume *mv : print_object.model_object()->volumes)
                {
                    if (!mv->is_model_part() || !mv->is_color_mixing_painted())
                        continue;

                    TriangleSelector temp_selector(mv->mesh());
                    temp_selector.deserialize(mv->color_mixing_facets.get_data(), false);
                    temp_selector.remap_states(
                        [&state_to_compact](TriangleStateType s) -> TriangleStateType
                        {
                            int val = (int) s;
                            auto it = state_to_compact.find(val);
                            if (it != state_to_compact.end())
                                return TriangleStateType(it->second);
                            return TriangleStateType::NONE;
                        });
                    remapped.emplace(mv, FacetsAnnotation::make_transient(temp_selector.serialize()));
                }

                const float max_width = float(print_object.config().mmu_segmented_region_max_width.value);
                const float inter_depth = float(print_object.config().mmu_segmented_region_interlocking_depth.value);
                const bool inter_beam = print_object.config().interlocking_beam.value;

                const auto extract_color_facets = [&remapped](const ModelVolume &mv) -> ModelVolumeFacetsInfo
                {
                    auto it = remapped.find(&mv);
                    if (it != remapped.end())
                        return {it->second, true, false};
                    return {mv.color_mixing_facets, mv.is_color_mixing_painted(), false};
                };

                // Run segmentation with compact state count (like MMU: small number)
                std::vector<std::vector<ExPolygons>> color_seg =
                    segmentation_by_painting(print_object, extract_color_facets, num_compact_states, max_width,
                                             inter_depth, inter_beam, IncludeTopAndBottomLayers::Yes, throw_on_cancel);

                // Build palette from the print's filament colors. Each palette entry's
                // layer_pattern selects the physical filament for layer L via
                // pattern[L % pattern.size()]. State value (1-based) maps to palette index
                // via state - 1. Same logic as the gizmo's init_palette.
                const auto &nozzle_diams = print_object.print()->config().nozzle_diameter;
                const auto &filament_colors = print_object.print()->config().filament_colour;
                const auto &filament_tds = print_object.print()->config().filament_transmission_distance;
                std::vector<FilamentOptics> optics;
                for (size_t i = 0; i < nozzle_diams.size() && i < filament_colors.size(); ++i)
                {
                    ColorRGB rgb;
                    if (filament_colors.values[i].size() >= 7) // "#RRGGBB"
                    {
                        unsigned int r, g, b;
                        sscanf(filament_colors.values[i].c_str() + 1, "%02x%02x%02x", &r, &g, &b);
                        rgb = ColorRGB((float) r / 255.f, (float) g / 255.f, (float) b / 255.f);
                    }
                    float td = (i < filament_tds.values.size()) ? (float) filament_tds.values[i] : DEFAULT_FILAMENT_TD;
                    optics.emplace_back(rgb, td);
                }

                // Palette cache keyed on (filament colors, TDs, layer height); regenerated
                // only on divergence. Mutex protects concurrent slicing; the per-slice copy
                // is cheap. Layer height drives dither-stack opacity so predicted_color
                // matches what the printer lays down.
                static std::mutex s_palette_cache_mutex;
                static uint64_t s_palette_cache_key = 0;
                static MixedColorPalette s_palette_cache;
                MixedColorPalette palette;
                {
                    const float layer_height = (float) print_object.config().layer_height.value;
                    const uint64_t key = MixedColorPalette::cache_key(optics, layer_height);
                    std::lock_guard<std::mutex> lock(s_palette_cache_mutex);
                    if (key != s_palette_cache_key || s_palette_cache.colors().empty())
                    {
                        s_palette_cache.clear();
                        if (optics.size() >= 2)
                            s_palette_cache.auto_generate(optics, layer_height, 12, 10);
                        s_palette_cache_key = key;
                    }
                    palette = s_palette_cache;
                }

                // Defensive: seed recipes from the current palette for any painted volume
                // that arrives without a recipe table (foreign 3MF, programmatic paint).
                // Best-effort: find_best_match later snaps each region to the closest
                // achievable color in the current loadout.
                for (ModelVolume *mv : print_object.model_object()->volumes)
                {
                    if (mv->is_model_part() && mv->is_color_mixing_painted() && mv->color_mixing_palette.empty())
                        ensure_color_mixing_recipes_for_used_states(palette, *mv);
                }

                // Find a recipe table to resolve painted state values against. All painted
                // volumes share the same recipe table contents (the gizmo mirrors the runtime
                // palette into each), so any painted volume's table is equivalent here.
                const std::vector<ColorMixingRecipe> *recipes = nullptr;
                for (const ModelVolume *mv : print_object.model_object()->volumes)
                {
                    if (mv->is_model_part() && mv->is_color_mixing_painted() && !mv->color_mixing_palette.empty())
                    {
                        recipes = &mv->color_mixing_palette;
                        break;
                    }
                }

                // Helper: pick the closest pure filament to a target rgb. Used as the safety
                // net when normal palette lookup fails so we never silently fall back to
                // extruder 0 regardless of what color was painted.
                auto closest_pure_filament = [&](uint32_t rgb) -> int
                {
                    if (optics.empty())
                        return 0;
                    ColorRGB target(float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f,
                                    float(rgb & 0xFF) / 255.f);
                    int best_idx = 0;
                    float best_de = std::numeric_limits<float>::infinity();
                    for (int i = 0; i < (int) optics.size(); ++i)
                    {
                        float de = ColorMixing::delta_e(target, optics[i].color);
                        if (de < best_de)
                        {
                            best_de = de;
                            best_idx = i;
                        }
                    }
                    return best_idx;
                };

                // Per-state resolution: locked recipes get a constant extruder, color-match
                // recipes carry a layer_pattern for dithering. fallback_extruder is the closest
                // pure filament to the recipe rgb -- used if the dither pattern is somehow
                // empty so we never silently fall back to extruder 0.
                struct StateResolution
                {
                    bool locked = false;
                    int extruder = 0;
                    int fallback_extruder = 0;
                    std::vector<int> pattern; // empty for locked entries
                };
                std::map<int, StateResolution> resolution_cache;

                auto resolve_state = [&](int original_state) -> const StateResolution &
                {
                    auto it = resolution_cache.find(original_state);
                    if (it != resolution_cache.end())
                        return it->second;
                    StateResolution r;
                    const int recipe_idx = original_state - 1;
                    if (recipes && recipe_idx >= 0 && recipe_idx < (int) recipes->size())
                    {
                        const ColorMixingRecipe &rec = (*recipes)[recipe_idx];
                        if (rec.is_locked())
                        {
                            r.locked = true;
                            r.extruder = (int) rec.extruder_lock;
                            r.fallback_extruder = (int) rec.extruder_lock;
                        }
                        else
                        {
                            r.fallback_extruder = closest_pure_filament(rec.rgb);

                            // Prefer direct index lookup: the recipe index matches the palette
                            // entry the user picked (mirror copies them 1:1). Only fall back to
                            // find_best_match when the entry no longer matches (filaments changed
                            // since painting). This preserves 3-way blends that would otherwise
                            // drift to a 2-way with a similar predicted color.
                            int best = -1;
                            if (recipe_idx >= 0 && recipe_idx < (int) palette.colors().size())
                            {
                                // Check the entry still matches the recipe rgb (same filaments)
                                const MixedColor &candidate = palette.colors()[recipe_idx];
                                uint32_t candidate_rgb =
                                    ((uint32_t) (std::clamp(candidate.predicted_color.r(), 0.f, 1.f) * 255.f + 0.5f)
                                     << 16) |
                                    ((uint32_t) (std::clamp(candidate.predicted_color.g(), 0.f, 1.f) * 255.f + 0.5f)
                                     << 8) |
                                    ((uint32_t) (std::clamp(candidate.predicted_color.b(), 0.f, 1.f) * 255.f + 0.5f));
                                // Allow small rounding tolerance (up to 2 LSB per channel)
                                int dr = std::abs((int) ((rec.rgb >> 16) & 0xFF) -
                                                  (int) ((candidate_rgb >> 16) & 0xFF));
                                int dg = std::abs((int) ((rec.rgb >> 8) & 0xFF) - (int) ((candidate_rgb >> 8) & 0xFF));
                                int db = std::abs((int) (rec.rgb & 0xFF) - (int) (candidate_rgb & 0xFF));
                                if (dr <= 2 && dg <= 2 && db <= 2)
                                    best = recipe_idx; // exact match, use the original entry
                            }
                            if (best < 0)
                                best = palette.find_best_match(rec.rgb);

                            if (best >= 0 && best < (int) palette.colors().size())
                                r.pattern = palette.colors()[best].layer_pattern;
                        }
                    }
                    else if (recipe_idx >= 0 && recipe_idx < (int) palette.colors().size())
                    {
                        // No recipe table -- treat the state value as a direct index into the
                        // runtime palette.
                        r.pattern = palette.colors()[recipe_idx].layer_pattern;
                        if (!r.pattern.empty())
                            r.fallback_extruder = r.pattern.front();
                    }
                    auto [ins_it, _] = resolution_cache.emplace(original_state, std::move(r));
                    return ins_it->second;
                };

                // Color mixing base layers: for the first N layers, route painted regions to a
                // single fixed extruder instead of running them through the dither cycle. This
                // keeps the bottom face of the print uniform (no visible striping where the
                // dither hasn't converged yet) and avoids constant filament swaps on the
                // bed-adhesion-critical layers.
                const int base_layer_count = std::max(0, print_object.config().color_mixing_base_layers.value);
                const ColorMixingBaseExtruder base_mode = print_object.config().color_mixing_base_extruder.value;
                int base_extruder = -1; // -1 means "let the region fall through to volume default"
                if (base_layer_count > 0 && !optics.empty() && base_mode != cmbeVolumeDefault)
                {
                    int pick = 0;
                    float best_lum = (base_mode == cmbeDarkest) ? 999.f : -1.f;
                    for (int i = 0; i < (int) optics.size(); ++i)
                    {
                        const float lum = optics[i].color.r() * 0.299f + optics[i].color.g() * 0.587f +
                                          optics[i].color.b() * 0.114f;
                        if ((base_mode == cmbeDarkest && lum < best_lum) ||
                            (base_mode == cmbeLightest && lum > best_lum))
                        {
                            best_lum = lum;
                            pick = i;
                        }
                    }
                    base_extruder = pick;
                }

                // Resolve: for each layer, move color mixing ExPolygons to physical extruder buckets
                for (size_t layer_id = 0; layer_id < color_seg.size() && layer_id < segmentation.size(); ++layer_id)
                {
                    auto &color_layer = color_seg[layer_id];
                    auto &mm_layer = segmentation[layer_id];

                    // Ensure mm_layer has enough extruder buckets
                    if (mm_layer.size() < num_extruders)
                        mm_layer.resize(num_extruders);

                    const bool in_base = (int) layer_id < base_layer_count;
                    // Volume-default mode for base layers: skip placing painted regions into any
                    // extruder bucket so the slicer's normal pipeline routes them via the volume's
                    // assigned extruder (the same path unpainted triangles take).
                    if (in_base && base_mode == cmbeVolumeDefault)
                        continue;

                    for (size_t idx = 0; idx < color_layer.size(); ++idx)
                    {
                        if (color_layer[idx].empty())
                            continue;

                        // Map compact index back to original state index
                        int compact_state = (int) idx + 1;
                        int original_state = 0;
                        if (compact_state >= 1 && compact_state <= (int) compact_to_state.size())
                            original_state = compact_to_state[compact_state - 1];

                        int physical_extruder;
                        if (in_base && base_extruder >= 0)
                        {
                            // Darkest/Lightest mode for base layers: route every painted region to
                            // the single chosen filament regardless of recipe.
                            physical_extruder = base_extruder;
                        }
                        else
                        {
                            const StateResolution &r = resolve_state(original_state);
                            if (r.locked)
                                physical_extruder = r.extruder;
                            else if (!r.pattern.empty())
                            {
                                DitherConfig config;
                                physical_extruder = resolve_layer_filament(r.pattern, (int) layer_id, config);
                            }
                            else
                                // No pattern available: snap to the closest pure filament instead of
                                // silently dropping the region onto extruder 0.
                                physical_extruder = r.fallback_extruder;
                        }
                        physical_extruder = std::clamp(physical_extruder, 0, (int) num_extruders - 1);

                        size_t seg_idx = (size_t) physical_extruder;
                        if (seg_idx >= mm_layer.size())
                            mm_layer.resize(seg_idx + 1);
                        append(mm_layer[seg_idx], std::move(color_layer[idx]));
                    }
                }
            }
        }
    }
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range)
        {
            const auto &layer_ranges = print_object.shared_regions()->layer_ranges;
            double z = print_object.get_layer(int(range.begin()))->slice_z;
            auto it_layer_range = layer_range_first(layer_ranges, z);
            const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();

            struct ByExtruder
            {
                ExPolygons expolygons;
                BoundingBox bbox;
            };

            struct ByRegion
            {
                ExPolygons expolygons;
                bool needs_merge{false};
            };

            std::vector<ByExtruder> by_extruder;
            std::vector<ByRegion> by_region;
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id)
            {
                throw_on_cancel();
                Layer &layer = *print_object.get_layer(int(layer_id));
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                // Gather per extruder expolygons.
                by_extruder.assign(num_extruders, ByExtruder());
                by_region.assign(layer.region_count(), ByRegion());
                bool layer_split = false;
                for (size_t extruder_id = 0; extruder_id < num_extruders; ++extruder_id)
                {
                    ByExtruder &region = by_extruder[extruder_id];
                    append(region.expolygons, std::move(segmentation[layer_id][extruder_id]));
                    if (!region.expolygons.empty())
                    {
                        region.bbox = get_extents(region.expolygons);
                        layer_split = true;
                    }
                }

                if (!layer_split)
                    continue;

                // Split LayerRegions by by_extruder regions.
                // layer_range.painted_regions are sorted by extruder ID and parent PrintObject region ID.
                auto it_painted_region_begin = layer_range.painted_regions.cbegin();
                for (int parent_layer_region_idx = 0; parent_layer_region_idx < layer.region_count();
                     ++parent_layer_region_idx)
                {
                    if (it_painted_region_begin == layer_range.painted_regions.cend())
                        continue;

                    const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                    const PrintRegion &parent_print_region = parent_layer_region.region();
                    assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                    if (parent_layer_region.slices().empty())
                        continue;

                    // Find the first PaintedRegion, which overrides the parent PrintRegion.
                    auto it_first_painted_region = std::find_if(
                        it_painted_region_begin, layer_range.painted_regions.cend(),
                        [&layer_range, &parent_print_region](const auto &painted_region)
                        {
                            return layer_range.volume_regions[painted_region.parent].region->print_object_region_id() ==
                                   parent_print_region.print_object_region_id();
                        });

                    if (it_first_painted_region == layer_range.painted_regions.cend())
                        continue; // This LayerRegion isn't overrides by any PaintedRegion.

                    assert(&parent_print_region == layer_range.volume_regions[it_first_painted_region->parent].region);

                    // Update the beginning PaintedRegion iterator for the next iteration.
                    it_painted_region_begin = it_first_painted_region;

                    const BoundingBox parent_layer_region_bbox = get_extents(parent_layer_region.slices().surfaces);
                    bool self_trimmed = false;
                    int self_extruder_id = -1; // 1-based extruder ID
                    for (int extruder_id = 1; extruder_id <= int(by_extruder.size()); ++extruder_id)
                    {
                        const ByExtruder &segmented = by_extruder[extruder_id - 1];
                        if (!segmented.bbox.defined || !parent_layer_region_bbox.overlap(segmented.bbox))
                            continue;

                        // Find the first target region iterator.
                        auto it_target_region = std::find_if(it_painted_region_begin,
                                                             layer_range.painted_regions.cend(),
                                                             [extruder_id](const auto &painted_region)
                                                             {
                                                                 return int(painted_region.extruder_id) >= extruder_id;
                                                             });

                        assert(it_target_region != layer_range.painted_regions.end());
                        assert(layer_range.volume_regions[it_target_region->parent].region == &parent_print_region &&
                               int(it_target_region->extruder_id) == extruder_id);

                        // Update the beginning PaintedRegion iterator for the next iteration.
                        it_painted_region_begin = it_target_region;

                        // FIXME: Don't trim by self, it is not reliable.
                        if (it_target_region->region == &parent_print_region)
                        {
                            self_extruder_id = extruder_id;
                            continue;
                        }

                        // Steal from this region.
                        int target_region_id = it_target_region->region->print_object_region_id();
                        ExPolygons stolen = intersection_ex(parent_layer_region.slices().surfaces,
                                                            segmented.expolygons);
                        if (!stolen.empty())
                        {
                            ByRegion &dst = by_region[target_region_id];
                            if (dst.expolygons.empty())
                            {
                                dst.expolygons = std::move(stolen);
                            }
                            else
                            {
                                append(dst.expolygons, std::move(stolen));
                                dst.needs_merge = true;
                            }
                        }
                    }

                    if (!self_trimmed)
                    {
                        // Trim slices of this LayerRegion with all the MM regions.
                        // OLD CODE: Polygons mine = to_polygons(parent_layer_region.slices().surfaces);
                        // to_polygons() flattens ExPolygons into separate Polygons, losing the hole relationship.
                        // Instead, work with ExPolygons directly to preserve holes.
                        ExPolygons mine = to_expolygons(parent_layer_region.slices().surfaces);
                        for (auto &segmented : by_extruder)
                        {
                            if (&segmented - by_extruder.data() + 1 != self_extruder_id && segmented.bbox.defined &&
                                parent_layer_region_bbox.overlap(segmented.bbox))
                            {
                                mine = diff_ex(mine, segmented.expolygons);
                                if (mine.empty())
                                    break;
                            }
                        }

                        // Filter out unprintable polygons produced by subtraction multi-material painted regions from layerm.region().
                        // ExPolygon returned from multi-material segmentation does not precisely match ExPolygons in layerm.region()
                        // (because of preprocessing of the input regions in multi-material segmentation). Therefore, subtraction from
                        // layerm.region() could produce a huge number of small unprintable regions for the model's base extruder.
                        // This could, on some models, produce bulges with the model's base color (#7109).
                        if (!mine.empty())
                        {
                            // opening_ex takes a single delta parameter (it internally does offset by -delta then +delta)
                            mine = opening_ex(mine, scaled<float>(5. * EPSILON));
                        }

                        if (!mine.empty())
                        {
                            ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                            if (dst.expolygons.empty())
                            {
                                dst.expolygons = std::move(mine);
                            }
                            else
                            {
                                append(dst.expolygons, std::move(mine));
                                dst.needs_merge = true;
                            }
                        }
                    }
                }

                // Re-create Surfaces of LayerRegions.
                for (int region_id = 0; region_id < layer.region_count(); ++region_id)
                {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge)
                    {
                        // Multiple regions were merged into one.
                        src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                    }

                    layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
                }
            }
        });
}

template<typename ThrowOnCancel>
void apply_fuzzy_skin_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns fuzzy skin segmentation based on painting in the fuzzy skin painting gizmo.
    std::vector<std::vector<ExPolygons>> segmentation = fuzzy_skin_segmentation_by_painting(print_object,
                                                                                            throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    struct ByRegion
    {
        ExPolygons expolygons;
        bool needs_merge{false};
    };

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range)
        {
            const auto &layer_ranges = print_object.shared_regions()->layer_ranges;
            auto it_layer_range = layer_range_first(layer_ranges, print_object.get_layer(int(range.begin()))->slice_z);

            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx)
            {
                throw_on_cancel();

                Layer &layer = *print_object.get_layer(int(layer_idx));
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;

                assert(segmentation[layer_idx].size() == 1);
                const ExPolygons &fuzzy_skin_segmentation = segmentation[layer_idx][0];
                const BoundingBox fuzzy_skin_segmentation_bbox = get_extents(fuzzy_skin_segmentation);
                if (fuzzy_skin_segmentation.empty())
                    continue;

                // Split LayerRegions by painted fuzzy skin regions.
                // layer_range.fuzzy_skin_painted_regions are sorted by parent PrintObject region ID.
                std::vector<ByRegion> by_region(layer.region_count());
                auto it_fuzzy_skin_region_begin = layer_range.fuzzy_skin_painted_regions.cbegin();
                for (int parent_layer_region_idx = 0; parent_layer_region_idx < layer.region_count();
                     ++parent_layer_region_idx)
                {
                    if (it_fuzzy_skin_region_begin == layer_range.fuzzy_skin_painted_regions.cend())
                        continue;

                    const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                    const PrintRegion &parent_print_region = parent_layer_region.region();
                    assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                    if (parent_layer_region.slices().empty())
                        continue;

                    // Find the first FuzzySkinPaintedRegion, which overrides the parent PrintRegion.
                    auto it_fuzzy_skin_region =
                        std::find_if(it_fuzzy_skin_region_begin, layer_range.fuzzy_skin_painted_regions.cend(),
                                     [&layer_range, &parent_print_region](const auto &fuzzy_skin_region)
                                     {
                                         return fuzzy_skin_region.parent_print_object_region_id(layer_range) ==
                                                parent_print_region.print_object_region_id();
                                     });

                    if (it_fuzzy_skin_region == layer_range.fuzzy_skin_painted_regions.cend())
                        continue; // This LayerRegion isn't overrides by any FuzzySkinPaintedRegion.

                    assert(it_fuzzy_skin_region->parent_print_object_region(layer_range) == &parent_print_region);

                    // Update the beginning FuzzySkinPaintedRegion iterator for the next iteration.
                    it_fuzzy_skin_region_begin = std::next(it_fuzzy_skin_region);

                    const BoundingBox parent_layer_region_bbox = get_extents(parent_layer_region.slices().surfaces);
                    ExPolygons layer_region_remaining_expolygons = to_expolygons(parent_layer_region.slices().surfaces);
                    // Don't trim by self, it is not reliable.
                    if (parent_layer_region_bbox.overlap(fuzzy_skin_segmentation_bbox) &&
                        it_fuzzy_skin_region->region != &parent_print_region)
                    {
                        // Steal from this region.
                        const int target_region_id = it_fuzzy_skin_region->region->print_object_region_id();
                        ExPolygons stolen = intersection_ex(parent_layer_region.slices().surfaces,
                                                            fuzzy_skin_segmentation);
                        if (!stolen.empty())
                        {
                            ByRegion &dst = by_region[target_region_id];
                            if (dst.expolygons.empty())
                            {
                                dst.expolygons = std::move(stolen);
                            }
                            else
                            {
                                append(dst.expolygons, std::move(stolen));
                                dst.needs_merge = true;
                            }
                        }

                        // Trim slices of this LayerRegion by the fuzzy skin region.
                        layer_region_remaining_expolygons = diff_ex(layer_region_remaining_expolygons,
                                                                    fuzzy_skin_segmentation);

                        // Filter out unprintable polygons. Detailed explanation is inside apply_mm_segmentation.
                        if (!layer_region_remaining_expolygons.empty())
                        {
                            // opening_ex takes a single delta parameter (it internally does offset by -delta then +delta)
                            layer_region_remaining_expolygons = opening_ex(layer_region_remaining_expolygons,
                                                                           scaled<float>(5. * EPSILON));
                        }
                    }

                    if (!layer_region_remaining_expolygons.empty())
                    {
                        ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                        if (dst.expolygons.empty())
                        {
                            dst.expolygons = std::move(layer_region_remaining_expolygons);
                        }
                        else
                        {
                            append(dst.expolygons, std::move(layer_region_remaining_expolygons));
                            dst.needs_merge = true;
                        }
                    }
                }

                // Re-create Surfaces of LayerRegions.
                for (int region_id = 0; region_id < layer.region_count(); ++region_id)
                {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge)
                    {
                        // Multiple regions were merged into one.
                        src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                    }

                    layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
                }
            }
        }); // end of parallel_for
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
void PrintObject::slice_volumes()
{
    BOOST_LOG_TRIVIAL(info) << "Slicing volumes..." << log_memory_info();
    const Print *print = this->print();
    const auto throw_on_cancel_callback = std::function<void()>([print]() { print->throw_if_canceled(); });

    // Clear old LayerRegions, allocate for new PrintRegions.
    for (Layer *layer : m_layers)
    {
        layer->m_regions.clear();
        layer->m_regions.reserve(m_shared_regions->all_regions.size());
        for (const std::unique_ptr<PrintRegion> &pr : m_shared_regions->all_regions)
            layer->m_regions.emplace_back(new LayerRegion(layer, pr.get()));
    }

    std::vector<float> slice_zs = zs_from_layers(m_layers);
    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(
        this->model_object()->volumes, *m_shared_regions, slice_zs,
        slice_volumes_inner(print->config(), this->config(), this->trafo_centered(), this->model_object()->volumes,
                            m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback),
        throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++region_id)
    {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++layer_id)
        {
            if (!by_layer[layer_id].empty())
            {
                by_layer[layer_id] = union_ex(by_layer[layer_id]);
            }
            m_layers[layer_id]->regions()[region_id]->m_slices.append(std::move(by_layer[layer_id]), stInternal);
        }
    }

    region_slices.clear();

    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - removing top empty layers";
    while (!m_layers.empty())
    {
        const Layer *layer = m_layers.back();
        if (!layer->empty())
            break;
        delete layer;
        m_layers.pop_back();
    }
    if (!m_layers.empty())
        m_layers.back()->upper_layer = nullptr;
    m_print->throw_if_canceled();

    // Is any ModelVolume multi-material painted or color mixing painted?
    const bool has_color_mixing_paint = std::any_of(this->model_object()->volumes.cbegin(),
                                                    this->model_object()->volumes.cend(), [](const ModelVolume *mv)
                                                    { return mv->is_color_mixing_painted(); });
    if (m_print->config().nozzle_diameter.size() > 1 &&
        (this->model_object()->is_mm_painted() || has_color_mixing_paint))
    {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object is multi-material painted.
        if (m_config.xy_size_compensation.value != 0.f)
        {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                _u8L(
                    "An object has enabled XY Size compensation which will not be used because it is also multi-material painted.\nXY Size "
                    "compensation cannot be combined with multi-material painting.") +
                    "\n" + (_u8L("Object name")) + ": " + this->model_object()->name);
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - MMU segmentation";
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    // Is any ModelVolume fuzzy skin painted?
    if (this->model_object()->is_fuzzy_skin_painted())
    {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object has custom fuzzy skin painted.
        if (m_config.xy_size_compensation.value != 0.f)
        {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                _u8L(
                    "An object has enabled XY Size compensation which will not be used because it is also fuzzy skin painted.\nXY Size "
                    "compensation cannot be combined with fuzzy skin painting.") +
                    "\n" + (_u8L("Object name")) + ": " + this->model_object()->name);
        }

        // Instead of calling apply_fuzzy_skin_segmentation() which steals geometry from parent regions
        // (causing jagged solid infill), we store the painted areas and use them in make_perimeters()
        // to create PerimeterRegions. This applies fuzzy skin to perimeters without modifying slice geometry.
        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - Storing fuzzy skin painted areas (no geometry theft)";
        std::vector<std::vector<ExPolygons>> segmentation =
            fuzzy_skin_segmentation_by_painting(*this, [print]() { print->throw_if_canceled(); });
        assert(segmentation.size() == m_layers.size());

        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
                          [this, &segmentation](const tbb::blocked_range<size_t> &range)
                          {
                              for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id)
                              {
                                  m_print->throw_if_canceled();
                                  Layer *layer = m_layers[layer_id];
                                  // Store painted areas for this layer (used by make_perimeters to create PerimeterRegions)
                                  if (!segmentation[layer_id].empty() && !segmentation[layer_id][0].empty())
                                  {
                                      layer->fuzzy_skin_painted_areas = std::move(segmentation[layer_id][0]);
                                  }
                              }
                          });
        // NOTE: We do NOT call apply_fuzzy_skin_segmentation() - no geometry theft!
    }

    if (m_config.interlocking_beam)
    {
        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - Applying multi-material interlocking";
        InterlockingGenerator::generate_interlocking_structure(*this);
        m_print->throw_if_canceled();
    }

    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - begin";
    {
        // Compensation value, scaled. Only applying the negative scaling here, as the positive scaling has already been applied during slicing.
        const size_t num_extruders = print->config().nozzle_diameter.size();
        const auto xy_compensation_scaled = (num_extruders > 1 && this->is_mm_painted())
                                                ? scaled<float>(0.f)
                                                : scaled<float>(std::min(m_config.xy_size_compensation.value, 0.));
        const float elephant_foot_compensation_scaled =
            (m_config.raft_layers == 0) ?
                                        // Only enable Elephant foot compensation if printing directly on the print bed.
                float(scale_(m_config.elefant_foot_compensation.value))
                                        : 0.f;
        // Uncompensated slices for the first layer in case the Elephant foot compensation is applied.
        ExPolygons lslices_1st_layer;
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, xy_compensation_scaled, elephant_foot_compensation_scaled,
             &lslices_1st_layer](const tbb::blocked_range<size_t> &range)
            {
                for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id)
                {
                    m_print->throw_if_canceled();
                    Layer *layer = m_layers[layer_id];
                    // Apply size compensation and perform clipping of multi-part objects.
                    float elfoot = (layer_id == 0) ? elephant_foot_compensation_scaled : 0.f;
                    if (layer->m_regions.size() == 1)
                    {
                        // Optimized version for a single region layer.
                        // Single region, growing or shrinking.
                        LayerRegion *layerm = layer->m_regions.front();
                        if (elfoot > 0)
                        {
                            // Apply the elephant foot compensation and store the 1st layer slices without the Elephant foot compensation applied.
                            lslices_1st_layer = to_expolygons(std::move(layerm->m_slices.surfaces));
                            float delta = xy_compensation_scaled;
                            if (delta > elfoot)
                            {
                                delta -= elfoot;
                                elfoot = 0.f;
                            }
                            else if (delta > 0)
                                elfoot -= delta;
                            layerm->m_slices.set(union_ex(Slic3r::elephant_foot_compensation(
                                                     (delta == 0.f) ? lslices_1st_layer
                                                                    : offset_ex(lslices_1st_layer, delta),
                                                     layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
                                                 stInternal);
                            if (xy_compensation_scaled < 0.f)
                                lslices_1st_layer = offset_ex(std::move(lslices_1st_layer), xy_compensation_scaled);
                        }
                        else if (xy_compensation_scaled < 0.f)
                        {
                            // Apply the XY compensation.
                            layerm->m_slices.set(offset_ex(to_expolygons(std::move(layerm->m_slices.surfaces)),
                                                           xy_compensation_scaled),
                                                 stInternal);
                        }
                    }
                    else
                    {
                        if (xy_compensation_scaled < 0.f || elfoot > 0.f)
                        {
                            // Apply the negative XY compensation.
                            Polygons trimming;
                            static const float eps = float(scale_(m_config.slice_closing_radius.value) * 1.5);
                            if (elfoot > 0.f)
                            {
                                lslices_1st_layer = offset_ex(layer->merged(eps),
                                                              std::min(xy_compensation_scaled, 0.f) - eps);
                                trimming = to_polygons(Slic3r::elephant_foot_compensation(
                                    lslices_1st_layer, layer->m_regions.front()->flow(frExternalPerimeter),
                                    unscale<double>(elfoot)));
                            }
                            else
                            {
                                trimming = offset(layer->merged(float(SCALED_EPSILON)),
                                                  xy_compensation_scaled - float(SCALED_EPSILON));
                            }

                            for (size_t region_id = 0; region_id < layer->m_regions.size(); ++region_id)
                                layer->m_regions[region_id]->trim_surfaces(trimming);
                        }
                    }

                    // Merge all regions' slices to get islands sorted topologically, chain them by a shortest path in separate index list
                    layer->make_slices();
                }
            });
        if (elephant_foot_compensation_scaled > 0.f && !m_layers.empty())
        {
            // The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
            // Store the uncompensated value there.
            //FIXME is this operation needed? MMU painting and brim now have to do work arounds to work with compensated layer, not with the uncompensated layer.
            // There may be subtle issues removing this block such as support raft sticking too well with the first object layer.
            Layer &layer = *m_layers.front();
            assert(layer.id() == 0);
            layer.lslices = std::move(lslices_1st_layer);
            layer.lslice_indices_sorted_by_print_order = chain_expolygons(layer.lslices);
        }
    }

    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - end";
}

std::vector<Polygons> PrintObject::slice_support_volumes(const ModelVolumeType model_volume_type) const
{
    auto it_volume = this->model_object()->volumes.begin();
    auto it_volume_end = this->model_object()->volumes.end();
    for (; it_volume != it_volume_end && (*it_volume)->type() != model_volume_type; ++it_volume)
        ;
    std::vector<Polygons> slices;
    if (it_volume != it_volume_end)
    {
        // Found at least a single support volume of model_volume_type.
        std::vector<float> zs = zs_from_layers(this->layers());
        std::vector<char> merge_layers;
        bool merge = false;
        const Print *print = this->print();
        auto throw_on_cancel_callback = std::function<void()>([print]() { print->throw_if_canceled(); });
        MeshSlicingParamsEx params;
        params.trafo = this->trafo_centered();
        for (; it_volume != it_volume_end; ++it_volume)
            if ((*it_volume)->type() == model_volume_type)
            {
                std::vector<ExPolygons> slices2 = slice_volume(*(*it_volume), zs, params, throw_on_cancel_callback);
                if (slices.empty())
                {
                    slices.reserve(slices2.size());
                    for (ExPolygons &src : slices2)
                        slices.emplace_back(to_polygons(std::move(src)));
                }
                else if (!slices2.empty())
                {
                    if (merge_layers.empty())
                        merge_layers.assign(zs.size(), false);
                    for (size_t i = 0; i < zs.size(); ++i)
                    {
                        if (slices[i].empty())
                            slices[i] = to_polygons(std::move(slices2[i]));
                        else if (!slices2[i].empty())
                        {
                            append(slices[i], to_polygons(std::move(slices2[i])));
                            merge_layers[i] = true;
                            merge = true;
                        }
                    }
                }
            }
        if (merge)
        {
            std::vector<Polygons *> to_merge;
            to_merge.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++i)
                if (merge_layers[i])
                    to_merge.emplace_back(&slices[i]);
            tbb::parallel_for(tbb::blocked_range<size_t>(0, to_merge.size()),
                              [&to_merge](const tbb::blocked_range<size_t> &range)
                              {
                                  for (size_t i = range.begin(); i < range.end(); ++i)
                                      *to_merge[i] = union_(*to_merge[i]);
                              });
        }
    }
    return slices;
}

} // namespace Slic3r
