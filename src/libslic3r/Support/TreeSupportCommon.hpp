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

#ifndef slic3r_TreeSupportCommon_hpp
#define slic3r_TreeSupportCommon_hpp

#include <assert.h>
#include <math.h>
#include <oneapi/tbb/scalable_allocator.h>
#include <stddef.h>
#include <string_view>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "SupportCommon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Support/SupportLayer.hpp"
#include "libslic3r/Support/SupportParameters.hpp"

namespace Slic3r
{
class PrintObject;
} // namespace Slic3r

using namespace Slic3r::FFFSupport;

namespace Slic3r
{

namespace FFFTreeSupport
{

using LayerIndex = int;

enum class InterfacePreference
{
    InterfaceAreaOverwritesSupport,
    SupportAreaOverwritesInterface,
    InterfaceLinesOverwriteSupport,
    SupportLinesOverwriteInterface,
    Nothing
};

struct TreeSupportMeshGroupSettings
{
    TreeSupportMeshGroupSettings() = default;
    explicit TreeSupportMeshGroupSettings(const PrintObject &print_object);

    /*********************************************************************/
    /* Print parameters, not support specific:                           */
    /*********************************************************************/
    coord_t layer_height{scaled<coord_t>(0.15)};
    // Maximum Deviation (meshfix_maximum_deviation)
    // The maximum deviation allowed when reducing the resolution for the Maximum Resolution setting. If you increase this,
    // the print will be less accurate, but the g-code will be smaller. Maximum Deviation is a limit for Maximum Resolution,
    // so if the two conflict the Maximum Deviation will always be held true.
    coord_t resolution{scaled<coord_t>(0.025)};
    // Minimum Feature Size (aka minimum line width) - Arachne specific
    // Minimum thickness of thin features. Model features that are thinner than this value will not be printed, while features thicker
    // than the Minimum Feature Size will be widened to the Minimum Wall Line Width.
    coord_t min_feature_size{scaled<coord_t>(0.1)};

    /*********************************************************************/
    /* General support parameters:                                       */
    /*********************************************************************/

    // Support Overhang Angle
    // The minimum angle of overhangs for which support is added. At a value of 0° all overhangs are supported, 90° will not provide any support.
    double support_angle{50. * M_PI / 180.};
    // Support Line Width
    // Width of a single support structure line.
    coord_t support_line_width{scaled<coord_t>(0.4)};
    // Support Roof Line Width: Width of a single support roof line.
    coord_t support_roof_line_width{scaled<coord_t>(0.4)};
    // Enable Support Floor (aka bottom interfaces)
    // Generate a dense slab of material between the bottom of the support and the model. This will create a skin between the model and support.
    bool support_bottom_enable{false};
    // Support Floor Thickness
    // The thickness of the support floors. This controls the number of dense layers that are printed on top of places of a model on which support rests.
    coord_t support_bottom_height{scaled<coord_t>(1.)};
    bool support_material_buildplate_only{false};
    // Support X/Y Distance
    // Distance of the support structure from the print in the X/Y directions.
    // minimum: 0, maximum warning: 1.5 * machine_nozzle_tip_outer_diameter
    coord_t support_xy_distance{scaled<coord_t>(0.7)};
    // Minimum Support X/Y Distance
    // Distance of the support structure from the overhang in the X/Y directions.
    // minimum_value: 0,  minimum warning": support_xy_distance - support_line_width * 2, maximum warning: support_xy_distance
    coord_t support_xy_distance_overhang{scaled<coord_t>(0.2)};
    // Support Top Distance
    // Distance from the top of the support to the print.
    coord_t support_top_distance{scaled<coord_t>(0.1)};
    // Support Bottom Distance
    // Distance from the print to the bottom of the support.
    coord_t support_bottom_distance{scaled<coord_t>(0.1)};
    //FIXME likely not needed, optimization for clipping of interface layers
    // When checking where there's model above and below the support, take steps of the given height. Lower values will slice slower, while higher values
    // may cause normal support to be printed in some places where there should have been support interface.
    coord_t support_interface_skip_height{scaled<coord_t>(0.3)};
    // Support Infill Line Directions
    // A list of integer line directions to use. Elements from the list are used sequentially as the layers progress and when the end
    // of the list is reached, it starts at the beginning again. The list items are separated by commas and the whole list is contained
    // in square brackets. Default is an empty list which means use the default angle 0 degrees.
    //    std::vector<double>             support_infill_angles                   {};
    // Enable Support Roof
    // Generate a dense slab of material between the top of support and the model. This will create a skin between the model and support.
    bool support_roof_enable{false};
    // Support Roof Thickness
    // The thickness of the support roofs. This controls the amount of dense layers at the top of the support on which the model rests.
    coord_t support_roof_layers{2};
    bool support_floor_enable{false};
    coord_t support_floor_layers{2};
    // Minimum Support Roof Area
    // Minimum area size for the roofs of the support. Polygons which have an area smaller than this value will be printed as normal support.
    double minimum_roof_area{scaled<double>(scaled<double>(1.))};
    // A list of integer line directions to use. Elements from the list are used sequentially as the layers progress
    // and when the end of the list is reached, it starts at the beginning again. The list items are separated
    // by commas and the whole list is contained in square brackets. Default is an empty list which means
    // use the default angles (alternates between 45 and 135 degrees if interfaces are quite thick or 90 degrees).
    std::vector<double> support_roof_angles{};
    // Support Roof Pattern (aka top interface)
    // The pattern with which the roofs of the support are printed.
    SupportMaterialInterfacePattern support_roof_pattern{smipAuto};
    // Support Pattern
    // The pattern of the support structures of the print. The different options available result in sturdy or easy to remove support.
    SupportMaterialPattern support_pattern{smpRectilinear};
    // Support Line Distance
    // Distance between the printed support structure lines. This setting is calculated by the support density.
    coord_t support_line_spacing{scaled<coord_t>(2.66 - 0.4)};
    // Support Floor Horizontal Expansion
    // Amount of offset applied to the floors of the support.
    coord_t support_bottom_offset{scaled<coord_t>(0.)};
    // Support Wall Line Count
    // The number of walls with which to surround support infill. Adding a wall can make support print more reliably
    // and can support overhangs better, but increases print time and material used.
    // tree: 1, zig-zag: 0, concentric: 1
    int support_wall_count{1};
    // Support Roof Line Distance
    // Distance between the printed support roof lines. This setting is calculated by the Support Roof Density, but can be adjusted separately.
    coord_t support_roof_line_distance{scaled<coord_t>(0.4)};
    // Minimum Support Area
    // Minimum area size for support polygons. Polygons which have an area smaller than this value will not be generated.
    // Filters tiny paint-on specs that can cause negative flow or crashes.
    // Value loaded from config.support_material_min_area in TreeSupportCommon.cpp.
    // Type is double for proper scaled² area units (like minimum_roof_area).
    double minimum_support_area{scaled<double>(scaled<double>(0.7))};
    // Minimum Support Floor Area
    // Minimum area size for the floors of the support. Polygons which have an area smaller than this value will be printed as normal support.
    coord_t minimum_bottom_area{scaled<coord_t>(1.0)};
    // Support Horizontal Expansion
    // Amount of offset applied to all support polygons in each layer. Positive values can smooth out the support areas and result in more sturdy support.
    coord_t support_offset{scaled<coord_t>(0.)};

