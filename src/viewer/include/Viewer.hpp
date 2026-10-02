///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "Types.hpp"
#include "PreparedLoad.hpp"

#include <functional>
#include <string>

namespace libvgcode
{

class ViewerImpl;
struct GCodeInputData;
struct PathVertex;
class ColorRange;
struct ColorPrint;

//
// Load-time wall neighbour search of the toolpath prefilter: the vertices on a wall segment, those with a bead
// found above or below, the wall segments left out for a non-finite or out-of-range attribute, the threads that
// searched, the search time, and why the search failed (empty when it ran; the prefilter then leaves every bead
// unfiltered).
//
struct PrefilterNeighbourStats
{
    size_t wall_vertices{0};
    size_t found_vertices{0};
    size_t excluded_segments{0};
    // Wall segments that took a bead that does not continue their surface, for want of one that does
    size_t unmatched_segments{0};
    unsigned search_threads{0};
    float search_ms{0.0f};
    std::string error;
};

//
// Sealed bead culling: the load-time pass (the extrusion segments it classified, those it hides in the full view,
// its threads, raster resolution and time, and why it failed: empty when it ran, culling is then off), and the last
// enabled segment list: whether culling applied to it, and the segments it drew out of those enabled before culling.
//
struct SealedBeadStats
{
    size_t segments{0};
    size_t sealed_full_view{0};
    unsigned threads{0};
    float resolution_mm{0.0f};
    float ms{0.0f};
    std::string error;
    bool active{false};
    size_t segments_drawn{0};
    size_t segments_total{0};
};

//
// Per-frame chunk culling: the chunks of the current enabled segment list and the time to build them; of the last
// visible pass, whether it drew a chunk selection (active) and the chunks and segments it drew (the whole list when
// not active); of the last shadow pass, the chunks and segments it drew; and the time of the last camera selection.
//
struct ViewChunkStats
{
    size_t chunks_total{0};
    // Sub-cells of the current chunks
    size_t subcells_total{0};
    size_t chunks_drawn{0};
    size_t segments_drawn{0};
    size_t shadow_chunks_drawn{0};
    size_t shadow_segments_drawn{0};
    float build_ms{0.0f};
    float select_ms{0.0f};
    bool active{false};
};

//
// The chunk structure of the whole print, built once per load (desktop OpenGL only) and filtered by the view settings:
// whether the load has one, its chunks, sub-cells, segments and the bytes it holds, its build time; of the last enabled
// list update, the time to flag the sub-cells that can hold an enabled segment (with the filter's application, when
// one ran), and how many it flagged. With occlusion culling on, the filter is applied to the structure (each
// sub-cell's enabled segments moved to the front of its slot): the time of the last application and the sub-cells it
// partitioned again (all of them, or those a change of the range or the types alone can change).
//
struct PrintChunkStats
{
    bool valid{false};
    size_t chunks{0};
    size_t subcells{0};
    size_t segments{0};
    size_t bytes{0};
    float build_ms{0.0f};
    float filter_ms{0.0f};
    size_t candidate_subcells{0};
    float apply_ms{0.0f};
    size_t applied_subcells{0};
};

//
// Occlusion culling of one view (the camera's, which the G-buffer and the visible pass share, or the shadow pass's) at
// its last pass: whether the pass drew the occlusion step's draw set (active) and why not ("off", "not available",
// "no structure", "clipping plane", "program failed", "target failed", or a failure of the step: "no viewport", "too
// many sub-cells", "upload failed", "draw failed", "test failed", "readback failed", "out of memory"); of the result
// drawn, the box tests run (rounds), the points tested (chunk boxes, and sub-cell boxes of the chunks that passed, over
// the rounds), the sub-cells that passed the last test, the segments drawn as occluders over the rounds, the enabled
// segments of the sub-cells that passed the first test and were not predicted (new_segments), the segments of the draw
// set, the step's lists written through a copy because their buffer could not be mapped and why the last one was
// ("map failed", "unmap failed"; empty for none), and the step's wall time (0 when the pass took the result of an
// unchanged view, whose other figures are those of the step that made it). With `all` the draw set is the whole
// enabled set and no step ran: no rounds and no points tested, every sub-cell that holds an enabled segment visible,
// and the time that of writing the list (0 when the pass took it unchanged). For the shadow view, `merged` when the
// pass wrote the step's occluder depth into the shadow map and drew only the residual (the enabled segments of the
// sub-cells that passed the last test and were not drawn as occluders) in place of the draw set, and the residual's
// segments when the step of that pass wrote one (0 for a stored or whole-set result).
//
struct OcclusionViewStats
{
    bool active{false};
    bool all{false};
    bool merged{false};
    std::string reason{"off"};
    size_t rounds{0};
    size_t chunks_tested{0};
    size_t subcells_tested{0};
    size_t subcells_visible{0};
    size_t occluder_segments{0};
    size_t new_segments{0};
    size_t segments_drawn{0};
    size_t residual_segments{0};
    size_t map_fallbacks{0};
    std::string map_fallback_reason;
    float ms{0.0f};
};

//
// Occlusion culling of one view, accumulated since the last reset_occlusion_bench() or load: the steps run, the
// passes that took a stored result, over the steps the box tests, the points tested, the segments of the draw sets, the
// segments drawn as occluders, the sub-cells that passed and the new segments (OcclusionViewStats), and the steps' wall
// time in milliseconds, in all and by part: choosing, writing and uploading occluders (emit); drawing them (depth);
// the pyramid; the box test draws (test); the readbacks, which wait for the GPU (readback); ordering, writing and
// uploading the draw set, and the residual when one is written (list). The draw parts time the GL calls, not the
// GPU's work, which the readback waits for. `all` counts the passes that drew the whole enabled set with no step,
// written or taken unchanged; they are in neither steps nor cached, and add nothing to the other figures. `merged`
// counts the passes that merged their step's depth and drew only its residual, and `residual_segments` sums those
// residuals' segments.
//
struct OcclusionBenchStats
{
    size_t steps{0};
    size_t cached{0};
    size_t all{0};
    size_t merged{0};
    size_t residual_segments{0};
    size_t rounds{0};
    size_t points{0};
    size_t segments{0};
    size_t occluders{0};
    size_t visible{0};
    size_t new_segments{0};
    double ms{0.0};
    double emit_ms{0.0};
    double depth_ms{0.0};
    double pyramid_ms{0.0};
    double test_ms{0.0};
    double readback_ms{0.0};
    double list_ms{0.0};
};

//
// Occlusion culling (desktop OpenGL only, off by default): the switch, each view's last pass, since the last load
// the steps run, the passes that took a step's stored result (a pass that drew the whole enabled set is in neither)
// and the lists written through a copy because their buffer could not be mapped (with why the last one was), the
// shadow passes whose step could not merge its depth, which drew the whole draw set instead (with why the last one
// could not: "occluder cap", "upload failed", "list short", "no step depth", "viewport size", "merge program failed",
// "merge failed"), and each view's accumulated timings.
//
struct OcclusionStats
{
    bool enabled{false};
    OcclusionViewStats camera;
    OcclusionViewStats shadow;
    size_t steps{0};
    size_t cached{0};
    size_t map_fallbacks{0};
    std::string map_fallback_reason;
    size_t shadow_merge_fallbacks{0};
    std::string shadow_merge_reason;
    OcclusionBenchStats camera_bench;
    OcclusionBenchStats shadow_bench;
};

//
// The work of the view updates (a change of the layer range, the moves range or the visible types, and the frames
// after it) since the last reset_view_update_stats(), wall milliseconds per part: the view ranges
// (compute_view_full_range), the color buffer updates with the bytes they wrote, the enabled lists (the segment and
// option lists, or the option list alone with its upload while the segment list is deferred to the first pass that
// needs it) with the updates that deferred it, the chunk set of the segment list, the uploads of the built lists with
// their bytes, and the filter of the whole print's structure (its flags and its application).
//
struct ViewUpdateStats
{
    double view_range_ms{0.0};
    double colors_ms{0.0};
    size_t colors_bytes{0};
    double lists_ms{0.0};
    size_t lists_deferred{0};
    double chunks_ms{0.0};
    double upload_ms{0.0};
    size_t upload_bytes{0};
    double filter_ms{0.0};

