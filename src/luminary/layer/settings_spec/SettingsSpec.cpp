///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
///|/ Provisional module: the settings specification belongs beside config by subject, and sits in layer because it reads the key invalidation lists from Print.hpp.
///|/
#include "SettingsSpec.hpp"

#include "luminary/core/I18N.hpp"
#include "luminary/presets/preset/Preset.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace Luminary
{

// The table: one row per settable key, in the build order of the Settings pages; a row that the
// sidebar also shows carries its sidebar page, group title and label where they differ, and its
// sidebar order. Born from the registry dumps of both surfaces.
const std::vector<SettingRow> &setting_rows()
{
    static const std::vector<SettingRow> rows = {
        // Print
        {"layer_height", SettingPresetPrint, L("Layers and perimeters"), L("Layer height"), 0, nullptr, "layers",
         nullptr, 0, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#layer-height"},
        {"first_layer_height", SettingPresetPrint, L("Layers and perimeters"), L("Layer height"), 1, nullptr, "layers",
         nullptr, 1, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#first-layer-height"},
        {"color_mixing_base_layers", SettingPresetPrint, L("Layers and perimeters"), L("Layer height"), 2, nullptr,
         "layers", nullptr, 2, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#color-mixing-base-layers"},
        {"color_mixing_base_extruder", SettingPresetPrint, L("Layers and perimeters"), L("Layer height"), 3, nullptr,
         "layers", nullptr, 3, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#color-mixing-base-extruder"},
        {"perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Vertical shells"), 4, nullptr, "layers",
         nullptr, 4, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#perimeters"},
        {"spiral_vase", SettingPresetPrint, L("Layers and perimeters"), L("Vertical shells"), 5, nullptr, "layers",
         nullptr, 5, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#spiral-vase"},
        {"top_solid_layers", SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), 7,
         L("Solid layers"), "layers", L("Top solid layers"), 6, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#solid-layers-top-bottom"},
        {"bottom_solid_layers", SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), 8,
         L("Solid layers"), "layers", L("Bottom solid layers"), 7, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#solid-layers-top-bottom"},
        {"top_solid_min_thickness", SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), 9,
         L("Minimum shell thickness"), "layers", L("Top min thickness"), 8, SettingWidget::Default, nullptr, false,
         false, nullptr, "top_min_thickness"},
        {"bottom_solid_min_thickness", SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), 10,
         L("Minimum shell thickness"), "layers", L("Bottom min thickness"), 9, SettingWidget::Default, nullptr, false,
         false, nullptr, "bottom_min_thickness"},
        {"serpentine_enabled", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 12, nullptr, "layers",
         nullptr, 10, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine",
         "no_spiral_vase"},
        {"serpentine_extrusion_width", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 13, nullptr,
         "layers", L("Extrusion width"), 11, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-width", "serpentine"},
        {"serpentine_overlap", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 14, nullptr, "layers",
         L("Overlap"), 12, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-overlap", "serpentine"},
        {"serpentine_max_bead", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 15, nullptr, "layers",
         L("Maximum bead width"), 13, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-max-bead", "serpentine"},
        {"serpentine_relaxed", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 16, nullptr, "layers",
         nullptr, 14, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine-relaxed",
         "serpentine"},
        {"serpentine_spacing", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 17, nullptr, "layers",
         nullptr, 15, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine-spacing",
         "serpentine_relaxed"},
        {"serpentine_outer_loop", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 18, nullptr,
         "layers", nullptr, 16, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-outer-loop", "serpentine"},
        {"serpentine_limit_depth", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 19, nullptr,
         "layers", L("Limit depth"), 17, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-limit-depth", "serpentine_strict"},
        {"serpentine_depth", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 20, nullptr, "layers",
         L("Depth"), 18, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine-depth",
         "serpentine_depth"},
        {"serpentine_solid_surfaces", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 21, nullptr,
         "layers", nullptr, 19, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#serpentine-solid-surfaces", "serpentine"},
        {"serpentine_ridges", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 22, nullptr, "layers",
         L("Ridges"), 20, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine-ridges",
         "serpentine_ridges"},
        {"serpentine_aim", SettingPresetPrint, L("Layers and perimeters"), L("Serpentine"), 23, nullptr, "layers",
         L("Aim"), 21, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#serpentine-aim",
         "serpentine_depth"},
        {"interlock_perimeters_enabled", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 25, nullptr,
         "layers", L("Enable interlock perimeters"), 22, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-perimeters", "no_spiral_vase"},
        {"interlock_perimeter_count", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 26, nullptr,
         "layers", L("Interlock perimeter count"), 23, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-count", "interlock"},
        {"interlock_regular_perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 27, nullptr,
         "layers", nullptr, 24, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-regular-perimeters", "interlock"},
        {"interlock_solid_layers_top", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 28,
         L("Solid layers"), "layers", L("Solid layers above"), 25, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-solid-layers", "interlock"},
        {"interlock_solid_layers_bottom", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 29,
         L("Solid layers"), "layers", L("Solid layers below"), 26, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-solid-layers", "interlock"},
        {"interlock_perimeter_overlap", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 30, nullptr,
         "layers", L("Interlock perimeter overlap"), 27, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-overlap", "interlock"},
        {"interlock_flow_detection", SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), 31, nullptr,
         "layers", L("Interlock flow detection"), 28, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#interlock-flow-detection", "interlock"},
        {"extra_perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 33, nullptr, "layers",
         nullptr, 29, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#extra-perimeters-if-needed", "perimeters"},
        {"extra_perimeters_on_overhangs", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 34, nullptr,
         "layers", L("Extra perimeters on overhangs"), 30, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#extra-perimeters-on-overhangs", "perimeters"},
        {"ensure_vertical_shell_thickness", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 35, nullptr,
         "layers", nullptr, 31, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#ensure-vertical-shell-thickness"},
        {"avoid_crossing_curled_overhangs", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 36, nullptr,
         "layers", L("Avoid crossing curled overhangs"), 32, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#avoid-crossing-curled-overhangs", "no_avoid_crossing_perimeters"},
        {"avoid_crossing_perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 37, nullptr,
         "layers", nullptr, 33, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#avoid-crossing-perimeters", "no_avoid_crossing_curled"},
        {"avoid_crossing_perimeters_max_detour", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 38,
         nullptr, "layers", L("Max detour length"), 34, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#avoid_crossing_perimeters_max_detour", "avoid_crossing_perimeters"},
        {"overhangs", SettingPresetPrint, L("Layers and perimeters"), L("Quality"), 39, nullptr, "layers", nullptr, 35,
         SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#detect-bridging-perimeters",
         "perimeters"},
        {"perimeter_generator", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 40, nullptr, "layers",
         nullptr, 36, SettingWidget::Default, nullptr, false, false, nullptr},
        {"seam_position", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 41, nullptr, "layers", nullptr,
         37, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#seam-position", "perimeters"},
        {"seam_type", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 42, nullptr, "layers", nullptr, 38,
         SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#seam-type", "perimeters"},
        {"seam_notch_width", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 43, nullptr, "layers",
         nullptr, 39, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#seam-notch-width",
         "seam_notch"},
        {"seam_notch_angle", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 44, nullptr, "layers",
         nullptr, 40, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#seam-notch-angle",
         "seam_notch"},
        {"seam_gap_distance", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 45, nullptr, "layers",
         L("Seam gap"), 41, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#seam-gap-distance"},
        {"staggered_inner_seams", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 46, nullptr, "layers",
         nullptr, 42, SettingWidget::Default, nullptr, false, false, "layers-and-perimeters_1748#staggered-inner-seams",
         "perimeters"},
        {"external_perimeters_first", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 47, nullptr,
         "layers", nullptr, 43, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748#external-perimeters-first", "perimeters"},
        {"scarf_seam_placement", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 48, nullptr, "layers",
         L("Scarf seam placement"), 44, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-joint-placement", "no_spiral_vase"},
        {"scarf_seam_only_on_smooth", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 49, nullptr,
         "layers", L("Only on smooth perimeters"), 45, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-joint-only-on-smooth-perimeters", "scarf_seam"},
        {"scarf_seam_start_height", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 50, nullptr,
         "layers", nullptr, 46, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-start-height", "scarf_seam"},
        {"scarf_seam_entire_loop", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 51, nullptr, "layers",
         L("Scarf entire loop"), 47, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-joint-around-entire-perimeter", "scarf_seam"},
        {"scarf_seam_length", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 52, nullptr, "layers",
         L("Scarf length"), 48, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-joint-length", "scarf_seam"},
        {"scarf_seam_max_segment_length", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 53, nullptr,
         "layers", L("Scarf max segment length"), 49, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#max-scarf-joint-segment-length", "scarf_seam"},
        {"scarf_seam_on_inner_perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Advanced"), 54, nullptr,
         "layers", L("Scarf on inner perimeters"), 50, SettingWidget::Default, nullptr, false, false,
         "seam-position_151069#scarf-joint-on-inner-perimeters", "scarf_seam"},
        {"top_surface_flow_reduction", SettingPresetPrint, L("Layers and perimeters"), L("Top surface flow"), 55,
         nullptr, "layers", nullptr, 51, SettingWidget::Default, nullptr, false, false, nullptr},
        {"top_surface_visibility_detection", SettingPresetPrint, L("Layers and perimeters"), L("Top surface flow"), 56,
         nullptr, "layers", nullptr, 52, SettingWidget::Default, nullptr, false, false, nullptr,
         "top_surface_flow_reduction"},
        {"fuzzy_skin_painted_perimeters", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 57, nullptr,
         "layers", L("Painted perimeters"), 53, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-painted-perimeters"},
        {"fuzzy_skin", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 58, nullptr, "layers",
         L("Fuzzy skin type"), 54, SettingWidget::Default, nullptr, false, false, "fuzzy-skin_246186/#fuzzy-skin-type"},
        {"fuzzy_skin_thickness", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 59, nullptr, "layers",
         nullptr, 55, SettingWidget::Default, nullptr, false, false, "fuzzy-skin_246186/#fuzzy-skin-thickness"},
        {"fuzzy_skin_point_dist", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 60, nullptr,
         "layers", nullptr, 56, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-point-distance"},
        {"fuzzy_skin_on_top", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 61, nullptr, "layers",
         L("Fuzzy skin on top"), 57, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-on-top"},
        {"fuzzy_skin_first_layer", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 62, nullptr,
         "layers", L("Fuzzy skin on first layer"), 58, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-on-bottom"},
        {"fuzzy_skin_visibility_detection", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 63,
         nullptr, "layers", nullptr, 59, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-visibility-detection"},
        {"fuzzy_skin_noise_type", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 64, nullptr,
         "layers", nullptr, 60, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-noise-type"},
        {"fuzzy_skin_mode", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 65, nullptr, "layers",
         nullptr, 61, SettingWidget::Default, nullptr, false, false, "fuzzy-skin_246186/#fuzzy-skin-mode"},
        {"fuzzy_skin_point_placement", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 66, nullptr,
         "layers", nullptr, 62, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-point-placement"},
        {"fuzzy_skin_scale", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 67, nullptr, "layers",
         L("Scale"), 63, SettingWidget::Default, nullptr, false, false, "fuzzy-skin_246186/#fuzzy-skin-scale",
         "fuzzy_structured_noise"},
        {"fuzzy_skin_octaves", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 68, nullptr, "layers",
         L("Octaves"), 64, SettingWidget::Default, nullptr, false, false, "fuzzy-skin_246186/#fuzzy-skin-octaves",
         "fuzzy_octaves"},
        {"fuzzy_skin_persistence", SettingPresetPrint, L("Layers and perimeters"), L("Fuzzy skin"), 69, nullptr,
         "layers", L("Persistence"), 65, SettingWidget::Default, nullptr, false, false,
         "fuzzy-skin_246186/#fuzzy-skin-persistence", "fuzzy_persistence"},
        {"top_one_perimeter_type", SettingPresetPrint, L("Layers and perimeters"), L("Single perimeter"), 70, nullptr,
         "layers", L("Top one perimeter type"), 66, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748/#top-one-perimeter-type"},
        {"only_one_perimeter_first_layer", SettingPresetPrint, L("Layers and perimeters"), L("Single perimeter"), 71,
         nullptr, "layers", nullptr, 67, SettingWidget::Default, nullptr, false, false,
         "layers-and-perimeters_1748/#only-one-perimeter-first-layer"},
        {"fill_density", SettingPresetPrint, L("Infill"), L("Infill"), 69, nullptr, "infill", L("Fill density"), 68,
         SettingWidget::Default, nullptr, false, false, "infill_42#fill-density"},
        {"fill_pattern", SettingPresetPrint, L("Infill"), L("Infill"), 70, nullptr, "infill", L("Fill pattern"), 69,
         SettingWidget::Default, nullptr, false, false, "infill_42#fill-pattern", "infill"},
        {"solid_fill_pattern", SettingPresetPrint, L("Infill"), L("Infill"), 71, nullptr, "infill", nullptr, 70,
         SettingWidget::Default, nullptr, false, false, "infill_42#solid-fill-pattern"},
        {"top_fill_pattern", SettingPresetPrint, L("Infill"), L("Infill"), 72, nullptr, "infill", L("Top fill pattern"),
         71, SettingWidget::Default, nullptr, false, false, "infill_42#top-fill-pattern", "solid_infill"},
        {"bottom_fill_pattern", SettingPresetPrint, L("Infill"), L("Infill"), 73, nullptr, "infill",
         L("Bottom fill pattern"), 72, SettingWidget::Default, nullptr, false, false, "infill_42#bottom-fill-pattern",
         "solid_infill"},
        {"infill_anchor", SettingPresetPrint, L("Infill"), L("Infill"), 74, nullptr, "infill",
         L("Infill anchor length"), 73, SettingWidget::Default, nullptr, false, false, "infill_42#fill-pattern",
         "infill_anchors"},
        {"infill_anchor_max", SettingPresetPrint, L("Infill"), L("Infill"), 75, nullptr, "infill",
         L("Infill anchor max length"), 74, SettingWidget::Default, nullptr, false, false, "infill_42#fill-pattern",
         "infill"},
        {"ironing", SettingPresetPrint, L("Infill"), L("Ironing"), 76, nullptr, "infill", nullptr, 75,
         SettingWidget::Default, nullptr, false, false, "ironing_177488#"},
        {"ironing_type", SettingPresetPrint, L("Infill"), L("Ironing"), 77, nullptr, "infill", L("Ironing type"), 76,
         SettingWidget::Default, nullptr, false, false, "ironing_177488#ironing-type", "ironing"},
        {"ironing_flowrate", SettingPresetPrint, L("Infill"), L("Ironing"), 78, nullptr, "infill", nullptr, 77,
         SettingWidget::Default, nullptr, false, false, "ironing_177488#flow-rate", "ironing"},
        {"ironing_speed", SettingPresetPrint, L("Infill"), L("Ironing"), 79, nullptr, "infill", nullptr, 78,
         SettingWidget::Default, nullptr, false, false, nullptr, "ironing"},
        {"ironing_spacing", SettingPresetPrint, L("Infill"), L("Ironing"), 80, nullptr, "infill", L("Spacing"), 79,
         SettingWidget::Default, nullptr, false, false, "ironing_177488#spacing-between-ironing-passes", "ironing"},
        {"automatic_infill_combination", SettingPresetPrint, L("Infill"), L("Time savings"), 81, nullptr, "infill",
         nullptr, 80, SettingWidget::Default, nullptr, false, false, nullptr, "infill"},
        {"automatic_infill_combination_max_layer_height", SettingPresetPrint, L("Infill"), L("Time savings"), 82,
         nullptr, "infill", L("Max combined layer height"), 81, SettingWidget::Default, nullptr, false, false, nullptr,
         "infill_automatic_combination"},
        {"infill_every_layers", SettingPresetPrint, L("Infill"), L("Time savings"), 83, nullptr, "infill", nullptr, 82,
         SettingWidget::Default, nullptr, false, false, "infill_42#combine-infill-every-x-layers",
         "infill_manual_combination"},
        {"narrow_to_athena", SettingPresetPrint, L("Infill"), L("Time savings"), 84, nullptr, "infill", nullptr, 83,
         SettingWidget::Default, nullptr, false, false, "infill_42#narrow-to-athena"},
        {"narrow_to_athena_top_bottom", SettingPresetPrint, L("Infill"), L("Time savings"), 85, nullptr, "infill",
         nullptr, 84, SettingWidget::Default, nullptr, false, false, "infill_42#narrow-to-athena-top-bottom",
         "narrow_to_athena"},
        {"narrow_to_athena_threshold", SettingPresetPrint, L("Infill"), L("Time savings"), 86, nullptr, "infill",
         L("Narrow to Athena threshold"), 85, SettingWidget::Default, nullptr, false, false,
         "infill_42#narrow-to-athena-threshold", "narrow_to_athena"},
        {"solid_infill_every_layers", SettingPresetPrint, L("Infill"), L("Advanced"), 87, nullptr, "infill", nullptr,
         86, SettingWidget::Default, nullptr, false, false, "infill_42#solid-infill-every-x-layers", "infill"},
        {"fill_angle", SettingPresetPrint, L("Infill"), L("Advanced"), 88, nullptr, "infill", nullptr, 87,
         SettingWidget::Default, nullptr, false, false, "infill_42#fill-angle", "infill_or_solid"},
        {"solid_infill_below_area", SettingPresetPrint, L("Infill"), L("Advanced"), 89, nullptr, "infill", nullptr, 88,
         SettingWidget::Default, nullptr, false, false, "infill_42#solid-infill-threshold-area", "infill"},
        {"bridge_angle", SettingPresetPrint, L("Infill"), L("Advanced"), 90, nullptr, "infill", L("Bridge angle"), 89,
         SettingWidget::Default, nullptr, false, false, nullptr, "infill_or_solid"},
        {"merge_top_solid_infills", SettingPresetPrint, L("Infill"), L("Advanced"), 91, nullptr, "infill", nullptr, 90,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"only_retract_when_crossing_perimeters", SettingPresetPrint, L("Infill"), L("Advanced"), 92, nullptr, "infill",
         nullptr, 91, SettingWidget::Default, nullptr, false, false, nullptr},
        {"infill_first", SettingPresetPrint, L("Infill"), L("Advanced"), 93, nullptr, "infill", nullptr, 92,
         SettingWidget::Default, nullptr, false, false, nullptr, "solid_infill"},
        {"skirts", SettingPresetPrint, L("Skirt and brim"), L("Skirt"), 94, nullptr, "skirt", nullptr, 93,
         SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#skirt"},
        {"skirt_distance", SettingPresetPrint, L("Skirt and brim"), L("Skirt"), 95, nullptr, "skirt",
         L("Distance from object"), 94, SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#skirt",
         "skirt"},
        {"skirt_height", SettingPresetPrint, L("Skirt and brim"), L("Skirt"), 96, nullptr, "skirt", nullptr, 95,
         SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#skirt", "skirt_height"},
        {"draft_shield", SettingPresetPrint, L("Skirt and brim"), L("Skirt"), 97, nullptr, "skirt", nullptr, 96,
         SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#skirt", "skirt"},
        {"min_skirt_length", SettingPresetPrint, L("Skirt and brim"), L("Skirt"), 98, nullptr, "skirt",
         L("Minimum extrusion length"), 97, SettingWidget::Default, nullptr, false, false,
         "skirt-and-brim_133969#skirt", "skirt"},
        {"brim_type", SettingPresetPrint, L("Skirt and brim"), L("Brim"), 99, nullptr, "skirt", nullptr, 98,
         SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#brim"},
        {"brim_width", SettingPresetPrint, L("Skirt and brim"), L("Brim"), 100, nullptr, "skirt", nullptr, 99,
         SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#brim"},
        {"brim_separation", SettingPresetPrint, L("Skirt and brim"), L("Brim"), 101, nullptr, "skirt",
         L("Brim separation"), 100, SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#brim"},
        {"brim_ears_max_angle", SettingPresetPrint, L("Skirt and brim"), L("Brim"), 102, nullptr, "skirt",
         L("Brim ears max angle"), 101, SettingWidget::Default, nullptr, false, false, "skirt-and-brim_133969#brim"},
        {"brim_ears_detection_length", SettingPresetPrint, L("Skirt and brim"), L("Brim"), 103, nullptr, "skirt",
         L("Brim ears detection length"), 102, SettingWidget::Default, nullptr, false, false,
         "skirt-and-brim_133969#brim"},
        {"support_material", SettingPresetPrint, L("Support material"), L("Support material"), 104, nullptr, "support",
         nullptr, 103, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#generate-support-material"},
        {"support_material_auto", SettingPresetPrint, L("Support material"), L("Support material"), 105, nullptr,
         "support", nullptr, 104, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#auto-generated-supports"},
        {"support_material_style", SettingPresetPrint, L("Support material"), L("Support material"), 106, nullptr,
         "support", nullptr, 105, SettingWidget::Default, nullptr, false, false, "support-material_1698#style",
         "support_auto"},
        {"support_material_threshold", SettingPresetPrint, L("Support material"), L("Support material"), 107, nullptr,
         "support", nullptr, 106, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#overhang-threshold", "support_auto"},
        {"support_alerts", SettingPresetPrint, L("Support material"), L("Support material"), 108, nullptr, "support",
         nullptr, 107, SettingWidget::Default, nullptr, false, false, "support-material_1698#support-alerts"},
        {"support_material_buildplate_only", SettingPresetPrint, L("Support material"), L("Support material"), 109,
         nullptr, "support", nullptr, 108, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#support-on-build-plate-only", "support_or_enforcers"},
        {"support_material_enforce_layers", SettingPresetPrint, L("Support material"), L("Support material"), 110,
         nullptr, "support", L("Enforce support for first"), 109, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#enforce-support-for-the-first"},
        {"raft_first_layer_density", SettingPresetPrint, L("Support material"), L("Support material"), 111, nullptr,
         "support", L("Raft first layer density"), 110, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#raft-first-layer-density"},
        {"raft_first_layer_expansion", SettingPresetPrint, L("Support material"), L("Support material"), 112, nullptr,
         "support", L("Raft first layer expansion"), 111, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#raft-first-layer-expansion"},
        {"raft_layers", SettingPresetPrint, L("Support material"), L("Raft"), 113, nullptr, "support", nullptr, 112,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#raft-layers"},
        {"raft_contact_distance", SettingPresetPrint, L("Support material"), L("Raft"), 114, nullptr, "support",
         nullptr, 113, SettingWidget::Default, nullptr, false, false, "support-material_1698#raft-layers",
         "raft_contact"},
        {"raft_expansion", SettingPresetPrint, L("Support material"), L("Raft"), 115, nullptr, "support", nullptr, 114,
         SettingWidget::Default, nullptr, false, false, nullptr, "raft"},
        {"support_material_contact_distance", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 116, nullptr, "support", L("Contact Z distance"), 115,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#contact-z-distance", "support"},
        {"support_material_contact_distance_custom", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 117, nullptr, "support", L("Custom contact Z distance"), 116,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#contact-z-distance",
         "support_custom_top_gap"},
        {"support_material_top_contact_extrusion_width", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 118, nullptr, "support", nullptr, 117, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#contact-z-distance"},
        {"support_material_bottom_contact_distance", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 119, nullptr, "support", nullptr, 118, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#contact-z-distance", "support"},
        {"support_material_bottom_contact_extrusion_width", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 120, nullptr, "support", nullptr, 119, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#contact-z-distance", "support_half_layer_gap"},
        {"support_material_pattern", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 121, nullptr, "support", nullptr, 120, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#pattern", "support"},
        {"support_material_bridge_no_gap", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 122, nullptr, "support", nullptr, 121, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#bridge-fill-with-no-gap"},
        {"support_material_with_sheath", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 123, nullptr, "support", L("With sheath around support"), 122,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#with-sheath-around-the-support",
         "support"},
        {"support_material_spacing", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 124, nullptr, "support", nullptr, 123, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#pattern-spacing-0-inf", "support"},
        {"support_material_angle", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 125, nullptr, "support", nullptr, 124, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#pattern-angle", "support"},
        {"support_material_closing_radius", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 126, nullptr, "support", nullptr, 125, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#pattern-angle", "support"},
        {"support_material_min_area", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 127, nullptr, "support", nullptr, 126, SettingWidget::Default,
         nullptr, false, false, nullptr, "support"},
        {"support_material_interface_layers", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 128, nullptr, "support", L("Interface layers"), 127,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#interface-layers", "support"},
        {"support_material_bottom_interface_layers", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 129, nullptr, "support", nullptr, 128, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#interface-layers", "support_interface"},
        {"support_material_interface_pattern", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 130, nullptr, "support", nullptr, 129, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#interface-pattern", "support"},
        {"support_material_interface_spacing", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 131, nullptr, "support", nullptr, 130, SettingWidget::Default,
         nullptr, false, false, "support-material_1698#interface-pattern-spacing", "support_interface"},
        {"support_material_interface_contact_loops", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 132, nullptr, "support", L("Interface contact loops"), 131,
         SettingWidget::Default, nullptr, false, false, "support-material_1698#interface-loops", "support_interface"},
        {"support_material_xy_spacing", SettingPresetPrint, L("Support material"),
         L("Options for support material and raft"), 133, nullptr, "support", L("XY separation"), 132,
         SettingWidget::Default, nullptr, false, false,
         "support-material_1698#xy-separation-between-an-object-and-its-support", "support"},
        {"dont_support_bridges", SettingPresetPrint, L("Support material"), L("Options for support material and raft"),
         134, nullptr, "support", nullptr, 133, SettingWidget::Default, nullptr, false, false,
         "support-material_1698#dont-support-bridges", "support"},
        {"support_tree_angle", SettingPresetPrint, L("Support material"), L("Organic supports"), 135, nullptr,
         "support", nullptr, 134, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_angle_slow", SettingPresetPrint, L("Support material"), L("Organic supports"), 136, nullptr,
         "support", nullptr, 135, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_branch_diameter", SettingPresetPrint, L("Support material"), L("Organic supports"), 137, nullptr,
         "support", nullptr, 136, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_branch_diameter_angle", SettingPresetPrint, L("Support material"), L("Organic supports"), 138,
         nullptr, "support", nullptr, 137, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_branch_diameter_double_wall", SettingPresetPrint, L("Support material"), L("Organic supports"),
         139, nullptr, "support", nullptr, 138, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_tip_diameter", SettingPresetPrint, L("Support material"), L("Organic supports"), 140, nullptr,
         "support", nullptr, 139, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_branch_distance", SettingPresetPrint, L("Support material"), L("Organic supports"), 141, nullptr,
         "support", nullptr, 140, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_top_rate", SettingPresetPrint, L("Support material"), L("Organic supports"), 142, nullptr,
         "support", nullptr, 141, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_tree_min_opening", SettingPresetPrint, L("Support material"), L("Organic supports"), 143, nullptr,
         "support", nullptr, 142, SettingWidget::Default, nullptr, false, false,
         "organic-supports_480131#organic-supports-settings", "support_or_enforcers"},
        {"support_baobab_angle", SettingPresetPrint, L("Support material"), L("Baobab supports"), 144, nullptr,
         "support", nullptr, 143, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_angle_slow", SettingPresetPrint, L("Support material"), L("Baobab supports"), 145, nullptr,
         "support", nullptr, 144, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_trunk_diameter", SettingPresetPrint, L("Support material"), L("Baobab supports"), 146, nullptr,
         "support", nullptr, 145, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_trunk_diameter_angle", SettingPresetPrint, L("Support material"), L("Baobab supports"), 147,
         nullptr, "support", nullptr, 146, SettingWidget::Default, nullptr, false, false, nullptr,
         "support_or_enforcers"},
        {"support_baobab_trunk_distance", SettingPresetPrint, L("Support material"), L("Baobab supports"), 148, nullptr,
         "support", nullptr, 147, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_trunk_consolidation", SettingPresetPrint, L("Support material"), L("Baobab supports"), 149,
         nullptr, "support", nullptr, 148, SettingWidget::Default, nullptr, false, false, nullptr,
         "support_or_enforcers"},
        {"support_baobab_canopy_density", SettingPresetPrint, L("Support material"), L("Baobab supports"), 150, nullptr,
         "support", nullptr, 149, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_max_canopy_angle", SettingPresetPrint, L("Support material"), L("Baobab supports"), 151,
         nullptr, "support", nullptr, 150, SettingWidget::Default, nullptr, false, false, nullptr,
         "support_or_enforcers"},
        {"support_baobab_plant_on_model", SettingPresetPrint, L("Support material"), L("Baobab supports"), 152, nullptr,
         "support", nullptr, 151, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"support_baobab_min_opening", SettingPresetPrint, L("Support material"), L("Baobab supports"), 153, nullptr,
         "support", nullptr, 152, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_enforcers"},
        {"auto_speed", SettingPresetPrint, L("Speed"), L("Speed for print moves"), 152, nullptr, "speed", nullptr, 151,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"max_volumetric_flow", SettingPresetPrint, L("Speed"), L("Speed for print moves"), 153, nullptr, "speed",
         nullptr, 152, SettingWidget::Default, nullptr, false, false, nullptr},
        {"max_print_speed", SettingPresetPrint, L("Speed"), L("Speed for print moves"), 154, nullptr, "speed", nullptr,
         153, SettingWidget::Default, nullptr, false, false, nullptr},
        {"perimeter_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 155, nullptr, "speed", nullptr, 154,
         SettingWidget::Default, nullptr, false, false, nullptr, "auto_speed_off"},
        {"external_perimeter_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 156, nullptr, "speed",
         nullptr, 155, SettingWidget::Default, nullptr, false, false, nullptr, "auto_speed_off"},
        {"small_perimeter_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 157, nullptr, "speed",
         nullptr, 156, SettingWidget::Default, nullptr, false, false, nullptr, "perimeters"},
        {"small_perimeter_diameter", SettingPresetPrint, L("Speed"), L("Print move speeds"), 158, nullptr, "speed",
         nullptr, 157, SettingWidget::Default, nullptr, false, false, nullptr, "perimeters"},
        {"infill_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 159, nullptr, "speed", nullptr, 158,
         SettingWidget::Default, nullptr, false, false, nullptr, "auto_speed_off"},
        {"solid_infill_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 160, nullptr, "speed", nullptr,
         159, SettingWidget::Default, nullptr, false, false, nullptr, "auto_speed_off"},
        {"top_solid_infill_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 161, nullptr, "speed",
         nullptr, 160, SettingWidget::Default, nullptr, false, false, nullptr, "top_solid_infill_speed"},
        {"support_material_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 162, nullptr, "speed",
         nullptr, 161, SettingWidget::Default, nullptr, false, false, nullptr, "auto_speed_off"},
        {"support_material_interface_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 163, nullptr,
         "speed", nullptr, 162, SettingWidget::Default, nullptr, false, false, nullptr, "support_interface_speed"},
        {"bridge_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 164, nullptr, "speed", nullptr, 163,
         SettingWidget::Default, nullptr, false, false, nullptr, "infill_or_solid"},
        {"over_bridge_speed", SettingPresetPrint, L("Speed"), L("Print move speeds"), 165, nullptr, "speed",
         L("Over bridge speed"), 164, SettingWidget::Default, nullptr, false, false, nullptr, "infill_or_solid"},
        {"enable_dynamic_overhang_speeds", SettingPresetPrint, L("Speed"), L("Overhang speed"), 166, nullptr, "speed",
         nullptr, 165, SettingWidget::Default, nullptr, false, false, nullptr, "perimeters"},
        {"overhang_speed_0", SettingPresetPrint, L("Speed"), L("Overhang speed"), 167, nullptr, "speed", nullptr, 166,
         SettingWidget::Default, nullptr, false, false, nullptr, "dynamic_overhang_speeds"},
        {"overhang_speed_1", SettingPresetPrint, L("Speed"), L("Overhang speed"), 168, nullptr, "speed", nullptr, 167,
         SettingWidget::Default, nullptr, false, false, nullptr, "dynamic_overhang_speeds"},
        {"overhang_speed_2", SettingPresetPrint, L("Speed"), L("Overhang speed"), 169, nullptr, "speed", nullptr, 168,
         SettingWidget::Default, nullptr, false, false, nullptr, "dynamic_overhang_speeds"},
        {"overhang_speed_3", SettingPresetPrint, L("Speed"), L("Overhang speed"), 170, nullptr, "speed", nullptr, 169,
         SettingWidget::Default, nullptr, false, false, nullptr, "dynamic_overhang_speeds"},
        {"travel_speed", SettingPresetPrint, L("Speed"), L("Travel speed"), 171, nullptr, "speed", nullptr, 170,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"travel_speed_z", SettingPresetPrint, L("Speed"), L("Travel speed"), 172, nullptr, "speed", nullptr, 171,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_speed", SettingPresetPrint, L("Speed"), L("Modifiers"), 173, nullptr, "speed", nullptr, 172,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_infill_speed", SettingPresetPrint, L("Speed"), L("Modifiers"), 174, nullptr, "speed",
         L("First layer infill speed"), 173, SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_travel_speed", SettingPresetPrint, L("Speed"), L("Modifiers"), 175, nullptr, "speed", nullptr,
         174, SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_speed_over_raft", SettingPresetPrint, L("Speed"), L("Modifiers"), 176, nullptr, "speed",
         L("First layer speed over raft"), 175, SettingWidget::Default, nullptr, false, false, nullptr, "raft"},
        {"default_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 177, nullptr, "speed", nullptr, 176,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"external_perimeter_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 178, nullptr, "speed",
         nullptr, 177, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"perimeter_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 179, nullptr, "speed", nullptr,
         178, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"top_solid_infill_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 180, nullptr, "speed",
         nullptr, 179, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"solid_infill_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 181, nullptr, "speed", nullptr,
         180, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"infill_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 182, nullptr, "speed", nullptr, 181,
         SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"bridge_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 183, nullptr, "speed", L("Bridges"),
         182, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"first_layer_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 184, nullptr, "speed", nullptr,
         183, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"first_layer_acceleration_over_raft", SettingPresetPrint, L("Speed"), L("Acceleration"), 185, nullptr, "speed",
         L("First layer over raft"), 184, SettingWidget::Default, nullptr, false, false, nullptr, "raft"},
        {"wipe_tower_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 186, nullptr, "speed", nullptr,
         185, SettingWidget::Default, nullptr, false, false, nullptr, "default_acceleration"},
        {"travel_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 187, nullptr, "speed", nullptr, 186,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"travel_short_distance_acceleration", SettingPresetPrint, L("Speed"), L("Acceleration"), 188, nullptr, "speed",
         L("Short distance travel"), 187, SettingWidget::Default, nullptr, false, false, nullptr},
        {"max_volumetric_extrusion_rate_slope_positive", SettingPresetPrint, L("Speed"), L("Pressure equalizer"), 189,
         nullptr, "speed", L("Max slope positive"), 188, SettingWidget::Default, nullptr, false, false,
         "pressure-equlizer_331504"},
        {"max_volumetric_extrusion_rate_slope_negative", SettingPresetPrint, L("Speed"), L("Pressure equalizer"), 190,
         nullptr, "speed", L("Max slope negative"), 189, SettingWidget::Default, nullptr, false, false,
         "pressure-equlizer_331504"},
        {"perimeter_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 191, nullptr, "extruders",
         nullptr, 190, SettingWidget::Default, nullptr, false, false, nullptr},
        {"interlocking_perimeter_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 192, nullptr,
         "extruders", nullptr, 191, SettingWidget::Default, nullptr, false, false, nullptr},
        {"infill_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 193, nullptr, "extruders",
         nullptr, 192, SettingWidget::Default, nullptr, false, false, nullptr, "infill"},
        {"solid_infill_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 194, nullptr,
         "extruders", nullptr, 193, SettingWidget::Default, nullptr, false, false, nullptr, "solid_infill"},
        {"support_material_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 195, nullptr,
         "extruders", nullptr, 194, SettingWidget::Default, nullptr, false, false, nullptr, "support_or_skirt"},
        {"support_material_interface_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 196,
         nullptr, "extruders", nullptr, 195, SettingWidget::Default, nullptr, false, false, nullptr,
         "support_interface"},
        {"wipe_tower_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 197, nullptr, "extruders",
         nullptr, 196, SettingWidget::Default, nullptr, false, false, nullptr},
        {"bed_temperature_extruder", SettingPresetPrint, L("Multiple Extruders"), L("Extruders"), 198, nullptr,
         "extruders", L("Bed temperature extruder"), 197, SettingWidget::Default, nullptr, false, false, nullptr},
        {"ooze_prevention", SettingPresetPrint, L("Multiple Extruders"), L("Ooze prevention"), 199, nullptr,
         "extruders", nullptr, 198, SettingWidget::Default, nullptr, false, false, nullptr},
        {"standby_temperature_delta", SettingPresetPrint, L("Multiple Extruders"), L("Ooze prevention"), 200, nullptr,
         "extruders", nullptr, 199, SettingWidget::Default, nullptr, false, false, nullptr, "ooze_prevention"},
        {"wipe_tower", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 201, nullptr, "extruders", nullptr,
         200, SettingWidget::Default, nullptr, false, false, nullptr},
        {"wipe_tower_width", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 202, nullptr, "extruders",
         nullptr, 201, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_brim_width", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 203, nullptr,
         "extruders", L("Brim width"), 202, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_bridging", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 204, nullptr, "extruders",
         L("Bridging"), 203, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_cone_angle", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 205, nullptr,
         "extruders", L("Cone angle"), 204, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_extra_spacing", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 206, nullptr,
         "extruders", L("Extra spacing"), 205, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_extra_flow", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 207, nullptr,
         "extruders", L("Extra flow"), 206, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"wipe_tower_no_sparse_layers", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 208, nullptr,
         "extruders", L("No sparse layers"), 207, SettingWidget::Default, nullptr, false, false, nullptr, "wipe_tower"},
        {"single_extruder_multi_material_priming", SettingPresetPrint, L("Multiple Extruders"), L("Wipe tower"), 209,
         nullptr, "extruders", L("Single extruder MM priming"), 208, SettingWidget::Default, nullptr, false, false,
         nullptr, "wipe_tower"},
        {"interface_shells", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 210, nullptr, "extruders",
         nullptr, 209, SettingWidget::Default, nullptr, false, false, nullptr},
        {"mmu_segmented_region_max_width", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 211, nullptr,
         "extruders", L("MMU segmented region max width"), 210, SettingWidget::Default, nullptr, false, false, nullptr,
         "no_interlocking_beam"},
        {"mmu_segmented_region_interlocking_depth", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 212,
         nullptr, "extruders", L("MMU segmented interlocking depth"), 211, SettingWidget::Default, nullptr, false,
         false, nullptr, "segmented_region"},
        {"interlocking_beam", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 213, nullptr, "extruders",
         L("Interlocking beam"), 212, SettingWidget::Default, nullptr, false, false, nullptr},
        {"interlocking_beam_width", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 214, nullptr,
         "extruders", L("Beam width"), 213, SettingWidget::Default, nullptr, false, false, nullptr,
         "interlocking_beam"},
        {"interlocking_orientation", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 215, nullptr,
         "extruders", L("Orientation"), 214, SettingWidget::Default, nullptr, false, false, nullptr,
         "interlocking_beam"},
        {"interlocking_beam_layer_count", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 216, nullptr,
         "extruders", L("Beam layer count"), 215, SettingWidget::Default, nullptr, false, false, nullptr,
         "interlocking_beam"},
        {"interlocking_depth", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 217, nullptr, "extruders",
         nullptr, 216, SettingWidget::Default, nullptr, false, false, nullptr, "interlocking_beam"},
        {"interlocking_boundary_avoidance", SettingPresetPrint, L("Multiple Extruders"), L("Advanced"), 218, nullptr,
         "extruders", L("Boundary avoidance"), 217, SettingWidget::Default, nullptr, false, false, nullptr,
         "interlocking_beam"},
        {"extrusion_width_percent_of_nozzle", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 219, nullptr,
         "advanced", nullptr, 218, SettingWidget::Default, nullptr, false, false, nullptr},
        {"extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 220, nullptr, "advanced", nullptr,
         219, SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 221, nullptr,
         "advanced", nullptr, 220, SettingWidget::Default, nullptr, false, false, nullptr},
        {"perimeter_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 222, nullptr, "advanced",
         nullptr, 221, SettingWidget::Default, nullptr, false, false, nullptr},
        {"external_perimeter_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 223, nullptr,
         "advanced", nullptr, 222, SettingWidget::Default, nullptr, false, false, nullptr, "perimeters"},
        {"infill_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 224, nullptr, "advanced",
         nullptr, 223, SettingWidget::Default, nullptr, false, false, nullptr, "infill_or_solid"},
        {"solid_infill_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 225, nullptr,
         "advanced", nullptr, 224, SettingWidget::Default, nullptr, false, false, nullptr, "solid_infill"},
        {"bridge_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 226, nullptr, "advanced",
         L("Bridge"), 225, SettingWidget::Default, nullptr, false, false, nullptr},
        {"top_infill_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 227, nullptr,
         "advanced", nullptr, 226, SettingWidget::Default, nullptr, false, false, nullptr, "top_solid_infill"},
        {"support_material_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 228, nullptr,
         "advanced", nullptr, 227, SettingWidget::Default, nullptr, false, false, nullptr},
        {"support_material_interface_extrusion_width", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 229,
         nullptr, "advanced", nullptr, 228, SettingWidget::Default, nullptr, false, false, nullptr},
        {"automatic_extrusion_widths", SettingPresetPrint, L("Advanced"), L("Extrusion width"), 230, nullptr,
         "advanced", nullptr, 229, SettingWidget::Default, nullptr, false, false, nullptr},
        {"external_perimeter_overlap", SettingPresetPrint, L("Advanced"), L("Overlap"), 231, nullptr, "advanced",
         L("External perimeter overlap"), 230, SettingWidget::Default, nullptr, false, false, nullptr},
        {"perimeter_perimeter_overlap", SettingPresetPrint, L("Advanced"), L("Overlap"), 232, nullptr, "advanced",
         L("Perimeter overlap"), 231, SettingWidget::Default, nullptr, false, false, nullptr},
        {"infill_overlap", SettingPresetPrint, L("Advanced"), L("Overlap"), 233, nullptr, "advanced", nullptr, 232,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"bridge_infill_perimeter_overlap", SettingPresetPrint, L("Advanced"), L("Overlap"), 234, nullptr, "advanced",
         L("Bridge infill perimeter overlap"), 233, SettingWidget::Default, nullptr, false, false, nullptr},
        {"bridge_infill_overlap", SettingPresetPrint, L("Advanced"), L("Overlap"), 235, nullptr, "advanced", nullptr,
         234, SettingWidget::Default, nullptr, false, false, nullptr},
        {"bridge_flow_ratio", SettingPresetPrint, L("Advanced"), L("Flow"), 236, nullptr, "advanced", nullptr, 235,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"slice_closing_radius", SettingPresetPrint, L("Advanced"), L("Slicing"), 237, nullptr, "advanced",
         L("Slice closing radius"), 236, SettingWidget::Default, nullptr, false, false, nullptr},
        {"slicing_mode", SettingPresetPrint, L("Advanced"), L("Slicing"), 238, nullptr, "advanced", L("Slicing mode"),
         237, SettingWidget::Default, nullptr, false, false, nullptr},
        {"resolution", SettingPresetPrint, L("Advanced"), L("Slicing"), 239, nullptr, "advanced", L("Resolution"), 238,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"gcode_resolution", SettingPresetPrint, L("Advanced"), L("Slicing"), 240, nullptr, "advanced", nullptr, 239,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"xy_size_compensation", SettingPresetPrint, L("Advanced"), L("Slicing"), 241, nullptr, "advanced",
         L("XY size compensation"), 240, SettingWidget::Default, nullptr, false, false, nullptr},
        {"elefant_foot_compensation", SettingPresetPrint, L("Advanced"), L("Slicing"), 242, nullptr, "advanced",
         nullptr, 241, SettingWidget::Default, nullptr, false, false, "elephant-foot-compensation_114487"},
        {"perimeter_compression", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 243,
         nullptr, "advanced", nullptr, 242, SettingWidget::Default, nullptr, false, false, nullptr, "athena"},
        {"thin_wall_precision", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 244,
         nullptr, "advanced", nullptr, 243, SettingWidget::Default, nullptr, false, false, nullptr, "athena"},
        {"max_perimeter_width", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 245,
         nullptr, "advanced", nullptr, 244, SettingWidget::Default, nullptr, false, false, nullptr, "athena"},
        {"min_feature_size", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 246, nullptr,
         "advanced", L("Min feature size"), 245, SettingWidget::Default, nullptr, false, false, nullptr,
         "arachne_or_athena"},
        {"min_wall_length", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 247, nullptr,
         "advanced", L("Min wall length"), 246, SettingWidget::Default, nullptr, false, false, nullptr,
         "arachne_or_athena"},
        {"wall_transition_angle", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 248,
         nullptr, "advanced", L("Wall transition angle"), 247, SettingWidget::Default, nullptr, false, false, nullptr,
         "arachne_or_athena"},
        {"wall_transition_filter_deviation", SettingPresetPrint, L("Advanced"),
         L("Athena / Arachne perimeter generator"), 249, nullptr, "advanced", L("Wall transition filter deviation"),
         248, SettingWidget::Default, nullptr, false, false, nullptr, "arachne_or_athena"},
        {"wall_transition_length", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 250,
         nullptr, "advanced", L("Wall transition length"), 249, SettingWidget::Default, nullptr, false, false, nullptr,
         "arachne_or_athena"},
        {"wall_distribution_count", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 251,
         nullptr, "advanced", L("Wall distribution count"), 250, SettingWidget::Default, nullptr, false, false, nullptr,
         "arachne"},
        {"min_bead_width", SettingPresetPrint, L("Advanced"), L("Athena / Arachne perimeter generator"), 252, nullptr,
         "advanced", L("Min bead width"), 251, SettingWidget::Default, nullptr, false, false, nullptr, "arachne"},
        {"custom_parameters_print", SettingPresetPrint, L("Advanced"), L("Custom parameters"), 253, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_parameters", true, false, nullptr},
        {"preprocessing_enabled_print", SettingPresetPrint, L("Preprocessing"), L("Script Execution"), 254, nullptr,
         nullptr, nullptr, -1, SettingWidget::Default, nullptr, false, false, nullptr},
        {"complete_objects", SettingPresetPrint, L("Output options"), L("Sequential printing"), 255, nullptr, "output",
         nullptr, 252, SettingWidget::Default, nullptr, false, false, "sequential-printing_124589"},
        {"gcode_comments", SettingPresetPrint, L("Output options"), L("Output file"), 256, nullptr, "output", nullptr,
         253, SettingWidget::Default, nullptr, false, false, nullptr},
        {"gcode_label_objects", SettingPresetPrint, L("Output options"), L("Output file"), 257, nullptr, "output",
         nullptr, 254, SettingWidget::Default, nullptr, false, false, nullptr},
        {"output_filename_format", SettingPresetPrint, L("Output options"), L("Output file"), 258, nullptr, "output",
         nullptr, 255, SettingWidget::Default, nullptr, true, false, nullptr},
        {"export_script_enabled", SettingPresetPrint, L("Output options"), L("Export to Script"), 259, nullptr, nullptr,
         nullptr, -1, SettingWidget::Default, nullptr, false, false, nullptr},
        {"gcode_substitutions", SettingPresetPrint, L("Output options"), L("Other"), 260, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "gcode_substitutions", false, false, "g-code-substitutions_301694"},
        {"post_process", SettingPresetPrint, L("Output options"), L("Post-processing scripts (Legacy)"), 261, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "post_process", true, false, nullptr},
        {"notes", SettingPresetPrint, L("Notes"), L("Notes"), 262, nullptr, nullptr, nullptr, -1, SettingWidget::Custom,
         "notes", true, false, nullptr},
        {"compatible_printers", SettingPresetPrint | SettingPresetFilament, L("Dependencies"),
         L("Profile dependencies"), 263, nullptr, nullptr, nullptr, -1, SettingWidget::Custom, "compatible_printers",
         false, false, nullptr},
        {"compatible_printers_condition", SettingPresetPrint | SettingPresetFilament, L("Dependencies"),
         L("Profile dependencies"), 264, nullptr, nullptr, nullptr, -1, SettingWidget::Default, nullptr, true, false,
         nullptr},
        // Filament
        {"filament_type", SettingPresetFilament, L("Filament"), L("Properties"), 0, nullptr, "filament", nullptr, 0,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_colour", SettingPresetFilament, L("Filament"), L("Properties"), 1, nullptr, "filament", nullptr, 1,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_soluble", SettingPresetFilament, L("Filament"), L("Properties"), 2, nullptr, "filament", nullptr, 2,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_abrasive", SettingPresetFilament, L("Filament"), L("Properties"), 3, nullptr, "filament", nullptr, 3,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_transmission_distance", SettingPresetFilament, L("Filament"), L("Properties"), 4, nullptr,
         "filament", nullptr, 4, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_diameter", SettingPresetFilament, L("Filament"), L("Properties"), 5, nullptr, "filament", nullptr, 5,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"extrusion_multiplier", SettingPresetFilament, L("Filament"), L("Properties"), 6, nullptr, "filament", nullptr,
         6, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_density", SettingPresetFilament, L("Filament"), L("Properties"), 7, nullptr, "filament", nullptr, 7,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_cost", SettingPresetFilament, L("Filament"), L("Properties"), 8, nullptr, "filament", nullptr, 8,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_spool_weight", SettingPresetFilament, L("Filament"), L("Properties"), 9, nullptr, "filament",
         nullptr, 9, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_max_volumetric_flow", SettingPresetFilament, L("Filament"), L("Print speed override"), 10, nullptr,
         "filament", nullptr, 10, SettingWidget::Default, nullptr, false, false, "max-volumetric-speed_127176"},
        {"filament_max_print_speed", SettingPresetFilament, L("Filament"), L("Print speed override"), 11, nullptr,
         "filament", nullptr, 11, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_infill_max_speed", SettingPresetFilament, L("Filament"), L("Print speed override"), 12, nullptr,
         "filament", L("Max infill speed"), 12, SettingWidget::Default, nullptr, false, false,
         "max-simple-infill-speed"},
        {"filament_infill_max_crossing_speed", SettingPresetFilament, L("Filament"), L("Print speed override"), 13,
         nullptr, "filament", nullptr, 13, SettingWidget::Default, nullptr, false, false, "max-crossing-infill-speed"},
        {"filament_enable_pressure_advance", SettingPresetFilament, L("Filament"), L("Pressure advance"), 14, nullptr,
         "filament", nullptr, 14, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_pressure_advance", SettingPresetFilament, L("Filament"), L("Pressure advance"), 15, nullptr,
         "filament", nullptr, 15, SettingWidget::Default, nullptr, false, false, nullptr, "pressure_advance"},
        {"idle_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 16, nullptr, "filament", nullptr,
         16, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"first_layer_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 17, L("Nozzle"), "filament",
         L("First layer nozzle"), 17, SettingWidget::Default, nullptr, false, false, nullptr},
        {"temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 18, L("Nozzle"), "filament",
         L("Other layers nozzle"), 18, SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_layer_bed_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 19, L("Bed"),
         "filament", L("First layer bed"), 19, SettingWidget::Default, nullptr, false, false, nullptr},
        {"bed_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 20, L("Bed"), "filament",
         L("Other layers bed"), 20, SettingWidget::Default, nullptr, false, false, nullptr},
        {"chamber_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 21, L("Chamber"), "filament",
         L("Chamber"), 21, SettingWidget::Default, nullptr, false, false, nullptr},
        {"chamber_minimal_temperature", SettingPresetFilament, L("Filament"), L("Temperature"), 22, L("Chamber"),
         "filament", L("Chamber minimal"), 22, SettingWidget::Default, nullptr, false, false, nullptr},
        {"fan_always_on", SettingPresetFilament, L("Cooling"), L("Enable"), 23, nullptr, "cooling", nullptr, 23,
         SettingWidget::Default, nullptr, false, false, nullptr, "no_manual_fan"},
        {"cooling", SettingPresetFilament, L("Cooling"), L("Enable"), 24, nullptr, "cooling", nullptr, 24,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"cooling_slowdown_logic", SettingPresetFilament, L("Cooling"), L("Enable"), 25, nullptr, "cooling",
         L("Slowdown logic"), 25, SettingWidget::Default, nullptr, false, false, nullptr, "cooling"},
        {"cooling_perimeter_transition_distance", SettingPresetFilament, L("Cooling"), L("Enable"), 26, nullptr,
         "cooling", nullptr, 26, SettingWidget::Default, nullptr, false, false, nullptr, "cooling_consistent_surface"},
        {"min_fan_speed", SettingPresetFilament, L("Cooling"), L("Fan settings"), 27, L("Fan speed"), "cooling",
         L("Min fan speed"), 27, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-settings",
         "fan_auto"},
        {"max_fan_speed", SettingPresetFilament, L("Cooling"), L("Fan settings"), 28, L("Fan speed"), "cooling",
         L("Max fan speed"), 28, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-settings",
         "cooling"},
        {"disable_fan_first_layers", SettingPresetFilament, L("Cooling"), L("Fan settings"), 29, nullptr, "cooling",
         L("Disable fan for first"), 29, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-settings"},
        {"full_fan_speed_layer", SettingPresetFilament, L("Cooling"), L("Fan settings"), 30, nullptr, "cooling",
         nullptr, 30, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-settings", "fan_auto"},
        {"enable_manual_fan_speeds", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 31, nullptr,
         "cooling", nullptr, 31, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls"},
        {"manual_fan_speed_perimeter", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 32, nullptr,
         "cooling", L("Perimeter"), 32, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_external_perimeter", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 33,
         nullptr, "cooling", L("External perimeter"), 33, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_overhang_perimeter", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 34,
         nullptr, "cooling", L("Overhang perimeter"), 34, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls"},
        {"manual_fan_speed_interlocking_perimeter", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 35,
         nullptr, "cooling", L("Interlocking perimeter"), 35, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_serpentine", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 36, nullptr,
         "cooling", nullptr, 36, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls",
         "manual_fan"},
        {"manual_fan_speed_serpentine_overhang", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 37,
         nullptr, "cooling", nullptr, 37, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_internal_infill", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 38, nullptr,
         "cooling", L("Internal infill"), 38, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_solid_infill", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 39, nullptr,
         "cooling", nullptr, 39, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls",
         "manual_fan"},
        {"bridge_fan_speed", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 40, nullptr, "cooling",
         L("Bridge"), 40, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls"},
        {"manual_fan_speed_top_solid_infill", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 41,
         nullptr, "cooling", nullptr, 41, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_ironing", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 42, nullptr,
         "cooling", nullptr, 42, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls",
         "manual_fan"},
        {"manual_fan_speed_skirt", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 43, nullptr,
         "cooling", L("Skirt"), 43, SettingWidget::Default, nullptr, false, false, "cooling_127569#manual-fan-controls",
         "manual_fan"},
        {"manual_fan_speed_support_material", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 44,
         nullptr, "cooling", nullptr, 44, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"manual_fan_speed_support_interface", SettingPresetFilament, L("Cooling"), L("Manual fan controls"), 45,
         nullptr, "cooling", nullptr, 45, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#manual-fan-controls", "manual_fan"},
        {"enable_dynamic_fan_speeds", SettingPresetFilament, L("Cooling"), L("Dynamic fan speeds"), 46, nullptr,
         "cooling", nullptr, 46, SettingWidget::Default, nullptr, false, false, "cooling_127569#dynamic-fan-speeds"},
        {"overhang_fan_speed_0", SettingPresetFilament, L("Cooling"), L("Dynamic fan speeds"), 47, nullptr, "cooling",
         nullptr, 47, SettingWidget::Default, nullptr, false, false, "cooling_127569#dynamic-fan-speeds",
         "dynamic_fan"},
        {"overhang_fan_speed_1", SettingPresetFilament, L("Cooling"), L("Dynamic fan speeds"), 48, nullptr, "cooling",
         nullptr, 48, SettingWidget::Default, nullptr, false, false, "cooling_127569#dynamic-fan-speeds",
         "dynamic_fan"},
        {"overhang_fan_speed_2", SettingPresetFilament, L("Cooling"), L("Dynamic fan speeds"), 49, nullptr, "cooling",
         nullptr, 49, SettingWidget::Default, nullptr, false, false, "cooling_127569#dynamic-fan-speeds",
         "dynamic_fan"},
        {"overhang_fan_speed_3", SettingPresetFilament, L("Cooling"), L("Dynamic fan speeds"), 50, nullptr, "cooling",
         nullptr, 50, SettingWidget::Default, nullptr, false, false, "cooling_127569#dynamic-fan-speeds",
         "dynamic_fan"},
        {"fan_spinup_bridge_infill", SettingPresetFilament, L("Cooling"), L("Fan spin-up"), 51, nullptr, "cooling",
         nullptr, 51, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-spinup"},
        {"fan_spinup_overhang_perimeter", SettingPresetFilament, L("Cooling"), L("Fan spin-up"), 52, nullptr, "cooling",
         L("Overhang perimeter"), 52, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-spinup"},
        {"fan_spinup_serpentine_overhang", SettingPresetFilament, L("Cooling"), L("Fan spin-up"), 53, nullptr,
         "cooling", nullptr, 53, SettingWidget::Default, nullptr, false, false, "cooling_127569#fan-spinup"},
        {"fan_below_layer_time", SettingPresetFilament, L("Cooling"), L("Cooling thresholds"), 54, nullptr, "cooling",
         L("Fan below layer time"), 54, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#cooling-thresholds", "cooling"},
        {"slowdown_below_layer_time", SettingPresetFilament, L("Cooling"), L("Cooling thresholds"), 55, nullptr,
         "cooling", L("Slowdown below layer time"), 55, SettingWidget::Default, nullptr, false, false,
         "cooling_127569#cooling-thresholds", "cooling"},
        {"dont_slow_down_outer_wall", SettingPresetFilament, L("Cooling"), L("Cooling thresholds"), 56, nullptr,
         "cooling", nullptr, 56, SettingWidget::Default, nullptr, false, false, "cooling_127569#cooling-thresholds",
         "cooling"},
        {"min_print_speed", SettingPresetFilament, L("Cooling"), L("Cooling thresholds"), 57, nullptr, "cooling",
         nullptr, 57, SettingWidget::Default, nullptr, false, false, "cooling_127569#cooling-thresholds", "cooling"},
        {"filament_shrinkage_compensation_x", SettingPresetFilament, L("Advanced"), L("Shrinkage compensation"), 58,
         nullptr, "advanced", L("X compensation"), 58, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_shrinkage_compensation_y", SettingPresetFilament, L("Advanced"), L("Shrinkage compensation"), 59,
         nullptr, "advanced", L("Y compensation"), 59, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_shrinkage_compensation_z", SettingPresetFilament, L("Advanced"), L("Shrinkage compensation"), 60,
         nullptr, "advanced", L("Z compensation"), 60, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_minimal_purge_on_wipe_tower", SettingPresetFilament, L("Advanced"), L("Wipe tower parameters"), 61,
         nullptr, "advanced", nullptr, 61, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_loading_speed_start", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 62, nullptr,
         "advanced", L("Loading speed (start)"), 62, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_loading_speed", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 63, nullptr,
         "advanced", nullptr, 63, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_unloading_speed_start", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 64, nullptr,
         "advanced", L("Unloading speed (start)"), 64, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_unloading_speed", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 65, nullptr,
         "advanced", nullptr, 65, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_load_time", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 66, nullptr, "advanced",
         L("Load time"), 66, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_unload_time", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 67, nullptr, "advanced",
         L("Unload time"), 67, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_toolchange_delay", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 68, nullptr,
         "advanced", L("Toolchange delay"), 68, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_cooling_moves", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 69, nullptr,
         "advanced", L("Cooling moves"), 69, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_cooling_initial_speed", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 70, nullptr,
         "advanced", L("Cooling initial speed"), 70, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_cooling_final_speed", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 71, nullptr,
         "advanced", L("Cooling final speed"), 71, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_stamping_loading_speed", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 72, nullptr,
         "advanced", nullptr, 72, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_stamping_distance", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 73, nullptr,
         "advanced", L("Stamping distance"), 73, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_purge_multiplier", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 74, nullptr,
         "advanced", L("Purge multiplier"), 74, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_ramming_parameters", SettingPresetFilament, L("Advanced"), L("Single extruder MM"), 75, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "filament_ramming_parameters", false, false, nullptr},
        {"filament_multitool_ramming", SettingPresetFilament, L("Advanced"), L("Multi extruder MM"), 76, nullptr,
         "advanced", L("Multitool ramming"), 75, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_multitool_ramming_volume", SettingPresetFilament, L("Advanced"), L("Multi extruder MM"), 77, nullptr,
         "advanced", L("Ramming volume"), 76, SettingWidget::Default, nullptr, false, false, nullptr,
         "multitool_ramming"},
        {"filament_multitool_ramming_flow", SettingPresetFilament, L("Advanced"), L("Multi extruder MM"), 78, nullptr,
         "advanced", L("Ramming flow"), 77, SettingWidget::Default, nullptr, false, false, nullptr,
         "multitool_ramming"},
        {"filament_retract_lift", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 79, nullptr,
         "overrides", L("Lift Z"), 78, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_travel_ramping_lift", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 80, nullptr,
         "overrides", L("Ramping lift"), 79, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_travel_max_lift", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 81, nullptr,
         "overrides", L("Max lift"), 80, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_travel_slope", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 82, nullptr,
         "overrides", L("Travel slope"), 81, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_travel_lift_before_obstacle", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 83,
         nullptr, "overrides", L("Lift before obstacle"), 82, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_lift_above", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 84, nullptr,
         "overrides", L("Only lift above"), 83, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_lift_below", SettingPresetFilament, L("Filament Overrides"), L("Travel lift"), 85, nullptr,
         "overrides", L("Only lift below"), 84, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_length", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 86, nullptr,
         "overrides", nullptr, 85, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_speed", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 87, nullptr,
         "overrides", L("Retraction speed"), 86, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_deretract_speed", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 88, nullptr,
         "overrides", L("Deretraction speed"), 87, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_restart_extra", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 89, nullptr,
         "overrides", L("Restart extra"), 88, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_before_travel", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 90, nullptr,
         "overrides", L("Minimum travel"), 89, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_layer_change", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 91, nullptr,
         "overrides", nullptr, 90, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_wipe", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 92, nullptr, "overrides",
         nullptr, 91, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_wipe_extend", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 93, nullptr,
         "overrides", L("Wipe extend"), 92, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_before_wipe", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 94, nullptr,
         "overrides", L("Retract before wipe"), 93, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_wipe_length", SettingPresetFilament, L("Filament Overrides"), L("Retraction"), 95, nullptr,
         "overrides", nullptr, 94, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"filament_retract_length_toolchange", SettingPresetFilament, L("Filament Overrides"),
         L("Tool change retraction"), 96, nullptr, "overrides", L("Retraction length"), 95, SettingWidget::Nullable,
         nullptr, false, false, nullptr},
        {"filament_retract_restart_extra_toolchange", SettingPresetFilament, L("Filament Overrides"),
         L("Tool change retraction"), 97, nullptr, "overrides", L("Restart extra"), 96, SettingWidget::Nullable,
         nullptr, false, false, nullptr},
        {"filament_seam_gap_distance", SettingPresetFilament, L("Filament Overrides"), L("Seams"), 98, nullptr,
         "overrides", nullptr, 97, SettingWidget::Nullable, nullptr, false, false, nullptr},
        {"start_filament_gcode", SettingPresetFilament, L("Custom G-code"), L("Start G-code"), 99, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"end_filament_gcode", SettingPresetFilament, L("Custom G-code"), L("End G-code"), 100, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"custom_parameters_filament", SettingPresetFilament, L("Custom G-code"), L("Custom parameters"), 101, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "custom_parameters", true, false, nullptr},
        {"preprocessing_enabled_filament", SettingPresetFilament, L("Preprocessing"), L("Script Execution"), 102,
         nullptr, nullptr, nullptr, -1, SettingWidget::Default, nullptr, false, false, nullptr},
        {"filament_notes", SettingPresetFilament, L("Notes"), L("Notes"), 103, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "notes", true, false, nullptr},
        {"compatible_prints", SettingPresetFilament, L("Dependencies"), L("Profile dependencies"), 264, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "compatible_prints", false, false, nullptr},
        {"compatible_prints_condition", SettingPresetFilament, L("Dependencies"), L("Profile dependencies"), 265,
         nullptr, nullptr, nullptr, -1, SettingWidget::Default, nullptr, true, false, nullptr},
        // Printer
        {"bed_shape", SettingPresetPrinter, L("General"), L("Size and coordinates"), 0, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "bed_shape", false, false, "custom-svg-and-png-bed-textures_124612"},
        {"max_print_height", SettingPresetPrinter, L("General"), L("Size and coordinates"), 1, nullptr, "general",
         nullptr, 0, SettingWidget::Default, nullptr, false, false, nullptr},
        {"z_offset", SettingPresetPrinter, L("General"), L("Size and coordinates"), 2, nullptr, "general", nullptr, 1,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"single_extruder_multi_material", SettingPresetPrinter, L("General"), L("Capabilities"), 3, nullptr, "general",
         L("Single extruder multi material"), 2, SettingWidget::Default, nullptr, false, false, nullptr,
         "multiple_extruders"},
        {"gcode_flavor", SettingPresetPrinter, L("General"), L("Firmware"), 4, nullptr, "general", nullptr, 3,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"thumbnails", SettingPresetPrinter, L("General"), L("Firmware"), 5, nullptr, "general", nullptr, 4,
         SettingWidget::Custom, "thumbnails", true, false, nullptr},
        {"silent_mode", SettingPresetPrinter, L("General"), L("Firmware"), 6, nullptr, "general", nullptr, 5,
         SettingWidget::Default, nullptr, false, false, nullptr, "marlin"},
        {"remaining_times", SettingPresetPrinter, L("General"), L("Firmware"), 7, nullptr, "general", nullptr, 6,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"binary_gcode", SettingPresetPrinter, L("General"), L("Firmware"), 8, nullptr, "general", L("Binary G-code"),
         7, SettingWidget::Default, nullptr, false, false, nullptr},
        {"use_relative_e_distances", SettingPresetPrinter, L("General"), L("Advanced"), 9, nullptr, "general", nullptr,
         8, SettingWidget::Default, nullptr, false, false, nullptr},
        {"use_firmware_retraction", SettingPresetPrinter, L("General"), L("Advanced"), 10, nullptr, "general", nullptr,
         9, SettingWidget::Default, nullptr, false, false, nullptr},
        {"use_volumetric_e", SettingPresetPrinter, L("General"), L("Advanced"), 11, nullptr, "general", nullptr, 10,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"variable_layer_height", SettingPresetPrinter, L("General"), L("Advanced"), 12, nullptr, "general",
         L("Supports variable layer height"), 11, SettingWidget::Default, nullptr, false, false, nullptr},
        {"prefer_clockwise_movements", SettingPresetPrinter, L("General"), L("Advanced"), 13, nullptr, "general",
         nullptr, 12, SettingWidget::Default, nullptr, false, false, nullptr},
        {"first_travel_combine_z", SettingPresetPrinter, L("General"), L("Advanced"), 14, nullptr, "general", nullptr,
         13, SettingWidget::Default, nullptr, false, false, nullptr},
        {"currency_symbol", SettingPresetPrinter, L("General"), L("Advanced"), 15, nullptr, "general", nullptr, 14,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"time_cost", SettingPresetPrinter, L("General"), L("Advanced"), 16, nullptr, "general", nullptr, 15,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"extruder_clearance_radius", SettingPresetPrinter, L("General"), L("Sequential printing limits"), 17, nullptr,
         "general", L("Extruder clearance radius"), 16, SettingWidget::Default, nullptr, false, false, nullptr},
        {"extruder_clearance_height", SettingPresetPrinter, L("General"), L("Sequential printing limits"), 18, nullptr,
         "general", L("Extruder clearance height"), 17, SettingWidget::Default, nullptr, false, false, nullptr},
        {"start_gcode", SettingPresetPrinter, L("Custom G-code"), L("Start G-code"), 18, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"autoemit_temperature_commands", SettingPresetPrinter, L("Custom G-code"), L("Start G-Code options"), 19,
         nullptr, nullptr, nullptr, -1, SettingWidget::Default, nullptr, false, false, nullptr},
        {"end_gcode", SettingPresetPrinter, L("Custom G-code"), L("End G-code"), 20, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"before_layer_gcode", SettingPresetPrinter, L("Custom G-code"), L("Before layer change G-code"), 21, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"layer_gcode", SettingPresetPrinter, L("Custom G-code"), L("After layer change G-code"), 22, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"toolchange_gcode", SettingPresetPrinter, L("Custom G-code"), L("Tool change G-code"), 23, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr, "multiple_extruders"},
        {"between_objects_gcode", SettingPresetPrinter, L("Custom G-code"),
         L("Between objects G-code (for sequential printing)"), 24, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"color_change_gcode", SettingPresetPrinter, L("Custom G-code"), L("Color Change G-code"), 25, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"pause_print_gcode", SettingPresetPrinter, L("Custom G-code"), L("Pause Print G-code"), 26, nullptr, nullptr,
         nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"template_custom_gcode", SettingPresetPrinter, L("Custom G-code"), L("Template Custom G-code"), 27, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "custom_gcode", true, false, nullptr},
        {"custom_parameters_printer", SettingPresetPrinter, L("Custom G-code"), L("Custom parameters"), 28, nullptr,
         nullptr, nullptr, -1, SettingWidget::Custom, "custom_parameters", true, false, nullptr},
        {"machine_limits_usage", SettingPresetPrinter, L("Machine limits"), L("General"), 29, nullptr, "limits",
         L("Machine limits usage"), 17, SettingWidget::Default, nullptr, false, false, nullptr},
        {"machine_max_feedrate_x", SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), 30,
         L("Maximum feedrate X"), "limits", L("X"), 18, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_feedrate_y", SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), 31,
         L("Maximum feedrate Y"), "limits", L("Y"), 19, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_feedrate_z", SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), 32,
         L("Maximum feedrate Z"), "limits", L("Z"), 20, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_feedrate_e", SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), 33,
         L("Maximum feedrate E"), "limits", L("E"), 21, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_acceleration_x", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), 34,
         L("Maximum acceleration X"), "limits", L("X"), 22, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_acceleration_y", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), 35,
         L("Maximum acceleration Y"), "limits", L("Y"), 23, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_acceleration_z", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), 36,
         L("Maximum acceleration Z"), "limits", L("Z"), 24, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_acceleration_e", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), 37,
         L("Maximum acceleration E"), "limits", L("E"), 25, SettingWidget::Default, nullptr, false, true, nullptr,
         "machine_limits"},
        {"machine_max_acceleration_extruding", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"),
         38, L("Maximum acceleration when extruding"), "limits", L("Extruding"), 26, SettingWidget::Default, nullptr,
         false, true, nullptr, "machine_limits"},
        {"machine_max_acceleration_retracting", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"),
         39, L("Maximum acceleration when retracting"), "limits", L("Retracting"), 27, SettingWidget::Default, nullptr,
         false, true, nullptr, "machine_limits"},
        {"machine_max_acceleration_travel", SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), 40,
         L("Maximum acceleration for travel moves"), "limits", L("Travel"), 28, SettingWidget::Default, nullptr, false,
         true, nullptr, "machine_limits_travel_acceleration"},
        {"machine_max_jerk_x", SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), 41, L("Maximum jerk X"),
         "limits", L("X"), 29, SettingWidget::Default, nullptr, false, true, nullptr, "machine_limits"},
        {"machine_max_jerk_y", SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), 42, L("Maximum jerk Y"),
         "limits", L("Y"), 30, SettingWidget::Default, nullptr, false, true, nullptr, "machine_limits"},
        {"machine_max_jerk_z", SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), 43, L("Maximum jerk Z"),
         "limits", L("Z"), 31, SettingWidget::Default, nullptr, false, true, nullptr, "machine_limits"},
        {"machine_max_jerk_e", SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), 44, L("Maximum jerk E"),
         "limits", L("E"), 32, SettingWidget::Default, nullptr, false, true, nullptr, "machine_limits"},
        {"machine_max_junction_deviation", SettingPresetPrinter, L("Machine limits"), L("Junction deviation"), 45,
         L("Junction deviation"), "limits", L("Junction deviation"), 33, SettingWidget::Default, nullptr, false, true,
         nullptr, "machine_limits"},
        {"machine_min_extruding_rate", SettingPresetPrinter, L("Machine limits"), L("Minimum feedrates"), 46,
         L("Minimum feedrate when extruding"), "limits", L("Minimum extruding rate"), 34, SettingWidget::Default,
         nullptr, false, true, nullptr, "machine_limits_min_feedrates"},
        {"machine_min_travel_rate", SettingPresetPrinter, L("Machine limits"), L("Minimum feedrates"), 47,
         L("Minimum travel feedrate"), "limits", L("Minimum travel rate"), 35, SettingWidget::Default, nullptr, false,
         true, nullptr, "machine_limits_min_feedrates"},
        {"machine_time_compensation", SettingPresetPrinter, L("Machine limits"), L("Time estimation"), 48, nullptr,
         "limits", L("Time compensation"), 45, SettingWidget::Default, nullptr, false, false, nullptr,
         "machine_limits"},
        {"machine_rrf_m566", SettingPresetPrinter, L("Machine limits"), L("RepRapFirmware M-codes"), 20025, nullptr,
         "limits", nullptr, 36, SettingWidget::Default, nullptr, true, false, nullptr, "machine_limits"},
        {"machine_rrf_m201", SettingPresetPrinter, L("Machine limits"), L("RepRapFirmware M-codes"), 20026, nullptr,
         "limits", L("M201 (Acceleration)"), 37, SettingWidget::Default, nullptr, true, false, nullptr,
         "machine_limits"},
        {"machine_rrf_m203", SettingPresetPrinter, L("Machine limits"), L("RepRapFirmware M-codes"), 20027, nullptr,
         "limits", L("M203 (Max feedrate)"), 38, SettingWidget::Default, nullptr, true, false, nullptr,
         "machine_limits"},
        {"machine_rrf_m204", SettingPresetPrinter, L("Machine limits"), L("RepRapFirmware M-codes"), 20028, nullptr,
         "limits", L("M204 (Acceleration)"), 39, SettingWidget::Default, nullptr, true, false, nullptr,
         "machine_limits"},
        {"machine_rrf_m207", SettingPresetPrinter, L("Machine limits"), L("RepRapFirmware M-codes"), 20029, nullptr,
         "limits", L("M207 (Retraction)"), 40, SettingWidget::Default, nullptr, true, false, nullptr, "machine_limits"},
        {"machine_klipper_max_velocity", SettingPresetPrinter, L("Machine limits"), L("Klipper machine limits"), 30021,
         nullptr, "limits", nullptr, 41, SettingWidget::Custom, "machine_limits_klipper", false, false, nullptr,
         "machine_limits"},
        {"machine_klipper_max_accel", SettingPresetPrinter, L("Machine limits"), L("Klipper machine limits"), 30022,
         nullptr, "limits", nullptr, 42, SettingWidget::Custom, "machine_limits_klipper", false, false, nullptr,
         "machine_limits"},
        {"machine_klipper_square_corner_velocity", SettingPresetPrinter, L("Machine limits"),
         L("Klipper machine limits"), 30023, nullptr, "limits", nullptr, 43, SettingWidget::Custom,
         "machine_limits_klipper", false, false, nullptr, "machine_limits"},
        {"machine_klipper_minimum_cruise_ratio", SettingPresetPrinter, L("Machine limits"), L("Klipper machine limits"),
         30024, nullptr, "limits", nullptr, 44, SettingWidget::Custom, "machine_limits_klipper", false, false, nullptr,
         "machine_limits"},
        {"nozzle_diameter", SettingPresetPrinter, L("Extruder"), L("Nozzle"), 49, nullptr, nullptr, nullptr, -1,
         SettingWidget::Default, nullptr, false, true, nullptr},
        {"nozzle_width_warning_min", SettingPresetPrinter, L("Extruder"), L("Nozzle"), 50, nullptr, "extruder_0",
         nullptr, 46, SettingWidget::Default, nullptr, false, true, nullptr},
        {"nozzle_width_warning_max", SettingPresetPrinter, L("Extruder"), L("Nozzle"), 51, nullptr, "extruder_0",
         nullptr, 47, SettingWidget::Default, nullptr, false, true, nullptr},
        {"extruder_colour", SettingPresetPrinter, L("Extruder"), L("Preview"), 52, nullptr, "extruder_0", nullptr, 48,
         SettingWidget::Default, nullptr, false, true, nullptr},
        {"fan_spinup_time", SettingPresetPrinter, L("Extruder"), L("Cooling fan"), 53, nullptr, "extruder_0",
         L("Fan spin-up time"), 49, SettingWidget::Default, nullptr, false, true, nullptr},
        {"fan_spinup_response_type", SettingPresetPrinter, L("Extruder"), L("Cooling fan"), 54, nullptr, "extruder_0",
         L("Response type"), 50, SettingWidget::Default, nullptr, false, true, nullptr},
        {"min_layer_height", SettingPresetPrinter, L("Extruder"), L("Layer height limits"), 55, nullptr, "extruder_0",
         L("Minimum"), 51, SettingWidget::Default, nullptr, false, true, nullptr},
        {"max_layer_height", SettingPresetPrinter, L("Extruder"), L("Layer height limits"), 56, nullptr, "extruder_0",
         L("Maximum"), 52, SettingWidget::Default, nullptr, false, true, nullptr},
        {"extruder_offset", SettingPresetPrinter, L("Extruder"), L("Position (for multi-extruder printers)"), 57,
         nullptr, "extruder_0", nullptr, 53, SettingWidget::Default, nullptr, false, true, nullptr},
        {"retract_lift", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 58, nullptr, "extruder_0", L("Lift Z"),
         54, SettingWidget::Default, nullptr, false, true, nullptr, "no_ramping_lift"},
        {"travel_ramping_lift", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 59, nullptr, "extruder_0",
         L("Ramping lift"), 55, SettingWidget::Default, nullptr, false, true, nullptr},
        {"travel_max_lift", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 60, nullptr, "extruder_0",
         L("Max lift"), 56, SettingWidget::Default, nullptr, false, true, nullptr, "ramping_lift"},
        {"travel_slope", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 61, nullptr, "extruder_0",
         L("Travel slope"), 57, SettingWidget::Default, nullptr, false, true, nullptr, "ramping_lift"},
        {"travel_lift_before_obstacle", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 62, nullptr,
         "extruder_0", L("Lift before obstacle"), 58, SettingWidget::Default, nullptr, false, true, nullptr,
         "ramping_lift"},
        {"retract_lift_above", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 63, L("Only lift"), "extruder_0",
         L("Only lift above"), 59, SettingWidget::Default, nullptr, false, true, nullptr, "lift"},
        {"retract_lift_below", SettingPresetPrinter, L("Extruder"), L("Travel lift"), 64, L("Only lift"), "extruder_0",
         L("Only lift below"), 60, SettingWidget::Default, nullptr, false, true, nullptr, "lift"},
        {"retract_length", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 65, nullptr, "extruder_0",
         nullptr, 61, SettingWidget::Default, nullptr, false, true, nullptr, "no_firmware_retraction"},
        {"retract_speed", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 66, nullptr, "extruder_0",
         L("Retraction speed"), 62, SettingWidget::Default, nullptr, false, true, nullptr, "retraction_in_slicer"},
        {"deretract_speed", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 67, nullptr, "extruder_0",
         L("Deretraction speed"), 63, SettingWidget::Default, nullptr, false, true, nullptr, "retraction_in_slicer"},
        {"retract_restart_extra", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 68, nullptr,
         "extruder_0", L("Restart extra"), 64, SettingWidget::Default, nullptr, false, true, nullptr,
         "retraction_in_slicer"},
        {"retract_before_wipe", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 69, nullptr, "extruder_0",
         L("Retract before wipe"), 65, SettingWidget::Default, nullptr, false, true, nullptr, "wipe"},
        {"retract_before_travel", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 70, nullptr,
         "extruder_0", L("Min travel after retraction"), 66, SettingWidget::Default, nullptr, false, true, nullptr,
         "retraction"},
        {"retract_layer_change", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 71, nullptr, "extruder_0",
         nullptr, 67, SettingWidget::Default, nullptr, false, true, nullptr, "retraction"},
        {"wipe", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 72, nullptr, "extruder_0", nullptr, 68,
         SettingWidget::Default, nullptr, false, true, nullptr, "no_firmware_retraction"},
        {"wipe_extend", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 73, nullptr, "extruder_0",
         L("Wipe extend"), 69, SettingWidget::Default, nullptr, false, true, nullptr},
        {"wipe_length", SettingPresetPrinter, L("Extruder"), L("Retraction / Wipe"), 74, nullptr, "extruder_0", nullptr,
         70, SettingWidget::Default, nullptr, false, true, nullptr},
        {"retract_length_toolchange", SettingPresetPrinter, L("Extruder"), L("Retraction when tool is disabled"), 75,
         nullptr, "extruder_0", L("Retraction length"), 71, SettingWidget::Default, nullptr, false, true, nullptr,
         "multiple_extruders"},
        {"retract_restart_extra_toolchange", SettingPresetPrinter, L("Extruder"), L("Retraction when tool is disabled"),
         76, nullptr, "extruder_0", L("Restart extra"), 72, SettingWidget::Default, nullptr, false, true, nullptr,
         "toolchange_retraction"},
        {"preprocessing_enabled_printer", SettingPresetPrinter, L("Preprocessing"), L("Script Execution"), 77, nullptr,
         nullptr, nullptr, -1, SettingWidget::Default, nullptr, false, false, nullptr},
        {"printer_notes", SettingPresetPrinter, L("Notes"), L("Notes"), 78, nullptr, nullptr, nullptr, -1,
         SettingWidget::Custom, "notes", true, false, nullptr},
        {"cooling_tube_retraction", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10001, nullptr, "single_extruder_mm", nullptr, 10002,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"cooling_tube_length", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10002, nullptr, "single_extruder_mm", nullptr, 10003,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"parking_pos_retraction", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10003, nullptr, "single_extruder_mm", nullptr, 10004,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"extra_loading_move", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10004, nullptr, "single_extruder_mm", nullptr, 10005,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"multimaterial_purging", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10005, nullptr, "single_extruder_mm", nullptr, 10006,
         SettingWidget::Default, nullptr, false, false, nullptr},
        {"high_current_on_filament_swap", SettingPresetPrinter, L("Single extruder MM"),
         L("Single extruder multimaterial parameters"), 10006, nullptr, "single_extruder_mm", nullptr, 10007,
         SettingWidget::Default, nullptr, false, false, nullptr},
    };
    return rows;
}

// The pages, in the order the Settings tabs show them, with their icons.
const std::vector<SettingPage> &setting_pages()
{
    static const std::vector<SettingPage> pages = {
        {SettingPresetPrint, L("Layers and perimeters"), "layers"},
        {SettingPresetPrint, L("Infill"), "infill"},
        {SettingPresetPrint, L("Skirt and brim"), "skirt+brim"},
        {SettingPresetPrint, L("Support material"), "support"},
        {SettingPresetPrint, L("Speed"), "time"},
        {SettingPresetPrint, L("Multiple Extruders"), "funnel"},
        {SettingPresetPrint, L("Advanced"), "wrench"},
        {SettingPresetPrint, L("Preprocessing"), "cog"},
        {SettingPresetPrint, L("Output options"), "output+page_white"},
        {SettingPresetPrint, L("Notes"), "note"},
        {SettingPresetPrint, L("Dependencies"), "wrench"},
        {SettingPresetFilament, L("Filament"), "spool"},
        {SettingPresetFilament, L("Cooling"), "cooling"},
        {SettingPresetFilament, L("Advanced"), "wrench"},
        {SettingPresetFilament, L("Filament Overrides"), "wrench"},
        {SettingPresetFilament, L("Custom G-code"), "cog"},
        {SettingPresetFilament, L("Preprocessing"), "cog"},
        {SettingPresetFilament, L("Notes"), "note"},
        {SettingPresetFilament, L("Dependencies"), "wrench"},
        {SettingPresetPrinter, L("General"), "printer"},
        {SettingPresetPrinter, L("Custom G-code"), "cog"},
        {SettingPresetPrinter, L("Machine limits"), "cog"},
        {SettingPresetPrinter, L("Extruder"), "funnel"},
        {SettingPresetPrinter, L("Preprocessing"), "cog"},
        {SettingPresetPrinter, L("Notes"), "note"},
        {SettingPresetPrinter, L("Dependencies"), "wrench"},
        {SettingPresetPrinter, L("Single extruder MM"), "printer"},
    };
    return pages;
}

// The composite lines, several rows on one line under one label, with the line's tooltip.
const std::vector<SettingLine> &setting_lines()
{
    static const std::vector<SettingLine> lines = {
        {SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), L("Solid layers"),
         L("Number of solid layers to generate on top and bottom surfaces. These layers provide a finished appearance and structural support for the printed object.")},
        {SettingPresetPrint, L("Layers and perimeters"), L("Horizontal shells"), L("Minimum shell thickness"),
         L("Minimum thickness (in mm) of top and bottom shells. The layer count will be automatically increased if needed to meet this thickness, which helps prevent pillowing on top surfaces when using variable layer heights.")},
        {SettingPresetPrint, L("Layers and perimeters"), L("Interlocking"), L("Solid layers"),
         L("Number of solid layers between interlocking perimeters and visible surfaces. Ensures the interlocking pattern is fully hidden inside the object.")},
        {SettingPresetFilament, L("Filament"), L("Temperature"), L("Nozzle"), nullptr},
        {SettingPresetFilament, L("Filament"), L("Temperature"), L("Bed"), nullptr},
        {SettingPresetFilament, L("Filament"), L("Temperature"), L("Chamber"), nullptr},
        {SettingPresetFilament, L("Cooling"), L("Fan settings"), L("Fan speed"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), L("Maximum feedrate X"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), L("Maximum feedrate Y"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), L("Maximum feedrate Z"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum feedrates"), L("Maximum feedrate E"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), L("Maximum acceleration X"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), L("Maximum acceleration Y"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), L("Maximum acceleration Z"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"), L("Maximum acceleration E"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"),
         L("Maximum acceleration when extruding"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"),
         L("Maximum acceleration when retracting"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Maximum accelerations"),
         L("Maximum acceleration for travel moves"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), L("Maximum jerk X"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), L("Maximum jerk Y"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), L("Maximum jerk Z"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Jerk limits"), L("Maximum jerk E"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Junction deviation"), L("Junction deviation"), nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Minimum feedrates"), L("Minimum feedrate when extruding"),
         nullptr},
        {SettingPresetPrinter, L("Machine limits"), L("Minimum feedrates"), L("Minimum travel feedrate"), nullptr},
        {SettingPresetPrinter, L("Extruder"), L("Travel lift"), L("Only lift"), nullptr},
    };
    return lines;
}

const std::vector<NoUiKey> &no_ui_keys()
{
    static const std::vector<NoUiKey> keys = {
        {"max_volumetric_speed", SettingPresetPrint, "legacy alias of max_volumetric_flow kept for scripting"},
        {"preprocessing_scripts_print", SettingPresetPrint, "owned by the preprocessing page's script list"},
        {"export_script", SettingPresetPrint, "owned by the export-script panel"},
        {"inherits", SettingPresetPrint | SettingPresetFilament | SettingPresetPrinter,
         "the preset system's parent link, never edited as a setting"},
        {"print_nozzle_diameters", SettingPresetPrint, "compatibility check filled by the profile, not edited"},
        {"print_high_flow_nozzle", SettingPresetPrint, "compatibility check filled by the profile, not edited"},
        {"filament_max_volumetric_speed", SettingPresetFilament,
         "legacy alias of filament_max_volumetric_flow kept for scripting"},
        {"filament_vendor", SettingPresetFilament, "preset identity, set by the profile source"},
        {"preprocessing_scripts_filament", SettingPresetFilament, "owned by the preprocessing page's script list"},
        {"printer_technology", SettingPresetPrinter, "preset identity; preFlight prints FFF only"},
        {"bed_custom_texture", SettingPresetPrinter, "part of the bed shape dialog"},
        {"bed_custom_model", SettingPresetPrinter, "part of the bed shape dialog"},
        {"host_type", SettingPresetPrinter, "owned by the print host upload group"},
        {"print_host", SettingPresetPrinter, "owned by the print host upload group"},
        {"printhost_apikey", SettingPresetPrinter, "owned by the print host upload group"},
        {"printhost_cafile", SettingPresetPrinter, "owned by the print host upload group"},
        {"printer_vendor", SettingPresetPrinter, "preset identity, set by the profile source"},
        {"printer_model", SettingPresetPrinter, "preset identity, set by the profile source"},
        {"printer_variant", SettingPresetPrinter, "preset identity, set by the profile source"},
        {"default_print_profile", SettingPresetPrinter, "preset identity, set by the profile source"},
        {"thumbnails_format", SettingPresetPrinter, "owned by the thumbnails editor"},
        {"nozzle_high_flow", SettingPresetPrinter, "owned by the extruder page's nozzle control"},
        {"preprocessing_scripts_printer", SettingPresetPrinter, "owned by the preprocessing page's script list"},
        {"default_filament_profile", SettingPresetPrinter, "preset identity, set by the profile source"},
    };
    return keys;
}

// The conditions the rules share, each reading the preset's edited config the way the rows'
// own tooltips describe it. The filament reads take the preset's own value (index 0); the
// per-extruder printer reads take the row's extruder.
namespace
{
bool have_perimeters(const ToggleContext &c)
{
    return c.config.opt_int("perimeters") > 0;
}
bool have_infill(const ToggleContext &c)
{
    return c.config.option<ConfigOptionPercent>("fill_density")->value > 0;
}
bool spiral_vase(const ToggleContext &c)
{
    return c.config.opt_bool("spiral_vase");
}
bool top_solid_layers(const ToggleContext &c)
{
    return c.config.opt_int("top_solid_layers") > 0;
}
bool bottom_solid_layers(const ToggleContext &c)
{
    return c.config.opt_int("bottom_solid_layers") > 0;
}
bool solid_infill(const ToggleContext &c)
{
    return top_solid_layers(c) || bottom_solid_layers(c);
}
// The top surface exists with top solid layers, or as the closing layers of a spiral vase.
bool top_surface(const ToggleContext &c)
{
    return top_solid_layers(c) || (spiral_vase(c) && bottom_solid_layers(c));
}
bool auto_speed(const ToggleContext &c)
{
    return c.config.opt_bool("auto_speed");
}
bool raft(const ToggleContext &c)
{
    return c.config.opt_int("raft_layers") > 0;
}
bool support(const ToggleContext &c)
{
    return c.config.opt_bool("support_material") || raft(c);
}
bool support_auto(const ToggleContext &c)
{
    return support(c) && c.config.opt_bool("support_material_auto");
}
bool support_soluble(const ToggleContext &c)
{
    return support(c) && c.config.opt_enum<SupportTopContactGap>("support_material_contact_distance") == stcgNoGap;
}
bool support_interface(const ToggleContext &c)
{
    return support(c) && c.config.opt_int("support_material_interface_layers") > 0;
}
bool support_or_enforcers(const ToggleContext &c)
{
    return c.config.opt_bool("support_material") || c.config.opt_int("support_material_enforce_layers") > 0;
}
bool skirt(const ToggleContext &c)
{
    return c.config.opt_int("skirts") > 0;
}
PerimeterGeneratorType generator(const ToggleContext &c)
{
    return c.config.opt_enum<PerimeterGeneratorType>("perimeter_generator");
}
bool serpentine(const ToggleContext &c)
{
    return !spiral_vase(c) && generator(c) == PerimeterGeneratorType::Athena && c.config.opt_bool("serpentine_enabled");
}
bool serpentine_strict(const ToggleContext &c)
{
    return serpentine(c) && !c.config.opt_bool("serpentine_relaxed");
}
FuzzySkinNoiseType fuzzy_noise(const ToggleContext &c)
{
    return c.config.opt_enum<FuzzySkinNoiseType>("fuzzy_skin_noise_type");
}
bool interlocking_beam(const ToggleContext &c)
{
    return c.config.opt_bool("interlocking_beam");
}
bool cooling(const ToggleContext &c)
{
    return c.config.opt_bool("cooling", 0);
}
bool manual_fan(const ToggleContext &c)
{
    return c.config.opt_bool("enable_manual_fan_speeds", 0);
}
GCodeFlavor flavor(const ToggleContext &c)
{
    return c.config.opt_enum<GCodeFlavor>("gcode_flavor");
}
bool marlin(const ToggleContext &c)
{
    return flavor(c) == gcfMarlinLegacy || flavor(c) == gcfMarlinFirmware;
}
bool machine_limits(const ToggleContext &c)
{
    return c.config.opt_enum<MachineLimitsUsage>("machine_limits_usage") != MachineLimitsUsage::Ignore;
}
bool firmware_retraction(const ToggleContext &c)
{
    return c.config.opt_bool("use_firmware_retraction");
}
bool ramping_lift(const ToggleContext &c)
{
    const auto *opt = c.config.option<ConfigOptionBools>("travel_ramping_lift");
    return opt != nullptr && c.extruder >= 0 && size_t(c.extruder) < opt->values.size() && opt->values[c.extruder];
}
bool retraction(const ToggleContext &c)
{
    return c.config.opt_float("retract_length", c.extruder) > 0 || firmware_retraction(c);
}
bool extruder_bool(const ToggleContext &c, const char *key)
{
    const auto *opt = c.config.option<ConfigOptionBools>(key);
    return opt != nullptr && c.extruder >= 0 && size_t(c.extruder) < opt->values.size() && opt->values[c.extruder];
}
} // namespace

// The rules, by id; the rows name them in `toggle_rule`.
const std::vector<ToggleRule> &toggle_rules()
{
    static const std::vector<ToggleRule> rules = {
        // Print
        {"perimeters", false, [](const ToggleContext &c) { return have_perimeters(c); },
         L("Available with at least one perimeter.")},
        {"seam_notch", false, [](const ToggleContext &c)
         { return have_perimeters(c) && c.config.opt_enum<SeamNotchType>("seam_type") != sntRegular; },
         L("Available with at least one perimeter and a seam type other than Regular.")},
        {"dynamic_overhang_speeds", false,
         [](const ToggleContext &c) { return c.config.opt_bool("enable_dynamic_overhang_speeds"); },
         L("Available when dynamic overhang speeds are enabled.")},
        {"infill", false, [](const ToggleContext &c) { return have_infill(c); },
         L("Available with a fill density above 0%.")},
        {"infill_manual_combination", false,
         [](const ToggleContext &c) { return have_infill(c) && !c.config.opt_bool("automatic_infill_combination"); },
         L("Available with infill, when automatic infill combination is disabled.")},
        {"infill_automatic_combination", false,
         [](const ToggleContext &c) { return have_infill(c) && c.config.opt_bool("automatic_infill_combination"); },
         L("Available with infill, when automatic infill combination is enabled.")},
        {"infill_anchors", false, [](const ToggleContext &c)
         { return have_infill(c) && c.config.option<ConfigOptionFloatOrPercent>("infill_anchor_max")->value > 0; },
         L("Available with infill and a maximum infill anchor length above 0.")},
        {"solid_infill", false, [](const ToggleContext &c) { return solid_infill(c); },
         L("Available with top or bottom solid layers.")},
        {"infill_or_solid", false, [](const ToggleContext &c) { return have_infill(c) || solid_infill(c); },
         L("Available with infill or solid layers.")},
        {"narrow_to_athena", false, [](const ToggleContext &c) { return c.config.opt_bool("narrow_to_athena"); },
         L("Available when Narrow to Athena is enabled.")},
        {"top_min_thickness", false,
         [](const ToggleContext &c)
         {
             return !spiral_vase(c) && top_solid_layers(c) &&
                    c.config.opt_enum<EnsureVerticalShellThickness>("ensure_vertical_shell_thickness") !=
                        EnsureVerticalShellThickness::Disabled;
         },
         L("Available with top solid layers and Ensure vertical shell thickness, without spiral vase.")},
        {"bottom_min_thickness", false,
         [](const ToggleContext &c)
         {
             return !spiral_vase(c) && bottom_solid_layers(c) &&
                    c.config.opt_enum<EnsureVerticalShellThickness>("ensure_vertical_shell_thickness") !=
                        EnsureVerticalShellThickness::Disabled;
         },
         L("Available with bottom solid layers and Ensure vertical shell thickness, without spiral vase.")},
        {"auto_speed_off", false, [](const ToggleContext &c) { return !auto_speed(c); },
         L("Disabled while Auto speed is active; it sets this speed from the maximum print speed and the volumetric "
           "flow.")},
        {"top_solid_infill_speed", false, [](const ToggleContext &c) { return top_surface(c) && !auto_speed(c); },
         L("Available with top solid layers, while Auto speed is not active.")},
        {"support_interface_speed", false,
         [](const ToggleContext &c) { return support_interface(c) && !auto_speed(c); },
         L("Available with support material and interface layers, while Auto speed is not active.")},
        {"fuzzy_structured_noise", false,
         [](const ToggleContext &c) { return fuzzy_noise(c) != FuzzySkinNoiseType::Classic; },
         L("Available with a fuzzy skin noise type other than Classic.")},
        {"fuzzy_octaves", false, [](const ToggleContext &c)
         { return fuzzy_noise(c) != FuzzySkinNoiseType::Classic && fuzzy_noise(c) != FuzzySkinNoiseType::Voronoi; },
         L("Available with the Perlin, Billow or Ridged fuzzy skin noise.")},
        {"fuzzy_persistence", false, [](const ToggleContext &c)
         { return fuzzy_noise(c) == FuzzySkinNoiseType::Perlin || fuzzy_noise(c) == FuzzySkinNoiseType::Billow; },
         L("Available with the Perlin or Billow fuzzy skin noise.")},
        {"no_spiral_vase", false, [](const ToggleContext &c) { return !spiral_vase(c); },
         L("Disabled while Spiral vase is active.")},
        {"serpentine", false, [](const ToggleContext &c) { return serpentine(c); },
         L("Available when Serpentine is enabled.")},
        {"serpentine_relaxed", false,
         [](const ToggleContext &c) { return serpentine(c) && c.config.opt_bool("serpentine_relaxed"); },
         L("Available when Serpentine and relaxed spacing are enabled.")},
        {"serpentine_strict", false, [](const ToggleContext &c) { return serpentine_strict(c); },
         L("Available when Serpentine is enabled and relaxed spacing is disabled.")},
        {"serpentine_depth", false,
         [](const ToggleContext &c) { return serpentine_strict(c) && c.config.opt_bool("serpentine_limit_depth"); },
         L("Available when Serpentine and its depth limit are enabled and relaxed spacing is disabled.")},
        {"serpentine_ridges", false,
         [](const ToggleContext &c) { return serpentine_strict(c) && !c.config.opt_bool("serpentine_outer_loop"); },
         L("Available when Serpentine is enabled and relaxed spacing and the outer loop are disabled; the outer "
           "loop aligns the ridges itself.")},
        {"interlock", false, [](const ToggleContext &c) { return c.config.opt_bool("interlock_perimeters_enabled"); },
         L("Available when interlocking perimeters are enabled.")},
        {"top_surface_flow_reduction", false, [](const ToggleContext &c)
         { return c.config.option<ConfigOptionPercent>("top_surface_flow_reduction")->value > 0; },
         L("Available with a top surface flow reduction above 0%.")},
        {"top_solid_infill", false, [](const ToggleContext &c) { return top_surface(c); },
         L("Available with top solid layers.")},
        {"default_acceleration", false,
         [](const ToggleContext &c) { return c.config.opt_float("default_acceleration") > 0; },
         L("Available with a default acceleration above 0; at 0 the firmware's acceleration is left alone.")},
        {"skirt", false, [](const ToggleContext &c) { return skirt(c); }, L("Available with at least one skirt loop.")},
        {"skirt_height", false,
         [](const ToggleContext &c) { return skirt(c) && c.config.opt_enum<DraftShield>("draft_shield") != dsEnabled; },
         L("Available with a skirt, while the draft shield is not enabled; an enabled draft shield sets the height "
           "itself.")},
        {"support", false, [](const ToggleContext &c) { return support(c); },
         L("Available when support material or a raft is enabled.")},
        {"support_auto", false, [](const ToggleContext &c) { return support_auto(c); },
         L("Available when supports are generated automatically; painted supports carry this in the paint.")},
        {"support_custom_top_gap", false,
         [](const ToggleContext &c)
         {
             return support(c) && !support_soluble(c) &&
                    c.config.opt_enum<SupportTopContactGap>("support_material_contact_distance") == stcgCustom;
         },
         L("Available with supports and a custom top contact Z distance.")},
        {"support_half_layer_gap", false,
         [](const ToggleContext &c)
         {
             return support(c) && c.config.opt_enum<SupportBottomContactGap>(
                                      "support_material_bottom_contact_distance") == sbcgHalfLayer;
         },
         L("Available with supports and a half-layer bottom contact Z distance.")},
        {"support_or_enforcers", false, [](const ToggleContext &c) { return support_or_enforcers(c); },
         L("Available when support material is enabled or support enforcers are painted.")},
        {"support_interface", false, [](const ToggleContext &c) { return support_interface(c); },
         L("Available with support material and interface layers.")},
        {"support_or_skirt", false, [](const ToggleContext &c) { return support(c) || skirt(c); },
         L("Available when support material, a raft or a skirt is enabled.")},
        {"raft_contact", false, [](const ToggleContext &c) { return raft(c) && !support_soluble(c); },
         L("Available with raft layers and a top contact Z distance above 0.")},
        {"raft", false, [](const ToggleContext &c) { return raft(c); }, L("Available with at least one raft layer.")},
        {"ironing", false, [](const ToggleContext &c) { return c.config.opt_bool("ironing"); },
         L("Available when ironing is enabled.")},
        {"ooze_prevention", false, [](const ToggleContext &c) { return c.config.opt_bool("ooze_prevention"); },
         L("Available when ooze prevention is enabled.")},
        {"wipe_tower", false, [](const ToggleContext &c) { return c.config.opt_bool("wipe_tower"); },
         L("Available when the wipe tower is enabled.")},
        {"no_avoid_crossing_perimeters", false,
         [](const ToggleContext &c) { return !c.config.opt_bool("avoid_crossing_perimeters"); },
         L("Disabled while Avoid crossing perimeters is active; the two are exclusive.")},
        {"no_avoid_crossing_curled", false,
         [](const ToggleContext &c) { return !c.config.opt_bool("avoid_crossing_curled_overhangs"); },
         L("Disabled while Avoid crossing curled overhangs is active; the two are exclusive.")},
        {"avoid_crossing_perimeters", false,
         [](const ToggleContext &c) { return c.config.opt_bool("avoid_crossing_perimeters"); },
         L("Available when Avoid crossing perimeters is enabled.")},
        {"arachne_or_athena", false, [](const ToggleContext &c)
         { return generator(c) == PerimeterGeneratorType::Arachne || generator(c) == PerimeterGeneratorType::Athena; },
         L("Available with the Arachne or Athena perimeter generator.")},
        {"arachne", false, [](const ToggleContext &c) { return generator(c) == PerimeterGeneratorType::Arachne; },
         L("Available with the Arachne perimeter generator; Athena keeps its widths fixed.")},
        {"athena", false, [](const ToggleContext &c) { return generator(c) == PerimeterGeneratorType::Athena; },
         L("Available with the Athena perimeter generator.")},
        {"scarf_seam", false,
         [](const ToggleContext &c)
         {
             return !spiral_vase(c) &&
                    c.config.opt_enum<ScarfSeamPlacement>("scarf_seam_placement") != ScarfSeamPlacement::nowhere;
         },
         L("Available with a scarf seam placement other than Nowhere, without spiral vase.")},
        {"interlocking_beam", false, [](const ToggleContext &c) { return interlocking_beam(c); },
         L("Available when interlocking beams are enabled.")},
        {"no_interlocking_beam", false, [](const ToggleContext &c) { return !interlocking_beam(c); },
         L("Disabled while interlocking beams are active; they replace the segmented regions.")},
        {"segmented_region", false, [](const ToggleContext &c)
         { return !interlocking_beam(c) && c.config.opt_float("mmu_segmented_region_max_width") > 0; },
         L("Available with a segmented region width above 0, while interlocking beams are not active.")},
        // Filament
        {"no_manual_fan", false, [](const ToggleContext &c) { return !manual_fan(c); },
         L("Disabled while manual fan speeds are active; they set the fan per feature.")},
        {"cooling", false, [](const ToggleContext &c) { return cooling(c); },
         L("Available when automatic cooling is enabled.")},
        {"fan_auto", false,
         [](const ToggleContext &c) { return (cooling(c) || c.config.opt_bool("fan_always_on", 0)) && !manual_fan(c); },
         L("Available when automatic cooling or the fan always on is enabled, while manual fan speeds are not "
           "active.")},
        {"manual_fan", false, [](const ToggleContext &c) { return manual_fan(c); },
         L("Available when manual fan speeds are enabled.")},
        {"dynamic_fan", false,
         [](const ToggleContext &c) { return !manual_fan(c) && c.config.opt_bool("enable_dynamic_fan_speeds", 0); },
         L("Available when dynamic fan speeds are enabled, while manual fan speeds are not active.")},
        {"cooling_consistent_surface", false,
         [](const ToggleContext &c)
         {
             // The edited preset holds the generic enum vector, read through getInts()
             return cooling(c) &&
                    static_cast<CoolingSlowdownLogicType>(c.config.option("cooling_slowdown_logic")->getInts().at(0)) ==
                        CoolingSlowdownLogicType::ConsistentSurface;
         },
         L("Available when automatic cooling uses the Consistent surface slowdown logic.")},
        {"multitool_ramming", false,
         [](const ToggleContext &c) { return c.config.opt_bool("filament_multitool_ramming", 0); },
         L("Available when multitool ramming is enabled.")},
        {"pressure_advance", false,
         [](const ToggleContext &c) { return c.config.opt_bool("filament_enable_pressure_advance", 0); },
         L("Available when pressure advance is enabled for this filament.")},
        // Printer
        {"multiple_extruders", false, [](const ToggleContext &c) { return c.extruders_count > 1; },
         L("Available with more than one extruder.")},
        {"marlin", false, [](const ToggleContext &c) { return marlin(c); },
         L("Available with a Marlin firmware flavor.")},
        {"machine_limits", false, [](const ToggleContext &c) { return machine_limits(c); },
         L("Available when the machine limits are in use, for the time estimate or emitted to G-code.")},
        {"machine_limits_min_feedrates", false, [](const ToggleContext &c) { return machine_limits(c) && marlin(c); },
         L("Available when the machine limits are in use, with a Marlin firmware flavor; only Marlin has minimum "
           "feedrates.")},
        {"machine_limits_travel_acceleration", false,
         [](const ToggleContext &c)
         {
             return machine_limits(c) &&
                    (flavor(c) == gcfMarlinFirmware || flavor(c) == gcfRepRapFirmware || flavor(c) == gcfRapid);
         },
         L("Available when the machine limits are in use, with a firmware that has a travel acceleration (Marlin 2, "
           "RepRapFirmware, Rapid).")},
        {"no_firmware_retraction", true, [](const ToggleContext &c) { return !firmware_retraction(c); },
         L("Disabled while firmware retraction is active; the firmware performs the retraction itself.")},
        {"no_ramping_lift", true, [](const ToggleContext &c) { return !ramping_lift(c); },
         L("Disabled while ramping lift is active; it replaces the fixed lift.")},
        {"ramping_lift", true, [](const ToggleContext &c) { return ramping_lift(c); },
         L("Available when ramping lift is enabled.")},
        {"retraction", true, [](const ToggleContext &c) { return retraction(c); },
         L("Available with a retraction length above 0 or firmware retraction.")},
        {"lift", true,
         [](const ToggleContext &c)
         {
             return (ramping_lift(c) && c.config.opt_float("travel_max_lift", c.extruder) > 0) ||
                    (!ramping_lift(c) && c.config.opt_float("retract_lift", c.extruder) > 0);
         },
         L("Available with a lift: a retract lift above 0, or ramping lift with a maximum lift above 0.")},
        {"retraction_in_slicer", true, [](const ToggleContext &c) { return retraction(c) && !firmware_retraction(c); },
         L("Available with a retraction length above 0, while firmware retraction is not active.")},
        {"wipe", true, [](const ToggleContext &c) { return extruder_bool(c, "wipe") && !firmware_retraction(c); },
         L("Available when wipe while retracting is enabled, while firmware retraction is not active.")},
        {"toolchange_retraction", true, [](const ToggleContext &c)
         { return c.extruders_count > 1 && c.config.opt_float("retract_length_toolchange", c.extruder) > 0; },
         L("Available with more than one extruder and a retraction length above 0 when the tool is disabled.")},
    };
    return rules;
}

const ToggleRule *find_toggle_rule(const std::string &id)
{
    for (const ToggleRule &rule : toggle_rules())
        if (id == rule.id)
            return &rule;
    return nullptr;
}

std::vector<ToggleState> apply_toggle_rules(unsigned preset, const DynamicPrintConfig &config, size_t extruders_count)
{
    std::vector<ToggleState> states;
    for (const SettingRow &row : setting_rows())
    {
        if ((row.presets & preset) == 0 || row.toggle_rule == nullptr)
            continue;
        const ToggleRule *rule = find_toggle_rule(row.toggle_rule);
        if (rule == nullptr)
            continue; // the check reports it
        if (rule->per_extruder)
        {
            for (size_t i = 0; i < extruders_count; ++i)
            {
                const bool enabled = rule->enabled({config, int(i), extruders_count});
                states.push_back({row.key, int(i), enabled, enabled ? nullptr : rule->reason});
            }
            continue;
        }
        const bool enabled = rule->enabled({config, -1, extruders_count});
        states.push_back({row.key, -1, enabled, enabled ? nullptr : rule->reason});
        // A row repeated per extruder under a rule that does not read the extruder gets the same
        // state for each extruder too, so a surface that registers the repeats finds them
        if (row.extruder_indexed)
            for (size_t i = 0; i < extruders_count; ++i)
                states.push_back({row.key, int(i), enabled, enabled ? nullptr : rule->reason});
    }
    return states;
}

const char *scope_reason_name(ScopeReason reason)
{
    switch (reason)
    {
    case ScopeReason::Plate:
        return "PLATE";
    case ScopeReason::Machine:
        return "MACHINE";
    case ScopeReason::Material:
        return "MATERIAL";
    case ScopeReason::File:
        return "FILE";
    case ScopeReason::Stream:
        return "STREAM";
    case ScopeReason::Travel:
        return "TRAVEL";
    case ScopeReason::LayerTime:
        return "LAYERTIME";
    case ScopeReason::Mmu:
        return "MMU";
    case ScopeReason::Editor:
        return "EDITOR";
    case ScopeReason::Engine:
        return "ENGINE";
    case ScopeReason::Ir:
        return "IR";
    case ScopeReason::None:
        break;
    }
    return "";
}

bool overridable_at(OverrideScope scope, const std::string &key)
{
    static const std::set<std::string> region_keys = []()
    {
        std::set<std::string> s;
        for (const std::string &k : PrintRegionConfig().keys())
            s.insert(k);
        return s;
    }();
    if (region_keys.count(key) != 0)
        return true;
    return scope == OverrideScope::Object && is_object_level_key(key);
}

std::vector<std::string> setting_preset_keys(unsigned preset)
{
    std::vector<std::string> keys;
    for (const SettingRow &row : setting_rows())
        if ((row.presets & preset) != 0)
            keys.emplace_back(row.key);
    for (const NoUiKey &key : no_ui_keys())
        if ((key.presets & preset) != 0)
            keys.emplace_back(key.key);
    return keys;
}

bool is_object_level_key(const std::string &key)
{
    static const std::set<std::string> keys = []()
    {
        std::set<std::string> s;
        for (const std::string &k : PrintObjectConfig().keys())
            s.insert(k);
        for (const std::string &k : PrintRegionConfig().keys())
            s.insert(k);
        return s;
    }();
    return keys.count(key) != 0;
}

// The print, filament and printer lists come from the row specification: every key with a
// row or a no-UI entry for the preset, so a new setting is one spec row and nothing else.
// They are declared on Preset and defined here, beside the specification they read.
const std::vector<std::string> &Preset::print_options()
{
    static const std::vector<std::string> s_opts = setting_preset_keys(SettingPresetPrint);
    return s_opts;
}
const std::vector<std::string> &Preset::filament_options()
{
    static const std::vector<std::string> s_opts = setting_preset_keys(SettingPresetFilament);
    return s_opts;
}
const std::vector<std::string> &Preset::printer_options()
{
    static const std::vector<std::string> s_opts = setting_preset_keys(SettingPresetPrinter);
    return s_opts;
}

unsigned setting_preset_bit(Preset::Type type)
{
    switch (type)
    {
    case Preset::TYPE_PRINT:
        return SettingPresetPrint;
    case Preset::TYPE_FILAMENT:
        return SettingPresetFilament;
    case Preset::TYPE_PRINTER:
        return SettingPresetPrinter;
    default:
        return 0;
    }
}

const SettingRow *find_setting_row(const std::string &key)
{
    for (const SettingRow &row : setting_rows())
        if (key == row.key)
            return &row;
    return nullptr;
}

const SettingLine *find_setting_line(const SettingRow &row)
{
    if (row.line == nullptr || row.page == nullptr || row.group == nullptr)
        return nullptr;
    for (const SettingLine &line : setting_lines())
        if ((line.presets & row.presets) != 0 && std::string(line.page) == row.page &&
            std::string(line.group) == row.group && std::string(line.label) == row.line)
            return &line;
    return nullptr;
}

// Every check names what is wrong; the count is the exit status of the console flag.
int check_settings_spec(std::ostream &report)
{
    int failures = 0;
    const auto fail = [&](const std::string &what)
    {
        report << "settings spec: " << what << '\n';
        ++failures;
    };
    std::map<std::string, int> seen;
    for (const SettingRow &row : setting_rows())
    {
        ++seen[row.key];
        if (print_config_def.get(row.key) == nullptr)
            fail(std::string("row '") + row.key + "' names a key the definition does not have");
        if (row.page == nullptr && row.sidebar_page == nullptr)
            fail(std::string("row '") + row.key + "' is on no surface");
        if (row.page != nullptr && (row.group == nullptr || *row.group == '\0'))
            fail(std::string("row '") + row.key + "' has a page but no group");
        if (row.widget == SettingWidget::Custom && row.custom == nullptr)
            fail(std::string("row '") + row.key + "' is Custom without a builder id");
        if (row.presets == 0)
            fail(std::string("row '") + row.key + "' belongs to no preset list");
    }
    for (const NoUiKey &key : no_ui_keys())
    {
        ++seen[key.key];
        if (print_config_def.get(key.key) == nullptr)
            fail(std::string("no-ui key '") + key.key + "' names a key the definition does not have");
        if (key.reason == nullptr || *key.reason == '\0')
            fail(std::string("no-ui key '") + key.key + "' has no reason");
    }
    for (const auto &[key, count] : seen)
        if (count > 1)
            fail("key '" + key + "' appears " + std::to_string(count) + " times");
    // Coverage: the preset lists are generated from the rows and the no-ui keys, so every key of
    // the FFF static configs must be in one of them, unless it is a named key no preset carries.
    static const std::set<std::string> preset_free = {
        "wipe_into_objects",                // an object's own override, never a preset value
        "wipe_into_infill",                 // an object's own override, never a preset value
        "wiping_volumes_matrix",            // held by the project config, never by a preset
        "wiping_volumes_use_custom_matrix", // held by the project config, never by a preset
        "extrusion_axis",                   // fixed to E, not offered
        "duplicate_distance",               // a command-line arrangement parameter
        "colorprint_heights",               // the legacy colour-change heights of a project
    };
    for (const std::string &key : FullPrintConfig().keys())
        if (seen.count(key) == 0 && preset_free.count(key) == 0)
            fail("engine key '" + key + "' is in no row and no no-ui entry, so in no preset list");
    for (const std::string &key : preset_free)
        if (seen.count(key) != 0)
            fail("preset-free key '" + key + "' has a row or a no-ui entry");
    // The scope: every print-level key of the three lists has a reason on its definition, no
    // object-level key has one.
    for (const unsigned bit :
         {unsigned(SettingPresetPrint), unsigned(SettingPresetFilament), unsigned(SettingPresetPrinter)})
        for (const std::string &key : setting_preset_keys(bit))
        {
            const ConfigOptionDef *def = print_config_def.get(key);
            if (def == nullptr)
                continue; // reported above
            const bool object_level = is_object_level_key(key);
            if (!object_level && def->scope_reason == ScopeReason::None)
                fail("print-level key '" + key +
                     "' has no scope reason (PLATE, MACHINE, MATERIAL, FILE, STREAM, "
                     "TRAVEL, LAYERTIME, MMU, EDITOR, ENGINE or IR)");
            else if (object_level && def->scope_reason != ScopeReason::None)
                fail("per-object key '" + key + "' carries the scope reason " + scope_reason_name(def->scope_reason));
        }
    // The invalidation tables: every object-level engine key has a row in the object table and
    // every other engine key of the FFF configs one in the print table; a key without a row
    // would fall on a catch-all.
    {
        const std::vector<std::string> object_rows = object_key_invalidation_keys();
        const std::vector<std::string> print_rows = print_key_invalidation_keys();
        const std::set<std::string> object_set(object_rows.begin(), object_rows.end());
        const std::set<std::string> print_set(print_rows.begin(), print_rows.end());
        for (const std::string &key : FullPrintConfig().keys())
        {
            if (is_object_level_key(key))
            {
                if (object_set.count(key) == 0)
                    fail("object key '" + key + "' has no row in the object invalidation table");
            }
            else if (print_set.count(key) == 0)
                fail("print key '" + key + "' has no row in the print invalidation table");
        }
    }
    // The page and line tables: every page a row names exists for the row's preset, every
    // composite line a row sits on has its entry (page, group, label).
    for (const SettingRow &row : setting_rows())
    {
        if (row.page == nullptr)
            continue;
        const bool page_known =
            std::any_of(setting_pages().begin(), setting_pages().end(), [&row](const SettingPage &page)
                        { return (page.presets & row.presets) != 0 && std::string(page.title) == row.page; });
        if (!page_known)
            fail(std::string("row '") + row.key + "' names page '" + row.page + "' the page table does not have");
        if (row.line != nullptr && find_setting_line(row) == nullptr)
            fail(std::string("row '") + row.key + "' sits on line '" + row.line + "' the line table does not have");
    }
    // The rule table: every rule a row names exists, has a reason, and every rule is named by
    // at least one row.
    std::set<std::string> named_rules;
    for (const SettingRow &row : setting_rows())
    {
        if (row.toggle_rule == nullptr)
            continue;
        named_rules.insert(row.toggle_rule);
        if (find_toggle_rule(row.toggle_rule) == nullptr)
            fail(std::string("row '") + row.key + "' names rule '" + row.toggle_rule +
                 "' the rule table does not have");
    }
    std::map<std::string, int> rule_ids;
    for (const ToggleRule &rule : toggle_rules())
    {
        ++rule_ids[rule.id];
        if (rule.reason == nullptr || *rule.reason == '\0')
            fail(std::string("rule '") + rule.id + "' has no reason");
        if (rule.enabled == nullptr)
            fail(std::string("rule '") + rule.id + "' has no predicate");
        if (named_rules.count(rule.id) == 0)
            fail(std::string("rule '") + rule.id + "' is named by no row");
    }
    for (const auto &[id, count] : rule_ids)
        if (count > 1)
            fail("rule '" + id + "' appears " + std::to_string(count) + " times");
    return failures;
}

} // namespace Luminary
