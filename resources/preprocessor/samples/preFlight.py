# preFlight Pre-Processor Python API
#
# This module is provided by preFlight via pybind11 bindings.
# These type stubs define the API contract - the actual implementation
# lives in C++ and is exposed to Python at runtime.
#
# Scripts define a process(gcode) function that preFlight calls
# after slicing, before preview rendering.

from enum import IntEnum
from typing import List, Dict, Optional, Tuple, Callable

# Module-level constant: preFlight version string (e.g. "0.9.13")
version: str


# ---------------------------------------------------------------------------
# Enums - mirror the C++ enums from GCodeProcessor
# ---------------------------------------------------------------------------

class MoveType(IntEnum):
    Noop = 0
    Retract = 1
    Unretract = 2
    Seam = 3
    ToolChange = 4
    ColorChange = 5
    PausePrint = 6
    CustomGCode = 7
    Travel = 8
    Wipe = 9
    Extrude = 10


class ExtrusionRole(IntEnum):
    NoRole = 0
    Perimeter = 1
    ExternalPerimeter = 2
    OverhangPerimeter = 3
    InterlockingPerimeter = 4
    InternalInfill = 5
    SolidInfill = 6
    TopSolidInfill = 7
    Ironing = 8
    BridgeInfill = 9
    GapFill = 10
    Skirt = 11
    SupportMaterial = 12
    SupportMaterialInterface = 13
    WipeTower = 14
    Custom = 15


class CustomEventType(IntEnum):
    ColorChange = 0
    PausePrint = 1
    ToolChange = 2
    Template = 3
    Custom = 4


# ---------------------------------------------------------------------------
# Data classes - thin wrappers over C++ structures via pybind11
# ---------------------------------------------------------------------------

class Move:
    """A single G-code movement command (MoveVertex).

    All positional values in mm, feedrates in mm/s, temperatures in C.
    Writable properties update both the internal structure and the
    corresponding G-code line in the virtual file.
    """

    # Read/write properties (propagate to G-code)
    feedrate: float              # commanded feedrate (mm/s) -> F parameter
    delta_e: float               # filament displacement (mm) -> E parameter
    fan_speed: float             # fan percentage (0-100) -> M106 insertion
    temperature: float           # hotend temperature (C) -> M104 insertion

    # Read/write properties (preview only, no G-code propagation)
    width: float                 # extrusion line width (mm)
    height: float                # layer/extrusion height (mm)

    # Per-move annotation (overrides gcode.annotation for this move)
    annotation: str

    # Read-only properties
    type: MoveType
    role: ExtrusionRole
    x: float
    y: float
    z: float
    extruder_id: int             # active extruder (0-based)
    actual_feedrate: float       # after acceleration limits (mm/s)
    mm3_per_mm: float            # volumetric rate constant
    volumetric_rate: float       # feedrate * mm3_per_mm (mm3/s)
    actual_volumetric_rate: float
    color_id: int                # sequential color change counter
    layer_id: int                # which layer this move belongs to
    gcode_line_id: int           # source line in the virtual file (1-based)
    internal_only: bool          # True for G2/G3 arc segments
    time: float                  # move duration in seconds (normal mode)
    time_stealth: float          # move duration in seconds (stealth mode)

    # Motion analysis (populated from time estimation)
    distance: float              # XYZ path length (mm)
    junction_angle: float        # angle from previous move (degrees, signed: + right, - left)
    acceleration: float          # effective acceleration (mm/s^2, normal mode)
    acceleration_stealth: float  # effective acceleration (mm/s^2, stealth mode)
    max_entry_speed: float       # junction-limited entry speed (mm/s, normal mode)
    max_entry_speed_stealth: float  # junction-limited entry speed (mm/s, stealth mode)

    # Fill region properties (from slicing geometry)
    region_area: float           # mm^2 (island boundary for perimeters, fill surface for infill, 0 if N/A)
    fill_pattern: str            # infill pattern name ("Rectilinear", "Gyroid", etc., "" for non-fill)


