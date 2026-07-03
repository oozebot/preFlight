///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_preFlight_GCodeViewer_hpp_
#define slic3r_preFlight_GCodeViewer_hpp_

#include "3DScene.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include <functional>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GLModel.hpp"
#include "preFlight.PreviewClipController.hpp"

#include "LibVGCode/LibVGCodeWrapper.hpp"
// needed for tech VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "../../src/libvgcode/include/Types.hpp"

#include <cstdint>
#include <float.h>
#include <optional>
#include <set>
#include <unordered_set>

namespace Slic3r
{

class AppConfig;
class BuildVolume;
class GLShaderProgram;
class Model;
class PresetBundle;
class Print;
class TriangleMesh;

namespace GUI
{

struct Camera;
class GLCanvas3D;
class ImGuiWrapper;
class NotificationManager;

class GCodeViewer
{
    // helper to render shells
    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{false};
        bool force_visible{false};
        double progress_height{0.0}; // Current Z height for progressive clipping (0.0 to max_z)
    };

    // helper to render center of gravity
    class COG
    {
        GLModel m_model;
        bool m_visible{false};
#if !VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        // whether or not to render the model with fixed screen size
        bool m_fixed_screen_size{true};
#endif // !VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        float m_scale_factor{1.0f};
        double m_total_mass{0.0};
        Vec3d m_total_position{Vec3d::Zero()};

    public:
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        void render(bool fixed_screen_size, const Camera &camera, GLShaderProgram *shader);
#else
        void render(const Camera &camera, GLShaderProgram *shader);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

        void reset()
        {
            m_total_position = Vec3d::Zero();
            m_total_mass = 0.0;
        }

        bool is_visible() const { return m_visible; }
        void set_visible(bool visible) { m_visible = visible; }

        void add_segment(const Vec3d &v1, const Vec3d &v2, double mass)
        {
            if (mass > 0.0)
            {
                m_total_position += mass * 0.5 * (v1 + v2);
                m_total_mass += mass;
            }
        }

        Vec3d cog() const { return (m_total_mass > 0.0) ? (Vec3d) (m_total_position / m_total_mass) : Vec3d::Zero(); }

    private:
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        void init(bool fixed_screen_size){
#else
        void init()
        {
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            if (m_model.is_initialized()) return;

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        const float radius = fixed_screen_size ? 10.0f : 1.0f;
#else
            const float radius = m_fixed_screen_size ? 10.0f : 1.0f;
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        m_model.init_from(smooth_sphere(32, radius));
    }
};

public:
struct SequentialView
{
#if ENABLE_ACTUAL_SPEED_DEBUG
    struct ActualSpeedImguiWidget
    {
        std::pair<float, float> y_range = {0.0f, 0.0f};
        std::vector<std::pair<float, ColorRGBA>> levels;
        struct Item
        {
            float pos{0.0f};
            float speed{0.0f};
            bool internal{false};
        };
        std::vector<Item> data;
        int plot(const char *label, const std::array<float, 2> &frame_size = {0.0f, 0.0f});
    };
#endif // ENABLE_ACTUAL_SPEED_DEBUG

    class Marker
    {
        ImGuiWrapper *m_imgui{nullptr};
        GLCanvas3D *m_canvas{nullptr};
        GLModel m_model;
        GLModel m_model_ht90_rod;
        Vec3f m_world_position;
        // For seams, the position of the marker is on the last endpoint of the toolpath containing it.
        // This offset is used to show the correct value of tool position in the "ToolPosition" window.
        // See implementation of render() method
        Vec3f m_world_offset;
        // z offset of the print
        float m_z_offset{0.0f};
        // z offset of the model
        float m_model_z_offset{0.5f};
        bool m_visible{true};
        bool m_fixed_screen_size{false};
        float m_scale_factor{1.0f};
        bool m_generic_marker{true};
        bool m_is_ht90{false};
#if ENABLE_ACTUAL_SPEED_DEBUG
        ActualSpeedImguiWidget m_actual_speed_imgui_widget;
#endif // ENABLE_ACTUAL_SPEED_DEBUG

    public:
        void init(std::optional<std::unique_ptr<GLModel>> &model_opt, bool is_ht90);