    /*********************************************************************/
    /* Parameters for the Cura tree supports implementation:             */
    /*********************************************************************/

    // Baobab Maximum canopy angle
    // The maximum angle, from vertical, at which a canopy tapers down onto its trunk. The bead
    // rule and a leaning trunk may both reduce it further; see baobab_taper_angle_rad().
    double support_baobab_max_canopy_angle{40. * M_PI / 180.};
    // Baobab: plant trunks on stable top surfaces of the model instead of always descending to
    // the plate. Set only by baobab_apply_tree_settings(), so non-baobab organic never plants.
    bool support_baobab_plant_on_model{false};
    // Tree Support Maximum branch angle
    // The maximum angle of the branches, when the branches have to avoid the model. Use a lower angle to make them more vertical and more stable. Use a higher angle to be able to have more reach.
    // minimum: 0, minimum warning: 20, maximum: 89, maximum warning": 85
    double support_tree_angle{60. * M_PI / 180.};
    // Tree Support Branch diameter angle
    // The angle of the branches' diameter as they gradually become thicker towards the bottom. An angle of 0 will cause the branches to have uniform thickness over their length.
    // A bit of an angle can increase stability of the tree support.
    // minimum: 0, maximum: 89.9999, maximum warning: 15
    double support_tree_branch_diameter_angle{5. * M_PI / 180.};
    // Tree Support Branch distance
    // How far apart the branches need to be when they touch the model. Making this distance small will cause
    // the tree support to touch the model at more points, causing better overhang but making support harder to remove.
    coord_t support_tree_branch_distance{scaled<coord_t>(1.)};
    // Tree Support Branch diameter
    // The diameter of the thinnest branches of tree support. Thicker branches are more sturdy. Branches towards the base will be thicker than this.
    // minimum: 0.001, minimum warning: support_line_width * 2
    coord_t support_tree_branch_diameter{scaled<coord_t>(2.)};

    /*********************************************************************/
    /* Parameters new to the Thomas Rahm's tree supports implementation: */
    /*********************************************************************/

    // Tree Support Preferred branch angle
    // The preferred angle of the branches, when they do not have to avoid the model. Use a lower angle to make them more vertical and more stable. Use a higher angle for branches to merge faster.
    // minimum: 0, minimum warning: 10, maximum: support_tree_angle, maximum warning: support_tree_angle-1
    double support_tree_angle_slow{50. * M_PI / 180.};
    // Tree Support Diameter Increase To Model
    // The most the diameter of a branch that has to connect to the model may increase by merging with branches that could reach the buildplate.
    // Increasing this reduces print time, but increases the area of support that rests on model
    // minimum: 0
    coord_t support_tree_max_diameter_increase_by_merges_when_support_to_model{scaled<coord_t>(1.0)};
    // Tree Support Minimum Height To Model
    // How tall a branch has to be if it is placed on the model. Prevents small blobs of support. This setting is ignored when a branch is supporting a support roof.
    // minimum: 0, maximum warning: 5
    coord_t support_tree_min_height_to_model{scaled<coord_t>(1.0)};
    // Tree Support Inital Layer Diameter
    // Diameter every branch tries to achieve when reaching the buildplate. Improves bed adhesion.
    // minimum: 0, maximum warning: 20
    coord_t support_tree_bp_diameter{scaled<coord_t>(7.5)};
    // Tree Support Branch density
    // Adjusts the density of the support structure used to generate the tips of the branches. A higher value results in better overhangs,
    // but the supports are harder to remove. Use Support Roof for very high values or ensure support density is similarly high at the top.
    // ->
    // Adjusts the density of the support structure used to generate the tips of the branches.
    // A higher value results in better overhangs but the supports are harder to remove, thus it is recommended to enable top support interfaces
    // instead of a high branch density value if dense interfaces are needed.
    // 5%-35%
    double support_tree_top_rate{15.};
    // Tree Support Tip diameter
    // The diameter of the top of the tip of the branches of tree support.
    // minimum: min_wall_line_width, minimum warning: min_wall_line_width+0.05, maximum_value: support_tree_branch_diameter, value: support_line_width
    coord_t support_tree_tip_diameter{scaled<coord_t>(0.4)};