class Layer:
    """A group of moves sharing the same layer_id.

    Provides convenience accessors and the ability to inject G-code
    at layer boundaries.
    """

    id: int                      # layer number
    z: float                     # Z height of this layer (mm)
    height: float                # layer height (mm) - delta from previous
    moves: List[Move]            # all moves in this layer
    time: float                  # total layer time (seconds, normal mode)

    def prepend(self, gcode: str, comment: str = "") -> None:
        """Insert G-code lines before the first move in this layer."""
        ...

    def append(self, gcode: str, comment: str = "") -> None:
        """Insert G-code lines after the last move in this layer."""
        ...

    def moves_by_type(self, move_type: MoveType) -> List[Move]:
        """Filter moves by type."""
        ...

    def moves_by_role(self, role: ExtrusionRole) -> List[Move]:
        """Filter moves by extrusion role."""
        ...

    def extrusion_length(self) -> float:
        """Total filament extruded in this layer (mm)."""
        ...

    def travel_distance(self) -> float:
        """Total travel (non-extrusion) distance in this layer (mm)."""
        ...


class FilamentUsage:
    """Filament consumption stats for a role or extruder."""
    meters: float
    grams: float
    volume_mm3: float
    cost: float


class CustomEvent:
    """A custom G-code event (color change, pause, etc.) at a specific Z."""
    z: float
    type: int                    # CustomEventType value
    extruder: int
    color: str
    extra: str                   # custom G-code text