        void set_world_position(const Vec3f &position) { m_world_position = position; }
        void set_world_offset(const Vec3f &offset) { m_world_offset = offset; }
        void set_z_offset(float z_offset) { m_z_offset = z_offset; }

#if ENABLE_ACTUAL_SPEED_DEBUG
        void set_actual_speed_y_range(const std::pair<float, float> &y_range)
        {
            m_actual_speed_imgui_widget.y_range = y_range;
        }
        void set_actual_speed_levels(const std::vector<std::pair<float, ColorRGBA>> &levels)
        {
            m_actual_speed_imgui_widget.levels = levels;
        }
        void set_actual_speed_data(const std::vector<ActualSpeedImguiWidget::Item> &data)
        {
            m_actual_speed_imgui_widget.data = data;
        }
#endif // ENABLE_ACTUAL_SPEED_DEBUG

        bool is_visible() const { return m_visible; }
        void set_visible(bool visible) { m_visible = visible; }

        void set_imgui(ImGuiWrapper *imgui) { m_imgui = imgui; }
        void set_canvas(GLCanvas3D *canvas) { m_canvas = canvas; }
        void render(const Camera &camera, GLShaderProgram *shader);
        void render_position_window(const libvgcode::Viewer *viewer);
    };

    class GCodeWindow
    {
        ImGuiWrapper *m_imgui{nullptr};
        GLCanvas3D *m_canvas{nullptr};
        struct Line
        {
            std::string command;
            std::string parameters;
            std::string comment;
        };

        struct Range
        {
            std::optional<size_t> min;
            std::optional<size_t> max;
            bool empty() const { return !min.has_value() || !max.has_value(); }
            bool contains(const Range &other) const
            {
                return !this->empty() && !other.empty() && *this->min <= *other.min && *this->max >= other.max;
            }
            size_t size() const { return empty() ? 0 : *this->max - *this->min + 1; }
        };

        bool m_visible{true};
        std::string m_filename;
        bool m_is_binary_file{false};
        // map for accessing data in file by line number
        std::vector<std::vector<size_t>> m_lines_ends;
        std::vector<Line> m_lines_cache;
        Range m_cache_range;
        size_t m_max_line_length{0};

        GCodeObject *const *m_gcode_object_ref = nullptr;
        GCodeObject *gcode_object() const
        {
            return (m_gcode_object_ref && *m_gcode_object_ref) ? *m_gcode_object_ref : nullptr;
        }

        int m_scroll_request{0};

    public:
        void load_gcode(const GCodeProcessorResult &gcode_result);
        void reset()
        {
            m_lines_ends.clear();
            m_lines_cache.clear();
            m_filename.clear();
            m_gcode_object_ref = nullptr;
        }
        void toggle_visibility() { m_visible = !m_visible; }
        void set_imgui(ImGuiWrapper *imgui) { m_imgui = imgui; }
        void set_canvas(GLCanvas3D *canvas) { m_canvas = canvas; }
        void render(float top, float bottom, size_t curr_line_id, float legend_width = 0.0f);

        int get_and_clear_scroll_request()
        {
            int request = m_scroll_request;
            m_scroll_request = 0;
            return request;
        }

    private:
        void add_gcode_line_to_lines_cache(const std::string &src);
    };

    Marker marker;
    GCodeWindow gcode_window;
    GLCanvas3D *canvas{nullptr};