    // Support Interface Priority
    // How support interface and support will interact when they overlap. Currently only implemented for support roof.
    //enum                           support_interface_priority { support_lines_overwrite_interface_area };
};

/*!
 * \brief This struct contains settings used in the tree support. Thanks to this most functions do not need to know of meshes etc. Also makes the code shorter.
 */
struct TreeSupportSettings
{
public:
    TreeSupportSettings() =
        default; // required for the definition of the config variable in the TreeSupportGenerator class.
    explicit TreeSupportSettings(const TreeSupportMeshGroupSettings &mesh_group_settings,
                                 const SlicingParameters &slicing_params, const PrintObject *print_object = nullptr);

    // some static variables dependent on other meshes that are not currently processed.
    // Has to be static because TreeSupportConfig will be used in TreeModelVolumes as this reduces redundancy.
    inline static bool soluble = false;
    /*!
     * \brief Width of a single line of support.
     */
    coord_t support_line_width;
    /*!
     * \brief Height of a single layer
     */
    coord_t layer_height;
    /*!
     * \brief Radius of a branch when it has left the tip.
     */
    coord_t branch_radius;
    /*!
     * \brief smallest allowed radius, required to ensure that even at DTT 0 every circle will still be printed
     */
    coord_t min_radius;
    /*!
     * \brief How far an influence area may move outward every layer at most.
     */
    coord_t maximum_move_distance;
    /*!
     * \brief How far every influence area will move outward every layer if possible.
     */
    coord_t maximum_move_distance_slow;
    /*!
     * \brief Amount of bottom layers. 0 if disabled.
     */
    size_t support_bottom_layers;
    /*!
     * \brief Amount of effectiveDTT increases are required to reach branch radius.
     */
    size_t tip_layers;
    /*!
     * \brief How much a branch radius increases with each layer to guarantee the prescribed tree widening.
     */
    double branch_radius_increase_per_layer;
    /*!
     * \brief How much a branch resting on the model may grow in radius by merging with branches that can reach the buildplate.
     */
    coord_t max_to_model_radius_increase;
    /*!
     * \brief If smaller (in layers) than that, all branches to model will be deleted
     */
    size_t min_dtt_to_model;
    /*!
     * \brief Increase radius in the resulting drawn branches, even if the avoidance does not allow it. Will be cut later to still fit.
     */
    coord_t increase_radius_until_radius;
    /*!
     * \brief Same as increase_radius_until_radius, but contains the DTT at which the radius will be reached.
     */
    size_t increase_radius_until_layer;
    /*!
     * \brief True if the branches may connect to the model.
     */
    bool support_rests_on_model;
    /*!
     * \brief How far should support be from the model.
     */
    coord_t xy_distance;
    /*!
     * \brief A minimum radius a tree trunk should expand to at the buildplate if possible.
     */
    coord_t bp_radius;
    /*!
     * \brief The layer index at which an increase in radius may be required to reach the bp_radius.
     */
    LayerIndex layer_start_bp_radius;
    /*!
     * \brief How much one is allowed to increase the tree branch radius close to print bed to reach the required bp_radius at layer 0.
     * Note that this radius increase will not happen in the tip, to ensure the tip is structurally sound.
     */
    double bp_radius_increase_per_layer;
    /*!
     * \brief minimum xy_distance. Only relevant when Z overrides XY, otherwise equal to xy_distance-
     */
    coord_t xy_min_distance;
    /*!
     * \brief Amount of layers distance required the top of the support to the model
     */
    size_t z_distance_top_layers;
    /*!
     * \brief Amount of layers distance required from the top of the model to the bottom of a support structure.
     */
    size_t z_distance_bottom_layers;
    /*!
     * \brief User specified angles for the support infill.
     */
    //        std::vector<double> support_infill_angles;
    /*!
     * \brief User specified angles for the support roof infill.
     */
    std::vector<double> support_roof_angles;
    /*!
     * \brief Pattern used in the support roof. May contain non relevant data if support roof is disabled.
     */
    SupportMaterialInterfacePattern roof_pattern;
    /*!
     * \brief Pattern used in the support infill.
     */
    SupportMaterialPattern support_pattern;
    /*!
     * \brief Line width of the support roof.
     */
    coord_t support_roof_line_width;
    /*!
     * \brief Distance between support infill lines.
     */
    coord_t support_line_spacing;
    /*!
     * \brief Offset applied to the support floor area.
     */
    coord_t support_bottom_offset;
    /*
     * \brief Amount of walls the support area will have.
     */
    int support_wall_count;
    /*
     * \brief Maximum allowed deviation when simplifying.
     */
    coord_t resolution;
    /*
     * \brief Distance between the lines of the roof.
     */
    coord_t support_roof_line_distance;
    /*
     * \brief How overlaps of an interface area with a support area should be handled.
     */
    InterfacePreference interface_preference;