class Settings:
    """All available slicer settings (from PrintConfig).

    Access via gcode.settings["key_name"]. Values are strings.
    Auto-generated from PrintConfig.hpp at build time.
    """

    auto_speed: str  # bool (0/1)
    autoemit_temperature_commands: str  # bool (0/1)
    automatic_extrusion_widths: str  # bool (0/1)
    automatic_infill_combination: str  # bool (0/1)
    automatic_infill_combination_max_layer_height: str  # float or percentage
    avoid_crossing_curled_overhangs: str  # bool (0/1)
    avoid_crossing_perimeters: str  # bool (0/1)
    avoid_crossing_perimeters_max_detour: str  # float or percentage
    bed_shape: str  # coordinate pairs
    bed_temperature: str  # semicolon-separated ints
    bed_temperature_extruder: str  # int
    before_layer_gcode: str
    between_objects_gcode: str
    binary_gcode: str  # bool (0/1)
    bottom_solid_layers: str  # int
    bottom_solid_min_thickness: str  # float
    bridge_acceleration: str  # float
    bridge_angle: str  # float
    bridge_extrusion_width: str  # float or percentage
    bridge_fan_speed: str  # semicolon-separated ints
    bridge_flow_ratio: str  # float
    bridge_infill_overlap: str  # float or percentage
    bridge_infill_perimeter_overlap: str  # float or percentage
    bridge_speed: str  # float
    brim_ears_detection_length: str  # float
    brim_ears_max_angle: str  # float
    brim_separation: str  # float
    brim_width: str  # float
    chamber_minimal_temperature: str  # semicolon-separated ints
    chamber_temperature: str  # semicolon-separated ints
    color_change_gcode: str
    color_mixing_base_layers: str  # int
    colorprint_heights: str  # semicolon-separated floats
    complete_objects: str  # bool (0/1)
    cooling: str  # semicolon-separated bools
    cooling_perimeter_transition_distance: str  # semicolon-separated floats
    cooling_tube_length: str  # float
    cooling_tube_retraction: str  # float
    counterbore_bridge_layers: str  # int
    currency_symbol: str
    custom_parameters_filament: str  # semicolon-separated
    custom_parameters_print: str
    custom_parameters_printer: str
    default_acceleration: str  # float
    deretract_speed: str  # semicolon-separated floats
    disable_fan_first_layers: str  # semicolon-separated ints
    dont_slow_down_outer_wall: str  # semicolon-separated bools
    dont_support_bridges: str  # bool (0/1)
    duplicate_distance: str  # float
    elefant_foot_compensation: str  # float
    enable_dynamic_fan_speeds: str  # semicolon-separated bools
    enable_dynamic_overhang_speeds: str  # bool (0/1)
    enable_manual_fan_speeds: str  # semicolon-separated bools
    end_filament_gcode: str  # semicolon-separated
    end_gcode: str
    export_script: str
    export_script_enabled: str  # bool (0/1)
    external_perimeter_acceleration: str  # float
    external_perimeter_extrusion_width: str  # float or percentage
    external_perimeter_overlap: str  # float or percentage
    external_perimeter_speed: str  # float or percentage
    external_perimeters_first: str  # bool (0/1)
    extra_loading_move: str  # float
    extra_perimeters: str  # bool (0/1)
    extra_perimeters_on_overhangs: str  # bool (0/1)
    extruder_clearance_height: str  # float
    extruder_clearance_radius: str  # float
    extruder_colour: str  # semicolon-separated
    extruder_offset: str  # coordinate pairs
    extrusion_axis: str
    extrusion_multiplier: str  # semicolon-separated floats
    extrusion_width: str  # float or percentage
    extrusion_width_percent_of_nozzle: str  # bool (0/1)
    fan_always_on: str  # semicolon-separated bools
    fan_below_layer_time: str  # semicolon-separated ints
    fan_spinup_bridge_infill: str  # semicolon-separated bools
    fan_spinup_overhang_perimeter: str  # semicolon-separated bools
    fan_spinup_serpentine_overhang: str  # semicolon-separated bools
    fan_spinup_time: str  # semicolon-separated ints
    filament_abrasive: str  # semicolon-separated bools
    filament_colour: str  # semicolon-separated
    filament_cooling_final_speed: str  # semicolon-separated floats
    filament_cooling_initial_speed: str  # semicolon-separated floats
    filament_cooling_moves: str  # semicolon-separated ints
    filament_cost: str  # semicolon-separated floats
    filament_density: str  # semicolon-separated floats
    filament_diameter: str  # semicolon-separated floats
    filament_enable_pressure_advance: str  # semicolon-separated bools
    filament_infill_max_crossing_speed: str  # semicolon-separated floats
    filament_infill_max_speed: str  # semicolon-separated floats
    filament_load_time: str  # semicolon-separated floats
    filament_loading_speed: str  # semicolon-separated floats
    filament_loading_speed_start: str  # semicolon-separated floats
    filament_max_print_speed: str  # semicolon-separated floats
    filament_max_volumetric_flow: str  # semicolon-separated floats
    filament_max_volumetric_speed: str  # semicolon-separated floats
    filament_minimal_purge_on_wipe_tower: str  # semicolon-separated floats
    filament_multitool_ramming: str  # semicolon-separated bools
    filament_multitool_ramming_flow: str  # semicolon-separated floats
    filament_multitool_ramming_volume: str  # semicolon-separated floats
    filament_notes: str  # semicolon-separated
    filament_pressure_advance: str  # semicolon-separated floats
    filament_purge_multiplier: str  # semicolon-separated percentages
    filament_ramming_parameters: str  # semicolon-separated
    filament_seam_gap_distance: str
    filament_shrinkage_compensation_x: str  # semicolon-separated percentages
    filament_shrinkage_compensation_y: str  # semicolon-separated percentages
    filament_shrinkage_compensation_z: str  # semicolon-separated percentages
    filament_soluble: str  # semicolon-separated bools
    filament_spool_weight: str  # semicolon-separated floats
    filament_stamping_distance: str  # semicolon-separated floats
    filament_stamping_loading_speed: str  # semicolon-separated floats
    filament_toolchange_delay: str  # semicolon-separated floats
    filament_transmission_distance: str  # semicolon-separated floats
    filament_type: str  # semicolon-separated
    filament_unload_time: str  # semicolon-separated floats
    filament_unloading_speed: str  # semicolon-separated floats
    filament_unloading_speed_start: str  # semicolon-separated floats
    fill_angle: str  # float
    fill_density: str  # percentage
    first_layer_acceleration: str  # float
    first_layer_acceleration_over_raft: str  # float
    first_layer_bed_temperature: str  # semicolon-separated ints
    first_layer_extrusion_width: str  # float or percentage
    first_layer_height: str  # float or percentage
    first_layer_infill_speed: str  # float or percentage
    first_layer_speed: str  # float or percentage
    first_layer_speed_over_raft: str  # float or percentage
    first_layer_temperature: str  # semicolon-separated ints
    first_layer_travel_speed: str  # float or percentage
    full_fan_speed_layer: str  # semicolon-separated ints
    fuzzy_skin_first_layer: str  # bool (0/1)
    fuzzy_skin_octaves: str  # int
    fuzzy_skin_on_top: str  # bool (0/1)
    fuzzy_skin_persistence: str  # float
    fuzzy_skin_point_dist: str  # float
    fuzzy_skin_scale: str  # float
    fuzzy_skin_thickness: str  # float
    gcode_comments: str  # bool (0/1)
    gcode_resolution: str  # float
    gcode_substitutions: str  # semicolon-separated
    high_current_on_filament_swap: str  # bool (0/1)
    idle_temperature: str
    infill_acceleration: str  # float
    infill_anchor: str  # float or percentage
    infill_anchor_max: str  # float or percentage
    infill_every_layers: str  # int
    infill_extruder: str  # int
    infill_extrusion_width: str  # float or percentage
    infill_first: str  # bool (0/1)
    infill_only_where_needed: str  # bool (0/1)
    infill_overlap: str  # float or percentage
    infill_speed: str  # float
    interface_shells: str  # bool (0/1)
    interlock_perimeter_count: str  # int
    interlock_perimeter_overlap: str  # float or percentage
    interlock_perimeter_strength: str  # percentage
    interlock_perimeters_enabled: str  # bool (0/1)
    interlock_regular_perimeters: str  # int
    interlock_solid_layers_bottom: str  # int
    interlock_solid_layers_top: str  # int
    interlocking_beam: str  # bool (0/1)
    interlocking_beam_layer_count: str  # int
    interlocking_beam_width: str  # float
    interlocking_boundary_avoidance: str  # int
    interlocking_depth: str  # int
    interlocking_orientation: str  # float
    interlocking_perimeter_extruder: str  # int
    ironing: str  # bool (0/1)
    ironing_flowrate: str  # percentage
    ironing_spacing: str  # float
    ironing_speed: str  # float
    layer_gcode: str
    layer_height: str  # float
    machine_klipper_max_accel: str  # float
    machine_klipper_max_velocity: str  # float
    machine_klipper_minimum_cruise_ratio: str  # float
    machine_klipper_square_corner_velocity: str  # float
    machine_max_acceleration_e: str  # semicolon-separated floats
    machine_max_acceleration_extruding: str  # semicolon-separated floats
    machine_max_acceleration_travel: str  # semicolon-separated floats
    machine_max_acceleration_x: str  # semicolon-separated floats
    machine_max_acceleration_y: str  # semicolon-separated floats
    machine_max_acceleration_z: str  # semicolon-separated floats
    machine_max_feedrate_e: str  # semicolon-separated floats
    machine_max_feedrate_x: str  # semicolon-separated floats
    machine_max_feedrate_y: str  # semicolon-separated floats
    machine_max_feedrate_z: str  # semicolon-separated floats
    machine_max_jerk_e: str  # semicolon-separated floats
    machine_max_jerk_x: str  # semicolon-separated floats
    machine_max_jerk_y: str  # semicolon-separated floats
    machine_max_jerk_z: str  # semicolon-separated floats
    machine_max_junction_deviation: str  # semicolon-separated floats
    machine_min_extruding_rate: str  # semicolon-separated floats
    machine_min_travel_rate: str  # semicolon-separated floats
    machine_rrf_m201: str
    machine_rrf_m203: str
    machine_rrf_m204: str
    machine_rrf_m207: str
    machine_rrf_m566: str
    machine_time_compensation: str  # percentage
    manual_fan_speed_external_perimeter: str  # semicolon-separated ints
    manual_fan_speed_interlocking_perimeter: str  # semicolon-separated ints
    manual_fan_speed_internal_infill: str  # semicolon-separated ints
    manual_fan_speed_ironing: str  # semicolon-separated ints
    manual_fan_speed_overhang_perimeter: str  # semicolon-separated ints
    manual_fan_speed_perimeter: str  # semicolon-separated ints
    manual_fan_speed_serpentine: str  # semicolon-separated ints
    manual_fan_speed_serpentine_overhang: str  # semicolon-separated ints
    manual_fan_speed_skirt: str  # semicolon-separated ints
    manual_fan_speed_solid_infill: str  # semicolon-separated ints
    manual_fan_speed_support_interface: str  # semicolon-separated ints
    manual_fan_speed_support_material: str  # semicolon-separated ints
    manual_fan_speed_top_solid_infill: str  # semicolon-separated ints
    max_fan_speed: str  # semicolon-separated ints
    max_layer_height: str  # semicolon-separated floats
    max_perimeter_width: str  # percentage
    max_print_height: str  # float
    max_print_speed: str  # float
    max_volumetric_extrusion_rate_slope_negative: str  # float
    max_volumetric_extrusion_rate_slope_positive: str  # float
    max_volumetric_flow: str  # float
    max_volumetric_speed: str  # float
    merge_top_solid_infills: str  # bool (0/1)
    min_bead_width: str  # float or percentage
    min_fan_speed: str  # semicolon-separated ints
    min_feature_size: str  # float or percentage
    min_layer_height: str  # semicolon-separated floats
    min_print_speed: str  # semicolon-separated floats
    min_skirt_length: str  # float
    mmu_segmented_region_interlocking_depth: str  # float
    mmu_segmented_region_max_width: str  # float
    multimaterial_purging: str  # float
    narrow_to_athena: str  # bool (0/1)
    narrow_to_athena_threshold: str  # float
    narrow_to_athena_top_bottom: str  # bool (0/1)
    notes: str
    nozzle_diameter: str  # semicolon-separated floats
    nozzle_high_flow: str  # semicolon-separated bools
    nozzle_width_warning_max: str  # semicolon-separated percentages
    nozzle_width_warning_min: str  # semicolon-separated percentages
    only_one_perimeter_first_layer: str  # bool (0/1)
    only_retract_when_crossing_perimeters: str  # bool (0/1)
    ooze_prevention: str  # bool (0/1)
    output_filename_format: str
    over_bridge_speed: str  # float or percentage
    overhang_fan_speed_0: str  # semicolon-separated ints
    overhang_fan_speed_1: str  # semicolon-separated ints
    overhang_fan_speed_2: str  # semicolon-separated ints
    overhang_fan_speed_3: str  # semicolon-separated ints
    overhang_speed_0: str  # float or percentage
    overhang_speed_1: str  # float or percentage
    overhang_speed_2: str  # float or percentage
    overhang_speed_3: str  # float or percentage
    overhangs: str  # bool (0/1)
    parking_pos_retraction: str  # float
    pause_print_gcode: str
    perimeter_acceleration: str  # float
    perimeter_extruder: str  # int
    perimeter_extrusion_width: str  # float or percentage
    perimeter_perimeter_overlap: str  # float or percentage
    perimeter_speed: str  # float
    perimeters: str  # int
    post_process: str  # semicolon-separated
    prefer_clockwise_movements: str  # bool (0/1)
    preprocessing_enabled_filament: str  # bool (0/1)
    preprocessing_enabled_print: str  # bool (0/1)
    preprocessing_enabled_printer: str  # bool (0/1)
    preprocessing_scripts_filament: str  # semicolon-separated
    preprocessing_scripts_print: str  # semicolon-separated
    preprocessing_scripts_printer: str  # semicolon-separated
    print_high_flow_nozzle: str  # semicolon-separated bools
    print_nozzle_diameters: str  # semicolon-separated floats
    printer_model: str
    printer_notes: str
    raft_contact_distance: str  # float
    raft_expansion: str  # float
    raft_first_layer_density: str  # percentage
    raft_first_layer_expansion: str  # float
    raft_layers: str  # int
    remaining_times: str  # bool (0/1)
    resolution: str  # float
    retract_before_travel: str  # semicolon-separated floats
    retract_before_wipe: str  # semicolon-separated percentages
    retract_layer_change: str  # semicolon-separated bools
    retract_length: str  # semicolon-separated floats
    retract_length_toolchange: str  # semicolon-separated floats
    retract_lift: str  # semicolon-separated floats
    retract_lift_above: str  # semicolon-separated floats
    retract_lift_below: str  # semicolon-separated floats
    retract_restart_extra: str  # semicolon-separated floats
    retract_restart_extra_toolchange: str  # semicolon-separated floats
    retract_speed: str  # semicolon-separated floats
    scarf_seam_entire_loop: str  # bool (0/1)
    scarf_seam_length: str  # float
    scarf_seam_max_segment_length: str  # float
    scarf_seam_on_inner_perimeters: str  # bool (0/1)
    scarf_seam_only_on_smooth: str  # bool (0/1)
    scarf_seam_start_height: str  # percentage
    seam_gap_distance: str  # float or percentage
    seam_notch_angle: str  # float
    seam_notch_width: str  # float
    seam_preferred_direction: str  # float
    seam_preferred_direction_jitter: str  # float
    serpentine_depth: str  # float
    serpentine_enabled: str  # bool (0/1)
    serpentine_extrusion_width: str  # float or percentage
    serpentine_limit_depth: str  # bool (0/1)
    serpentine_max_bead: str  # percentage
    serpentine_overlap: str  # float or percentage
    serpentine_solid_surfaces: str  # bool (0/1)
    silent_mode: str  # bool (0/1)
    single_extruder_multi_material: str  # bool (0/1)
    single_extruder_multi_material_priming: str  # bool (0/1)
    skirt_distance: str  # float
    skirt_height: str  # int
    skirts: str  # int
    slice_closing_radius: str  # float
    slowdown_below_layer_time: str  # semicolon-separated ints
    small_perimeter_speed: str  # float or percentage
    solid_infill_acceleration: str  # float
    solid_infill_below_area: str  # float
    solid_infill_every_layers: str  # int
    solid_infill_extruder: str  # int
    solid_infill_extrusion_width: str  # float or percentage
    solid_infill_speed: str  # float or percentage
    spiral_vase: str  # bool (0/1)
    staggered_inner_seams: str  # bool (0/1)
    standby_temperature_delta: str  # int
    start_filament_gcode: str  # semicolon-separated
    start_gcode: str
    support_material: str  # bool (0/1)
    support_material_angle: str  # float
    support_material_auto: str  # bool (0/1)
    support_material_bottom_contact_extrusion_width: str  # percentage
    support_material_bottom_interface_layers: str  # int
    support_material_bridge_no_gap: str  # bool (0/1)
    support_material_buildplate_only: str  # bool (0/1)
    support_material_closing_radius: str  # float
    support_material_contact_distance_custom: str  # float
    support_material_enforce_layers: str  # int
    support_material_extruder: str  # int
    support_material_extrusion_width: str  # float or percentage
    support_material_interface_contact_loops: str  # bool (0/1)
    support_material_interface_extruder: str  # int
    support_material_interface_extrusion_width: str  # float or percentage
    support_material_interface_layers: str  # int
    support_material_interface_spacing: str  # float
    support_material_interface_speed: str  # float or percentage
    support_material_min_area: str  # float
    support_material_spacing: str  # float
    support_material_speed: str  # float
    support_material_synchronize_layers: str  # bool (0/1)
    support_material_threshold: str  # int
    support_material_with_sheath: str  # bool (0/1)
    support_material_xy_spacing: str  # float or percentage
    support_tree_angle: str  # float
    support_tree_angle_slow: str  # float
    support_tree_branch_diameter: str  # float
    support_tree_branch_diameter_angle: str  # float
    support_tree_branch_diameter_double_wall: str  # float
    support_tree_branch_distance: str  # float
    support_tree_tip_diameter: str  # float
    support_tree_top_rate: str  # percentage
    temperature: str  # semicolon-separated ints
    template_custom_gcode: str
    thick_bridges: str  # bool (0/1)
    thin_walls: str  # bool (0/1)
    threads: str  # int
    thumbnails: str
    time_cost: str  # float
    toolchange_gcode: str
    top_infill_extrusion_width: str  # float or percentage
    top_solid_infill_acceleration: str  # float
    top_solid_infill_speed: str  # float or percentage
    top_solid_layers: str  # int
    top_solid_min_thickness: str  # float
    top_surface_flow_reduction: str  # percentage
    travel_acceleration: str  # float
    travel_lift_before_obstacle: str  # semicolon-separated bools
    travel_max_lift: str  # semicolon-separated floats
    travel_ramping_lift: str  # semicolon-separated bools
    travel_short_distance_acceleration: str  # float
    travel_slope: str  # semicolon-separated floats
    travel_speed: str  # float
    travel_speed_z: str  # float
    use_firmware_retraction: str  # bool (0/1)
    use_relative_e_distances: str  # bool (0/1)
    use_volumetric_e: str  # bool (0/1)
    variable_layer_height: str  # bool (0/1)
    wall_distribution_count: str  # int
    wall_transition_angle: str  # float
    wall_transition_filter_deviation: str  # float or percentage
    wall_transition_length: str  # float or percentage
    wipe: str  # semicolon-separated bools
    wipe_extend: str  # semicolon-separated bools
    wipe_into_infill: str  # bool (0/1)
    wipe_into_objects: str  # bool (0/1)
    wipe_length: str  # semicolon-separated floats
    wipe_tower: str  # bool (0/1)
    wipe_tower_acceleration: str  # float
    wipe_tower_bridging: str  # float
    wipe_tower_brim_width: str  # float
    wipe_tower_cone_angle: str  # float
    wipe_tower_extra_flow: str  # percentage
    wipe_tower_extra_spacing: str  # percentage
    wipe_tower_extruder: str  # int
    wipe_tower_no_sparse_layers: str  # bool (0/1)
    wipe_tower_per_color_wipe: str  # float
    wipe_tower_width: str  # float
    wiping_volumes_matrix: str  # semicolon-separated floats
    wiping_volumes_use_custom_matrix: str  # bool (0/1)
    xy_size_compensation: str  # float
    z_offset: str  # float


