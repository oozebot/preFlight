///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "TreeSupportCommon.hpp"

#include <limits>
#include <cstdio>

#include "BaobabSupport.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r::FFFTreeSupport
{

TreeSupportMeshGroupSettings::TreeSupportMeshGroupSettings(const PrintObject &print_object)
{
    const PrintConfig &print_config = print_object.print()->config();
    const PrintObjectConfig &config = print_object.config();
    const SlicingParameters &slicing_params = print_object.slicing_parameters();
    //    const std::vector<unsigned int>  printing_extruders = print_object.object_extruders();

    // Support must be enabled and set to Tree style.
    assert(config.support_material);
    assert(config.support_material_style == smsTree || config.support_material_style == smsOrganic ||
           config.support_material_style == smsBaobab);

    // Calculate maximum external perimeter width over all printing regions, taking into account the default layer height.
    coordf_t external_perimeter_width = 0.;
    for (size_t region_id = 0; region_id < print_object.num_printing_regions(); ++region_id)
    {
        const PrintRegion &region = print_object.printing_region(region_id);
        external_perimeter_width = std::max<coordf_t>(
            external_perimeter_width, region.flow(print_object, frExternalPerimeter, config.layer_height).width());
    }

    this->layer_height = scaled<coord_t>(config.layer_height.value);
    this->resolution = scaled<coord_t>(print_config.gcode_resolution.value);
    // Arache feature
    this->min_feature_size = scaled<coord_t>(config.min_feature_size.value);
    // +1 makes the threshold inclusive
    this->support_angle = 0.5 * M_PI -
                          std::clamp<double>((config.support_material_threshold + 1) * M_PI / 180., 0., 0.5 * M_PI);
    this->support_line_width = support_material_flow(&print_object, config.layer_height).scaled_width();
    this->support_roof_line_width = support_material_interface_flow(&print_object, config.layer_height).scaled_width();
    //FIXME add it to SlicingParameters and reuse in both tree and normal supports?
    this->support_bottom_enable = config.support_material_interface_layers.value > 0 &&
                                  config.support_material_bottom_interface_layers.value != 0;
    this->support_bottom_height = this->support_bottom_enable
                                      ? (config.support_material_bottom_interface_layers.value > 0
                                             ? config.support_material_bottom_interface_layers.value
                                             : config.support_material_interface_layers.value) *
                                            this->layer_height
                                      : 0;
    this->support_material_buildplate_only = config.support_material_buildplate_only;
    this->support_xy_distance = scaled<coord_t>(
        config.support_material_xy_spacing.get_abs_value(external_perimeter_width));
    // Separation of interfaces, it is likely smaller than support_xy_distance.
    this->support_xy_distance_overhang = std::min(this->support_xy_distance,
                                                  scaled<coord_t>(0.5 * external_perimeter_width));
    this->support_top_distance = scaled<coord_t>(slicing_params.gap_support_object);
    this->support_bottom_distance = scaled<coord_t>(slicing_params.gap_object_support);
    //    this->support_interface_skip_height =
    //    this->support_infill_angles     =
    this->support_roof_enable = config.support_material_interface_layers.value > 0;
    this->support_roof_layers = this->support_roof_enable ? config.support_material_interface_layers.value : 0;
    this->support_floor_enable = config.support_material_interface_layers.value > 0 &&
                                 config.support_material_bottom_interface_layers.value > 0;
    this->support_floor_layers = this->support_floor_enable ? config.support_material_bottom_interface_layers.value : 0;
    //    this->minimum_roof_area         =
    //    this->support_roof_angles       =
    this->support_roof_pattern = config.support_material_interface_pattern;
    this->support_pattern = config.support_material_pattern;
    this->support_line_spacing = scaled<coord_t>(config.support_material_spacing.value);
    //    this->support_bottom_offset     =
    //    this->support_wall_count        = config.support_material_with_sheath ? 1 : 0;
    this->support_wall_count = 1;
    this->support_roof_line_distance = scaled<coord_t>(config.support_material_interface_spacing.value) +
                                       this->support_roof_line_width;
    // Area is in mm², need to convert to scaled² units for area comparisons
    this->minimum_support_area = scaled<double>(scaled<double>(config.support_material_min_area.value));
    //    this->minimum_bottom_area       =
    //    this->support_offset            =
    this->support_tree_branch_distance = scaled<coord_t>(config.support_tree_branch_distance.value);
    this->support_baobab_max_canopy_angle = std::clamp<double>(config.support_baobab_max_canopy_angle * M_PI / 180., 0.,
                                                               0.5 * M_PI - EPSILON);
    this->support_tree_angle = std::clamp<double>(config.support_tree_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    this->support_tree_angle_slow = std::clamp<double>(config.support_tree_angle_slow * M_PI / 180., 0.,
                                                       this->support_tree_angle - EPSILON);
    this->support_tree_branch_diameter = scaled<coord_t>(config.support_tree_branch_diameter.value);
    this->support_tree_branch_diameter_angle = std::clamp<double>(config.support_tree_branch_diameter_angle * M_PI /
                                                                      180.,
                                                                  0., 0.5 * M_PI - EPSILON);
    this->support_tree_top_rate = config.support_tree_top_rate.value; // percent

    //    this->support_tree_tip_diameter = this->support_line_width;
    this->support_tree_tip_diameter = std::clamp(scaled<coord_t>(config.support_tree_tip_diameter.value), coord_t(0),
                                                 this->support_tree_branch_diameter);

    // Baobab parameterizes the tree here, at the single point where these settings are built
    // from the object, and after every stock assignment so no Organic-derived value survives
    // it. TreeModelVolumes and its precalculate() construct their own copy from the
    // PrintObject, so an override applied at only some call sites would leave the avoidance
    // cache quantized and sized for a different branch radius than the one the pathing engine
    // goes on to request - which spins getAvoidance() forever on a radius the cache can never
    // hold.
    if (is_baobab_object(print_object))
        baobab_apply_tree_settings(*this, config);
}

TreeSupportSettings::TreeSupportSettings(const TreeSupportMeshGroupSettings &mesh_group_settings,
                                         const SlicingParameters &slicing_params, const PrintObject *print_object)
    : support_line_width(mesh_group_settings.support_line_width)
    , layer_height(mesh_group_settings.layer_height)
    , branch_radius(mesh_group_settings.support_tree_branch_diameter / 2)
    , min_radius(mesh_group_settings.support_tree_tip_diameter / 2)
    , // The actual radius is 50 microns larger as the resulting branches will be increased by 50 microns to avoid rounding errors effectively increasing the xydistance
    maximum_move_distance((mesh_group_settings.support_tree_angle < M_PI / 2.)
                              ? (coord_t) (tan(mesh_group_settings.support_tree_angle) * layer_height)
                              : std::numeric_limits<coord_t>::max())
    , maximum_move_distance_slow((mesh_group_settings.support_tree_angle_slow < M_PI / 2.)
                                     ? (coord_t) (tan(mesh_group_settings.support_tree_angle_slow) * layer_height)
                                     : std::numeric_limits<coord_t>::max())
    , support_bottom_layers(mesh_group_settings.support_bottom_enable
                                ? (mesh_group_settings.support_bottom_height + layer_height / 2) / layer_height
                                : 0)
    ,
    // Ensure lines always stack nicely even if layer height is large.
    tip_layers(std::max((branch_radius - min_radius) / (support_line_width / 3), branch_radius / layer_height))
    , branch_radius_increase_per_layer(tan(mesh_group_settings.support_tree_branch_diameter_angle) * layer_height)
    , max_to_model_radius_increase(
          mesh_group_settings.support_tree_max_diameter_increase_by_merges_when_support_to_model / 2)
    , min_dtt_to_model(round_up_divide(mesh_group_settings.support_tree_min_height_to_model, layer_height))
    , increase_radius_until_radius(mesh_group_settings.support_tree_branch_diameter / 2)
    , increase_radius_until_layer(increase_radius_until_radius <= branch_radius
                                      ? tip_layers * (increase_radius_until_radius / branch_radius)
                                      : (increase_radius_until_radius - branch_radius) /
                                            branch_radius_increase_per_layer)
    , support_rests_on_model(!mesh_group_settings.support_material_buildplate_only)
    , xy_distance(mesh_group_settings.support_xy_distance)
    , xy_min_distance(
          std::min(mesh_group_settings.support_xy_distance, mesh_group_settings.support_xy_distance_overhang))
    , bp_radius(mesh_group_settings.support_tree_bp_diameter / 2)
    ,
    // Increase by half a line overlap, but not faster than 40 degrees angle (0 degrees means zero increase in radius).
    bp_radius_increase_per_layer(std::min(tan(0.7) * layer_height, 0.5 * support_line_width))
    , z_distance_bottom_layers(
          size_t(round(double(mesh_group_settings.support_bottom_distance) / double(layer_height))))
    , z_distance_top_layers(size_t(round(double(mesh_group_settings.support_top_distance) / double(layer_height))))
    ,
    //              support_infill_angles(mesh_group_settings.support_infill_angles),
    support_roof_angles(mesh_group_settings.support_roof_angles)
    , roof_pattern(mesh_group_settings.support_roof_pattern)
    , support_pattern(mesh_group_settings.support_pattern)
    , support_roof_line_width(mesh_group_settings.support_roof_line_width)
    , support_line_spacing(mesh_group_settings.support_line_spacing)
    , support_bottom_offset(mesh_group_settings.support_bottom_offset)
    , support_wall_count(mesh_group_settings.support_wall_count)
    , resolution(mesh_group_settings.resolution)
    , support_roof_line_distance(mesh_group_settings.support_roof_line_distance)
    , // in the end the actual infill has to be calculated to subtract interface from support areas according to interface_preference.
    settings(mesh_group_settings)
    , min_feature_size(mesh_group_settings.min_feature_size)
{
    // At least one tip layer must be defined.
    assert(tip_layers > 0);

    layer_start_bp_radius = (bp_radius - branch_radius) / bp_radius_increase_per_layer;

    if (TreeSupportSettings::soluble)
    {
        // safeOffsetInc can only work in steps of the size xy_min_distance in the worst case => xy_min_distance has to be a bit larger than 0 in this worst case and should be large enough for performance to not suffer extremely
        // When for all meshes the z bottom and top distance is more than one layer though the worst case is xy_min_distance + min_feature_size
        // This is not the best solution, but the only one to ensure areas can not lag though walls at high maximum_move_distance.
        xy_min_distance = std::max(xy_min_distance, scaled<coord_t>(0.1));
        xy_distance = std::max(xy_distance, xy_min_distance);
    }

    //            const std::unordered_map<std::string, InterfacePreference> interface_map = { { "support_area_overwrite_interface_area", InterfacePreference::SupportAreaOverwritesInterface }, { "interface_area_overwrite_support_area", InterfacePreference::InterfaceAreaOverwritesSupport }, { "support_lines_overwrite_interface_area", InterfacePreference::SupportLinesOverwriteInterface }, { "interface_lines_overwrite_support_area", InterfacePreference::InterfaceLinesOverwriteSupport }, { "nothing", InterfacePreference::Nothing } };
    //            interface_preference = interface_map.at(mesh_group_settings.get<std::string>("support_interface_priority"));
    //FIXME this was the default
    //            interface_preference = InterfacePreference::SupportLinesOverwriteInterface;
    //interface_preference = InterfacePreference::SupportAreaOverwritesInterface;
    interface_preference = InterfacePreference::InterfaceAreaOverwritesSupport;

    if (slicing_params.raft_layers() > 0)
    {
        // Fill in raft_layers with the heights of the layers below the first object layer.
        // First layer
        double z = slicing_params.first_print_layer_height;
        this->raft_layers.emplace_back(z);
        // Raft base layers
        for (size_t i = 1; i < slicing_params.base_raft_layers; ++i)
        {
            z += slicing_params.base_raft_layer_height;
            this->raft_layers.emplace_back(z);
        }
        // Raft interface layers
        for (size_t i = 0; i + 1 < slicing_params.interface_raft_layers; ++i)
        {
            z += slicing_params.interface_raft_layer_height;
            this->raft_layers.emplace_back(z);
        }
        // Raft contact layer
        if (slicing_params.raft_layers() > 1)
        {
            z = slicing_params.raft_contact_top_z;
            this->raft_layers.emplace_back(z);
        }
        if (double dist_to_go = slicing_params.object_print_z_min - z; dist_to_go > EPSILON)
        {
            // Layers between the raft contacts and bottom of the object.
            auto nsteps = int(ceil(dist_to_go / slicing_params.max_suport_layer_height));
            double step = dist_to_go / nsteps;
            for (int i = 0; i < nsteps; ++i)
            {
                z += step;
                this->raft_layers.emplace_back(z);
            }
        }
    }

    this->support_layer_zs = generate_support_layer_zs(slicing_params);

    // Under a variable layer height profile the object leaves that grid, so the two stacks stop
    // being the same thing and every input the engine reads per object layer has to be carried
    // across by height. Both walks collapse to the identity when the layer height is fixed.
    // All tables or none: consumers gate remaps on these being populated, and a populated walk
    // against an empty grid would map every layer to slot zero and drop everything.
    if (print_object != nullptr && !print_object->layers().empty() && !this->support_layer_zs.empty())
    {
        const auto layers = print_object->layers();

        this->grid_matches_object = layers.size() == this->support_layer_zs.size();
        if (this->grid_matches_object)
            for (size_t j = 0; j < layers.size(); ++j)
                if (std::abs(layers[j]->print_z - this->support_layer_zs[j]) > EPSILON)
                {
                    this->grid_matches_object = false;
                    break;
                }

        const auto object_layer_at = [&layers](const coordf_t z)
        {
            const auto it = std::lower_bound(layers.begin(), layers.end(), z - EPSILON,
                                             [](const Layer *l, const coordf_t v) { return l->print_z < v; });
            return size_t(std::min<ptrdiff_t>(it - layers.begin(), ptrdiff_t(layers.size()) - 1));
        };
        // Rounds down. The support layer carrying an overhang has to sit at or below the surface it
        // holds up; rounding to the nearest can land it above, which puts the branch tip inside the
        // object and the collision pass then drops the support entirely.
        const auto support_layer_at = [this](const coordf_t z)
        {
            const auto it = std::upper_bound(this->support_layer_zs.begin(), this->support_layer_zs.end(), z + EPSILON);
            return size_t(std::max<ptrdiff_t>(0, (it - this->support_layer_zs.begin()) - 1));
        };

        this->object_layer_of.reserve(this->support_layer_zs.size());
        for (const coordf_t z : this->support_layer_zs)
            this->object_layer_of.emplace_back(object_layer_at(z));

        // The slot an overhang is carried to keys off the surface it rests on, the layer's
        // bottom, one slot above the surface's own. A thin layer's top lands there anyway; a
        // thick layer's top can sit several slots higher, and carrying by it would place the
        // stack inside the layer's own collision, where no roof can be generated.
        const auto layer_bottom = [&layers](const size_t j)
        {
            return j > 0 ? layers[j - 1]->print_z : layers.front()->print_z - layers.front()->height;
        };
        this->support_layer_of.reserve(layers.size());
        for (size_t j = 0; j < layers.size(); ++j)
            this->support_layer_of.emplace_back(
                std::min(support_layer_at(layers[j]->print_z), support_layer_at(layer_bottom(j)) + 1));

        // The object surface resting on each support layer: the lowest surface among the layers
        // whose overhangs are carried there, so the contact anchors to exactly what it has to
        // clear. Slots that receive nothing fall back to the bottom of the layer covering their
        // height, which keeps every entry a real surface for reads that drifted off the carry.
        this->object_bottom_z_of.reserve(this->support_layer_zs.size());
        for (const size_t j : this->object_layer_of)
            this->object_bottom_z_of.emplace_back(layer_bottom(j));
        {
            std::vector<coordf_t> carried_min(this->support_layer_zs.size(), std::numeric_limits<coordf_t>::max());
            for (size_t j = 0; j < layers.size(); ++j)
                if (const size_t s = this->support_layer_of[j]; s < carried_min.size())
                    carried_min[s] = std::min(carried_min[s], layer_bottom(j));
            for (size_t i = 0; i < carried_min.size(); ++i)
                if (carried_min[i] != std::numeric_limits<coordf_t>::max())
                    this->object_bottom_z_of[i] = carried_min[i];
        }
    }
}

#if defined(TREE_SUPPORT_SHOW_ERRORS) && defined(_WIN32)
#define TREE_SUPPORT_SHOW_ERRORS_WIN32
#include <windows.h>
#endif

// Shared with generate_support_areas()
bool g_showed_critical_error = false;
bool g_showed_performance_warning = false;

void tree_supports_show_error(std::string_view message, bool critical)
{
#ifdef TREE_SUPPORT_SHOW_ERRORS_WIN32
    static bool showed_critical = false;
    static bool showed_performance = false;
    auto bugtype = std::string(critical ? " This is a critical bug. It may cause missing or malformed branches.\n"
                                        : "This bug should only decrease performance.\n");
    bool show = (critical && !g_showed_critical_error) || (!critical && !g_showed_performance_warning);
    (critical ? g_showed_critical_error : g_showed_performance_warning) = true;
    if (show)
        MessageBoxA(
            nullptr,
            std::string(
                "TreeSupport_2 MOD detected an error while generating the tree support.\nPlease report this back to me with profile and model.\nRevision 5.0\n" +
                std::string(message) + "\n" + bugtype)
                .c_str(),
            "Bug detected!", MB_OK | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_ICONWARNING);
#endif // TREE_SUPPORT_SHOW_ERRORS_WIN32
}

} // namespace Slic3r::FFFTreeSupport