    /*
     * \brief The infill class wants a settings object. This one will be the correct one for all settings it uses.
     */
    TreeSupportMeshGroupSettings settings;

    /*
     * \brief Minimum thickness of any model features.
     */
    coord_t min_feature_size;

    // Extra raft layers below the object.
    std::vector<coordf_t> raft_layers;

    // Print z of every support layer above the raft. Built by the object's own layer generator at a
    // fixed layer height, so it carries the whole-millimetre snapping object layers get. With no
    // variable layer height profile this is the object's layer stack; with one, the object leaves
    // this grid and the supports stay on it, printing as they would with the profile switched off.
    std::vector<coordf_t> support_layer_zs;

    // Once the two stacks differ, every input the engine reads per object layer has to be carried
    // across by height. object_layer_of[support layer] is the object layer covering that height,
    // support_layer_of[object layer] is the support layer at that object layer's height. Both are
    // the identity when the layer height is fixed.
    std::vector<size_t> object_layer_of;
    std::vector<size_t> support_layer_of;

    // Print z of the object surface resting on each support layer: the bottom of the object layer
    // covering that height. Contacts anchor to this instead of the support grid, so the air gap
    // stays the requested distance instead of picking up whatever the two stacks happen to differ
    // by. At a fixed layer height every entry equals the grid layer below, so anchoring here moves
    // nothing.
    std::vector<coordf_t> object_bottom_z_of;

    // True when the object's layers sit exactly on the support grid, i.e. no variable layer
    // height profile is in effect. Every cross-walk remap is an identity copy then and is skipped
    // outright, so a fixed-height slice structurally cannot be altered by the remapping code.
    bool grid_matches_object = false;

public:
    bool operator==(const TreeSupportSettings &other) const
    {
        return branch_radius == other.branch_radius && tip_layers == other.tip_layers &&
               branch_radius_increase_per_layer == other.branch_radius_increase_per_layer &&
               layer_start_bp_radius == other.layer_start_bp_radius && bp_radius == other.bp_radius &&
               // as a recalculation of the collision areas is required to set a new min_radius.
               bp_radius_increase_per_layer == other.bp_radius_increase_per_layer && min_radius == other.min_radius &&
               xy_min_distance == other.xy_min_distance &&
               xy_distance - xy_min_distance ==
                   other.xy_distance -
                       other.xy_min_distance && // if the delta of xy_min_distance and xy_distance is different the collision areas have to be recalculated.
               support_rests_on_model == other.support_rests_on_model &&
               increase_radius_until_layer == other.increase_radius_until_layer &&
               min_dtt_to_model == other.min_dtt_to_model &&
               max_to_model_radius_increase == other.max_to_model_radius_increase &&
               maximum_move_distance == other.maximum_move_distance &&
               maximum_move_distance_slow == other.maximum_move_distance_slow &&
               z_distance_bottom_layers == other.z_distance_bottom_layers &&
               support_line_width == other.support_line_width && support_line_spacing == other.support_line_spacing &&
               support_roof_line_width ==
                   other.support_roof_line_width && // can not be set on a per-mesh basis currently, so code to enable processing different roof line width in the same iteration seems useless.
               support_bottom_offset == other.support_bottom_offset &&
               support_wall_count == other.support_wall_count && support_pattern == other.support_pattern &&
               roof_pattern ==
                   other.roof_pattern && // can not be set on a per-mesh basis currently, so code to enable processing different roof patterns in the same iteration seems useless.
               support_roof_angles == other.support_roof_angles &&
               //support_infill_angles == other.support_infill_angles &&
               increase_radius_until_radius == other.increase_radius_until_radius &&
               support_bottom_layers == other.support_bottom_layers && layer_height == other.layer_height &&
               z_distance_top_layers == other.z_distance_top_layers &&
               resolution == other.resolution && // Infill generation depends on deviation and resolution.
               support_roof_line_distance == other.support_roof_line_distance &&
               interface_preference == other.interface_preference &&
               min_feature_size ==
                   other.min_feature_size // interface_preference should be identical to ensure the tree will correctly interact with the roof.
        // The infill class now wants the settings object and reads a lot of settings, and as the infill class is used to calculate support roof lines for interface-preference. Not all of these may be required to be identical, but as I am not sure, better safe than sorry
#if 0
                && (interface_preference == InterfacePreference::InterfaceAreaOverwritesSupport || interface_preference == InterfacePreference::SupportAreaOverwritesInterface
                // Perimeter generator parameters
                   || 
                        (settings.get<bool>("fill_outline_gaps") == other.settings.get<bool>("fill_outline_gaps") && 
                         settings.get<coord_t>("min_bead_width") == other.settings.get<coord_t>("min_bead_width") && 
                         settings.get<double>("wall_transition_angle") == other.settings.get<double>("wall_transition_angle") && 
                         settings.get<coord_t>("wall_transition_length") == other.settings.get<coord_t>("wall_transition_length") && 
                         settings.get<Ratio>("wall_split_middle_threshold") == other.settings.get<Ratio>("wall_split_middle_threshold") && 
                         settings.get<Ratio>("wall_add_middle_threshold") == other.settings.get<Ratio>("wall_add_middle_threshold") && 
                         settings.get<int>("wall_distribution_count") == other.settings.get<int>("wall_distribution_count") && 
                         settings.get<coord_t>("wall_transition_filter_distance") == other.settings.get<coord_t>("wall_transition_filter_distance") && 
                         settings.get<coord_t>("wall_transition_filter_deviation") == other.settings.get<coord_t>("wall_transition_filter_deviation") && 
                         settings.get<coord_t>("wall_line_width_x") == other.settings.get<coord_t>("wall_line_width_x") && 
                         settings.get<int>("meshfix_maximum_extrusion_area_deviation") == other.settings.get<int>("meshfix_maximum_extrusion_area_deviation"))
                    )
#endif
               && raft_layers == other.raft_layers && support_layer_zs == other.support_layer_zs;
    }