    double total_ms() const { return view_range_ms + colors_ms + lists_ms + chunks_ms + upload_ms + filter_ms; }
};

//
// The enabled list and chunk selection uploads since the load (desktop OpenGL only): those whose buffer did not take
// the data (a GL error, or a buffer size other than the bytes handed over), the enabled list rebuilds made because of
// one (the next frame's, at most one per failed update), and why the last one failed ("gl error 0x0505", "size 0,
// expected 4096"; a size of -1 is a size query that wrote nothing). Empty and 0 by design.
//
struct ListUploadStats
{
    size_t failures{0};
    size_t retries{0};
    std::string reason;
};

#ifdef PREFLIGHT_TEST_HOOKS
//
// Visibility probe (test hooks builds only): one visible pass with a chunk set compared with what its camera sees.
// The camera's view of the whole chunk order is rendered as order positions (true-size beads, nearest surface, at the
// viewport's size, in a target of its own), so each pixel has its segment and sub-cell, and every sub-cell that owns a
// pixel is classed: dropped by the chunk selection (frustum; none when the pass drew no selection) or drawn by the
// pass. Pixels are those of the probe's target. Every probe also runs the occlusion box test and its faces variant
// against that view's own depth (box_*). A shadow pass that runs in the probe's frame, before its visible pass, checks
// the shadow map it drew (shadow_*).
//
struct VisibilityProbeStats
{
    bool ran{false};
    std::string error; // why it did not run (empty when it did)
    int width{0};
    int height{0};
    // Segments: of the enabled list (the chunk order), drawn by the pass, owning a pixel, held by the sub-cells that
    // own a pixel, held by the chunks that hold such a sub-cell; and those chunks
    size_t list_segments{0};
    size_t drawn_segments{0};
    size_t visible_segments{0};
    size_t view_segments{0};
    size_t view_chunk_segments{0};
    size_t view_chunks{0};
    size_t view_subcells{0}; // sub-cells that own at least one pixel
    size_t view_pixels{0};   // toolpath pixels
    size_t chunk_culled_subcells{0};
    size_t chunk_culled_pixels{0};
    float ms{0.0f}; // without the box test below
    // The occlusion box test against the view's own depth, through its farthest-depth pyramid. Chunk boxes: tested,
    // passed, off the view or beyond the far plane (the rest are occluded), and the segments of those that passed.
    // Sub-cell boxes: those of the chunks that passed (tested), those that passed and their segments (the draw set),
    // the same with the full-resolution depth in place of the pyramid. Then the sub-cells that own a pixel and are not
    // in the draw set, and the time of the box test.
    size_t box_chunks_tested{0};
    size_t box_chunks_visible{0};
    size_t box_chunks_outside{0};
    size_t box_chunk_segments{0};
    size_t box_subcells_tested{0};
    size_t box_subcells_visible{0};
    size_t box_segments{0};
    size_t box_exact_subcells_visible{0};
    size_t box_exact_segments{0};
    size_t box_misses{0};
    float box_ms{0.0f};
    // The faces test (each box's front faces bound its depth per texel, read at the level where its rectangle covers at
    // most 4 or 8 texels per axis). Of the sub-cells tested above: those that pass with 4 taps, their segments and the
    // sub-cells that own a pixel and do not pass; the same with 8 taps. Chunk boxes with 4 taps, in place of the box
    // test: those that pass, their segments, and the sub-cells that own a pixel in a chunk that does not. The time of
    // the faces test.
    size_t box4_subcells_visible{0};
    size_t box4_segments{0};
    size_t box4_misses{0};
    size_t box8_subcells_visible{0};
    size_t box8_segments{0};
    size_t box8_misses{0};
    size_t box4_chunks_visible{0};
    size_t box4_chunk_segments{0};
    size_t box4_chunk_misses{0};
    float box_faces_ms{0.0f};
    // The faces test on the chunk structure of the whole print, with its tight boxes: 4 texels on the box of every
    // chunk that holds a flagged sub-cell (tested, passed), then 8 on the flagged sub-cells of the chunks that pass
    // (tested, passed); the enabled segments of the sub-cells that pass (what the design would draw); its sub-cells
    // that own a pixel and do not pass (their chunk or their own box failed, or their flag is 0); the enabled segments
    // of every sub-cell; the time of this part.
    size_t pc_chunks_tested{0};
    size_t pc_chunks_visible{0};
    size_t pc_subcells_tested{0};
    size_t pc_subcells_visible{0};
    size_t pc_segments{0};
    size_t pc_misses{0};
    size_t pc_enabled_segments{0};
    float pc_ms{0.0f};
    // The segment test (8 texels per axis) on each enabled segment of the sub-cells that pass above, its ends' balls
    // the positions and max(half height, half width) the segments shader reads: the segments tested (pc_segments),
    // those that pass (what a third level would draw), those that own a pixel and do not pass (their sub-cell failed,
    // or their own test; the design needs 0), the segments that own a pixel (visible_segments), the time of this part
    // (which pc_ms leaves out).
    size_t seg_tested{0};
    size_t seg_visible{0};
    size_t seg_misses{0};
    size_t seg_owners{0};
    float seg_ms{0.0f};
    // When the pass drew the occlusion draw set (occl): its segments; the whole print's sub-cells that own a pixel of
    // the view and are not in the camera view's draw set, those pixels and the segments of them that own one (the
    // design needs 0; occl_error says why they could not be counted). Then the cross-check of the GPU box test against
    // test_box_occlusion_faces on the CPU, both on the culler's depth of one round from the camera view's draw set:
    // the chunks and flagged sub-cells compared, those only the GPU passes (harmless) and those only the CPU passes
    // (unsafe, expected 0), and why it did not run.
    bool occl{false};
    size_t occl_drawn_segments{0};
    size_t occl_missing_subcells{0};
    size_t occl_missing_pixels{0};
    size_t occl_missing_segments{0};
    std::string occl_error;
    size_t occl_xcheck_chunks{0};
    size_t occl_xcheck_chunk_gpu_only{0};
    size_t occl_xcheck_chunk_cpu_only{0};
    size_t occl_xcheck_subcells{0};
    size_t occl_xcheck_gpu_only{0};
    size_t occl_xcheck_cpu_only{0};
    std::string occl_xcheck_error;
    // The shadow map check (reset by the request, kept by the camera probe): the light's view of the whole enabled
    // list drawn with the shadow pass's program into a depth target of its own, compared with the map the pass left.
    // Whether it ran and why not (both unset when the frame had no shadow pass); the texels a bead covers, those where
    // the bead lies nearer than the map (missing from it; the design needs 0) and the largest such gap in window
    // depth; the segments the pass drew and the enabled list's; the time.
    bool shadow_ran{false};
    std::string shadow_error;
    size_t shadow_texels{0};
    size_t shadow_missing_texels{0};
    float shadow_max_gap{0.0f};
    size_t shadow_segments_drawn{0};
    size_t shadow_list_segments{0};
    float shadow_ms{0.0f};
};
#endif // PREFLIGHT_TEST_HOOKS

//
// The phases of the last load, wall times in milliseconds. load() fills its own: the CPU preparation (everything in
// the preparation and the install that is neither a GPU upload call nor one of the four passes, wherever the
// preparation ran), the GPU uploads (the time spent in the GL buffer and texture calls, accumulated; the GPU is not
// waited for), the four passes (the times their own statistics hold: the sealed bead pass, the prefilter's neighbour
// search, the chunk build of the load's enabled list, the build of the chunk structure of the whole print), the
// vertices loaded and the bytes handed to the buffer uploads. The host times the phases around its call to load() and the total, and the rest below: where the
// preparation ran and why not off the UI thread, the UI thread's part of the load, the host's work after the load
// returned, the first frame drawing the loaded toolpaths, and the longest time the UI thread stayed away from its
// event loop from the start of the slice to that frame (measured only in builds with the test hooks).
//
struct LoadPhaseStats
{
    float convert_ms{0.0f};
    float viewer_cpu_ms{0.0f};
    float gl_upload_ms{0.0f};
    float sealed_ms{0.0f};
    float pf_search_ms{0.0f};
    float chunk_build_ms{0.0f};
    float print_chunks_ms{0.0f};
    float cog_ms{0.0f};
    float bounds_ms{0.0f};
    float gcode_window_ms{0.0f};
    float total_ms{0.0f};
    size_t gl_upload_bytes{0};
    size_t vertices{0};
    bool prepared_off_ui{false};
    std::string prepare_fallback;
    float install_ms{0.0f};
    float post_ms{0.0f};
    float first_frame_ms{0.0f};
    float ui_max_stall_ms{0.0f};
    // The install by part: taking the prepared tables, the enabled lists step and the colors step (each with its
    // uploads, which gl_upload_ms also holds); the preparation's view stage; whether the install took the prepared
    // enabled lists and colors, and the view settings that differed from the preparation's (empty when none)
    float install_swap_ms{0.0f};
    float install_enabled_ms{0.0f};
    float install_colors_ms{0.0f};
    float prepare_view_ms{0.0f};
    // The install's index of the vertices by layer and option type (part of the CPU preparation)
    float view_index_ms{0.0f};
    bool lists_prepared{false};
    bool colors_prepared{false};
    std::string view_settings_changed;
    // The host's work after the load by part: the release of the result's moves on the UI thread, the layer range
    // calls, the moves slider update (with the visible range calls it makes) and those visible range calls
    float post_release_ms{0.0f};
    float post_layers_range_ms{0.0f};
    float post_moves_slider_ms{0.0f};
    float post_visible_range_ms{0.0f};
};

class Viewer
{
public:
    Viewer();
    ~Viewer();
    Viewer(const Viewer &other) = delete;
    Viewer(Viewer &&other) = delete;
    Viewer &operator=(const Viewer &other) = delete;
    Viewer &operator=(Viewer &&other) = delete;

