///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "Settings.hpp"
#include "SegmentTemplate.hpp"
#include "OptionTemplate.hpp"
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "CogMarker.hpp"
#include "ToolMarker.hpp"
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "../include/PathVertex.hpp"
#include "../include/ColorRange.hpp"
#include "../include/ColorPrint.hpp"
#include "Bitset.hpp"
#include "ViewRange.hpp"
#include "Layers.hpp"
#include "ExtrusionRoles.hpp"
#include "PrefilterNeighbours.hpp"
#include "OcclusionCuller.hpp"
#include "PrintChunks.hpp"
#include "SealedBeads.hpp"
#include "ViewChunks.hpp"
#include "PreparedLoadData.hpp"
#include "../include/Viewer.hpp"

#include <array>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <limits>

namespace libvgcode
{

struct GCodeInputData;

class ViewerImpl
{
public:
    ViewerImpl();
    ~ViewerImpl() { shutdown(); }
    ViewerImpl(const ViewerImpl &other) = delete;
    ViewerImpl(ViewerImpl &&other) = delete;
    ViewerImpl &operator=(const ViewerImpl &other) = delete;
    ViewerImpl &operator=(ViewerImpl &&other) = delete;

    //
    // Initialize shaders, uniform indices and segment geometry.
    //
    void init(const std::string &opengl_context_version);
    //
    // Release the resources used by the viewer.
    //
    void shutdown();
    //
    // Reset all caches and free gpu memory.
    //
    void reset();
    //
    // Setup all the variables used for visualization of the toolpaths
    // from the given gcode data: prepare_load() then the install below, on the calling thread.
    //
    void load(GCodeInputData &&gcode_data);
    //
    // Installs a prepared load: takes its data, uploads the buffers and builds the tables that depend on the
    // current settings. UI thread (GL context) only.
    //
    void load(PreparedLoad &&prepared);
    //
    // The settings a preparation for this viewer reads
    //
    PrepareSettings get_prepare_settings() const
    {
        return {m_travels_radius, m_wipes_radius, m_keep_prefilter_neighbours};
    }
    //
    // The view settings the enabled lists and the colors read, as this viewer holds them
    //
    ViewSettings get_view_settings() const;
    //
    // The extrusion role colors reset_default_extrusion_roles_colors() sets
    //
    static const std::array<Color, size_t(EGCodeExtrusionRole::COUNT)> &default_extrusion_roles_colors();

    //
    // Update the visibility property of toolpaths in dependence
    // of the current settings
    //
    void update_enabled_entities();
    //
    // Update the color of toolpaths in dependence of the current
    // view type and settings
    //
    void update_colors();
    void update_colors_texture();

    //
    // Render the toolpaths
    //
    void render(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix);

    //
    // Scene passes (Full lighting tier): shadow-map and G-buffer support.
    // The visible segments shader modulates its baked lighting by a shadow map
    // and an ambient-occlusion texture when params.enabled is set.
    //
    void set_scene_pass_params(const ScenePassParams &params) { m_scene_pass_params = params; }
    //
    // Renders the enabled segments with pass matrices: for the shadow pass, pass
    // the light's matrices and position (the imposters then face the light) with
    // gbuffer = false; for the G-buffer pass, the camera's with gbuffer = true.
    //
    void render_segments_pass(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix, const Vec3 &camera_position,
                              bool gbuffer);

    EViewType get_view_type() const { return m_settings.view_type; }
    void set_view_type(EViewType type);

    ETimeMode get_time_mode() const { return m_settings.time_mode; }
    void set_time_mode(ETimeMode mode);

    const Interval &get_layers_view_range() const { return m_layers.get_view_range(); }
    void set_layers_view_range(const Interval &range) { set_layers_view_range(range[0], range[1]); }
    void set_layers_view_range(Interval::value_type min, Interval::value_type max);

    bool is_top_layer_only_view_range() const { return m_settings.top_layer_only_view_range; }
    void toggle_top_layer_only_view_range();

    bool is_spiral_vase_mode() const { return m_settings.spiral_vase_mode; }

    std::vector<ETimeMode> get_time_modes() const;

    size_t get_layers_count() const { return m_layers.count(); }
    float get_layer_z(size_t layer_id) const { return m_layers.get_layer_z(layer_id); }
    std::vector<float> get_layers_zs() const { return m_layers.get_zs(); }

    size_t get_layer_id_at(float z) const { return m_layers.get_layer_id_at(z); }

    size_t get_used_extruders_count() const { return m_used_extruders.size(); }
    std::vector<uint8_t> get_used_extruders_ids() const;

    size_t get_color_prints_count(uint8_t extruder_id) const;
    std::vector<ColorPrint> get_color_prints(uint8_t extruder_id) const;