    /*!
     * \brief Get the Radius part will have based on numeric values.
     * \param distance_to_top[in] The effective distance_to_top of the element
     * \param elephant_foot_increases[in] The elephant_foot_increases of the element.
     * \return The radius an element with these attributes would have.
     */
    [[nodiscard]] inline coord_t getRadius(size_t distance_to_top, const double elephant_foot_increases = 0) const
    {
        return (distance_to_top <= tip_layers ? min_radius + (branch_radius - min_radius) * distance_to_top / tip_layers
                                              :                                              // tip
                    branch_radius +                                                          // base
                        (distance_to_top - tip_layers) * branch_radius_increase_per_layer) + // gradual increase
               elephant_foot_increases *
                   (std::max(bp_radius_increase_per_layer - branch_radius_increase_per_layer, 0.0));
    }

    /*!
     * \brief Get the Radius an element should at least have at a given layer.
     * \param layer_idx[in] The layer.
     * \return The radius every element should aim to achieve.
     */
    [[nodiscard]] inline coord_t recommendedMinRadius(LayerIndex layer_idx) const
    {
        double num_layers_widened = layer_start_bp_radius - layer_idx;
        return num_layers_widened > 0 ? branch_radius + num_layers_widened * bp_radius_increase_per_layer : 0;
    }

#if 0
    /*!
     * \brief Return on which z in microns the layer will be printed. Used only for support infill line generation.
     * \param layer_idx[in] The layer.
     * \return The radius every element should aim to achieve.
     */
    [[nodiscard]] inline coord_t getActualZ(LayerIndex layer_idx)
    {
        return layer_idx < coord_t(known_z.size()) ? known_z[layer_idx] : (layer_idx - known_z.size()) * layer_height + known_z.size() ? known_z.back() : 0;
    }