class GCode:
    """Top-level object representing the entire sliced G-code.

    This is the root object passed to process(). It wraps
    GCodeProcessorResult and provides structured access to
    everything GCodeProcessor parsed.
    """

    # Print geometry
    layers: List[Layer]
    moves: List[Move]            # flat access to all moves
    max_print_height: float      # mm
    z_offset: float              # mm
    bed_shape: List[Tuple[float, float]]
    spiral_vase_mode: bool

    # Extruder configuration
    extruder_count: int
    extruder_colors: List[str]   # hex colors (#FF8000)
    filament_diameters: List[float]
    filament_densities: List[float]

    # Timing
    time_estimate_normal: float  # total seconds (normal mode)
    time_estimate_stealth: float # total seconds (stealth mode)
    first_layer_time: float      # first layer time (seconds)

    # Cost
    filament_cost: List[float]   # cost per extruder
    time_cost: float             # machine time cost rate
    currency_symbol: str         # currency symbol (e.g. "$")

    # Active presets
    preset_print: str            # print profile name
    preset_filament: List[str]   # filament profile name(s)
    preset_printer: str          # printer profile name

    # Filament usage
    filament_by_role: Dict[ExtrusionRole, FilamentUsage]
    filament_by_extruder: Dict[int, FilamentUsage]
    filament_by_color_change: List[float]  # volumes (mm3) per color segment

    # Events
    custom_events: List[CustomEvent]

    # Performance metrics
    role_metrics: Dict[ExtrusionRole, dict]  # {role: {max_commands_per_sec, max_layer}}
    overall_metrics: dict        # {max_commands_per_sec, max_layer}
    conflict: Optional[dict]     # {object1, object2, height, layer} or None

    # Slicer settings (all print/printer/filament config as key-value strings)
    # See _settings.py for full list of available keys with types (auto-generated at build)
    settings: Settings         # e.g. settings["layer_height"], settings["nozzle_diameter"]

    # Raw G-code access
    line_count: int              # total lines in the virtual G-code file

    # Global annotation (fallback for moves without per-move annotation)
    annotation: str

    def insert(self, line: int, gcode: str, position: str = "after", comment: str = "") -> None:
        """Insert raw G-code before or after the specified line number.

        position: "after" (default) or "before"
        comment: optional comment appended to the line (prefixed with "; ")
        """
        ...

    def get_line(self, line_id: int) -> str:
        """Read a raw G-code line from the virtual file (1-based line number)."""
        ...

    def rewrite(self, line_id: int, gcode: str, comment: str = "") -> None:
        """Replace a raw G-code line in the virtual file.

        comment: optional comment appended to the line (prefixed with "; ")
        """
        ...

    def find_line(self, text: str) -> int:
        """Find the first line containing text. Returns 1-based line_id, or 0 if not found."""
        ...

    def find_lines(self, text: str) -> List[int]:
        """Find all lines containing text. Returns list of 1-based line_ids."""
        ...

    def find_moves(self, type: MoveType = None, role: ExtrusionRole = None,
                   extruder: int = None, z_min: float = None,
                   z_max: float = None) -> List[Move]:
        """Query moves by criteria. All parameters are optional filters."""
        ...

    def remove_moves(self, predicate: Callable[[Move], bool]) -> int:
        """Remove all moves matching predicate. Returns count removed."""
        ...