    AABox get_bounding_box(const std::vector<EMoveType> &types = {EMoveType::Retract, EMoveType::Unretract,
                                                                  EMoveType::Seam, EMoveType::ToolChange,
                                                                  EMoveType::ColorChange, EMoveType::PausePrint,
                                                                  EMoveType::CustomGCode, EMoveType::Travel,
                                                                  EMoveType::Wipe, EMoveType::Extrude}) const;
    AABox get_extrusion_bounding_box(const std::vector<EGCodeExtrusionRole> &roles = {
                                         EGCodeExtrusionRole::Perimeter, EGCodeExtrusionRole::ExternalPerimeter,
                                         EGCodeExtrusionRole::OverhangPerimeter, EGCodeExtrusionRole::InternalInfill,
                                         EGCodeExtrusionRole::SolidInfill, EGCodeExtrusionRole::TopSolidInfill,
                                         EGCodeExtrusionRole::Ironing, EGCodeExtrusionRole::BridgeInfill,
                                         EGCodeExtrusionRole::GapFill, EGCodeExtrusionRole::Skirt,
                                         EGCodeExtrusionRole::SupportMaterial,
                                         EGCodeExtrusionRole::SupportMaterialInterface, EGCodeExtrusionRole::WipeTower,
                                         EGCodeExtrusionRole::Custom}) const;

    bool is_option_visible(EOptionType type) const;
    void toggle_option_visibility(EOptionType type);

    bool is_extrusion_role_visible(EGCodeExtrusionRole role) const;
    void toggle_extrusion_role_visibility(EGCodeExtrusionRole role);

    const Interval &get_view_full_range() const { return m_view_range.get_full(); }
    const Interval &get_view_enabled_range() const { return m_view_range.get_enabled(); }
    const Interval &get_view_visible_range() const { return m_view_range.get_visible(); }
    void set_view_visible_range(Interval::value_type min, Interval::value_type max);

    size_t get_vertices_count() const { return m_vertices.size(); }
    const PathVertex &get_current_vertex() const { return get_vertex_at(get_current_vertex_id()); }
    size_t get_current_vertex_id() const { return static_cast<size_t>(m_view_range.get_visible()[1]); }
    const PathVertex &get_vertex_at(size_t id) const
    {
        return (id < m_vertices.size()) ? m_vertices[id] : PathVertex::DUMMY_PATH_VERTEX;
    }
    float get_estimated_time() const { return m_total_time[static_cast<size_t>(m_settings.time_mode)]; }
    float get_estimated_time_at(size_t id) const;
    Color get_vertex_color(const PathVertex &vertex) const;

    size_t get_extrusion_roles_count() const { return m_extrusion_roles.get_roles_count(); }
    std::vector<EGCodeExtrusionRole> get_extrusion_roles() const { return m_extrusion_roles.get_roles(); }
    float get_extrusion_role_estimated_time(EGCodeExtrusionRole role) const
    {
        return m_extrusion_roles.get_time(role, m_settings.time_mode);
    }

    size_t get_options_count() const { return m_options.size(); }
    const std::vector<EOptionType> &get_options() const { return m_options; }

    float get_travels_estimated_time() const { return m_travels_time[static_cast<size_t>(m_settings.time_mode)]; }
    std::vector<float> get_layers_estimated_times() const { return m_layers.get_times(m_settings.time_mode); }

    size_t get_tool_colors_count() const { return m_tool_colors.size(); }
    const Palette &get_tool_colors() const { return m_tool_colors; }
    void set_tool_colors(const Palette &colors);

    size_t get_color_print_colors_count() const { return m_color_print_colors.size(); }
    const Palette &get_color_print_colors() const { return m_color_print_colors; }
    void set_color_print_colors(const Palette &colors);

    const Color &get_extrusion_role_color(EGCodeExtrusionRole role) const;
    void set_extrusion_role_color(EGCodeExtrusionRole role, const Color &color);
    void reset_default_extrusion_roles_colors();

    const Color &get_option_color(EOptionType type) const;
    void set_option_color(EOptionType type, const Color &color);
    void reset_default_options_colors();

    const ColorRange &get_color_range(EViewType type) const;
    void set_color_range_palette(EViewType type, const Palette &palette);

    float get_travels_radius() const { return m_travels_radius; }
    void set_travels_radius(float radius);
    float get_wipes_radius() const { return m_wipes_radius; }
    void set_wipes_radius(float radius);

    // Per-sample shading of the toolpaths on multisampled targets; whether the last draw used it, and whether
    // the gate kept it off and why ("prefilter active", "pixel budget", "sections resolvable"; empty otherwise)
    bool get_sample_shading() const { return m_sample_shading; }
    void set_sample_shading(bool enable) { m_sample_shading = enable; }
    bool is_sample_shading_active() const { return m_sample_shading_active; }
    bool is_sample_shading_gated() const { return m_sample_shading_gated; }
    const std::string &get_sample_shading_reason() const { return m_sample_shading_reason; }