    /*!
     * \brief Set the z every Layer is printed at. Required for getActualZ to work
     * \param z[in] The z every LayerIndex is printed. Vector is used as a map<LayerIndex,coord_t> with the index of each element being the corresponding LayerIndex
     * \return The radius every element should aim to achieve.
     */
    void setActualZ(std::vector<coord_t>& z)
    {
        known_z = z;
    }
#endif

private:
    //    std::vector<coord_t> known_z;
};

static constexpr const bool polygons_strictly_simple = false;

static constexpr const auto tiny_area_threshold = sqr(scaled<double>(0.001));

void tree_supports_show_error(std::string_view message, bool critical);

inline double layer_z(const SlicingParameters &slicing_params, const TreeSupportSettings &config,
                      const size_t layer_idx)
{
    if (layer_idx < config.raft_layers.size())
        return config.raft_layers[layer_idx];
    const size_t idx = layer_idx - config.raft_layers.size();
    if (idx < config.support_layer_zs.size())
        return config.support_layer_zs[idx];
    if (config.support_layer_zs.empty())
        return slicing_params.object_print_z_min + slicing_params.first_object_layer_height +
               double(idx) * slicing_params.layer_height;
    // Above the top of the object. Carry the grid on at the nominal height so callers that probe
    // past the last layer still get a monotonic answer.
    return config.support_layer_zs.back() +
           double(idx + 1 - config.support_layer_zs.size()) * slicing_params.layer_height;
}
// Lowest support layer whose print z reaches z. Above the grid top both helpers extrapolate from
// the last grid layer at the nominal height, exactly as layer_z does, so the three functions stay
// mutual inverses on and above a populated grid; with an empty grid all three fall back to the
// same nominal ladder.
inline LayerIndex layer_idx_ceil(const SlicingParameters &slicing_params, const TreeSupportSettings &config,
                                 const double z)
{
    const auto &layers = config.support_layer_zs;
    const auto it = std::lower_bound(layers.begin(), layers.end(), z - EPSILON);
    if (it != layers.end())
        return LayerIndex(config.raft_layers.size()) + LayerIndex(it - layers.begin());
    if (layers.empty())
        return LayerIndex(config.raft_layers.size()) +
               std::max<LayerIndex>(0, LayerIndex(std::ceil((z - slicing_params.object_print_z_min -
                                                             slicing_params.first_object_layer_height) /
                                                            slicing_params.layer_height)));
    return LayerIndex(config.raft_layers.size()) + LayerIndex(layers.size()) - 1 +
           std::max<LayerIndex>(0, LayerIndex(std::ceil((z - EPSILON - layers.back()) / slicing_params.layer_height)));
}
// Highest support layer whose print z does not pass z.
inline LayerIndex layer_idx_floor(const SlicingParameters &slicing_params, const TreeSupportSettings &config,
                                  const double z)
{
    const auto &layers = config.support_layer_zs;
    const auto it = std::upper_bound(layers.begin(), layers.end(), z + EPSILON);
    if (it != layers.end())
        return LayerIndex(config.raft_layers.size()) + std::max<LayerIndex>(0, LayerIndex(it - layers.begin()) - 1);
    if (layers.empty())
        return LayerIndex(config.raft_layers.size()) +
               std::max<LayerIndex>(0, LayerIndex(std::floor((z - slicing_params.object_print_z_min -
                                                              slicing_params.first_object_layer_height) /
                                                             slicing_params.layer_height)));
    return LayerIndex(config.raft_layers.size()) + LayerIndex(layers.size()) - 1 +
           std::max<LayerIndex>(0, LayerIndex(std::floor((z + EPSILON - layers.back()) / slicing_params.layer_height)));
}

inline SupportGeneratorLayer &layer_initialize(SupportGeneratorLayer &layer_new,
                                               const SlicingParameters &slicing_params,
                                               const TreeSupportSettings &config, const size_t layer_idx)
{
    layer_new.print_z = layer_z(slicing_params, config, layer_idx);
    layer_new.bottom_z = layer_idx > 0 ? layer_z(slicing_params, config, layer_idx - 1) : 0;
    layer_new.height = layer_new.print_z - layer_new.bottom_z;
    return layer_new;
}

// Using the std::deque as an allocator.
inline SupportGeneratorLayer &layer_allocate_unguarded(SupportGeneratorLayerStorage &layer_storage,
                                                       SupporLayerType layer_type,
                                                       const SlicingParameters &slicing_params,
                                                       const TreeSupportSettings &config, size_t layer_idx)
{
    SupportGeneratorLayer &layer = layer_storage.allocate_unguarded(layer_type);
    return layer_initialize(layer, slicing_params, config, layer_idx);
}

inline SupportGeneratorLayer &layer_allocate(SupportGeneratorLayerStorage &layer_storage, SupporLayerType layer_type,
                                             const SlicingParameters &slicing_params, const TreeSupportSettings &config,
                                             size_t layer_idx)
{
    SupportGeneratorLayer &layer = layer_storage.allocate(layer_type);
    return layer_initialize(layer, slicing_params, config, layer_idx);
}

// Contact anchoring telemetry. One tally per tree support group, shared by every copy of the
// InterfacePlacer so parallel tip placement and the raft contact land in the same counts.
struct InterfaceAnchorStats
{
    std::atomic<size_t> anchor_object{0};          // layers anchored to an object surface
    std::atomic<size_t> anchor_grid_fallback{0};   // layers anchored to the grid: no object table or read off its end
    std::atomic<size_t> anchor_raft{0};            // raft contacts, grid-anchored by design
    std::atomic<size_t> height_clamped_floor{0};   // interface heights raised to the minimum
    std::atomic<size_t> height_clamped_ceiling{0}; // interface heights cut to the ceiling
    std::atomic<int64_t> offset_up_max_um{0};      // farthest an anchor sat above its grid layer, micrometers
    std::atomic<int64_t> offset_down_max_um{0};    // farthest an anchor sat below its grid layer, micrometers

    void note_offset(const double offset_mm)
    {
        std::atomic<int64_t> &slot = offset_mm > 0. ? offset_up_max_um : offset_down_max_um;
        const auto um = int64_t(std::abs(offset_mm) * 1000. + 0.5);
        int64_t cur = slot.load(std::memory_order_relaxed);
        while (um > cur && !slot.compare_exchange_weak(cur, um, std::memory_order_relaxed))
        {
        }
    }
};

// Used by generate_initial_areas() in parallel by multiple layers.
class InterfacePlacer
{
public:
    InterfacePlacer(const SlicingParameters &slicing_parameters, const SupportParameters &support_parameters,
                    const TreeSupportSettings &config, SupportGeneratorLayerStorage &layer_storage,
                    SupportGeneratorLayersPtr &top_contacts, SupportGeneratorLayersPtr &top_interfaces,
                    SupportGeneratorLayersPtr &top_base_interfaces)
        : slicing_parameters(slicing_parameters)
        , support_parameters(support_parameters)
        , config(config)
        , anchor_stats(std::make_shared<InterfaceAnchorStats>())
        , layer_storage(layer_storage)
        , top_contacts(top_contacts)
        , top_interfaces(top_interfaces)
        , top_base_interfaces(top_base_interfaces)
    {
    }
    InterfacePlacer(const InterfacePlacer &rhs)
        : slicing_parameters(rhs.slicing_parameters)
        , support_parameters(rhs.support_parameters)
        , config(rhs.config)
        , anchor_stats(rhs.anchor_stats)
        , layer_storage(rhs.layer_storage)
        , top_contacts(rhs.top_contacts)
        , top_interfaces(rhs.top_interfaces)
        , top_base_interfaces(rhs.top_base_interfaces)
    {
    }

    const SlicingParameters &slicing_parameters;
    const SupportParameters &support_parameters;
    const TreeSupportSettings &config;
    // Shared across copies so one report covers the whole group.
    const std::shared_ptr<InterfaceAnchorStats> anchor_stats;
    SupportGeneratorLayersPtr &top_contacts_mutable() { return this->top_contacts; }