    //
    // Initialize the viewer.
    // Param opengl_context_version must be the string returned by glGetString(GL_VERSION).
    // This method must be called after a valid OpenGL context has been already created
    // and before calling any other method of the viewer.
    // Throws an std::runtime_error exception if:
    // * the method is called before creating an OpenGL context
    // * the created OpenGL context does not support OpenGL 3.2 or greater
    // * any of the shaders fails to compile, except the toolpath prefilter program, which is built on the first
    //   render that draws with it; its failure leaves the prefilter off (see get_toolpath_prefilter_shader_log())
    //
    void init(const std::string &opengl_context_version);
    //
    // Release the resources used by the viewer.
    // This method must be called before releasing the OpenGL context if the viewer
    // goes out of scope after releasing it.
    //
    void shutdown();
    //
    // Reset the contents of the viewer.
    // Automatically called by load() method.
    //
    void reset();
    //
    // Setup the viewer content from the given data: prepare() then load() of the prepared load, on the calling
    // thread.
    // See: GCodeInputData
    //
    void load(GCodeInputData &&gcode_data);
    //
    // The CPU part of load(), on any thread (see PreparedLoad and prepare_load()). The settings are the viewer's as
    // get_prepare_settings() returns them on the thread that owns the viewer. Throws PrepareCanceled when `canceled`
    // returns true.
    //
    static PreparedLoad prepare(GCodeInputData &&gcode_data, const PrepareSettings &settings,
                                const std::function<void(float)> &progress = {},
                                const std::function<bool()> &canceled = {});
    //
    // The settings a preparation for this viewer reads: the travel and wipe radii, whether the prefilter's
    // per-vertex neighbour data is kept.
    //
    PrepareSettings get_prepare_settings() const;
    //
    // The view settings the enabled lists and the colors read, as this viewer holds them (see ViewSettings).
    //
    ViewSettings get_view_settings() const;
    //
    // The extrusion role colors reset_default_extrusion_roles_colors() sets.
    //
    static const std::array<Color, GCODE_EXTRUSION_ROLES_COUNT> &get_default_extrusion_roles_colors();
    //
    // Installs a prepared load: takes its data, uploads the buffers (a radius changed since the preparation is
    // applied to them) and builds the tables that depend on the current settings, as load() does. On the thread that
    // owns the OpenGL context.
    //
    void load(PreparedLoad &&prepared);
    //
    // Render the toolpaths according to the current settings and
    // using the given camera matrices.
    //
    void render(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix);