    // Toolpath prefilter of the wall shading of perimeters seen near level, or from above up to steep views where the
    // next layer was found; the target pixels per output pixel its width is measured in; whether the last visible
    // draw used it, and why not when it did not; the compiler or linker output when its program failed to build
    // (empty otherwise)
    bool get_toolpath_prefilter() const { return m_prefilter_enabled; }
    void set_toolpath_prefilter(bool enable) { m_prefilter_enabled = enable; }
    void set_output_pixel_scale(float scale);
    bool is_toolpath_prefilter_active() const { return m_prefilter_active; }
    const std::string &get_toolpath_prefilter_reason() const { return m_prefilter_reason; }
    const std::string &get_toolpath_prefilter_shader_log() const { return m_segments_pf_shader_log; }
    // The compiler or linker output when the shadow pass's depth-only program failed to build (empty otherwise)
    const std::string &get_toolpath_depth_shader_log() const { return m_segments_depth_shader_log; }

    // The prefilter's wall neighbour data of the loaded toolpaths, per vertex (kept after a load only when asked
    // for), and the statistics of its search
    void set_keep_prefilter_neighbours(bool keep) { m_keep_prefilter_neighbours = keep; }
    const PrefilterNeighbourStats &get_prefilter_neighbour_stats() const { return m_prefilter_stats; }
    const std::vector<float> &get_prefilter_offsets_x() const { return m_prefilter_neighbours.offset_x; }
    const std::vector<float> &get_prefilter_offsets_y() const { return m_prefilter_neighbours.offset_y; }
    const std::vector<uint8_t> &get_prefilter_flags() const { return m_prefilter_neighbours.flags; }

    // Sealed bead culling of the enabled segment list, and the statistics of its load-time pass and last list
    bool get_sealed_bead_culling() const { return m_sealed_enabled; }
    void set_sealed_bead_culling(bool enable);
    const SealedBeadStats &get_sealed_bead_stats() const;

    // Per-frame chunk culling of the enabled segment list (view frustum), and the statistics of its chunk set and last
    // selection
    bool get_chunk_culling() const { return m_chunk_culling_enabled; }
    void set_chunk_culling(bool enable);
    const ViewChunkStats &get_view_chunk_stats() const { return m_view_chunk_stats; }

    // The chunk structure of the whole print: the statistics of it and of its last filter
    const PrintChunkStats &get_print_chunk_stats() const { return m_print_chunk_stats; }

    // Occlusion culling of each pass against its view's own depth, its occluder cap (0: none), the most enabled
    // segments the shadow view draws whole with no step (0: never), whether the shadow pass merges its step's depth
    // and draws only the residual, the statistics of its last passes, and the reset of their accumulated timings
    bool get_occlusion_culling() const { return m_occlusion_enabled; }
    void set_occlusion_culling(bool enable);
    size_t get_occlusion_occluder_cap() const { return m_occlusion.occluder_cap(); }
    void set_occlusion_occluder_cap(size_t segments) { m_occlusion.set_occluder_cap(segments); }
    size_t get_occlusion_shadow_all_max() const { return m_occlusion_shadow_all_max; }
    void set_occlusion_shadow_all_max(size_t segments) { m_occlusion_shadow_all_max = segments; }
    bool get_occlusion_shadow_merge() const { return m_occlusion_shadow_merge; }
    void set_occlusion_shadow_merge(bool merge) { m_occlusion_shadow_merge = merge; }
    const OcclusionStats &get_occlusion_stats() const { return m_occlusion_stats; }
    void reset_occlusion_bench();

    // The work of the view updates since the last reset
    const ViewUpdateStats &get_view_update_stats() const { return m_view_update_stats; }
    void reset_view_update_stats() { m_view_update_stats = ViewUpdateStats(); }
    // The list uploads whose buffer did not take the data since the load
    const ListUploadStats &get_list_upload_stats() const { return m_list_upload_stats; }
#ifdef PREFLIGHT_TEST_HOOKS
    // The visibility probe of the next visible pass with its result
    void request_visibility_probe();
    const VisibilityProbeStats &get_visibility_probe_stats() const { return m_probe_stats; }
    const std::vector<uint8_t> &get_visibility_probe_mask() const { return m_probe_mask; }
#endif // PREFLIGHT_TEST_HOOKS

    // The phases of the last load
    const LoadPhaseStats &get_load_phase_stats() const { return m_load_stats; }

    size_t get_used_cpu_memory() const;
    size_t get_used_gpu_memory() const;

    // Clipping plane for the preview; setting or clearing it rebuilds the enabled segments, as sealed bead culling
    // applies only without one
    void set_clipping_plane(float nx, float ny, float nz, float offset);
    void reset_clipping_plane();

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    Vec3 get_cog_marker_position() const { return m_cog_marker.get_position(); }

    float get_cog_marker_scale_factor() const { return m_cog_marker_scale_factor; }
    void set_cog_marker_scale_factor(float factor) { m_cog_marker_scale_factor = std::max(factor, 0.001f); }

    const Vec3 &get_tool_marker_position() const { return m_tool_marker.get_position(); }

    float get_tool_marker_offset_z() const { return m_tool_marker.get_offset_z(); }
    void set_tool_marker_offset_z(float offset_z) { m_tool_marker.set_offset_z(offset_z); }