    void render(float legend_height, const libvgcode::Viewer *viewer, uint32_t gcode_id, const Camera &camera,
                GLShaderProgram *marker_shader, float legend_width = 0.0f);
};

private:
Camera *m_camera{nullptr};
ImGuiWrapper *m_imgui{nullptr};
NotificationManager *m_notification_manager{nullptr};
GLCanvas3D *m_canvas{nullptr};
const AppConfig *m_app_config{nullptr};
std::function<void(bool)> m_enable_preview_moves_slider;
std::function<void()> m_update_preview_moves_slider;
std::function<void(std::optional<int>, std::optional<int>)> m_update_preview_moves_slider_range;
std::function<void(bool)> m_set_keep_current_preview_type;
std::function<bool()> m_is_sidebar_collapsed;
std::function<const BuildVolume &()> m_get_build_volume;
std::function<Model &()> m_get_model;
std::function<void(const std::vector<Vec2d> &, double, const std::string &, const std::string &, bool)> m_set_bed_shape;
std::function<void()> m_set_default_bed_shape;
const PresetBundle *m_preset_bundle{nullptr};
std::function<bool()> m_is_editor;
std::function<bool()> m_is_gcode_viewer;
std::function<int()> m_em_unit;
std::function<GLShaderProgram *(const std::string &)> m_get_shader;
std::function<GLShaderProgram *()> m_get_current_shader;
bool m_gl_data_initialized{false};
unsigned int m_last_result_id{0};
// bounding box of toolpaths
BoundingBoxf3 m_paths_bounding_box;
// bounding box of shells
BoundingBoxf3 m_shells_bounding_box;
// bounding box of toolpaths + marker tools + shells
BoundingBoxf3 m_max_bounding_box;
float m_max_print_height{0.0f};
float m_z_offset{0.0f};
size_t m_extruders_count;
std::vector<float> m_filament_diameters;
std::vector<float> m_filament_densities;
std::vector<float> m_filament_costs;
float m_time_cost{0.0f};
std::string m_currency_symbol{"$"};

// Precomputed job cost estimate, populated once at load time
struct JobEstimate
{
    struct ExtruderEstimate
    {
        double filament_m{0.0};
        double filament_g{0.0};
        double cost{0.0};
        double wipe_tower_g{0.0}; // waste subset of filament_g
    };
    std::map<size_t, ExtruderEstimate> per_extruder;
    double total_filament_m{0.0};
    double total_filament_g{0.0};
    double total_material_cost{0.0};
    double total_wipe_tower_g{0.0};
    float time_cost_per_hour{0.0f};
    bool has_cost_data{false};