    // Print z of the object surface resting on the support layer at layer_idx: the bottom of the
    // object layer covering that height. The interface stack is positioned against this so the
    // requested air gap is what actually gets printed no matter how the support grid and the
    // object's layers line up. Off the table it falls back to the grid layer below, which is the
    // same height when the two stacks agree.
    coordf_t supported_object_z(const size_t layer_idx) const
    {
        if (const size_t num_raft = config.raft_layers.size();
            layer_idx >= num_raft && layer_idx - num_raft < config.object_bottom_z_of.size())
        {
            anchor_stats->anchor_object.fetch_add(1, std::memory_order_relaxed);
            return config.object_bottom_z_of[layer_idx - num_raft];
        }
        anchor_stats->anchor_grid_fallback.fetch_add(1, std::memory_order_relaxed);
        return layer_z(slicing_parameters, config, layer_idx > 0 ? layer_idx - 1 : 0);
    }

public:
    // Insert the contact layer and some of the inteface and base interface layers below.
    void add_roofs(std::vector<Polygons> &&new_roofs, const size_t insert_layer_idx)
    {
        if (!new_roofs.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
            for (size_t idx = 0; idx < new_roofs.size(); ++idx)
                if (!new_roofs[idx].empty())
                    add_roof_unguarded(std::move(new_roofs[idx]), insert_layer_idx - idx, idx);
        }
    }

    void add_roof(Polygons &&new_roof, const size_t insert_layer_idx, const size_t dtt_tip)
    {
        std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
        add_roof_unguarded(std::move(new_roof), insert_layer_idx, dtt_tip);
    }

    // called by sample_overhang_area()
    void add_roof_build_plate(Polygons &&overhang_areas, size_t dtt_roof)
    {
        std::lock_guard<std::mutex> lock(m_mutex_layer_storage);
        this->add_roof_unguarded(std::move(overhang_areas), 0,
                                 std::min(dtt_roof, this->support_parameters.num_top_interface_layers));
    }

    // Returns the adjusted height for interface layers to achieve the user's desired gap.
    // Gap is created by REDUCING interface layer heights. The reduction is distributed
    // across all interface layers (including TopContact).
    coordf_t get_interface_layer_height() const
    {
        const size_t num_interface = support_parameters.num_top_interface_layers +
                                     (support_parameters.has_top_contacts ? 1 : 0);
        if (num_interface == 0)
            return slicing_parameters.layer_height;

        const coordf_t gap = slicing_parameters.gap_support_object;
        const coordf_t layer_height = slicing_parameters.layer_height;
        const coordf_t nozzle_diam = support_parameters.support_material_interface_flow.nozzle_diameter();
        const coordf_t min_height = nozzle_diam * 0.25;
        const coordf_t max_height = nozzle_diam * 0.75;

        // For full layer gap or larger, use standard layer height (gap achieved by positioning)
        if (gap >= layer_height - EPSILON)
            return layer_height;

        // No gap - use standard layer height
        if (gap <= EPSILON)
            return layer_height;

        // Try single layer reduction first (only TopContact reduced)
        const coordf_t single_layer_height = layer_height - gap;
        if (single_layer_height >= min_height)
            return layer_height; // Other interface layers stay at full height

        // Single layer too thin - distribute across all interface layers
        const coordf_t distributed_height = layer_height - gap / num_interface;
        return std::clamp(distributed_height, min_height, max_height);
    }

    // Get the height specifically for TopContact layer (may differ from other interface layers)
    coordf_t get_top_contact_height() const
    {
        const size_t num_interface = support_parameters.num_top_interface_layers +
                                     (support_parameters.has_top_contacts ? 1 : 0);
        if (num_interface == 0)
            return slicing_parameters.layer_height;

        const coordf_t gap = slicing_parameters.gap_support_object;
        const coordf_t layer_height = slicing_parameters.layer_height;
        const coordf_t nozzle_diam = support_parameters.support_material_interface_flow.nozzle_diameter();
        const coordf_t min_height = nozzle_diam * 0.25;
        const coordf_t max_height = nozzle_diam * 0.75;

        // For full layer gap or larger, use standard layer height
        if (gap >= layer_height - EPSILON)
            return layer_height;

        // No gap - use standard layer height
        if (gap <= EPSILON)
            return layer_height;

        // Try single layer reduction first
        const coordf_t single_layer_height = layer_height - gap;
        if (single_layer_height >= min_height)
            return single_layer_height; // TopContact is reduced

        // Distribute across all interface layers
        const coordf_t distributed_height = layer_height - gap / num_interface;
        return std::clamp(distributed_height, min_height, max_height);
    }