    float get_tool_marker_scale_factor() const { return m_tool_marker_scale_factor; }
    void set_tool_marker_scale_factor(float factor) { m_tool_marker_scale_factor = std::max(factor, 0.001f); }

    const Color &get_tool_marker_color() const { return m_tool_marker.get_color(); }
    void set_tool_marker_color(const Color &color) { m_tool_marker.set_color(color); }

    float get_tool_marker_alpha() const { return m_tool_marker.get_alpha(); }
    void set_tool_marker_alpha(float alpha) { m_tool_marker.set_alpha(alpha); }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

private:
    //
    // Settings used to render the toolpaths
    //
    Settings m_settings;
    //
    // Detected layers
    //
    Layers m_layers;
    //
    // Detected extrusion roles
    //
    ExtrusionRoles m_extrusion_roles;
    //
    // Detected options
    //
    std::vector<EOptionType> m_options;
    //
    // Detected used extruders ids
    //
    std::map<uint8_t, std::vector<ColorPrint>> m_used_extruders;
    //
    // Vertices ranges for visualization
    //
    ViewRange m_view_range;
    //
    // Detected total moves times
    //
    std::array<float, TIME_MODES_COUNT> m_total_time{0.0f, 0.0f};
    //
    // Detected travel moves times
    //
    std::array<float, TIME_MODES_COUNT> m_travels_time{0.0f, 0.0f};
    //
    // Radius of cylinders used to render travel moves segments
    //
    float m_travels_radius{DEFAULT_TRAVELS_RADIUS_MM};
    //
    // Radius of cylinders used to render wipe moves segments
    //
    float m_wipes_radius{DEFAULT_WIPES_RADIUS_MM};
    //
    // Per-sample shading of the segments on multisampled targets; whether the last visible draw used it, and
    // whether the gate kept it off and why (empty when it did not)
    //
    bool m_sample_shading{true};
    bool m_sample_shading_active{false};
    bool m_sample_shading_gated{false};
    std::string m_sample_shading_reason;
    //
    // Toolpath prefilter: whether it is enabled, whether the last visible draw used it and why not (empty when it
    // did), and the target pixels per output pixel (the supersampling scale)
    //
    bool m_prefilter_enabled{true};
    bool m_prefilter_active{false};
    std::string m_prefilter_reason{"no view in range"};
    float m_output_pixel_scale{1.0f};
    //
    // Toolpath prefilter: every vertex's XY offset to the wall bead above and its flags, found at load (desktop
    // OpenGL only) and dropped once the buffers hold them unless kept for a test hook, and the statistics of that
    // search
    //
    PrefilterNeighbourData m_prefilter_neighbours;
    PrefilterNeighbourStats m_prefilter_stats;
    bool m_keep_prefilter_neighbours{false};
    //
    // Sealed bead culling: whether it is enabled, the per-segment exposure found at load (kept, every enabled list
    // rebuild reads it), and the statistics of that pass and of the last list
    //
    bool m_sealed_enabled{true};
    SealedBeadData m_sealed;
    // Mutable for the deferred list's count before culling, taken when the statistics are read (m_sealed_total_pending)
    mutable SealedBeadStats m_sealed_stats;
    mutable bool m_sealed_total_pending{false};
    //
    // Chunk culling: whether it is enabled, the chunks of the current enabled segment list (valid when built for
    // it), and the statistics
    //
    bool m_chunk_culling_enabled{true};
    ViewChunkSet m_view_chunks;
    bool m_view_chunks_valid{false};
    ViewChunkStats m_view_chunk_stats;
    //
    // A per-view selection of the chunk set: the parameters it was selected with, its segments (vertex ids) and
    // the GPU buffer holding them, drawn in place of the full enabled list. The camera passes (visible, G-buffer)
    // share one; the shadow pass draws the whole enabled list, as its frustum holds every chunk.
    //
    struct ChunkSelection
    {
        bool valid{false};
        ViewCullParams params{};
        std::vector<uint32_t> segments;
        size_t chunks{0};
        float select_ms{0.0f};
        unsigned int buf_id{0};
        unsigned int tex_id{0};
        size_t tex_size{0};
        // Every chunk kept: the pass draws the enabled list's own buffer, and nothing is uploaded
        bool use_base{false};
        // The occlusion step's draw set: its buffer and texture are the culler's, and no chunk selection made it; the
        // culler wrote its segments straight into that buffer, so `segments` stays empty and this holds their count
        bool occlusion{false};
        size_t occlusion_segments{0};
        // The segments the pass draws from it
        size_t segment_count() const { return occlusion ? occlusion_segments : segments.size(); }
    };
    ChunkSelection m_camera_selection;
    //
    // The chunk structure of the whole print (valid when the load built one; nothing is drawn from it yet), the byte
    // per sub-cell that flags the sub-cells able to hold an enabled segment under the current settings (recomputed
    // with every enabled list), and the statistics of both
    //
    PrintChunks m_print_chunks;
    std::vector<uint8_t> m_print_chunk_flags;
    PrintChunkStats m_print_chunk_stats;
    //
    // The filter of the current view range, layer range, settings and valid lines
    //
    PrintChunkFilter print_chunk_filter() const;
    //
    // Recomputes the sub-cell flags under the current filter, timed (no flags without a valid structure), and the
    // occlusion step's emission filter of the same list: with the sealed bead test when `cull` (the list was culled)
    //
    void update_print_chunk_flags(bool cull);
    //
    // Occlusion culling: whether it is on, the culler, the draw sets it gave the camera and the shadow view (their
    // buffers are the culler's), the emission filter of the current enabled list, the generations of the structure,
    // its boxes and the flags (a result is kept while they hold), the flags generation whose filter the structure has
    // applied (0: none), the visible passes drawn since the viewer was made (a camera result serves its frame at any
    // viewport size), and the statistics. The shadow view draws the whole enabled set with no step while it holds at
    // most m_occlusion_shadow_all_max segments (set from OCCLUSION_SHADOW_ALL_MAX_SEGMENTS when the viewer is made);
    // after a step, with m_occlusion_shadow_merge, its pass merges the step's depth and draws only the residual.
    //
    bool m_occlusion_enabled{true};
    size_t m_occlusion_shadow_all_max{0};
    bool m_occlusion_shadow_merge{true};
    OcclusionCuller m_occlusion;
    ChunkSelection m_occl_camera_selection;
    ChunkSelection m_occl_shadow_selection;
    PrintChunkFilter m_occlusion_filter;
    uint64_t m_print_chunks_generation{0};
    uint64_t m_print_chunk_boxes_generation{0};
    uint64_t m_print_chunk_flags_generation{0};
    uint64_t m_print_chunks_applied_generation{0};
    uint64_t m_occlusion_frame{0};
    OcclusionStats m_occlusion_stats;
    //
    // Applies the emission filter to the structure when it holds another one, timed into the filter statistics; false
    // when that failed (out of memory)
    //
    bool apply_occlusion_filter();
    //
    // Why occlusion culling does not apply to a pass (nullptr when it does)
    //
    const char *occlusion_inactive_reason() const;
    //
    // What the occlusion step reads from the viewer
    //
    OcclusionInputs occlusion_inputs() const;
    //
    // The occlusion draw set of the given view (OCCLUSION_VIEW_*) for a pass with these matrices and camera position at
    // the current viewport; nullptr, with the view's statistics saying why, when the pass draws without it. For the
    // shadow view after a step, with the merge on, it writes the step's depth into the bound target and returns the
    // residual instead (the draw set when the merge failed).
    //
    const ChunkSelection *occlusion_selection(size_t view, const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix,
                                              const Vec3 &camera_position);
    //
    // The phases of the last load; while a load runs, the GPU buffer uploads add their time and bytes to it
    //
    LoadPhaseStats m_load_stats;
    bool m_load_running{false};
#ifdef PREFLIGHT_TEST_HOOKS
    //
    // Test hooks: whether the next visible pass runs the probe, and the last probe's statistics and per-pixel classes
    //
    bool m_probe_requested{false};
    VisibilityProbeStats m_probe_stats;
    std::vector<uint8_t> m_probe_mask;
    //
    // Compares the camera's view of the whole chunk order with what the visible pass drew (the given selection, null
    // when it drew the enabled list, with its matrices and eye); restores every GL state it touches and never throws
    //
    void run_visibility_probe(const ChunkSelection *selection, const Mat4x4 &view_matrix,
                              const Mat4x4 &projection_matrix, const Vec3 &camera_position);
    //
    // Checks that the shadow map holds the nearest depth of every enabled segment, once the shadow pass drew the given
    // selection (null when it drew the enabled list) and with its program and uniforms still bound; restores every GL
    // state it touches and never throws
    //
    void run_shadow_probe(const ChunkSelection *selection);
#endif // PREFLIGHT_TEST_HOOKS
    //
    // Per layer: the extrusion sizes and the z ranges of the positions and of the bead tops (see LayerExtent); with
    // the xy range of all positions, the extent the per-sample shading bound works from
    //
    std::vector<LayerExtent> m_layers_extent;
    std::array<float, 4> m_toolpaths_xy_range{{FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX}};
    //
    // The box holding every visible toolpath: the xy range of all positions and the z range of the visible layers,
    // padded by the largest tube size; with the smallest and largest tube size. None when no tube is visible.
    //
    struct ToolpathsBox
    {
        std::array<float, 2> xs;
        std::array<float, 2> ys;
        std::array<float, 2> zs;
        float min_size;
        float max_size;
    };
    std::optional<ToolpathsBox> visible_toolpaths_box() const;
    //
    // A lower bound, in pixels of the given viewport, of the projected cross-section of every toolpath in the
    // visible layer range
    //
    float min_cross_section_px(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix, const Vec3 &camera_position,
                               int viewport_width, int viewport_height) const;
    //
    // Whether some visible toolpath can be seen from the camera position within the prefilter's view elevation
    //
    bool prefilter_can_fire(const Vec3 &camera_position) const;
    //
    // The bead tops of the bottom and top displayed layers, from the nearest layer inward that has extrusions;
    // {FLT_MAX, -FLT_MAX} when none has
    //
    std::array<float, 2> displayed_bead_tops() const;
    //
    // Whether the enabled segment list leaves out sealed beads: enabled, the pass data valid, no clipping plane and
    // every extrusion role of the toolpaths visible
    //
    bool sealed_bead_culling_applies() const;
    //
    // The loaded vertices seen in place by the load-time passes and the chunk builder: position (z is the bead top),
    // height, width and layer. Requires at least one vertex.
    //
    PrefilterVertexView vertex_view() const;
    //
    // Builds the chunk set of the given enabled segment list (drawn segments only) and times it. The given chunk set
    // of this list, when built, is taken in place of building it.
    //
    void update_view_chunks(const std::vector<uint32_t> &enabled_segments, ListChunks *prepared = nullptr);
    //
    // Takes the given enabled lists (the segments enabled before culling counted in segments_total): the sealed bead
    // statistics, the chunk set (from the given prepared one when built), the structure's flags unless the caller
    // updated them for these settings, the uploads
    //
    void apply_enabled_lists(std::vector<uint32_t> &&enabled_segments, std::vector<uint32_t> &&enabled_options,
                             size_t segments_total, bool cull, ListChunks *prepared_chunks, bool update_flags = true);
    //
    // Builds the enabled lists of the current settings and takes them (apply_enabled_lists())
    //
    void build_enabled_lists(bool cull, bool update_flags);
    //
    // Deferred enabled segment list: while occlusion culling serves every pass, a settings change applies the filter to
    // the whole print's structure and lists the options only; the segment list, its chunk set and its upload wait for
    // the first reader that needs them (a pass the occlusion step does not serve, the visibility probe), which
    // calls ensure_enabled_lists(). The structure's count of the list stands in for the list's own meanwhile.
    //
    bool m_enabled_lists_deferred{false};
    size_t m_deferred_segments{0};
    //
    // Whether the segment list can wait: occlusion culling applies and the structure holds the current filter
    //
    bool enabled_lists_deferrable() const;
    //
    // Lists the options from the index, takes the structure's counts as the list's statistics, drops the list's chunk
    // set and GPU copy, and uploads the options
    //
    void defer_enabled_lists(bool cull);
    //
    // Builds the deferred segment list of the current settings; nothing when none is deferred
    //
    void ensure_enabled_lists();
    //
    // The segments of the enabled list, built or deferred
    //
    size_t enabled_segments_count() const;
    //
    // The vertices by layer and option type, built at load: the view ranges, the options of a deferred list and the
    // colors read it
    //
    ViewIndex m_view_index;
    //
    // The work of the view updates since the last reset
    //
    ViewUpdateStats m_view_update_stats;
    //
    // The list uploads whose buffer did not take the data since the load. An enabled list update with a failed upload
    // leaves the lists to be rebuilt by the next render() (m_list_upload_retry); every list update of that render() is
    // the retry (m_list_upload_retrying), which builds the segment list rather than deferring it and leaves no rebuild
    // pending, whatever its outcome
    //
    ListUploadStats m_list_upload_stats;
    bool m_list_upload_retry{false};
    bool m_list_upload_retrying{false};
    //
    // Counts a failed list upload with its reason
    //
    void count_list_upload_failure(const std::string &reason);
    //
    // Ends an enabled list update, its uploads failed or not: whether the next render() is to rebuild the lists
    //
    bool end_enabled_list_update(bool upload_failed);
    //
    // What the color buffer holds (valid while no color and nothing a color reads has changed since it was written
    // whole): a color update writes only the vertices whose darkening changes
    //
    bool m_colors_upload_valid{false};
    ColorDarkening m_colors_upload;
    //
    // What a vertex's color reads in this viewer
    //
    ColorInputs color_inputs() const
    {
        return {m_settings,    m_extrusion_roles_colors, m_options_colors, m_ranges, m_layers,
                m_tool_colors, m_color_print_colors};
    }
    //
    // The selection to draw a camera pass with for the given view (the chunks in its frustum), recomputed and uploaded
    // only when the view differs from the one it was selected for; nullptr when chunk culling does not apply (the full
    // enabled list is drawn)
    //
    const ChunkSelection *select_chunks(ChunkSelection &selection, const Mat4x4 &view_matrix,
                                        const Mat4x4 &projection_matrix);
    //
    // Frees the camera selection's buffers and marks it stale
    //
    void reset_chunk_selection();
    //
    // Builds the toolpath prefilter program on its first use; false when it failed, then or before
    //
    bool build_prefilter_program();
    //
    // Palette used to render extrusion roles
    //
    std::array<Color, size_t(EGCodeExtrusionRole::COUNT)> m_extrusion_roles_colors;
    //
    // Palette used to render options
    //
    std::array<Color, size_t(EOptionType::COUNT)> m_options_colors;

