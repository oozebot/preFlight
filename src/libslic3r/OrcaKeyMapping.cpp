///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OrcaKeyMapping.hpp"

#include <algorithm>

namespace Slic3r
{

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------

OrcaKeyMapper &OrcaKeyMapper::instance()
{
    static OrcaKeyMapper s_instance;
    return s_instance;
}

OrcaKeyMapper::OrcaKeyMapper()
{
    build_printer_mappings();
    build_filament_mappings();
    build_process_mappings();
}

// -----------------------------------------------------------------------
// Public query API
// -----------------------------------------------------------------------

const OrcaKeyEntry *OrcaKeyMapper::find_entry(const std::string &orca_key, Preset::Type preset_type) const
{
    auto it = m_mappings.find(preset_type);
    if (it == m_mappings.end())
        return nullptr;
    for (const auto &e : it->second)
        if (e.orca_key == orca_key)
            return &e;
    return nullptr;
}

std::pair<std::string, std::string> OrcaKeyMapper::map_key_value(const std::string &orca_key,
                                                                 const std::string &orca_value,
                                                                 Preset::Type preset_type) const
{
    const OrcaKeyEntry *entry = find_entry(orca_key, preset_type);
    if (!entry || entry->type == OrcaKeyEntry::MapType::OrcaOnly || entry->type == OrcaKeyEntry::MapType::Ignored)
        return {"", ""};

    const std::string &pf_key = entry->preflight_key.empty() ? entry->orca_key : entry->preflight_key;
    std::string pf_val = entry->transform ? entry->transform(orca_value) : orca_value;
    return {pf_key, pf_val};
}

bool OrcaKeyMapper::is_ignored(const std::string &orca_key, Preset::Type preset_type) const
{
    const OrcaKeyEntry *entry = find_entry(orca_key, preset_type);
    return entry && entry->type == OrcaKeyEntry::MapType::Ignored;
}

bool OrcaKeyMapper::is_orca_only(const std::string &orca_key, Preset::Type preset_type) const
{
    const OrcaKeyEntry *entry = find_entry(orca_key, preset_type);
    return entry && entry->type == OrcaKeyEntry::MapType::OrcaOnly;
}

std::vector<std::string> OrcaKeyMapper::get_orca_only_keys(Preset::Type preset_type) const
{
    std::vector<std::string> result;
    auto it = m_mappings.find(preset_type);
    if (it == m_mappings.end())
        return result;
    for (const auto &e : it->second)
        if (e.type == OrcaKeyEntry::MapType::OrcaOnly)
            result.push_back(e.orca_key);
    return result;
}

// -----------------------------------------------------------------------
// Value transformation functions
// -----------------------------------------------------------------------

std::string OrcaKeyMapper::transform_infill_pattern(const std::string &value)
{
    if (value == "zig-zag")
        return "rectilinear";
    return value; // All other names match
}

std::string OrcaKeyMapper::transform_seam_position(const std::string &value)
{
    if (value == "back")
        return "rear";
    return value; // nearest, aligned, random match
}

std::string OrcaKeyMapper::transform_support_style(const std::string &value)
{
    if (value == "default")
        return "grid";
    return value; // grid, snug, tree, organic match
}

std::string OrcaKeyMapper::transform_brim_type(const std::string &value)
{
    if (value == "auto_brim")
        return "outer_only";
    return value; // no_brim, outer_only, inner_only, outer_and_inner match
}

std::string OrcaKeyMapper::transform_wall_sequence_to_bool(const std::string &value)
{
    // Orca: "outer wall first" / "inner wall first" / "inner-outer-inner wall"
    // preFlight: external_perimeters_first = 0/1
    if (value == "outer wall first" || value == "outer_wall_first")
        return "1";
    return "0";
}

std::string OrcaKeyMapper::transform_print_sequence_to_bool(const std::string &value)
{
    // Orca: "by layer" / "by object"
    // preFlight: complete_objects = 0/1
    if (value == "by object" || value == "by_object")
        return "1";
    return "0";
}

std::string OrcaKeyMapper::transform_only_one_wall_top(const std::string &value)
{
    // Orca: bool (0/1) -> preFlight: top_one_perimeter_type enum
    // 1 -> "top_surfaces", 0 -> "none"
    if (value == "1" || value == "true")
        return "top_surfaces";
    return "none";
}

std::string OrcaKeyMapper::transform_emit_machine_limits(const std::string &value)
{
    // Orca: bool (0/1) -> preFlight: machine_limits_usage enum
    // 1 -> "emit_to_gcode", 0 -> "time_estimate_only"
    if (value == "1" || value == "true")
        return "emit_to_gcode";
    return "time_estimate_only";
}

std::string OrcaKeyMapper::transform_wall_generator(const std::string &value)
{
    // Orca: "arachne" / "classic" -> preFlight: "Arachne" / "Classic"
    if (value == "arachne")
        return "Arachne";
    if (value == "classic")
        return "Classic";
    return value;
}

std::string OrcaKeyMapper::transform_gcode_label_objects(const std::string &value)
{
    // Orca: "1"/"0" or other -> preFlight: "octoprint"/"disabled"
    if (value == "1" || value == "true")
        return "octoprint";
    return "disabled";
}

std::string OrcaKeyMapper::transform_fuzzy_skin(const std::string &value)
{
    // Orca and preFlight share "none", "external", "all" - pass through
    return value;
}

// -----------------------------------------------------------------------
// Helper macros for building mapping tables concisely
// -----------------------------------------------------------------------

#define DIRECT(k) {k, k, OrcaKeyEntry::MapType::Direct, nullptr}
#define RENAMED(ok, pk) {ok, pk, OrcaKeyEntry::MapType::Renamed, nullptr}
#define XFORM(ok, pk, fn) {ok, pk, OrcaKeyEntry::MapType::Transformed, fn}
#define ORCA_ONLY(k) {k, "", OrcaKeyEntry::MapType::OrcaOnly, nullptr}
#define SKIP(k) {k, "", OrcaKeyEntry::MapType::Ignored, nullptr}

// -----------------------------------------------------------------------
// Printer settings mappings
// -----------------------------------------------------------------------

void OrcaKeyMapper::build_printer_mappings()
{
    m_mappings[Preset::TYPE_PRINTER] = {
        // Metadata - skip
        SKIP("name"),
        SKIP("inherits"),
        SKIP("version"),
        SKIP("from"),
        SKIP("is_custom_defined"),
        SKIP("setting_id"),

        // Direct mappings
        DIRECT("printer_technology"),
        DIRECT("printer_model"),
        DIRECT("printer_variant"),
        DIRECT("nozzle_diameter"),
        DIRECT("retract_lift_above"),
        DIRECT("retract_lift_below"),
        DIRECT("retract_before_wipe"),
        DIRECT("wipe"),
        DIRECT("use_firmware_retraction"),
        DIRECT("use_relative_e_distances"),
        DIRECT("machine_max_acceleration_x"),
        DIRECT("machine_max_acceleration_y"),
        DIRECT("machine_max_acceleration_z"),
        DIRECT("machine_max_acceleration_e"),
        DIRECT("machine_max_jerk_x"),
        DIRECT("machine_max_jerk_y"),
        DIRECT("machine_max_jerk_z"),
        DIRECT("machine_max_jerk_e"),
        DIRECT("machine_max_acceleration_extruding"),
        DIRECT("machine_max_acceleration_retracting"),
        DIRECT("machine_max_acceleration_travel"),
        DIRECT("machine_min_extruding_rate"),
        DIRECT("machine_min_travel_rate"),
        DIRECT("machine_max_junction_deviation"),
        DIRECT("silent_mode"),
        DIRECT("thumbnails"),
        DIRECT("thumbnails_format"),
        DIRECT("host_type"),
        DIRECT("extruder_colour"),
        DIRECT("extruder_offset"),
        DIRECT("single_extruder_multi_material"),
        DIRECT("cooling_tube_length"),
        DIRECT("cooling_tube_retraction"),
        DIRECT("parking_pos_retraction"),
        DIRECT("high_current_on_filament_swap"),
        DIRECT("extra_loading_move"),
        DIRECT("bed_custom_model"),
        DIRECT("bed_custom_texture"),
        DIRECT("default_filament_profile"),
        DIRECT("default_print_profile"),
        DIRECT("extruder_clearance_radius"),
        DIRECT("retract_length_toolchange"),
        DIRECT("retract_restart_extra"),
        DIRECT("retract_restart_extra_toolchange"),
        DIRECT("max_layer_height"),
        DIRECT("min_layer_height"),
        DIRECT("printer_notes"),
        DIRECT("printer_settings_id"),
        DIRECT("z_offset"),
        DIRECT("printhost_authorization_type"),
        DIRECT("printhost_ssl_ignore_revoke"),
        DIRECT("template_custom_gcode"),

        // Renamed mappings
        RENAMED("printable_height", "max_print_height"),
        RENAMED("machine_start_gcode", "start_gcode"),
        RENAMED("machine_end_gcode", "end_gcode"),
        RENAMED("before_layer_change_gcode", "before_layer_gcode"),
        RENAMED("layer_change_gcode", "layer_gcode"),
        RENAMED("change_filament_gcode", "toolchange_gcode"),
        RENAMED("retraction_length", "retract_length"),
        RENAMED("retraction_speed", "retract_speed"),
        RENAMED("retraction_minimum_travel", "retract_before_travel"),
        RENAMED("z_hop", "retract_lift"),
        RENAMED("wipe_distance", "wipe_length"),
        RENAMED("deretraction_speed", "deretract_speed"),
        RENAMED("machine_max_speed_x", "machine_max_feedrate_x"),
        RENAMED("machine_max_speed_y", "machine_max_feedrate_y"),
        RENAMED("machine_max_speed_z", "machine_max_feedrate_z"),
        RENAMED("machine_max_speed_e", "machine_max_feedrate_e"),
        RENAMED("retract_when_changing_layer", "retract_layer_change"),
        RENAMED("extruder_clearance_height_to_rod", "extruder_clearance_height"),

        // Transformed mappings
        XFORM("gcode_flavor", "gcode_flavor", nullptr), // values match for common flavors
        XFORM("printable_area", "bed_shape", nullptr),  // handled specially in importer (JSON array -> CSV)
        XFORM("emit_machine_limits_to_gcode", "machine_limits_usage", transform_emit_machine_limits),

        // Orca-only (no preFlight equivalent)
        ORCA_ONLY("nozzle_type"),
        ORCA_ONLY("nozzle_hrc"),
        ORCA_ONLY("nozzle_volume"),
        ORCA_ONLY("nozzle_height"),
        ORCA_ONLY("auxiliary_fan"),
        ORCA_ONLY("fan_kickstart"),
        ORCA_ONLY("fan_speedup_time"),
        ORCA_ONLY("fan_speedup_overhangs"),
        ORCA_ONLY("support_chamber_temp_control"),
        ORCA_ONLY("support_air_filtration"),
        ORCA_ONLY("support_multi_bed_types"),
        ORCA_ONLY("scan_first_layer"),
        ORCA_ONLY("bed_mesh_min"),
        ORCA_ONLY("bed_mesh_max"),
        ORCA_ONLY("bed_mesh_probe_distance"),
        ORCA_ONLY("adaptive_bed_mesh_margin"),
        ORCA_ONLY("time_lapse_gcode"),
        ORCA_ONLY("head_wrap_detect_zone"),
        ORCA_ONLY("pellet_modded_printer"),
        ORCA_ONLY("z_hop_types"),
        ORCA_ONLY("travel_slope"),
        ORCA_ONLY("long_retractions_when_cut"),
        ORCA_ONLY("retraction_distances_when_cut"),
        ORCA_ONLY("resonance_avoidance"),
        ORCA_ONLY("min_resonance_avoidance_speed"),
        ORCA_ONLY("max_resonance_avoidance_speed"),
        ORCA_ONLY("bbl_use_printhost"),
        ORCA_ONLY("best_object_pos"),
        ORCA_ONLY("preferred_orientation"),
        ORCA_ONLY("change_extrusion_role_gcode"),
        ORCA_ONLY("machine_pause_gcode"),
        ORCA_ONLY("printing_by_object_gcode"),
        ORCA_ONLY("default_bed_type"),
        ORCA_ONLY("disable_m73"),
        ORCA_ONLY("enable_filament_ramming"),
        ORCA_ONLY("enable_long_retraction_when_cut"),
        ORCA_ONLY("extruder_clearance_height_to_lid"),
        ORCA_ONLY("machine_load_filament_time"),
        ORCA_ONLY("machine_unload_filament_time"),
        ORCA_ONLY("machine_tool_change_time"),
        ORCA_ONLY("manual_filament_change"),
        ORCA_ONLY("printer_structure"),
        ORCA_ONLY("purge_in_prime_tower"),
        ORCA_ONLY("time_cost"),
        ORCA_ONLY("upward_compatible_machine"),
    };
}

// -----------------------------------------------------------------------
// Filament settings mappings
// -----------------------------------------------------------------------

void OrcaKeyMapper::build_filament_mappings()
{
    m_mappings[Preset::TYPE_FILAMENT] = {
        // Metadata - skip
        SKIP("name"),
        SKIP("inherits"),
        SKIP("version"),
        SKIP("from"),
        SKIP("is_custom_defined"),
        SKIP("setting_id"),
        SKIP("filament_id"),

        // Direct mappings
        DIRECT("filament_type"),
        DIRECT("filament_cost"),
        DIRECT("filament_density"),
        DIRECT("filament_diameter"),
        DIRECT("filament_max_volumetric_speed"),
        DIRECT("full_fan_speed_layer"),
        DIRECT("filament_wipe"),
        DIRECT("filament_ramming_parameters"),
        DIRECT("filament_loading_speed"),
        DIRECT("filament_loading_speed_start"),
        DIRECT("filament_unloading_speed"),
        DIRECT("filament_unloading_speed_start"),
        DIRECT("filament_cooling_moves"),
        DIRECT("filament_cooling_initial_speed"),
        DIRECT("filament_cooling_final_speed"),
        DIRECT("compatible_printers"),
        DIRECT("compatible_printers_condition"),
        DIRECT("compatible_prints"),
        DIRECT("compatible_prints_condition"),
        DIRECT("idle_temperature"),
        DIRECT("chamber_temperature"),
        DIRECT("filament_notes"),
        DIRECT("filament_soluble"),
        DIRECT("filament_minimal_purge_on_wipe_tower"),
        DIRECT("filament_retract_before_wipe"),
        DIRECT("filament_retract_lift_above"),
        DIRECT("filament_retract_lift_below"),
        DIRECT("filament_retract_restart_extra"),
        DIRECT("filament_toolchange_delay"),
        DIRECT("filament_settings_id"),
        DIRECT("filament_multitool_ramming"),
        DIRECT("filament_multitool_ramming_flow"),
        DIRECT("filament_multitool_ramming_volume"),

        // Renamed mappings
        RENAMED("filament_flow_ratio", "extrusion_multiplier"),
        RENAMED("nozzle_temperature", "temperature"),
        RENAMED("nozzle_temperature_initial_layer", "first_layer_temperature"),
        RENAMED("fan_min_speed", "min_fan_speed"),
        RENAMED("fan_max_speed", "max_fan_speed"),
        RENAMED("fan_cooling_layer_time", "fan_below_layer_time"),
        RENAMED("slow_down_layer_time", "slowdown_below_layer_time"),
        RENAMED("slow_down_min_speed", "min_print_speed"),
        RENAMED("close_fan_the_first_x_layers", "disable_fan_first_layers"),
        RENAMED("overhang_fan_speed", "bridge_fan_speed"),
        RENAMED("filament_start_gcode", "start_filament_gcode"),
        RENAMED("filament_end_gcode", "end_filament_gcode"),
        RENAMED("filament_retraction_length", "filament_retract_length"),
        RENAMED("filament_retraction_speed", "filament_retract_speed"),
        RENAMED("filament_deretraction_speed", "filament_deretract_speed"),
        RENAMED("filament_z_hop", "filament_retract_lift"),
        RENAMED("filament_retract_when_changing_layer", "filament_retract_layer_change"),
        RENAMED("filament_retraction_minimum_travel", "filament_retract_before_travel"),
        RENAMED("filament_wipe_distance", "filament_wipe_length"),
        RENAMED("default_filament_colour", "filament_colour"),
        RENAMED("filament_shrink", "filament_shrinkage_compensation_x"),

        // Bed temperature keys are handled specially in the importer (lossy mapping).
        // Register them as OrcaOnly here so they go into the "dropped" bucket by default;
        // the importer will pull values directly from the JSON before that happens.
        ORCA_ONLY("cool_plate_temp"),
        ORCA_ONLY("cool_plate_temp_initial_layer"),
        ORCA_ONLY("eng_plate_temp"),
        ORCA_ONLY("eng_plate_temp_initial_layer"),
        ORCA_ONLY("hot_plate_temp"),
        ORCA_ONLY("hot_plate_temp_initial_layer"),
        ORCA_ONLY("textured_plate_temp"),
        ORCA_ONLY("textured_plate_temp_initial_layer"),
        ORCA_ONLY("supertack_plate_temp"),
        ORCA_ONLY("supertack_plate_temp_initial_layer"),
        ORCA_ONLY("textured_cool_plate_temp"),
        ORCA_ONLY("textured_cool_plate_temp_initial_layer"),

        // Orca-only (no preFlight equivalent)
        ORCA_ONLY("filament_vendor"),
        ORCA_ONLY("pressure_advance"),
        ORCA_ONLY("enable_pressure_advance"),
        ORCA_ONLY("adaptive_pressure_advance"),
        ORCA_ONLY("adaptive_pressure_advance_bridges"),
        ORCA_ONLY("adaptive_pressure_advance_model"),
        ORCA_ONLY("adaptive_pressure_advance_overhangs"),
        ORCA_ONLY("temperature_vitrification"),
        ORCA_ONLY("required_nozzle_HRC"),
        ORCA_ONLY("nozzle_temperature_range_low"),
        ORCA_ONLY("nozzle_temperature_range_high"),
        ORCA_ONLY("enable_overhang_bridge_fan"),
        ORCA_ONLY("overhang_fan_threshold"),
        ORCA_ONLY("activate_chamber_temp_control"),
        ORCA_ONLY("activate_air_filtration"),
        ORCA_ONLY("during_print_exhaust_fan_speed"),
        ORCA_ONLY("complete_print_exhaust_fan_speed"),
        ORCA_ONLY("additional_cooling_fan_speed"),
        ORCA_ONLY("filament_is_support"),
        ORCA_ONLY("filament_long_retractions_when_cut"),
        ORCA_ONLY("filament_retract_lift_enforce"),
        ORCA_ONLY("filament_retraction_distances_when_cut"),
        ORCA_ONLY("filament_z_hop_types"),
        ORCA_ONLY("ironing_fan_speed"),
        ORCA_ONLY("reduce_fan_stop_start_freq"),
        ORCA_ONLY("slow_down_for_layer_cooling"),
        ORCA_ONLY("support_material_interface_fan_speed"),
        ORCA_ONLY("filament_shrinkage_compensation_z"),
        ORCA_ONLY("pellet_flow_coefficient"),
        ORCA_ONLY("filament_stamping_distance"),
        ORCA_ONLY("filament_stamping_loading_speed"),
        ORCA_ONLY("dont_slow_down_outer_wall"),
        ORCA_ONLY("internal_bridge_fan_speed"),
    };
}

// -----------------------------------------------------------------------
// Process / print settings mappings
// -----------------------------------------------------------------------

void OrcaKeyMapper::build_process_mappings()
{
    m_mappings[Preset::TYPE_PRINT] = {
        // Metadata - skip
        SKIP("name"),
        SKIP("inherits"),
        SKIP("version"),
        SKIP("from"),
        SKIP("is_custom_defined"),
        SKIP("setting_id"),

        // Direct mappings
        DIRECT("layer_height"),
        DIRECT("small_perimeter_speed"),
        DIRECT("travel_speed"),
        DIRECT("bridge_speed"),
        DIRECT("brim_width"),
        DIRECT("skirt_distance"),
        DIRECT("skirt_height"),
        DIRECT("raft_layers"),
        DIRECT("ironing"),
        DIRECT("ironing_type"),
        DIRECT("ironing_speed"),
        DIRECT("ironing_spacing"),
        DIRECT("fuzzy_skin_thickness"),
        DIRECT("fuzzy_skin_point_dist"),
        DIRECT("elefant_foot_compensation"),
        DIRECT("gcode_comments"),
        DIRECT("print_settings_id"),
        DIRECT("only_one_perimeter_first_layer"),

        // Renamed mappings
        RENAMED("initial_layer_print_height", "first_layer_height"),
        RENAMED("wall_loops", "perimeters"),
        RENAMED("top_shell_layers", "top_solid_layers"),
        RENAMED("bottom_shell_layers", "bottom_solid_layers"),
        RENAMED("top_shell_thickness", "top_solid_min_thickness"),
        RENAMED("bottom_shell_thickness", "bottom_solid_min_thickness"),
        RENAMED("sparse_infill_density", "fill_density"),
        RENAMED("inner_wall_line_width", "perimeter_extrusion_width"),
        RENAMED("outer_wall_line_width", "external_perimeter_extrusion_width"),
        RENAMED("top_surface_line_width", "top_infill_extrusion_width"),
        RENAMED("sparse_infill_line_width", "infill_extrusion_width"),
        RENAMED("internal_solid_infill_line_width", "solid_infill_extrusion_width"),
        RENAMED("support_line_width", "support_material_extrusion_width"),
        RENAMED("initial_layer_line_width", "first_layer_extrusion_width"),
        RENAMED("inner_wall_speed", "perimeter_speed"),
        RENAMED("outer_wall_speed", "external_perimeter_speed"),
        RENAMED("sparse_infill_speed", "infill_speed"),
        RENAMED("internal_solid_infill_speed", "solid_infill_speed"),
        RENAMED("top_surface_speed", "top_solid_infill_speed"),
        RENAMED("gap_infill_speed", "gap_fill_speed"),
        RENAMED("initial_layer_speed", "first_layer_speed"),
        RENAMED("initial_layer_infill_speed", "first_layer_infill_speed"),
        RENAMED("internal_bridge_speed", "over_bridge_speed"),
        RENAMED("enable_support", "support_material"),
        RENAMED("support_threshold_angle", "support_material_threshold"),
        RENAMED("support_on_build_plate_only", "support_material_buildplate_only"),
        RENAMED("support_base_pattern", "support_material_pattern"),
        RENAMED("support_interface_pattern", "support_material_interface_pattern"),
        RENAMED("support_top_z_distance", "support_material_contact_distance"),
        RENAMED("support_bottom_z_distance", "support_material_bottom_contact_distance"),
        RENAMED("support_interface_top_layers", "support_material_interface_layers"),
        RENAMED("support_interface_bottom_layers", "support_material_bottom_interface_layers"),
        RENAMED("support_object_xy_distance", "support_material_xy_spacing"),
        RENAMED("brim_object_gap", "brim_separation"),
        RENAMED("skirt_loops", "skirts"),
        RENAMED("ironing_flow", "ironing_flowrate"),
        RENAMED("enable_prime_tower", "wipe_tower"),
        RENAMED("prime_tower_width", "wipe_tower_width"),
        RENAMED("prime_tower_brim_width", "wipe_tower_brim_width"),
        RENAMED("flush_into_infill", "wipe_into_infill"),
        RENAMED("flush_into_support", "wipe_into_objects"),
        RENAMED("xy_contour_compensation", "xy_size_compensation"),
        RENAMED("reduce_infill_retraction", "only_retract_when_crossing_perimeters"),
        RENAMED("enable_arc_fitting", "arc_fitting"),
        RENAMED("spiral_mode", "spiral_vase"),
        RENAMED("enable_overhang_speed", "enable_dynamic_overhang_speeds"),

        // Tree support renamed keys
        RENAMED("tree_support_branch_angle", "support_tree_angle"),
        RENAMED("tree_support_branch_diameter", "support_tree_branch_diameter"),
        RENAMED("tree_support_branch_distance", "support_tree_top_rate"),
        RENAMED("tree_support_wall_count", "support_tree_tip_diameter"),

        // Transformed mappings
        XFORM("sparse_infill_pattern", "fill_pattern", transform_infill_pattern),
        XFORM("top_surface_pattern", "top_fill_pattern", transform_infill_pattern),
        XFORM("bottom_surface_pattern", "bottom_fill_pattern", transform_infill_pattern),
        XFORM("seam_position", "seam_position", transform_seam_position),
        XFORM("support_style", "support_material_style", transform_support_style),
        XFORM("support_type", "support_material_style", transform_support_style),
        XFORM("brim_type", "brim_type", transform_brim_type),
        XFORM("wall_sequence", "external_perimeters_first", transform_wall_sequence_to_bool),
        XFORM("print_sequence", "complete_objects", transform_print_sequence_to_bool),
        XFORM("only_one_wall_top", "top_one_perimeter_type", transform_only_one_wall_top),
        XFORM("wall_generator", "perimeter_generator", transform_wall_generator),
        XFORM("gcode_label_objects", "gcode_label_objects", transform_gcode_label_objects),
        XFORM("fuzzy_skin", "fuzzy_skin", transform_fuzzy_skin),

        // Orca-only (no preFlight equivalent)
        ORCA_ONLY("timelapse_type"),
        ORCA_ONLY("wall_direction"),
        ORCA_ONLY("precise_outer_wall"),
        ORCA_ONLY("overhang_reverse"),
        ORCA_ONLY("overhang_reverse_threshold"),
        ORCA_ONLY("counterbore_hole_bridging"),
        ORCA_ONLY("slowdown_for_curled_perimeters"),
        ORCA_ONLY("enable_extra_bridge_layer"),
        ORCA_ONLY("thick_internal_bridges"),
        ORCA_ONLY("reduce_crossing_wall"),
        ORCA_ONLY("max_travel_detour_distance"),
        ORCA_ONLY("xy_hole_compensation"),
    };
}

#undef DIRECT
#undef RENAMED
#undef XFORM
#undef ORCA_ONLY
#undef SKIP

} // namespace Slic3r