    void add_roof_unguarded(Polygons &&new_roofs, const size_t insert_layer_idx, const size_t dtt_roof)
    {
        assert(support_parameters.has_top_contacts);
        assert(dtt_roof <= support_parameters.num_top_interface_layers);
        SupportGeneratorLayersPtr &layers = dtt_roof == 0 ? this->top_contacts
                                            : dtt_roof <= support_parameters.num_top_interface_layers_only()
                                                ? this->top_interfaces
                                                : this->top_base_interfaces;
        SupportGeneratorLayer *&l = layers[insert_layer_idx];
        if (l == nullptr)
        {
            l = &layer_allocate_unguarded(layer_storage,
                                          dtt_roof == 0 ? SupporLayerType::TopContact : SupporLayerType::TopInterface,
                                          slicing_parameters, config, insert_layer_idx);

            // The layer was allocated with standard layer_height based on insert_layer_idx.
            // For custom gap, we need to reposition the interface stack to achieve the exact gap.
            //
            // Strategy: Calculate from the OBJECT position down
            // - All layers in the stack use the SAME object_z (from TopContact's perspective)
            // - TopContact print_z = object_z - gap
            // - Interface layers stack down from TopContact with equal heights
            const size_t num_interface = support_parameters.num_top_interface_layers +
                                         (support_parameters.has_top_contacts ? 1 : 0);
            const coordf_t gap = slicing_parameters.gap_support_object;
            const coordf_t layer_height = slicing_parameters.layer_height;

            // Anchor the stack to the object surface and adjust interface layer heights so the
            // requested gap, including a gap of zero, is what actually prints.
            if (num_interface > 0)
            {
                // Calculate TopContact's layer_idx by adding dtt_roof offset
                // dtt_roof=0 for TopContact, dtt_roof=1 for layer below, etc.
                const size_t top_contact_layer_idx = insert_layer_idx + dtt_roof;

                // The overhang this stack holds up was carried onto the support layer
                // z_distance_top_layers + 1 above the contact, and the surface it rests against is
                // the bottom of the object layer covering that height, read off the object rather
                // than the support grid so the gap below is exactly the requested distance. Raft
                // contacts support the raft-to-object seam, not an overhang, and stay on the grid.
                const size_t z_dist_layers = config.z_distance_top_layers;
                const size_t overhang_layer_idx = top_contact_layer_idx + z_dist_layers + 1;
                const coordf_t grid_anchor_z = layer_z(slicing_parameters, config, overhang_layer_idx - 1);
                const bool raft_contact = top_contact_layer_idx < config.raft_layers.size();
                if (raft_contact)
                    anchor_stats->anchor_raft.fetch_add(1, std::memory_order_relaxed);
                const coordf_t object_z = raft_contact ? grid_anchor_z : this->supported_object_z(overhang_layer_idx);

                // How far the real surface sits off the grid layer it coincides with at a fixed
                // layer height. The stack hangs from the object but has to meet the support body,
                // which is on the grid, so the offset is spread over the stack instead of shifting
                // its bottom off the body and opening a void. Zero whenever the two grids agree,
                // which is what leaves a fixed layer height untouched. Negative when the profile
                // ran thicker than nominal and the surface dropped below its grid slot; the
                // unclamped formula stays valid then, and profiles deep enough to drive the
                // heights below the printable minimum are refused up front in Print::validate().
                const coordf_t anchor_offset = object_z - grid_anchor_z;
                anchor_stats->note_offset(anchor_offset);

                // Reposition when a gap is requested or the surface left the grid. With no gap
                // the contact still has to move up to meet the object or an unrequested gap
                // prints; when both are zero the allocated grid position is already exact.
                if (gap > EPSILON || std::abs(anchor_offset) > EPSILON)
                {
                    const coordf_t nozzle_diam = support_parameters.support_material_interface_flow.nozzle_diameter();
                    const coordf_t min_height = nozzle_diam * 0.25;
                    const coordf_t max_height = nozzle_diam * 0.75;

                    // TopContact should be at object_z - gap
                    const coordf_t top_contact_print_z = object_z - gap;

                    // Calculate interface height based on GAP REMAINDER only
                    // Full layers of gap are handled by layer positioning (TopContact moves down),
                    // not by reducing interface layer heights.
                    // This ensures gap = N * layer_height results in normal layer heights.
                    const coordf_t gap_remainder = std::fmod(gap, layer_height);
                    const coordf_t height_reduction = gap_remainder / num_interface;
                    coordf_t interface_height = layer_height - height_reduction + anchor_offset / num_interface;
                    // Safety net only. The ceiling admits the anchor correction, which is under
                    // one support layer spread over the stack; clipping it would separate the
                    // stack from the body below.
                    const coordf_t span_ceiling = layer_height * (1. + 1. / coordf_t(num_interface));
                    const coordf_t height_ceiling = std::max(max_height, span_ceiling);
                    if (interface_height < min_height)
                        anchor_stats->height_clamped_floor.fetch_add(1, std::memory_order_relaxed);
                    else if (interface_height > height_ceiling)
                        anchor_stats->height_clamped_ceiling.fetch_add(1, std::memory_order_relaxed);
                    interface_height = std::clamp(interface_height, min_height, height_ceiling);

                    // Calculate this layer's position
                    // dtt_roof=0 is TopContact (top), dtt_roof=num_interface-1 is bottom
                    const size_t position_from_top = dtt_roof; // 0 for TopContact

                    // Stack layers down from TopContact
                    coordf_t this_print_z = top_contact_print_z - (position_from_top * interface_height);
                    coordf_t this_height = interface_height;
                    coordf_t this_bottom_z = this_print_z - this_height;

                    l->bottom_z = this_bottom_z;
                    l->print_z = this_print_z;
                    l->height = this_height;
                }
            }
        }
        // will be unioned in finalize_interface_and_support_areas()
        append(l->polygons, std::move(new_roofs));
    }

private:
    // Outputs
    SupportGeneratorLayerStorage &layer_storage;
    SupportGeneratorLayersPtr &top_contacts;
    SupportGeneratorLayersPtr &top_interfaces;
    SupportGeneratorLayersPtr &top_base_interfaces;

    // Mutexes, guards
    std::mutex m_mutex_layer_storage;
};

} // namespace FFFTreeSupport

} // namespace Slic3r

#endif // slic3r_TreeSupportCommon_hpp