    bool m_initialized{false};

    // Clipping plane data {nx, ny, nz, offset}, default clips nothing
    std::array<float, 4> m_clipping_plane{0.0f, 0.0f, 1.0f, std::numeric_limits<float>::max()};
    // Whether a clipping plane is set: reset_clipping_plane() leaves the default, whose offset clips nothing
    bool clipping_plane_active() const { return m_clipping_plane[3] != std::numeric_limits<float>::max(); }

    //
    // The OpenGL element used to represent all toolpath segments
    //
    SegmentTemplate m_segment_template;
    //
    // The OpenGL element used to represent all option markers
    //
    OptionTemplate m_option_template;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    //
    // The OpenGL element used to represent the center of gravity
    //
    CogMarker m_cog_marker;
    float m_cog_marker_scale_factor{1.0f};
    //
    // The OpenGL element used to represent the tool nozzle
    //
    ToolMarker m_tool_marker;
    float m_tool_marker_scale_factor{1.0f};
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    //
    // cpu buffer to store vertices
    //
    std::vector<PathVertex> m_vertices;

    // Cache for the colors to reduce the need to recalculate colors of all the vertices.
    std::vector<float> m_vertices_colors;

    //
    // Variables used for toolpaths visibiliity
    //
    BitSet<> m_valid_lines_bitset;
    //
    // Variables used for toolpaths coloring
    //
    std::optional<Settings> m_settings_used_for_ranges;
    ColorRanges m_ranges;
    // The ranges by the names the rest of the viewer uses
    ColorRange &m_height_range = m_ranges.height;
    ColorRange &m_width_range = m_ranges.width;
    ColorRange &m_speed_range = m_ranges.speed;
    ColorRange &m_actual_speed_range = m_ranges.actual_speed;
    ColorRange &m_fan_speed_range = m_ranges.fan_speed;
    ColorRange &m_temperature_range = m_ranges.temperature;
    ColorRange &m_volumetric_rate_range = m_ranges.volumetric_rate;
    ColorRange &m_actual_volumetric_rate_range = m_ranges.actual_volumetric_rate;
    std::array<ColorRange, COLOR_RANGE_TYPES_COUNT> &m_layer_time_range = m_ranges.layer_time;
    Palette m_tool_colors;
    Palette m_color_print_colors;
    //
    // OpenGL shaders ids
    //
    unsigned int m_segments_shader_id{0};
    unsigned int m_segments_gbuffer_shader_id{0};
    // Depth-only segments program of the shadow map pass; 0 when it failed to build, and the shadow pass then draws
    // with the visible program; the compiler or linker output of that failure (empty otherwise)
    unsigned int m_segments_depth_shader_id{0};
    std::string m_segments_depth_shader_log;
    // Visible segments program with the toolpath prefilter, built on the first frame that draws with it; 0 until then
    // and when it failed to build, which leaves the prefilter off until the viewer is initialized again; the
    // compiler or linker output of that failure
    unsigned int m_segments_pf_shader_id{0};
    bool m_segments_pf_shader_failed{false};
    std::string m_segments_pf_shader_log;
    unsigned int m_options_shader_id{0};
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    unsigned int m_cog_marker_shader_id{0};
    unsigned int m_tool_marker_shader_id{0};
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    //
    // Caches for OpenGL uniforms id of a visible segments program, one instance for the plain program and one for
    // the prefilter variant, so one code path sets whichever draws. The pf_ ids stay -1 in the plain program, where
    // setting them does nothing.
    //
    struct SegmentsUniforms
    {
        int view_matrix{-1};
        int projection_matrix{-1};
        int camera_position{-1};
        int positions_tex{-1};
        int height_width_angle_tex{-1};
        int colors_tex{-1};
        int segment_index_tex{-1};
        int clipping_plane{-1};
        // Scene-pass uniforms
        int shadow_vp{-1};
        int scene_passes{-1};
        int shadow_tex{-1};
        int ao_tex{-1};
        int viewport_size{-1};
        int viewport_origin{-1};
        int shadow_offset_margin{-1};
        // Toolpath prefilter uniforms
        int pf_viewport_px{-1};
        int pf_width{-1};
        int pf_fade_elevation{-1};
        int pf_fade_pitch{-1};
        int pf_fade_above{-1};
        int pf_top_z{-1};
        int pf_bottom_z{-1};