    //
    // Scene passes (Full lighting tier): set the host's shadow/AO parameters for
    // the visible render, and draw the enabled segments for a host-driven pass
    // (shadow map: light matrices/position, gbuffer = false; G-buffer: camera
    // matrices/position, gbuffer = true).
    //
    void set_scene_pass_params(const ScenePassParams &params);
    void render_segments_pass(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix, const Vec3 &camera_position,
                              bool gbuffer);

    //
    // ************************************************************************
    // Settings
    // The following methods can be used to query/customize the parameters
    // used to render the toolpaths.
    // ************************************************************************
    //

    //
    // View type
    // See: EViewType
    //
    EViewType get_view_type() const;
    void set_view_type(EViewType type);
    //
    // Time mode
    // See: ETimeMode
    //
    ETimeMode get_time_mode() const;
    void set_time_mode(ETimeMode mode);
    //
    // Top layer only
    // Whether or not the visible range is limited to the current top layer only.
    //
    bool is_top_layer_only_view_range() const;
    //
    // Toggle the top layer only state.
    //
    void toggle_top_layer_only_view_range();
    //
    // Returns true if the given option is visible.
    //
    bool is_option_visible(EOptionType type) const;
    //
    // Toggle the visibility state of the given option.
    //
    void toggle_option_visibility(EOptionType type);
    //
    // Returns true if the given extrusion role is visible.
    //
    bool is_extrusion_role_visible(EGCodeExtrusionRole role) const;
    //
    // Toggle the visibility state of the given extrusion role.
    //
    void toggle_extrusion_role_visibility(EGCodeExtrusionRole role);
    //
    // Return the color used to render the given extrusion rols.
    //
    const Color &get_extrusion_role_color(EGCodeExtrusionRole role) const;
    //
    // Set the color used to render the given extrusion role.
    //
    void set_extrusion_role_color(EGCodeExtrusionRole role, const Color &color);
    //
    // Reset the colors used to render the extrusion roles to the default value.
    //
    void reset_default_extrusion_roles_colors();
    //
    // Return the color used to render the given option.
    //
    const Color &get_option_color(EOptionType type) const;
    //
    // Set the color used to render the given option.
    //
    void set_option_color(EOptionType type, const Color &color);
    //
    // Reset the colors used to render the options to the default value.
    //
    void reset_default_options_colors();
    //
    // Return the count of colors in the palette used to render
    // the toolpaths when the view type is EViewType::Tool.
    //
    size_t get_tool_colors_count() const;
    //
    // Return the palette used to render the toolpaths when
    // the view type is EViewType::Tool.
    //
    const Palette &get_tool_colors() const;
    //
    // Set the palette used to render the toolpaths when
    // the view type is EViewType::Tool with the given one.
    //
    void set_tool_colors(const Palette &colors);
    //
    // Return the count of colors in the palette used to render
    // the toolpaths when the view type is EViewType::ColorPrint.
    //
    size_t get_color_print_colors_count() const;
    //
    // Return the palette used to render the toolpaths when
    // the view type is EViewType::ColorPrint.
    //
    const Palette &get_color_print_colors() const;
    //
    // Set the palette used to render the toolpaths when
    // the view type is EViewType::ColorPrint with the given one.
    //
    void set_color_print_colors(const Palette &colors);
    //
    // Get the color range for the given view type.
    // Valid view types are:
    // EViewType::Height
    // EViewType::Width
    // EViewType::Speed
    // EViewType::ActualSpeed
    // EViewType::FanSpeed
    // EViewType::Temperature
    // EViewType::VolumetricFlowRate
    // EViewType::ActualVolumetricFlowRate
    // EViewType::LayerTimeLinear
    // EViewType::LayerTimeLogarithmic
    //
    const ColorRange &get_color_range(EViewType type) const;
    //
    // Set the palette for the color range corresponding to the given view type
    // with the given value.
    // Valid view types are:
    // EViewType::Height
    // EViewType::Width
    // EViewType::Speed
    // EViewType::ActualSpeed
    // EViewType::FanSpeed
    // EViewType::Temperature
    // EViewType::VolumetricFlowRate
    // EViewType::ActualVolumetricFlowRate
    // EViewType::LayerTimeLinear
    // EViewType::LayerTimeLogarithmic
    //
    void set_color_range_palette(EViewType type, const Palette &palette);
    //
    // Get the radius, in mm, of the cylinders used to render the travel moves.
    //
    float get_travels_radius() const;
    //
    // Set the radius, in mm, of the cylinders used to render the travel moves.
    // Radius is clamped to [MIN_TRAVELS_RADIUS_MM..MAX_TRAVELS_RADIUS_MM]
    //
    void set_travels_radius(float radius);
    //
    // Get the radius, in mm, of the cylinders used to render the wipe moves.
    //
    float get_wipes_radius() const;
    //
    // Set the radius, in mm, of the cylinders used to render the wipe moves.
    // Radius is clamped to [MIN_WIPES_RADIUS_MM..MAX_WIPES_RADIUS_MM]
    //
    void set_wipes_radius(float radius);
    //
    // Per-sample shading of the toolpaths when the target is multisampled (needs OpenGL 4.0): MSAA then
    // resolves their shading from every sample. It is used only in frames where some visible toolpath can
    // project its cross-section to a few pixels. Default: on.
    //
    bool get_sample_shading() const;
    void set_sample_shading(bool enable);
    //
    // Whether the last toolpath draw used per-sample shading, whether the gate kept it off, and why
    // ("prefilter active", "pixel budget", "sections resolvable"; empty when it was on or did not apply).
    //
    bool is_sample_shading_active() const;
    bool is_sample_shading_gated() const;
    const std::string &get_sample_shading_reason() const;
    //
    // Screen-space prefilter of the wall shading of perimeters seen near level, or from above up to steep views
    // where the load found the next layer; on by default.
    //
    void set_toolpath_prefilter(bool enable);
    bool get_toolpath_prefilter() const;
    //
    // Target pixels per output pixel: the supersampling scale, 1 without supersampling.
    //
    void set_output_pixel_scale(float scale);
    //
    // Whether the last visible toolpath draw used the prefilter program, and why not when it did not
    // ("disabled", "no view in range", "shader failed to compile"; empty when used).
    //
    bool is_toolpath_prefilter_active() const;
    const std::string &get_toolpath_prefilter_reason() const;
    //
    // The compiler or linker output of the prefilter program when it failed to build on its first use; empty
    // otherwise. A failure lasts until the viewer is initialized again.
    //
    const std::string &get_toolpath_prefilter_shader_log() const;
    //
    // Statistics of the prefilter's wall neighbour search of the loaded toolpaths.
    //
    const PrefilterNeighbourStats &get_prefilter_neighbour_stats() const;
    //
    // Keep the per-vertex neighbour data below after each load; by default it is dropped once uploaded.
    //
    void set_keep_prefilter_neighbours(bool keep);
    //
    // Per-vertex neighbour data of the loaded toolpaths, indexed like the vertices; empty before a load, in ES builds
    // and unless kept (set_keep_prefilter_neighbours). Offsets: the XY step (mm) from the vertex to the wall bead one
    // layer above. Flags: 1 the vertex is on a wall segment, 2 a bead above or below it was found.
    //
    const std::vector<float> &get_prefilter_offsets_x() const;
    const std::vector<float> &get_prefilter_offsets_y() const;
    const std::vector<uint8_t> &get_prefilter_flags() const;
    //
    // Sealed bead culling: extrusion beads that cannot be seen from outside the print in the displayed layer range
    // are left out of the draw. It applies only while no clipping plane is set and every extrusion role is visible;
    // on by default.
    //
    void set_sealed_bead_culling(bool enable);
    bool get_sealed_bead_culling() const;
    //
    // Statistics of the load-time pass and of the last enabled segment list.
    //
    const SealedBeadStats &get_sealed_bead_stats() const;
    //
    // Chunk culling: each camera pass draws only the chunks of the enabled segments that lie in the view frustum.
    // Desktop OpenGL only; on by default.
    //
    void set_chunk_culling(bool enable);
    bool get_chunk_culling() const;
    //
    // Statistics of the chunk set and of the last passes' draws.
    //
    const ViewChunkStats &get_view_chunk_stats() const;
    //
    // Statistics of the chunk structure of the whole print and of its last filter (see PrintChunkStats).
    //
    const PrintChunkStats &get_print_chunk_stats() const;
    //
    // Occlusion culling: each camera pass (G-buffer, visible) and the shadow pass draws only the sub-cells of the whole
    // print's chunk structure whose boxes are not hidden behind the depth of what was drawn for that view, refined
    // until the sub-cells it uncovers fit a budget; a view that does not change keeps its result. It applies with the
    // chunk structure built, no clipping plane set and its programs built, in desktop OpenGL; otherwise each pass draws
    // as with it off. On by default.
    //
    void set_occlusion_culling(bool enable);
    bool get_occlusion_culling() const;
    //
    // The most segments of the last draw set (nearest first) a step draws as its first occluders; 0, the default, draws
    // them all. Any subset of the drawn segments is a sound occluder set: a smaller one costs less to draw and lets
    // more boxes pass.
    //
    void set_occlusion_occluder_cap(size_t segments);
    size_t get_occlusion_occluder_cap() const;
    //
    // The most enabled segments the shadow pass draws whole, with no occlusion step, while occlusion culling applies:
    // the light turns with the camera, so the shadow view would run a step every frame of an orbit, and below this
    // count drawing every enabled segment costs less and is exact. 0 never does; the default is 3 million (safe range
    // 1 to 4 million).
    //
    void set_occlusion_shadow_all_max(size_t segments);
    size_t get_occlusion_shadow_all_max() const;
    //
    // After a shadow step, the shadow pass writes the step's occluder depth into the shadow map and draws only the
    // segments that depth lacks, in place of the whole draw set; the map is the same. For comparisons; on by default.
    //
    void set_occlusion_shadow_merge(bool merge);
    bool get_occlusion_shadow_merge() const;
    //
    // Statistics of its last passes (see OcclusionStats), and the reset of their accumulated timings.
    //
    const OcclusionStats &get_occlusion_stats() const;
    void reset_occlusion_bench();
    //
    // The work of the view updates since the last reset (see ViewUpdateStats), and that reset.
    //
    const ViewUpdateStats &get_view_update_stats() const;
    void reset_view_update_stats();
    //
    // The list uploads whose buffer did not take the data since the load (see ListUploadStats).
    //
    const ListUploadStats &get_list_upload_stats() const;
#ifdef PREFLIGHT_TEST_HOOKS
    //
    // Test hooks: the next visible pass runs the visibility probe (see VisibilityProbeStats) once it has drawn; a pass
    // without a chunk set leaves the probe's error set. A shadow pass before it checks the shadow map it drew.
    //
    void request_visibility_probe();
    const VisibilityProbeStats &get_visibility_probe_stats() const;
    //
    // The probe's class of each pixel, width x height bytes, rows bottom up: 0 no toolpath, 1 drawn by the pass, 2
    // dropped by the chunk selection or, when the pass drew the occlusion draw set, owned by a sub-cell that set left
    // out. Empty when the probe did not run.
    //
    const std::vector<uint8_t> &get_visibility_probe_mask() const;
#endif // PREFLIGHT_TEST_HOOKS
    //
    // The phases of the last load() (see LoadPhaseStats; the host's phases are 0 here).
    //
    const LoadPhaseStats &get_load_phase_stats() const;
    //
    // Return the count of detected layers.
    //
    size_t get_layers_count() const;
    //
    // Return the current visible layers range.
    //
    const Interval &get_layers_view_range() const;
    //
    // Set the current visible layers range with the given interval.
    // Values are clamped to [0..get_layers_count() - 1].
    //
    void set_layers_view_range(const Interval &range);
    //
    // Set the current visible layers range with the given min and max values.
    // Values are clamped to [0..get_layers_count() - 1].
    //
    void set_layers_view_range(Interval::value_type min, Interval::value_type max);
    //
    // Return the current visible range.
    // Three ranges are defined: full, enabled and visible.
    // For all of them the range endpoints represent:
    // [0] -> min vertex id
    // [1] -> max vertex id
    // Full is the range of vertices that could potentially be visualized accordingly to the current settings.
    // Enabled is the part of the full range that is selected for visualization accordingly to the current settings.
    // Visible is the part of the enabled range that is actually visualized accordingly to the current settings.
    //
    const Interval &get_view_visible_range() const;
    //
    // Set the current visible range.
    // Values are clamped to the current view enabled range;
    //
    void set_view_visible_range(Interval::value_type min, Interval::value_type max);
    //
    // Return the current full range.
    //
    const Interval &get_view_full_range() const;
    //
    // Return the current enabled range.
    //
    const Interval &get_view_enabled_range() const;