    void reset()
    {
        per_extruder.clear();
        total_filament_m = 0.0;
        total_filament_g = 0.0;
        total_material_cost = 0.0;
        total_wipe_tower_g = 0.0;
        time_cost_per_hour = 0.0f;
        has_cost_data = false;
    }
};
JobEstimate m_job_estimate;

SequentialView m_sequential_view;
Shells m_shells;
COG m_cog;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
// whether or not to render the cog model with fixed screen size
bool m_cog_marker_fixed_screen_size{true};
float m_cog_marker_size{1.0f};
bool m_tool_marker_fixed_screen_size{false};
float m_tool_marker_size{1.0f};
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
bool m_legend_visible{true};
bool m_legend_enabled{true};
struct ViewTypeCache
{
    bool write{false};
    bool load{false};
    libvgcode::EViewType value{libvgcode::EViewType::FeatureType};
};
ViewTypeCache m_view_type_cache;

struct LegendResizer
{
    bool dirty{true};
    void reset() { dirty = true; }
};
LegendResizer m_legend_resizer;
float m_legend_width{0.0f};
PrintEstimatedStatistics m_print_statistics;
GCodeProcessorResult::SettingsIds m_settings_ids;

std::vector<CustomGCode::Item> m_custom_gcode_per_print_z;

bool m_contained_in_bed{true};

ConflictResultOpt m_conflict_result;
std::optional<std::pair<std::string, std::string>> m_sequential_collision_detected;

std::array<GCodeProcessorResult::RoleMetrics, static_cast<size_t>(GCodeExtrusionRole::Count)> m_role_metrics{};
GCodeProcessorResult::RoleMetrics m_overall_metrics{};

libvgcode::Viewer m_viewer;
bool m_loaded_as_preview{false};

public:
GCodeViewer();
~GCodeViewer()
{
    reset();
}

void set_camera(Camera *camera)
{
    m_camera = camera;
}
const Camera &get_camera() const
{
    return *m_camera;
}
void set_imgui(ImGuiWrapper *imgui)
{
    m_imgui = imgui;
    m_sequential_view.marker.set_imgui(imgui);
    m_sequential_view.gcode_window.set_imgui(imgui);
}
ImGuiWrapper *get_imgui() const
{
    return m_imgui;
}
void set_notification_manager(NotificationManager *nm)
{
    m_notification_manager = nm;
}
NotificationManager *get_notification_manager() const
{
    return m_notification_manager;
}
void set_canvas(GLCanvas3D *canvas)
{
    m_canvas = canvas;
    m_sequential_view.canvas = canvas;
    m_sequential_view.marker.set_canvas(canvas);
    m_sequential_view.gcode_window.set_canvas(canvas);
}
GLCanvas3D *canvas() const
{
    return m_canvas;
}
void set_app_config(const AppConfig *config)
{
    m_app_config = config;
}
const AppConfig *app_config() const
{
    return m_app_config;
}
void set_preset_bundle(const PresetBundle *bundle)
{
    m_preset_bundle = bundle;
}
void set_plater_callbacks(
    std::function<void(bool)> enable_slider, std::function<void()> update_slider,
    std::function<void(std::optional<int>, std::optional<int>)> update_slider_range,
    std::function<void(bool)> set_keep_preview_type, std::function<bool()> is_sidebar_collapsed,
    std::function<const BuildVolume &()> get_build_volume, std::function<Model &()> get_model,
    std::function<void(const std::vector<Vec2d> &, double, const std::string &, const std::string &, bool)>
        set_bed_shape,
    std::function<void()> set_default_bed_shape)
{
    m_enable_preview_moves_slider = std::move(enable_slider);
    m_update_preview_moves_slider = std::move(update_slider);
    m_update_preview_moves_slider_range = std::move(update_slider_range);
    m_set_keep_current_preview_type = std::move(set_keep_preview_type);
    m_is_sidebar_collapsed = std::move(is_sidebar_collapsed);
    m_get_build_volume = std::move(get_build_volume);
    m_get_model = std::move(get_model);
    m_set_bed_shape = std::move(set_bed_shape);
    m_set_default_bed_shape = std::move(set_default_bed_shape);
}
void set_app_state(std::function<bool()> is_editor, std::function<bool()> is_gcode_viewer, std::function<int()> em_unit)
{
    m_is_editor = std::move(is_editor);
    m_is_gcode_viewer = std::move(is_gcode_viewer);
    m_em_unit = std::move(em_unit);
}
bool is_editor() const
{
    return m_is_editor && m_is_editor();
}
bool is_gcode_viewer() const
{
    return m_is_gcode_viewer && m_is_gcode_viewer();
}
int em_unit() const
{
    return m_em_unit ? m_em_unit() : 10;
}
void set_shader_getters(std::function<GLShaderProgram *(const std::string &)> get_shader,
                        std::function<GLShaderProgram *()> get_current_shader)
{
    m_get_shader = std::move(get_shader);
    m_get_current_shader = std::move(get_current_shader);
}
GLShaderProgram *get_shader(const std::string &name) const
{
    return m_get_shader ? m_get_shader(name) : nullptr;
}
GLShaderProgram *get_current_shader() const
{
    return m_get_current_shader ? m_get_current_shader() : nullptr;
}

void init();

// extract rendering data from the given parameters
void load_as_gcode(const GCodeProcessorResult &gcode_result, const Print &print,
                   const std::vector<std::string> &str_tool_colors,
                   const std::vector<std::string> &str_color_print_colors);
void load_as_preview(libvgcode::GCodeInputData &&data);
void update_shells_color_by_extruder(const DynamicPrintConfig *config);

void reset();
void render();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
void render_cog()
{
    if (!m_loaded_as_preview && m_viewer.get_layers_count() > 0)
        m_cog.render(m_cog_marker_fixed_screen_size, get_camera(), get_shader("toolpaths_cog"));
}
#else
    void render_cog()
    {
        if (!m_loaded_as_preview && m_viewer.get_layers_count() > 0)
            m_cog.render(get_camera(), get_shader("toolpaths_cog"));
    }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
bool has_data() const
{
    return !m_viewer.get_extrusion_roles().empty();
}

bool can_export_toolpaths() const;

const BoundingBoxf3 &get_paths_bounding_box() const
{
    return m_paths_bounding_box;
}
const BoundingBoxf3 &get_shells_bounding_box() const
{
    return m_shells_bounding_box;
}

const BoundingBoxf3 &get_max_bounding_box() const
{
    BoundingBoxf3 &max_bounding_box = const_cast<BoundingBoxf3 &>(m_max_bounding_box);
    if (!max_bounding_box.defined)
    {
        if (m_shells_bounding_box.defined)
            max_bounding_box = m_shells_bounding_box;
        if (m_paths_bounding_box.defined)
        {
            max_bounding_box.merge(m_paths_bounding_box);
            // Reserve tool-marker headroom above the paths from the stable build height so the
            // preview frustum stays fixed when the marker model changes.
            max_bounding_box.merge(m_paths_bounding_box.max + static_cast<double>(m_max_print_height) * Vec3d::UnitZ());
        }
    }
    return m_max_bounding_box;
}

std::vector<double> get_layers_zs() const
{
    const std::vector<float> zs = m_viewer.get_layers_zs();
    std::vector<double> ret;
    std::transform(zs.begin(), zs.end(), std::back_inserter(ret), [](float z) { return static_cast<double>(z); });
    return ret;
}
std::vector<float> get_layers_times() const
{
    return m_viewer.get_layers_estimated_times();
}

const SequentialView &get_sequential_view() const
{
    return m_sequential_view;
}
SequentialView &get_sequential_view()
{
    return m_sequential_view;
}
void update_sequential_view_current(unsigned int first, unsigned int last);

const libvgcode::Interval &get_gcode_view_full_range() const
{
    return m_viewer.get_view_full_range();
}
const libvgcode::Interval &get_gcode_view_enabled_range() const
{
    return m_viewer.get_view_enabled_range();
}
const libvgcode::Interval &get_gcode_view_visible_range() const
{
    return m_viewer.get_view_visible_range();
}
const libvgcode::Interval &get_layers_view_range() const
{
    return m_viewer.get_layers_view_range();
}
const libvgcode::PathVertex &get_gcode_vertex_at(size_t id) const
{
    return m_viewer.get_vertex_at(id);
}

bool is_contained_in_bed() const
{
    return m_contained_in_bed;
}

void set_view_type(libvgcode::EViewType type)
{
    m_viewer.set_view_type((m_view_type_cache.load && m_view_type_cache.value != type) ? m_view_type_cache.value
                                                                                       : type);
    const libvgcode::EViewType view_type = get_view_type();
    if (m_view_type_cache.write && m_view_type_cache.value != view_type)
        m_view_type_cache.value = view_type;
}

libvgcode::EViewType get_view_type() const
{
    return m_viewer.get_view_type();
}
void enable_view_type_cache_load(bool enable)
{
    m_view_type_cache.load = enable;
}
void enable_view_type_cache_write(bool enable)
{
    m_view_type_cache.write = enable;
}
bool is_view_type_cache_load_enabled() const
{
    return m_view_type_cache.load;
}
bool is_view_type_cache_write_enabled() const
{
    return m_view_type_cache.write;
}
void set_layers_z_range(const std::array<unsigned int, 2> &layers_z_range);

bool is_legend_shown() const
{
    return m_legend_visible && m_legend_enabled;
}
void show_legend(bool show)
{
    m_legend_visible = show;
}
void enable_legend(bool enable)
{
    m_legend_enabled = enable;
}

void set_force_shells_visible(bool visible)
{
    m_shells.force_visible = visible;
}

void set_shell_progress_height(double height)
{
    m_shells.progress_height = height;
}

// preFlight: Preview clipping plane support
PreviewClipController &get_preview_clip_controller()
{
    return m_preview_clip_controller;
}
const PreviewClipController &get_preview_clip_controller() const
{
    return m_preview_clip_controller;
}
GLVolumeCollection &get_shells_volumes()
{
    return m_shells.volumes;
}
bool are_shells_visible() const
{
    return m_shells.visible;
}
void set_shells_visible(bool visible)
{
    m_shells.visible = visible;
}
libvgcode::Viewer &get_libvgcode_viewer()
{
    return m_viewer;
}
void set_preview_clipping_plane(const std::array<double, 4> &plane)
{
    m_preview_clipping_plane = plane;
}
void reset_preview_clipping_plane()
{
    m_preview_clipping_plane.reset();
}

void export_toolpaths_to_obj(const char *filename) const;

void toggle_gcode_window_visibility()
{
    m_sequential_view.gcode_window.toggle_visibility();
}

size_t get_extruders_count()
{
    return m_extruders_count;
}

void invalidate_legend()
{
    m_legend_resizer.reset();
}

const ConflictResultOpt &get_conflict_result() const
{
    return m_conflict_result;
}
std::optional<std::pair<std::string, std::string>> get_sequential_collision_detected() const
{
    return m_sequential_collision_detected;
}

void load_shells(const Print &print);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
float get_cog_marker_scale_factor() const
{
    return m_viewer.get_cog_marker_scale_factor();
}
void set_cog_marker_scale_factor(float factor)
{
    return m_viewer.set_cog_marker_scale_factor(factor);
}
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

private:
void load_wipetower_shell(const Print &print);
void render_toolpaths();
void render_shells();
void render_legend(float &legend_height);

// preFlight: Preview clipping plane
PreviewClipController m_preview_clip_controller;
std::optional<std::array<double, 4>> m_preview_clipping_plane; // set by PreviewClipController
}; // namespace GUI

} // namespace Slic3r
} // namespace Slic3r

#endif // slic3r_preFlight_GCodeViewer_hpp_