        void init(unsigned int shader_id);
    };
    SegmentsUniforms m_uni_segments;
    SegmentsUniforms m_uni_segments_pf;
    //
    // Caches for OpenGL uniforms id for the segments G-buffer shader
    //
    int m_uni_gbuffer_view_matrix_id{-1};
    int m_uni_gbuffer_projection_matrix_id{-1};
    int m_uni_gbuffer_camera_position_id{-1};
    int m_uni_gbuffer_positions_tex_id{-1};
    int m_uni_gbuffer_height_width_angle_tex_id{-1};
    int m_uni_gbuffer_colors_tex_id{-1};
    int m_uni_gbuffer_segment_index_tex_id{-1};
    int m_uni_gbuffer_clipping_plane_id{-1};
    //
    // Caches for OpenGL uniforms id for the segments depth-only shader
    //
    int m_uni_depth_view_matrix_id{-1};
    int m_uni_depth_projection_matrix_id{-1};
    int m_uni_depth_camera_position_id{-1};
    int m_uni_depth_positions_tex_id{-1};
    int m_uni_depth_height_width_angle_tex_id{-1};
    int m_uni_depth_segment_index_tex_id{-1};
    int m_uni_depth_clipping_plane_id{-1};
#ifdef PREFLIGHT_TEST_HOOKS
    //
    // Visibility probe ID program (the depth-only geometry writing each drawn instance's id); 0 when it failed to
    // build, which leaves the probe unavailable; and its uniforms
    //
    unsigned int m_segments_id_shader_id{0};
    int m_uni_id_view_matrix_id{-1};
    int m_uni_id_projection_matrix_id{-1};
    int m_uni_id_camera_position_id{-1};
    int m_uni_id_positions_tex_id{-1};
    int m_uni_id_height_width_angle_tex_id{-1};
    int m_uni_id_colors_tex_id{-1};
    int m_uni_id_segment_index_tex_id{-1};
    int m_uni_id_clipping_plane_id{-1};
    int m_uni_id_instance_id_tex_id{-1};
#endif // PREFLIGHT_TEST_HOOKS