    //
    // ************************************************************************
    // Property getters
    // The following methods can be used to query detected properties.
    // ************************************************************************
    //

    //
    // Spiral vase mode
    // Whether or not the gcode was generated with spiral vase mode enabled.
    // See: GCodeInputData
    //
    bool is_spiral_vase_mode() const;
    //
    // Return the z of the layer with the given id
    // or 0.0f if the id does not belong to [0..get_layers_count() - 1].
    //
    float get_layer_z(size_t layer_id) const;
    //
    // Return the list of zs of the detected layers.
    //
    std::vector<float> get_layers_zs() const;
    //
    // Return the id of the layer closest to the given z.
    //
    size_t get_layer_id_at(float z) const;
    //
    // Return the count of detected used extruders.
    //
    size_t get_used_extruders_count() const;
    //
    // Return the list of ids of the detected used extruders.
    //
    std::vector<uint8_t> get_used_extruders_ids() const;
    //
    // Return the list of detected time modes.
    //
    std::vector<ETimeMode> get_time_modes() const;
    //
    // Return the count of vertices used to render the toolpaths
    //
    size_t get_vertices_count() const;
    //
    // Return the vertex pointed by the max value of the view visible range
    //
    const PathVertex &get_current_vertex() const;
    //
    // Return the index of vertex pointed by the max value of the view visible range
    //
    size_t get_current_vertex_id() const;
    //
    // Return the vertex at the given index
    //
    const PathVertex &get_vertex_at(size_t id) const;
    //
    // Return the total estimated time, in seconds, using the current time mode.
    //
    float get_estimated_time() const;
    //
    // Return the estimated time, in seconds, at the vertex with the given index
    // using the current time mode.
    //
    float get_estimated_time_at(size_t id) const;
    //
    // Return the color used to render the given vertex with the current settings.
    //
    Color get_vertex_color(const PathVertex &vertex) const;
    //
    // Return the count of detected extrusion roles
    //
    size_t get_extrusion_roles_count() const;
    //
    // Return the list of detected extrusion roles
    //
    std::vector<EGCodeExtrusionRole> get_extrusion_roles() const;
    //
    // Return the count of detected options.
    //
    size_t get_options_count() const;
    //
    // Return the list of detected options.
    //
    const std::vector<EOptionType> &get_options() const;
    //
    // Return the count of detected color prints.
    //
    size_t get_color_prints_count(uint8_t extruder_id) const;
    //
    // Return the list of detected color prints.
    //
    std::vector<ColorPrint> get_color_prints(uint8_t extruder_id) const;
    //
    // Return the estimated time for the given role and the current time mode.
    //
    float get_extrusion_role_estimated_time(EGCodeExtrusionRole role) const;
    //
    // Return the estimated time for the travel moves and the current time mode.
    //
    float get_travels_estimated_time() const;
    //
    // Return the list of layers time for the current time mode.
    //
    std::vector<float> get_layers_estimated_times() const;
    //
    // Return the axes aligned bounding box containing all the given types.
    //
    AABox get_bounding_box(const std::vector<EMoveType> &types = {EMoveType::Retract, EMoveType::Unretract,
                                                                  EMoveType::Seam, EMoveType::ToolChange,
                                                                  EMoveType::ColorChange, EMoveType::PausePrint,
                                                                  EMoveType::CustomGCode, EMoveType::Travel,
                                                                  EMoveType::Wipe, EMoveType::Extrude}) const;
    //
    // Return the axes aligned bounding box containing all the extrusions with the given roles.
    //
    AABox get_extrusion_bounding_box(const std::vector<EGCodeExtrusionRole> &roles = {
                                         EGCodeExtrusionRole::Perimeter, EGCodeExtrusionRole::ExternalPerimeter,
                                         EGCodeExtrusionRole::OverhangPerimeter, EGCodeExtrusionRole::InternalInfill,
                                         EGCodeExtrusionRole::SolidInfill, EGCodeExtrusionRole::TopSolidInfill,
                                         EGCodeExtrusionRole::Ironing, EGCodeExtrusionRole::BridgeInfill,
                                         EGCodeExtrusionRole::GapFill, EGCodeExtrusionRole::Skirt,
                                         EGCodeExtrusionRole::SupportMaterial,
                                         EGCodeExtrusionRole::SupportMaterialInterface, EGCodeExtrusionRole::WipeTower,
                                         EGCodeExtrusionRole::Custom}) const;
    //
    // Return the size of the used cpu memory, in bytes
    //
    size_t get_used_cpu_memory() const;
    //
    // Return the size of the used gpu memory, in bytes
    //
    size_t get_used_gpu_memory() const;

    //
    // Set/reset the clipping plane for toolpath rendering.
    // The plane is defined as (nx, ny, nz, offset) where fragments with
    // dot(vec4(pos, 1.0), clipping_plane) < 0 are discarded.
    // Default state clips nothing (offset = FLT_MAX).
    //
    void set_clipping_plane(float nx, float ny, float nz, float offset);
    void reset_clipping_plane();

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    //
    // Returns the position of the center of gravity of the toolpaths.
    // It does not take in account extrusions of type:
    // Skirt
    // Support Material
    // Support Material Interface
    // WipeTower
    // Custom
    //
    Vec3 get_cog_position() const;

    float get_cog_marker_scale_factor() const;
    void set_cog_marker_scale_factor(float factor);

    const Vec3 &get_tool_marker_position() const;

    float get_tool_marker_offset_z() const;
    void set_tool_marker_offset_z(float offset_z);

    float get_tool_marker_scale_factor() const;
    void set_tool_marker_scale_factor(float factor);

    const Color &get_tool_marker_color() const;
    void set_tool_marker_color(const Color &color);

    float get_tool_marker_alpha() const;
    void set_tool_marker_alpha(float alpha);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

private:
    ViewerImpl *m_impl{nullptr};
};

} // namespace libvgcode