    ScenePassParams m_scene_pass_params;
#ifdef __APPLE__
    // 1 x 1 textures bound to the shadow and AO units while the scene passes are off: the macOS driver reports a
    // sampler whose unit holds no texture, also one the program does not read
    unsigned int m_shadow_placeholder_tex_id{0};
    unsigned int m_ao_placeholder_tex_id{0};
#endif // __APPLE__
    //
    // Caches for OpenGL uniforms id for options shader
    //
    int m_uni_options_view_matrix_id{-1};
    int m_uni_options_projection_matrix_id{-1};
    int m_uni_options_positions_tex_id{-1};
    int m_uni_options_height_width_angle_tex_id{-1};
    int m_uni_options_colors_tex_id{-1};
    int m_uni_options_segment_index_tex_id{-1};
    int m_uni_options_clipping_plane_id{-1};
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    //
    // Caches for OpenGL uniforms id for cog marker shader
    //
    int m_uni_cog_marker_world_center_position{-1};
    int m_uni_cog_marker_scale_factor{-1};
    int m_uni_cog_marker_view_matrix{-1};
    int m_uni_cog_marker_projection_matrix{-1};
    //
    // Caches for OpenGL uniforms id for tool marker shader
    //
    int m_uni_tool_marker_world_origin{-1};
    int m_uni_tool_marker_scale_factor{-1};
    int m_uni_tool_marker_view_matrix{-1};
    int m_uni_tool_marker_projection_matrix{-1};
    int m_uni_tool_marker_color_base{-1};
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    //
    // OpenGL buffers to store positions
    //
    unsigned int m_positions_buf_id{0};
    unsigned int m_positions_tex_id{0};
    //
    // OpenGL buffers to store heights, widths and angles
    //
    unsigned int m_heights_widths_angles_buf_id{0};
    unsigned int m_heights_widths_angles_tex_id{0};
    //
    // OpenGL buffers to store colors
    //
    unsigned int m_colors_buf_id{0};
    unsigned int m_colors_tex_id{0};
    //
    // OpenGL buffers to store enabled segments
    //
    unsigned int m_enabled_segments_buf_id{0};
    unsigned int m_enabled_segments_tex_id{0};
    size_t m_enabled_segments_count{0};
    //
    // OpenGL buffers to store enabled options
    //
    unsigned int m_enabled_options_buf_id{0};
    unsigned int m_enabled_options_tex_id{0};
    size_t m_enabled_options_count{0};
    //
    // Caches for size of data sent to gpu, in bytes
    //
    size_t m_positions_tex_size{0};
    size_t m_height_width_angle_tex_size{0};
    size_t m_colors_tex_size{0};
    size_t m_enabled_segments_tex_size{0};
    size_t m_enabled_options_tex_size{0};

    void update_view_full_range();
    void update_color_ranges();
    void update_heights_widths();
    void render_segments(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix, const Vec3 &camera_position);
    // Binds the segment texture buffers, draws the enabled segments (or the given chunk selection of them) with the
    // currently bound program, and restores the previous buffer bindings.
    void draw_enabled_segments(const ChunkSelection *selection = nullptr);
    // The same with an explicit segment list (texture buffer over a GL_R32UI buffer of vertex ids)
    void draw_segment_list(unsigned int segments_tex_id, unsigned int segments_buf_id, size_t segments_count);
    void render_options(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix);
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    void render_cog_marker(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix);
    void render_tool_marker(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
};

} // namespace libvgcode
