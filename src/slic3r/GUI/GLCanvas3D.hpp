///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include <stddef.h>
#include <memory>
#include <chrono>
#include <cstdint>

#include "GLToolbar.hpp"
#include "Event.hpp"
#include "EventBridge.hpp"
#include "InputEvents.hpp"
#include "GLSurface.hpp"
#include "ITimer.hpp"
#include "Selection.hpp"
#include "Gizmos/GLGizmosManager.hpp"
#include "GUI_ObjectLayers.hpp"
#include "GLSelectionRectangle.hpp"
#include "MeshUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GCodeViewer.hpp"
#include "Camera.hpp"
#include "SceneRaycaster.hpp"
#include "GUI_Utils.hpp"

#include <arrange-wrapper/ArrangeSettingsDb_AppCfg.hpp>
#include "ArrangeSettingsDialogImgui.hpp"

#include "libslic3r/Slicing.hpp"

#include <float.h>
#include <functional>

class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxPaintEvent;
class wxGLCanvas;

// Support for Retina OpenGL on Mac OS.
// wxGTK3 seems to simulate OSX behavior in regard to HiDPI scaling support, enable it as well.
#define ENABLE_RETINA_GL (__APPLE__ || __WXGTK3__)

namespace Slic3r
{

class AppConfig;
class BackgroundSlicingProcess;
class PresetBundle;
class BuildVolume;
struct ThumbnailData;
struct ThumbnailsParams;
class ModelObject;
class ModelInstance;
class PrintObject;
class Print;
class SLAPrint;
namespace CustomGCode
{
struct Item;
}

namespace GUI
{

class Bed3D;
class ImGuiWrapper;
class Mouse3DController;
class Worker;
class NotificationManager;

#if ENABLE_RETINA_GL
class RetinaHelper;
#endif

class Size
{
    int m_width{0};
    int m_height{0};
    float m_scale_factor{1.0f};

public:
    Size() = default;
    Size(int width, int height, float scale_factor = 1.0f)
        : m_width(width), m_height(height), m_scale_factor(scale_factor)
    {
    }

    int get_width() const { return m_width; }
    void set_width(int width) { m_width = width; }

    int get_height() const { return m_height; }
    void set_height(int height) { m_height = height; }

    float get_scale_factor() const { return m_scale_factor; }
    void set_scale_factor(float factor) { m_scale_factor = factor; }
};

wxDECLARE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);

using Vec2dEvent = Event<Vec2d>;
// _bool_ value is used as a indicator of selection in the 3DScene
using RBtnEvent = Event<std::pair<Vec2d, bool>>;
template<size_t N>
using Vec2dsEvent = ArrayEvent<Vec2d, N>;

using Vec3dEvent = Event<Vec3d>;
template<size_t N>
using Vec3dsEvent = ArrayEvent<Vec3d, N>;

using HeightProfileSmoothEvent = Event<HeightProfileSmoothingParams>;
using StringEvent = Event<std::string>;

wxDECLARE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_FORCE_INVALIDATE_SLICE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, RBtnEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ARRANGE_CURRENT_BED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>); // data: +1 => increase, -1 => decrease
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_FORCE_UPDATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_WIPETOWER_TOUCHED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESET_SKEW, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_MIRRORED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDECLARE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESETGIZMOS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SLIDERS_MANIPULATION, wxKeyEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UNDO, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_REDO, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, Event<float>);
wxDECLARE_EVENT(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, HeightProfileSmoothEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RELOAD_FROM_DISK, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAKE_SNAPSHOT, StringEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAKE_SNAPSHOT_SELECTION, StringEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_INFO_ITEMS, Event<int>);
wxDECLARE_EVENT(EVT_GLCANVAS_OBJ_LIST_SELECTION_CHANGED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_SELECTIONS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_HIDE_SLICE_BUTTON, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SHOW_SLICE_BUTTON, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TOGGLE_RENDER_STATISTIC_DIALOG, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SHOW_AUTOSLICING_ACTION_BUTTONS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SWITCH_TO_AUTOSLICING, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SWITCH_FROM_AUTOSLICING, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_OBJECT_LIST_CHANGED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR_TOGGLE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_MINIMIZE_WINDOW, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAKE_GIZMO_SNAPSHOT, StringEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_MANIPULATION_DIRTY, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_MANIPULATION_UPDATE_AND_SHOW, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_CHANGED_OBJECT, Event<int>);
wxDECLARE_EVENT(EVT_GLCANVAS_SET_PLATER_DIRTY, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_PLATER_UPDATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAB_UPDATE_DIRTY, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_OBJ_LIST_UPDATE_AFTER_UNDO_REDO, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_OBJ_LIST_ADD_OBJECT, Event<int>);
wxDECLARE_EVENT(EVT_GLCANVAS_OBJ_LIST_UPDATE_ITEM_ERROR_ICON, Event<int>);
wxDECLARE_EVENT(EVT_GLCANVAS_SCALE_SELECTION_TO_FIT_PRINT_VOLUME, SimpleEvent);

class GLCanvas3D
{
    static const double DefaultCameraZoomToBoxMarginFactor;

public:
    using ShaderGetterFn = std::function<GLShaderProgram *(const std::string &)>;
    using CurrentShaderGetterFn = std::function<GLShaderProgram *()>;

private:
    class LayersEditing
    {
    public:
        enum EState : unsigned char
        {
            Unknown,
            Editing,
            Completed,
            Paused,
            Num_States
        };

        static const float THICKNESS_BAR_WIDTH;

    private:
        bool m_enabled{false};
        unsigned int m_z_texture_id{0};
        ShaderGetterFn m_get_shader;
        // Not owned by LayersEditing.
        const DynamicPrintConfig *m_config{nullptr};
        // ModelObject for the currently selected object (Model::objects[last_object_id]).
        const ModelObject *m_model_object{nullptr};
        // Maximum z of the currently selected object (Model::objects[last_object_id]).
        float m_object_max_z{0.0f};
        // Owned by LayersEditing.
        SlicingParameters *m_slicing_parameters{nullptr};
        std::vector<double> m_layer_height_profile;
        bool m_layer_height_profile_modified{false};
        // Shrinkage compensation to apply when we need to use object_max_z with Z compensation.
        Vec3d m_shrinkage_compensation{Vec3d::Ones()};

        mutable float m_adaptive_quality{0.5f};
        mutable HeightProfileSmoothingParams m_smooth_params;

        static float s_overlay_window_width;

        struct LayersTexture
        {
            // Texture data
            std::vector<char> data;
            // Width of the texture, top level.
            size_t width{0};
            // Height of the texture, top level.
            size_t height{0};
            // For how many levels of detail is the data allocated?
            size_t levels{0};
            // Number of texture cells allocated for the height texture.
            size_t cells{0};
            // Does it need to be refreshed?
            bool valid{false};
        };
        LayersTexture m_layers_texture;

    public:
        EState state{Unknown};
        float band_width{2.0f};
        float strength{0.005f};
        int last_object_id{-1};
        float last_z{0.0f};
        LayerHeightEditActionType last_action{LAYER_HEIGHT_EDIT_ACTION_INCREASE};

        struct Profile
        {
            GLModel baseline;
            GLModel profile;
            GLModel background;
            struct OldCanvasWidth
            {
                float background{0.0f};
                float baseline{0.0f};
                float profile{0.0f};
            };
            OldCanvasWidth old_canvas_width;
            std::vector<double> old_layer_height_profile;
        };
        Profile m_profile;

        LayersEditing() = default;
        ~LayersEditing();

        void init();

        void set_config(const DynamicPrintConfig *config);
        void select_object(const Model &model, int object_id);
        void set_shader_getter(ShaderGetterFn fn) { m_get_shader = std::move(fn); }

        bool is_allowed() const;

        bool is_enabled() const { return m_enabled; }
        void set_enabled(bool enabled) { m_enabled = is_allowed() && enabled; }

        void render_overlay(const GLCanvas3D &canvas);
        void render_volumes(const GLCanvas3D &canvas, const GLVolumeCollection &volumes);

        void adjust_layer_height_profile();
        void accept_changes(GLCanvas3D &canvas);
        void reset_layer_height_profile(GLCanvas3D &canvas);
        void adaptive_layer_height_profile(GLCanvas3D &canvas, float quality_factor);
        void smooth_layer_height_profile(GLCanvas3D &canvas, const HeightProfileSmoothingParams &smoothing_params);

        static float get_cursor_z_relative(const GLCanvas3D &canvas);
        static bool bar_rect_contains(const GLCanvas3D &canvas, float x, float y);
        static Rect get_bar_rect_screen(const GLCanvas3D &canvas);
        static float get_overlay_window_width() { return LayersEditing::s_overlay_window_width; }

        float object_max_z() const { return m_object_max_z; }

        std::string get_tooltip(const GLCanvas3D &canvas) const;

        std::pair<SlicingParameters, const std::vector<double>> get_layers_height_data();

        void set_shrinkage_compensation(const Vec3d &shrinkage_compensation)
        {
            m_shrinkage_compensation = shrinkage_compensation;
        };

    private:
        bool is_initialized() const;
        void generate_layer_height_texture();
        void render_active_object_annotations(const GLCanvas3D &canvas);
        void render_profile(const GLCanvas3D &canvas);
        void update_slicing_parameters();

        static float thickness_bar_width(const GLCanvas3D &canvas);
    };

    struct Mouse
    {
        struct Drag
        {
            static const Point Invalid_2D_Point;
            static const Vec3d Invalid_3D_Point;
            static const int MoveThresholdPx;

            Point start_position_2D{Invalid_2D_Point};
            Vec3d start_position_3D{Invalid_3D_Point};
            Vec3d camera_start_target{Invalid_3D_Point};
            int move_volume_idx{-1};
            bool move_requires_threshold{false};
            Point move_start_threshold_position_2D{Invalid_2D_Point};
        };

        bool dragging{false};
        Vec2d position{DBL_MAX, DBL_MAX};
        Vec3d scene_position{DBL_MAX, DBL_MAX, DBL_MAX};
        bool ignore_left_up{false};
#ifndef _WIN32
        bool left_down_on_canvas{false}; // preFlight: tracks genuine LeftDown to filter phantom LeftUp (Linux/macOS)
#endif
        Drag drag;

        void set_start_position_2D_as_invalid() { drag.start_position_2D = Drag::Invalid_2D_Point; }
        void set_start_position_3D_as_invalid() { drag.start_position_3D = Drag::Invalid_3D_Point; }
        void set_camera_start_target_as_invalid() { drag.camera_start_target = Drag::Invalid_3D_Point; }
        void set_move_start_threshold_position_2D_as_invalid()
        {
            drag.move_start_threshold_position_2D = Drag::Invalid_2D_Point;
        }

        bool is_start_position_2D_defined() const { return drag.start_position_2D != Drag::Invalid_2D_Point; }
        bool is_start_position_3D_defined() const { return drag.start_position_3D != Drag::Invalid_3D_Point; }
        bool is_camera_start_target_defined() { return drag.camera_start_target != Drag::Invalid_3D_Point; }

        bool is_move_start_threshold_position_2D_defined() const
        {
            return (drag.move_start_threshold_position_2D != Drag::Invalid_2D_Point);
        }
        bool is_move_threshold_met(const Point &mouse_pos) const
        {
            return (std::abs(mouse_pos(0) - drag.move_start_threshold_position_2D(0)) > Drag::MoveThresholdPx) ||
                   (std::abs(mouse_pos(1) - drag.move_start_threshold_position_2D(1)) > Drag::MoveThresholdPx);
        }
    };

    struct SlaCap
    {
        struct Triangles
        {
            GLModel object;
            GLModel supports;
        };
        typedef std::map<unsigned int, Triangles> ObjectIdToModelsMap;
        double z;
        ObjectIdToModelsMap triangles;

        SlaCap() { reset(); }
        void reset()
        {
            z = DBL_MAX;
            triangles.clear();
        }
        bool matches(double z) const { return this->z == z; }
    };

    enum class EWarning
    {
        ObjectOutside,
        ToolpathOutside,
        SlaSupportsOutside,
        SomethingNotShown,
        ObjectClashed,
        GCodeConflict,
        SequentialCollision
    };

    class RenderStats
    {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_measuring_start;
        int m_fps_out = -1;
        int m_fps_running = 0;

    public:
        void increment_fps_counter() { ++m_fps_running; }
        int get_fps() { return m_fps_out; }
        int get_fps_and_reset_if_needed()
        {
            auto cur_time = std::chrono::high_resolution_clock::now();
            int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(cur_time - m_measuring_start).count();
            if (elapsed_ms > 1000 || m_fps_out == -1)
            {
                m_measuring_start = cur_time;
                m_fps_out = int(1000. * m_fps_running / elapsed_ms);
                m_fps_running = 0;
            }
            return m_fps_out;
        }
    };

    class Labels
    {
        bool m_enabled{false};
        bool m_shown{false};
        GLCanvas3D &m_canvas;

    public:
        explicit Labels(GLCanvas3D &canvas) : m_canvas(canvas) {}
        void enable(bool enable) { m_enabled = enable; }
        void show(bool show) { m_shown = m_enabled ? show : false; }
        bool is_shown() const { return m_shown; }
        void render(const std::vector<const ModelInstance *> &sorted_instances) const;
    };

    class Tooltip
    {
        std::string m_text;
        std::chrono::steady_clock::time_point m_start_time;
        // Indicator that the mouse is inside an ImGUI dialog, therefore the tooltip should be suppressed.
        bool m_in_imgui = false;
        float m_cursor_height{16.0f};

    public:
        bool is_empty() const { return m_text.empty(); }
        void set_text(const std::string &text);
        void render(const Vec2d &mouse_position, GLCanvas3D &canvas);
        // Indicates that the mouse is inside an ImGUI dialog, therefore the tooltip should be suppressed.
        void set_in_imgui(bool b) { m_in_imgui = b; }
        bool is_in_imgui() const { return m_in_imgui; }
    };

    class Slope
    {
        bool m_enabled{false};
        GLVolumeCollection &m_volumes;

    public:
        Slope(GLVolumeCollection &volumes) : m_volumes(volumes) {}

        void enable(bool enable) { m_enabled = enable; }
        bool is_enabled() const { return m_enabled; }
        void use(bool use) { m_volumes.set_slope_active(m_enabled ? use : false); }
        bool is_used() const { return m_volumes.is_slope_active(); }
        void set_normal_angle(float angle_in_deg) const
        {
            m_volumes.set_slope_normal_z(-::cos(Geometry::deg2rad(90.0f - angle_in_deg)));
        }
    };

public:
    enum class CanvasRole : unsigned char
    {
        View3D,
        Preview
    };

    struct ArrangeSettings
    {
        float distance = 6.f;
        float distance_from_bed = 0.f;
        //        float distance_seq_print = 6.;    // Used when sequential print is ON
        //        float distance_sla       = 6.;
        float accuracy = 0.65f; // Unused currently
        bool enable_rotation = false;
        int alignment = 0;
        int geometry_handling = 0;
        int strategy = 0;
    };

    enum class ESLAViewType
    {
        Original,
        Processed
    };

private:
    wxGLCanvas *m_canvas;
    IGLSurface *m_surface{nullptr};
    CanvasRole m_canvas_role{CanvasRole::View3D};
    ICanvasEventPoster *m_event_poster{nullptr};
    Camera *m_camera{nullptr};
    ImGuiWrapper *m_imgui{nullptr};
    ShaderGetterFn m_get_shader;
    CurrentShaderGetterFn m_get_current_shader;
    std::function<bool()> m_is_editor;
    std::function<bool()> m_is_gcode_viewer;
    std::function<int()> m_em_unit;
    std::function<bool()> m_dark_mode;
    std::function<int()> m_get_mode;
    std::function<int()> m_extruders_edited_cnt;
    std::function<int()> m_printer_technology;
    NotificationManager *m_notification_manager{nullptr};
    Mouse3DController *m_mouse3d_controller{nullptr};
    GLToolbar *m_collapse_toolbar{nullptr};
    GLToolbar *m_view_toolbar{nullptr};
    std::function<void()> m_obj_manipul_set_dirty;
    std::function<ECoordinatesType()> m_get_coordinates_type;
    std::function<bool()> m_get_uniform_scaling;
    AppConfig *m_app_config{nullptr};
    const PresetBundle *m_preset_bundle{nullptr};
    std::function<const BuildVolume &()> m_get_build_volume;
    std::function<bool()> m_is_preview_shown;
    std::function<bool()> m_is_view3D_shown;
    std::function<bool()> m_is_preview_loaded;
    std::function<bool()> m_can_layers_editing;
    std::function<bool()> m_is_sidebar_collapsed;
    std::function<std::vector<std::unique_ptr<Print>> &()> m_get_fff_prints;
    std::function<bool()> m_is_render_statistic_dialog_visible;
    std::function<void()> m_render_notes_dialog;
    std::function<void(GLCanvas3D &)> m_render_sliders;
    std::function<bool()> m_is_slicing;
    std::function<std::vector<ColorRGBA>()> m_get_extruder_colors;
    std::function<void(std::function<void()>)> m_call_after;
    std::function<bool()> m_init_opengl;
    std::function<void()> m_init_environment_texture;
    std::function<void()> m_render_project_state_debug;
    std::function<void()> m_render_obj_manipul_debug;
    std::function<bool()> m_has_loaded_gcode;
    std::function<void()> m_suppress_snapshots_start;
    std::function<void()> m_suppress_snapshots_end;
    std::function<float(bool &)> m_toolbar_icon_scale;
    std::function<void(float)> m_set_auto_toolbar_icon_scale;
    std::function<bool()> m_init_view_toolbar;
    std::function<bool()> m_init_collapse_toolbar;
    std::function<void(int, int)> m_set_preview_layer_range;
    std::function<bool()> m_can_delete;
    std::function<bool()> m_can_delete_all;
    std::function<bool()> m_can_arrange;
    std::function<bool()> m_can_copy_to_clipboard;
    std::function<bool()> m_can_paste_from_clipboard;
    std::function<bool()> m_can_increase_instances;
    std::function<bool()> m_can_decrease_instances;
    std::function<bool()> m_can_split_to_objects;
    std::function<bool()> m_can_split_to_volumes;
    std::function<bool()> m_can_undo;
    std::function<bool()> m_can_redo;
    std::function<bool(bool, int, const char **)> m_undo_redo_string_getter;
    std::function<void(bool, std::string &)> m_undo_redo_topmost_string_getter;
    std::function<void(int)> m_undo_to;
    std::function<void(int)> m_redo_to;
    std::function<float()> m_get_screen_scale_factor;
    std::function<bool()> m_has_selected_cut_object;
    std::function<void()> m_enter_gizmos_stack;
    std::function<void()> m_leave_gizmos_stack;
    std::function<bool()> m_is_project_dirty;
    std::function<void(const DynamicPrintConfig &)> m_on_config_change;
    std::function<void(ModelVolume &, const std::string &)> m_clear_before_change_volume;
    std::function<void(size_t)> m_remove_object;
    std::function<void(int)> m_changed_mesh;
    std::function<std::string()> m_validate_current_print;
    std::function<bool()> m_is_bg_process_update_scheduled;
    std::function<void(int, const ModelObject &, bool)> m_reslice_until_step;
    std::function<void(const std::string &, int)> m_take_typed_snapshot;
    std::function<void(size_t, const ModelObjectPtrs &)> m_apply_cut_object_to_model;
    std::function<Worker &()> m_get_job_worker;
    std::function<void(int, const ModelVolume *)> m_reorder_volumes_and_select;
    SceneRaycaster m_scene_raycaster;
    Bed3D &m_bed;
    int m_last_active_bed_id{-1};
#if ENABLE_RETINA_GL
    std::unique_ptr<RetinaHelper> m_retina_helper;
#endif
    bool m_in_render;

    // Real-time boolean preview for objects with negative volumes
    std::unique_ptr<class CSGPreviewManager> m_csg_preview;

    std::unique_ptr<ITimer> m_timer;
    LayersEditing m_layers_editing;
    Mouse m_mouse;
    GLGizmosManager m_gizmos;
    GLToolbar m_main_toolbar;
    GLToolbar m_undoredo_toolbar;
    std::array<ClippingPlane, 2> m_clipping_planes;
    ClippingPlane m_camera_clipping_plane;
    bool m_use_clipping_planes;
    std::array<SlaCap, 2> m_sla_caps;
    int m_layer_slider_index = -1;
    std::string m_sidebar_field;
    // when true renders an extra frame by not resetting m_dirty to false
    // see request_extra_frame()
    bool m_extra_frame_requested;
    bool m_event_handlers_bound{false};
    float m_bed_selector_current_height = 0.f;

    GLVolumeCollection m_volumes;
#if SLIC3R_OPENGL_ES
    std::vector<TriangleMesh> m_wipe_tower_meshes;
#endif // SLIC3R_OPENGL_ES
    std::array<std::optional<BoundingBoxf>, MAX_NUMBER_OF_BEDS> m_wipe_tower_bounding_boxes;

    GCodeViewer m_gcode_viewer;

    std::unique_ptr<ITimer> m_render_timer;

    Selection m_selection;
    const DynamicPrintConfig *m_config;
    Model *m_model;

public:
    BackgroundSlicingProcess *m_process;

private:
    bool m_requires_check_outside_state{false};

    void select_bed(int i, bool triggered_by_user);

    std::array<unsigned int, 2> m_old_size{0, 0};

    // Screen is only refreshed from the OnIdle handler if it is dirty.
    bool m_dirty;
    // When true, on_idle() skips all rendering work (used during window drag to prevent sluggish movement)
    bool m_rendering_paused{false};
    bool m_initialized;
    bool m_apply_zoom_to_volumes_filter;
    bool m_picking_enabled;
    bool m_moving_enabled;
    bool m_dynamic_background_enabled;
    bool m_multisample_allowed;
    bool m_moving;
    bool m_tab_down;
    CursorType m_cursor_type;
    GLSelectionRectangle m_rectangle_selection;
    std::vector<int> m_hover_volume_idxs;

    // Following variable is obsolete and it should be safe to remove it.
    // I just don't want to do it now before a release (Lukas Matena 24.3.2019)
    bool m_render_sla_auxiliaries;

    bool m_reload_delayed;

#if ENABLE_RENDER_PICKING_PASS
    bool m_show_picking_texture;
#endif // ENABLE_RENDER_PICKING_PASS

    KeyAutoRepeatFilter m_shift_kar_filter;
    KeyAutoRepeatFilter m_ctrl_kar_filter;

    RenderStats m_render_stats;

    int m_imgui_undo_redo_hovered_pos{-1};
    int m_mouse_wheel{0};
    int m_selected_extruder;

    Labels m_labels;
    Tooltip m_tooltip;
    bool m_tooltip_enabled{true};
    Slope m_slope;

    class SLAView
    {
    public:
        explicit SLAView(GLCanvas3D &parent) : m_parent(parent) {}
        void detect_type_from_volumes(const GLVolumePtrs &volumes);
        void set_type(ESLAViewType type);
        void set_type(const GLVolume::CompositeID &id, ESLAViewType type);
        void update_volumes_visibility(GLVolumePtrs &volumes);
        void update_instances_cache(
            const std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>> &new_to_old_ids_map);
        void render_switch_button();

#if ENABLE_SLA_VIEW_DEBUG_WINDOW
        void render_debug_window();
#endif // ENABLE_SLA_VIEW_DEBUG_WINDOW

    private:
        GLCanvas3D &m_parent;
        typedef std::pair<GLVolume::CompositeID, ESLAViewType> InstancesCacheItem;
        std::vector<InstancesCacheItem> m_instances_cache;
        bool m_use_instance_bbox{true};

        InstancesCacheItem *find_instance_item(const GLVolume::CompositeID &id);
        void select_full_instance(const GLVolume::CompositeID &id);
    };

    SLAView m_sla_view;
    bool m_sla_view_type_detection_active{false};

    bool is_arrange_alignment_enabled() const;

    ArrangeSettingsDb_AppCfg m_arrange_settings_db;
    ArrangeSettingsDialogImgui m_arrange_settings_dialog;

    // used to show layers times on the layers slider when pre-gcode view is active
    std::vector<float> m_gcode_layers_times_cache;

    // Returns the right margin width (e.g., layers slider width in Preview mode)
    std::function<float()> m_right_margin_fn;

public:
    struct ContoursList
    {
        // list of unique contours
        Polygons contours;
        // if defined: list of transforms to apply to contours
        std::optional<std::vector<std::pair<size_t, Transform3d>>> trafos;

        bool empty() const { return contours.empty(); }
    };

private:
    struct ToolbarHighlighter
    {
        void set_timer(std::unique_ptr<ITimer> timer) { m_timer = std::move(timer); }
        void init(GLToolbarItem *toolbar_item, GLCanvas3D *canvas);
        void blink();
        void invalidate();
        bool m_render_arrow{false};
        GLToolbarItem *m_toolbar_item{nullptr};

    private:
        GLCanvas3D *m_canvas{nullptr};
        int m_blink_counter{0};
        std::unique_ptr<ITimer> m_timer;
    } m_toolbar_highlighter;

    struct GizmoHighlighter
    {
        void set_timer(std::unique_ptr<ITimer> timer) { m_timer = std::move(timer); }
        void init(GLGizmosManager *manager, GLGizmosManager::EType gizmo, GLCanvas3D *canvas);
        void blink();
        void invalidate();
        bool m_render_arrow{false};
        GLGizmosManager::EType m_gizmo_type;

    private:
        GLGizmosManager *m_gizmo_manager{nullptr};
        GLCanvas3D *m_canvas{nullptr};
        int m_blink_counter{0};
        std::unique_ptr<ITimer> m_timer;

    } m_gizmo_highlighter;

#if ENABLE_SHOW_CAMERA_TARGET
    struct CameraTarget
    {
        std::array<GLModel, 3> axis;
        Vec3d target{Vec3d::Zero()};
    };

    CameraTarget m_camera_target;
    GLModel m_target_validation_box;
#endif // ENABLE_SHOW_CAMERA_TARGET
    GLModel m_background;

public:
    GLCanvas3D(wxGLCanvas *canvas, Bed3D &bed, AppConfig *app_config = nullptr, ImGuiWrapper *imgui = nullptr);
    ~GLCanvas3D();

    bool is_initialized() const { return m_initialized; }

    void set_surface(IGLSurface *surface) { m_surface = surface; }
    IGLSurface *surface() { return m_surface; }
    void set_canvas_role(CanvasRole role) { m_canvas_role = role; }
    CanvasRole canvas_role() const { return m_canvas_role; }
    void set_right_margin_fn(std::function<float()> fn) { m_right_margin_fn = std::move(fn); }
    void set_camera(Camera *camera)
    {
        m_camera = camera;
        m_selection.set_camera(camera);
        m_gcode_viewer.set_camera(camera);
    }
    Camera &get_camera() { return *m_camera; }
    const Camera &get_camera() const { return *m_camera; }

    void set_shader_getters(ShaderGetterFn get_shader, CurrentShaderGetterFn get_current_shader);
    GLShaderProgram *get_shader(const std::string &name) const { return m_get_shader ? m_get_shader(name) : nullptr; }
    GLShaderProgram *get_current_shader() const { return m_get_current_shader ? m_get_current_shader() : nullptr; }

    // Per-pixel lit model shader (phong) with automatic fallback to gouraud
    GLShaderProgram *get_model_shader() const;
    // Per-pixel lit utility shader (phong_light) with automatic fallback to gouraud_light
    GLShaderProgram *get_utility_shader() const;
    void set_imgui(ImGuiWrapper *imgui)
    {
        m_imgui = imgui;
        m_selection.set_imgui(imgui);
        m_gcode_viewer.set_imgui(imgui);
    }
    ImGuiWrapper *get_imgui() { return m_imgui; }
    ImGuiWrapper *get_imgui() const { return m_imgui; }
    void set_notification_manager(NotificationManager *nm)
    {
        m_notification_manager = nm;
        m_gcode_viewer.set_notification_manager(nm);
    }
    NotificationManager *get_notification_manager() { return m_notification_manager; }
    const NotificationManager *get_notification_manager() const { return m_notification_manager; }
    void set_mouse3d_controller(Mouse3DController *ctrl) { m_mouse3d_controller = ctrl; }
    Mouse3DController &get_mouse3d_controller() { return *m_mouse3d_controller; }
    const Mouse3DController &get_mouse3d_controller() const { return *m_mouse3d_controller; }
    void set_collapse_toolbar(GLToolbar *toolbar) { m_collapse_toolbar = toolbar; }
    GLToolbar &get_collapse_toolbar() { return *m_collapse_toolbar; }
    const GLToolbar &get_collapse_toolbar() const { return *m_collapse_toolbar; }
    void set_view_toolbar(GLToolbar *toolbar) { m_view_toolbar = toolbar; }
    GLToolbar &get_view_toolbar() { return *m_view_toolbar; }
    const GLToolbar &get_view_toolbar() const { return *m_view_toolbar; }
    void set_obj_manipul_callbacks(std::function<void()> set_dirty,
                                   std::function<ECoordinatesType()> get_coordinates_type,
                                   std::function<bool()> get_uniform_scaling)
    {
        m_obj_manipul_set_dirty = std::move(set_dirty);
        m_get_coordinates_type = std::move(get_coordinates_type);
        m_get_uniform_scaling = std::move(get_uniform_scaling);
    }
    void obj_manipul_set_dirty()
    {
        if (m_obj_manipul_set_dirty)
            m_obj_manipul_set_dirty();
    }
    ECoordinatesType get_coordinates_type() const
    {
        return m_get_coordinates_type ? m_get_coordinates_type() : ECoordinatesType::World;
    }
    bool is_local_coordinates() const { return get_coordinates_type() == ECoordinatesType::Local; }
    bool is_instance_coordinates() const { return get_coordinates_type() == ECoordinatesType::Instance; }
    bool is_world_coordinates() const { return get_coordinates_type() == ECoordinatesType::World; }
    bool get_uniform_scaling() const { return m_get_uniform_scaling ? m_get_uniform_scaling() : false; }
    GLRenderContext build_render_context() const;
    void set_app_config(AppConfig *config)
    {
        m_app_config = config;
        m_gcode_viewer.set_app_config(config);
    }
    void set_preset_bundle(const PresetBundle *bundle)
    {
        m_preset_bundle = bundle;
        m_gcode_viewer.set_preset_bundle(bundle);
    }
    const PresetBundle *preset_bundle() const { return m_preset_bundle; }
    PresetBundle *preset_bundle() { return const_cast<PresetBundle *>(m_preset_bundle); }
    void set_build_volume_getter(std::function<const BuildVolume &()> fn) { m_get_build_volume = std::move(fn); }
    const BuildVolume &build_volume() const;
    const AppConfig *app_config() const { return m_app_config; }
    AppConfig *app_config() { return m_app_config; }
    void set_plater_queries(std::function<bool()> is_preview_shown, std::function<bool()> is_view3D_shown,
                            std::function<bool()> is_preview_loaded, std::function<bool()> can_layers_editing,
                            std::function<bool()> is_sidebar_collapsed,
                            std::function<std::vector<std::unique_ptr<Print>> &()> get_fff_prints,
                            std::function<bool()> is_render_statistic_dialog_visible)
    {
        m_is_preview_shown = std::move(is_preview_shown);
        m_is_view3D_shown = std::move(is_view3D_shown);
        m_is_preview_loaded = std::move(is_preview_loaded);
        m_can_layers_editing = std::move(can_layers_editing);
        m_is_sidebar_collapsed = std::move(is_sidebar_collapsed);
        m_get_fff_prints = std::move(get_fff_prints);
        m_is_render_statistic_dialog_visible = std::move(is_render_statistic_dialog_visible);
    }
    bool is_preview_shown() const { return m_is_preview_shown && m_is_preview_shown(); }
    bool is_view3D_shown() const { return m_is_view3D_shown && m_is_view3D_shown(); }
    bool is_preview_loaded() const { return m_is_preview_loaded && m_is_preview_loaded(); }
    bool can_layers_editing() const { return m_can_layers_editing && m_can_layers_editing(); }
    bool is_sidebar_collapsed() const { return m_is_sidebar_collapsed && m_is_sidebar_collapsed(); }
    std::vector<std::unique_ptr<Print>> &get_fff_prints();
    bool is_render_statistic_dialog_visible() const
    {
        return m_is_render_statistic_dialog_visible && m_is_render_statistic_dialog_visible();
    }
    void set_render_callbacks(std::function<void()> render_notes, std::function<void(GLCanvas3D &)> render_sliders)
    {
        m_render_notes_dialog = std::move(render_notes);
        m_render_sliders = std::move(render_sliders);
    }
    void render_notes_dialog()
    {
        if (m_render_notes_dialog)
            m_render_notes_dialog();
    }
    void render_sliders()
    {
        if (m_render_sliders)
            m_render_sliders(*this);
    }
    void set_misc_callbacks(std::function<bool()> is_slicing,
                            std::function<std::vector<ColorRGBA>()> get_extruder_colors,
                            std::function<void(std::function<void()>)> call_after, std::function<bool()> init_opengl,
                            std::function<void()> init_environment_texture,
                            std::function<void()> render_project_state_debug = nullptr,
                            std::function<void()> render_obj_manipul_debug = nullptr)
    {
        m_is_slicing = std::move(is_slicing);
        m_get_extruder_colors = std::move(get_extruder_colors);
        m_call_after = std::move(call_after);
        m_init_opengl = std::move(init_opengl);
        m_init_environment_texture = std::move(init_environment_texture);
        m_render_project_state_debug = std::move(render_project_state_debug);
        m_render_obj_manipul_debug = std::move(render_obj_manipul_debug);
    }
    bool is_slicing() const { return m_is_slicing && m_is_slicing(); }
    std::vector<ColorRGBA> get_extruder_colors_from_plater_config()
    {
        return m_get_extruder_colors ? m_get_extruder_colors() : std::vector<ColorRGBA>{};
    }
    void set_toolbar_queries(std::function<bool()> can_delete, std::function<bool()> can_delete_all,
                             std::function<bool()> can_arrange, std::function<bool()> can_copy,
                             std::function<bool()> can_paste, std::function<bool()> can_increase,
                             std::function<bool()> can_decrease, std::function<bool()> can_split_obj,
                             std::function<bool()> can_split_vol, std::function<bool()> has_loaded_gcode,
                             std::function<void()> suppress_start, std::function<void()> suppress_end,
                             std::function<float(bool &)> toolbar_icon_scale,
                             std::function<void(float)> set_auto_toolbar_icon_scale,
                             std::function<bool()> init_view_toolbar, std::function<bool()> init_collapse_toolbar,
                             std::function<void(int, int)> set_preview_layer_range)
    {
        m_can_delete = std::move(can_delete);
        m_can_delete_all = std::move(can_delete_all);
        m_can_arrange = std::move(can_arrange);
        m_can_copy_to_clipboard = std::move(can_copy);
        m_can_paste_from_clipboard = std::move(can_paste);
        m_can_increase_instances = std::move(can_increase);
        m_can_decrease_instances = std::move(can_decrease);
        m_can_split_to_objects = std::move(can_split_obj);
        m_can_split_to_volumes = std::move(can_split_vol);
        m_has_loaded_gcode = std::move(has_loaded_gcode);
        m_suppress_snapshots_start = std::move(suppress_start);
        m_suppress_snapshots_end = std::move(suppress_end);
        m_toolbar_icon_scale = std::move(toolbar_icon_scale);
        m_set_auto_toolbar_icon_scale = std::move(set_auto_toolbar_icon_scale);
        m_init_view_toolbar = std::move(init_view_toolbar);
        m_init_collapse_toolbar = std::move(init_collapse_toolbar);
        m_set_preview_layer_range = std::move(set_preview_layer_range);
    }
    void set_undo_redo_callbacks(std::function<bool()> can_undo, std::function<bool()> can_redo,
                                 std::function<bool(bool, int, const char **)> string_getter,
                                 std::function<void(bool, std::string &)> topmost_string_getter,
                                 std::function<void(int)> undo_to, std::function<void(int)> redo_to)
    {
        m_can_undo = std::move(can_undo);
        m_can_redo = std::move(can_redo);
        m_undo_redo_string_getter = std::move(string_getter);
        m_undo_redo_topmost_string_getter = std::move(topmost_string_getter);
        m_undo_to = std::move(undo_to);
        m_redo_to = std::move(redo_to);
    }
    bool can_undo() const { return m_can_undo && m_can_undo(); }
    bool can_redo() const { return m_can_redo && m_can_redo(); }
    bool undo_redo_string_getter(bool is_undo, int idx, const char **out)
    {
        return m_undo_redo_string_getter ? m_undo_redo_string_getter(is_undo, idx, out) : false;
    }
    void undo_redo_topmost_string_getter(bool is_undo, std::string &out)
    {
        if (m_undo_redo_topmost_string_getter)
            m_undo_redo_topmost_string_getter(is_undo, out);
    }
    void undo_to(int idx)
    {
        if (m_undo_to)
            m_undo_to(idx);
    }
    void redo_to(int idx)
    {
        if (m_redo_to)
            m_redo_to(idx);
    }

    void set_app_state(std::function<bool()> is_editor, std::function<bool()> is_gcode_viewer,
                       std::function<int()> em_unit, std::function<bool()> dark_mode, std::function<int()> get_mode,
                       std::function<int()> extruders_edited_cnt, std::function<int()> printer_technology);
    bool is_editor() const { return m_is_editor && m_is_editor(); }
    bool is_gcode_viewer() const { return m_is_gcode_viewer && m_is_gcode_viewer(); }
    int em_unit() const { return m_em_unit ? m_em_unit() : 10; }
    bool dark_mode() const { return m_dark_mode && m_dark_mode(); }
    int get_mode() const { return m_get_mode ? m_get_mode() : 0; }
    int extruders_edited_cnt() const { return m_extruders_edited_cnt ? m_extruders_edited_cnt() : 1; }
    int printer_technology() const { return m_printer_technology ? m_printer_technology() : 0; }
    void call_after(std::function<void()> fn)
    {
        if (m_call_after)
            m_call_after(std::move(fn));
    }
    void set_screen_scale_factor_getter(std::function<float()> fn) { m_get_screen_scale_factor = std::move(fn); }
    float get_screen_scale_factor() const { return m_get_screen_scale_factor ? m_get_screen_scale_factor() : 1.0f; }
    void set_has_selected_cut_object(std::function<bool()> fn) { m_has_selected_cut_object = std::move(fn); }
    bool has_selected_cut_object() const { return m_has_selected_cut_object && m_has_selected_cut_object(); }
    void set_plater_action_callbacks(std::function<void()> enter_gizmos_stack, std::function<void()> leave_gizmos_stack,
                                     std::function<bool()> is_project_dirty,
                                     std::function<void(const DynamicPrintConfig &)> on_config_change,
                                     std::function<void(ModelVolume &, const std::string &)> clear_before_change_volume,
                                     std::function<void(size_t)> remove_object, std::function<void(int)> changed_mesh,
                                     std::function<std::string()> validate_current_print,
                                     std::function<bool()> is_bg_process_update_scheduled,
                                     std::function<void(int, const ModelObject &, bool)> reslice_until_step,
                                     std::function<void(const std::string &, int)> take_typed_snapshot)
    {
        m_enter_gizmos_stack = std::move(enter_gizmos_stack);
        m_leave_gizmos_stack = std::move(leave_gizmos_stack);
        m_is_project_dirty = std::move(is_project_dirty);
        m_on_config_change = std::move(on_config_change);
        m_clear_before_change_volume = std::move(clear_before_change_volume);
        m_remove_object = std::move(remove_object);
        m_changed_mesh = std::move(changed_mesh);
        m_validate_current_print = std::move(validate_current_print);
        m_is_bg_process_update_scheduled = std::move(is_bg_process_update_scheduled);
        m_reslice_until_step = std::move(reslice_until_step);
        m_take_typed_snapshot = std::move(take_typed_snapshot);
    }
    void enter_gizmos_stack()
    {
        if (m_enter_gizmos_stack)
            m_enter_gizmos_stack();
    }
    void leave_gizmos_stack()
    {
        if (m_leave_gizmos_stack)
            m_leave_gizmos_stack();
    }
    bool is_project_dirty() const { return m_is_project_dirty && m_is_project_dirty(); }
    void on_config_change(const DynamicPrintConfig &cfg)
    {
        if (m_on_config_change)
            m_on_config_change(cfg);
    }
    void clear_before_change_volume(ModelVolume &mv, const std::string &msg)
    {
        if (m_clear_before_change_volume)
            m_clear_before_change_volume(mv, msg);
    }
    void remove_object(size_t idx)
    {
        if (m_remove_object)
            m_remove_object(idx);
    }
    void changed_mesh(int idx)
    {
        if (m_changed_mesh)
            m_changed_mesh(idx);
    }
    std::string validate_current_print()
    {
        return m_validate_current_print ? m_validate_current_print() : std::string();
    }
    bool is_bg_process_update_scheduled() const
    {
        return m_is_bg_process_update_scheduled && m_is_bg_process_update_scheduled();
    }
    void reslice_until_step(int step, const ModelObject &obj, bool postpone = false)
    {
        if (m_reslice_until_step)
            m_reslice_until_step(step, obj, postpone);
    }
    void take_typed_snapshot(const std::string &name, int type)
    {
        if (m_take_typed_snapshot)
            m_take_typed_snapshot(name, type);
    }
    void take_gizmo_snapshot(const std::string &name);
    void set_apply_cut_callback(std::function<void(size_t, const ModelObjectPtrs &)> fn)
    {
        m_apply_cut_object_to_model = std::move(fn);
    }
    void apply_cut_object_to_model(size_t idx, const ModelObjectPtrs &objs)
    {
        if (m_apply_cut_object_to_model)
            m_apply_cut_object_to_model(idx, objs);
    }
    void set_job_worker_getter(std::function<Worker &()> fn) { m_get_job_worker = std::move(fn); }
    Worker &get_job_worker() { return m_get_job_worker(); }
    void set_reorder_volumes_callback(std::function<void(int, const ModelVolume *)> fn)
    {
        m_reorder_volumes_and_select = std::move(fn);
    }
    void reorder_volumes_and_select(int obj_idx, const ModelVolume *volume)
    {
        if (m_reorder_volumes_and_select)
            m_reorder_volumes_and_select(obj_idx, volume);
    }

    wxGLCanvas *get_wxglcanvas() { return m_canvas; }
    const wxGLCanvas *get_wxglcanvas() const { return m_canvas; }

    wxWindow *get_wxglcanvas_parent();

    bool init();
    void set_event_poster(ICanvasEventPoster *poster)
    {
        m_event_poster = poster;
        m_selection.set_event_poster(poster);
        m_selection.set_canvas(this);
        m_gcode_viewer.set_canvas(this);
    }
    ICanvasEventPoster *event_poster() { return m_event_poster; }
    ICanvasEventPoster *event_poster() const { return m_event_poster; }

    std::shared_ptr<SceneRaycasterItem> add_raycaster_for_picking(SceneRaycaster::EType type, int id,
                                                                  const MeshRaycaster &raycaster,
                                                                  const Transform3d &trafo = Transform3d::Identity(),
                                                                  bool use_back_faces = false)
    {
        return m_scene_raycaster.add_raycaster(type, id, raycaster, trafo, use_back_faces);
    }
    void remove_raycasters_for_picking(SceneRaycaster::EType type, int id)
    {
        m_scene_raycaster.remove_raycasters(type, id);
    }
    void remove_raycasters_for_picking(SceneRaycaster::EType type) { m_scene_raycaster.remove_raycasters(type); }

    std::vector<std::shared_ptr<SceneRaycasterItem>> *get_raycasters_for_picking(SceneRaycaster::EType type)
    {
        return m_scene_raycaster.get_raycasters(type);
    }

    void set_raycaster_gizmos_on_top(bool value) { m_scene_raycaster.set_gizmos_on_top(value); }

    void set_as_dirty() { m_dirty = true; }
    void pause_rendering() { m_rendering_paused = true; }
    void resume_rendering()
    {
        m_rendering_paused = false;
        m_dirty = true;
    }
    // preFlight: Release the GL context so the GPU driver can drop to idle power state.
    // The context is re-acquired automatically by _set_current() before the next render.
    void release_gl_context();
    // preFlight: Public wrapper to ensure the GL context is current. Needed by code
    // that runs GL operations outside the render cycle (e.g. gcode data loading).
    bool ensure_gl_current() { return _set_current(); }
    void requires_check_outside_state() { m_requires_check_outside_state = true; }

    unsigned int get_volumes_count() const { return (unsigned int) m_volumes.volumes.size(); }
    const GLVolumeCollection &get_volumes() const { return m_volumes; }
    void reset_volumes();
    ModelInstanceEPrintVolumeState check_volumes_outside_state(bool selection_only = true) const;
    // update the is_outside state of all the volumes contained in the given collection
    void check_volumes_outside_state(GLVolumeCollection &volumes) const
    {
        check_volumes_outside_state(volumes, nullptr, false);
    }

private:
    // returns true if all the volumes are completely contained in the print volume
    // returns the containment state in the given out_state, if non-null
    bool check_volumes_outside_state(GLVolumeCollection &volumes, ModelInstanceEPrintVolumeState *out_state,
                                     bool selection_only = true) const;

public:
    void init_gcode_viewer()
    {
        _set_current();
        m_gcode_viewer.init();
    }
    void reset_gcode_toolpaths() { m_gcode_viewer.reset(); }
    GCodeViewer &get_gcode_viewer() { return m_gcode_viewer; }
    const GCodeViewer &get_gcode_viewer() const { return m_gcode_viewer; }
    const GCodeViewer::SequentialView &get_gcode_sequential_view() const
    {
        return m_gcode_viewer.get_sequential_view();
    }
    void update_gcode_sequential_view_current(unsigned int first, unsigned int last)
    {
        m_gcode_viewer.update_sequential_view_current(first, last);
    }
    const libvgcode::Interval &get_gcode_view_full_range() const { return m_gcode_viewer.get_gcode_view_full_range(); }
    const libvgcode::Interval &get_gcode_view_enabled_range() const
    {
        return m_gcode_viewer.get_gcode_view_enabled_range();
    }
    const libvgcode::Interval &get_gcode_view_visible_range() const
    {
        return m_gcode_viewer.get_gcode_view_visible_range();
    }
    const libvgcode::PathVertex &get_gcode_vertex_at(size_t id) const { return m_gcode_viewer.get_gcode_vertex_at(id); }

    std::pair<std::optional<std::unique_ptr<GLModel>>, bool> get_current_marker_model() const;

    void toggle_sla_auxiliaries_visibility(bool visible, const ModelObject *mo = nullptr, int instance_idx = -1);
    void toggle_model_objects_visibility(bool visible, const ModelObject *mo = nullptr, int instance_idx = -1,
                                         const ModelVolume *mv = nullptr);
    void update_instance_printable_state_for_object(size_t obj_idx);
    void update_instance_printable_state_for_objects(const std::vector<size_t> &object_idxs);

    void set_config(const DynamicPrintConfig *config);
    const DynamicPrintConfig *config() const { return m_config; }
    void set_process(BackgroundSlicingProcess *process) { m_process = process; }
    void set_model(Model *model);
    const Model *get_model() const { return m_model; }
    Model *get_model() { return m_model; }

    const arr2::ArrangeSettingsView *get_arrange_settings_view() const { return &m_arrange_settings_dialog; }

    const Selection &get_selection() const { return m_selection; }
    Selection &get_selection() { return m_selection; }

    const GLGizmosManager &get_gizmos_manager() const { return m_gizmos; }
    GLGizmosManager &get_gizmos_manager() { return m_gizmos; }
    CSGPreviewManager *get_csg_preview() { return m_csg_preview.get(); }

    void bed_shape_changed();

    void set_layer_slider_index(int i) { m_layer_slider_index = i; }

    void set_clipping_plane(unsigned int id, const ClippingPlane &plane)
    {
        if (id < 2)
        {
            m_clipping_planes[id] = plane;
            m_sla_caps[id].reset();
        }
    }
    void reset_clipping_planes_cache()
    {
        m_sla_caps[0].triangles.clear();
        m_sla_caps[1].triangles.clear();
    }
    void set_use_clipping_planes(bool use) { m_use_clipping_planes = use; }

    bool get_use_clipping_planes() const { return m_use_clipping_planes; }
    const std::array<ClippingPlane, 2> &get_clipping_planes() const { return m_clipping_planes; };

    void set_use_color_clip_plane(bool use) { m_volumes.set_use_color_clip_plane(use); }
    void set_color_clip_plane(const Vec3d &cp_normal, double offset)
    {
        m_volumes.set_color_clip_plane(cp_normal, offset);
    }
    void set_color_clip_plane_colors(const std::array<ColorRGBA, 2> &colors)
    {
        m_volumes.set_color_clip_plane_colors(colors);
    }

    void refresh_camera_scene_box();

    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 scene_bounding_box() const;

    bool is_layers_editing_enabled() const { return m_layers_editing.is_enabled(); }
    bool is_layers_editing_allowed() const { return m_layers_editing.is_allowed(); }

    void reset_layer_height_profile();
    void adaptive_layer_height_profile(float quality_factor);
    void smooth_layer_height_profile(const HeightProfileSmoothingParams &smoothing_params);

    bool is_reload_delayed() const { return m_reload_delayed; }

    void enable_layers_editing(bool enable);
    void enable_picking(bool enable) { m_picking_enabled = enable; }
    void enable_moving(bool enable) { m_moving_enabled = enable; }
    void enable_gizmos(bool enable) { m_gizmos.set_enabled(enable); }
    void enable_selection(bool enable) { m_selection.set_enabled(enable); }
    void enable_main_toolbar(bool enable) { m_main_toolbar.set_enabled(enable); }
    void enable_undoredo_toolbar(bool enable) { m_undoredo_toolbar.set_enabled(enable); }
    void enable_dynamic_background(bool enable) { m_dynamic_background_enabled = enable; }
    void enable_labels(bool enable) { m_labels.enable(enable); }
    void enable_slope(bool enable) { m_slope.enable(enable); }
    void allow_multisample(bool allow) { m_multisample_allowed = allow; }

    void zoom_to_bed();
    void zoom_to_volumes();
    void zoom_to_selection();
    void zoom_to_gcode();
    void select_view(const std::string &direction);

    PrinterTechnology current_printer_technology() const;

    void update_volumes_colors_by_extruder();

    bool is_dragging() const
    {
        return m_gizmos.is_dragging() || (m_moving && !m_mouse.scene_position.isApprox(m_mouse.drag.start_position_3D));
    }

    void render();
    // printable_only == false -> render also non printable volumes as grayed
    // parts_only == false -> render also sla support and pad
    void render_thumbnail(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                          const ThumbnailsParams &thumbnail_params, Camera::EType camera_type);
    void render_thumbnail(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                          const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                          Camera::EType camera_type);

    void select_all();
    void deselect_all();
    void delete_selected() { m_selection.erase(); }
    void ensure_on_bed(unsigned int object_idx, bool allow_negative_z);

    std::vector<double> get_gcode_layers_zs() const { return m_gcode_viewer.get_layers_zs(); }
    std::vector<float> get_gcode_layers_times() const { return m_gcode_viewer.get_layers_times(); }
    const std::vector<float> &get_gcode_layers_times_cache() const { return m_gcode_layers_times_cache; }
    void reset_gcode_layers_times_cache() { m_gcode_layers_times_cache.clear(); }
    void set_volumes_z_range(const std::array<double, 2> &range)
    {
        m_volumes.set_range(range[0] - 1e-6, range[1] + 1e-6);
    }
    void set_toolpaths_z_range(const std::array<unsigned int, 2> &range);
    size_t get_gcode_extruders_count() { return m_gcode_viewer.get_extruders_count(); }

    std::vector<int> load_object(const ModelObject &model_object, int obj_idx, std::vector<int> instance_idxs);
    std::vector<int> load_object(const Model &model, int obj_idx);

    void mirror_selection(Axis axis);

    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);

    void load_gcode_shells();
    void load_gcode_preview(const GCodeProcessorResult &gcode_result, const std::vector<std::string> &str_tool_colors,
                            const std::vector<std::string> &str_color_print_colors);
    void set_shell_progress_height(double height) { m_gcode_viewer.set_shell_progress_height(height); }
    void set_gcode_view_type(libvgcode::EViewType type) { return m_gcode_viewer.set_view_type(type); }
    libvgcode::EViewType get_gcode_view_type() const { return m_gcode_viewer.get_view_type(); }
    void enable_gcode_view_type_cache_load(bool enable) { m_gcode_viewer.enable_view_type_cache_load(enable); }
    void enable_gcode_view_type_cache_write(bool enable) { m_gcode_viewer.enable_view_type_cache_write(enable); }
    bool is_gcode_view_type_cache_load_enabled() const { return m_gcode_viewer.is_view_type_cache_load_enabled(); }
    bool is_gcode_view_type_cache_write_enabled() const { return m_gcode_viewer.is_view_type_cache_write_enabled(); }

    void load_preview(const std::vector<std::string> &str_tool_colors,
                      const std::vector<std::string> &str_color_print_colors,
                      const std::vector<CustomGCode::Item> &color_print_values);
    void load_sla_preview();
    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent &evt) { m_dirty = true; }
    void on_idle(wxIdleEvent &evt);
    void on_char(wxKeyEvent &evt);
    void on_key(wxKeyEvent &evt);
    void on_mouse_wheel(wxMouseEvent &evt);
    void on_timer_internal();
    void on_render_timer_internal();
    void on_mouse(wxMouseEvent &evt);
    void on_paint(wxPaintEvent &evt);
    void on_set_focus(wxFocusEvent &evt);

    // Internal input handlers that accept MouseInput/KeyInput instead of wx events.
    void on_mouse_internal(MouseInput &mouse);
    void on_char_internal(KeyInput &key);
    void on_key_internal(KeyInput &key);
    void on_mouse_wheel_internal(MouseInput &mouse);

    Size get_canvas_size() const;
    Vec2d get_local_mouse_position() const;

    // store opening position of menu
    std::optional<Vec2d> m_popup_menu_positon; // position of mouse right click
    void set_popup_menu_position(const Vec2d &position) { m_popup_menu_positon = position; }
    const std::optional<Vec2d> &get_popup_menu_position() const { return m_popup_menu_positon; }
    void clear_popup_menu_position() { m_popup_menu_positon.reset(); }

    void set_tooltip(const std::string &tooltip);

    // the following methods add a snapshot to the undo/redo stack, unless the given string is empty
    void do_move(const std::string &snapshot_type);
    void do_rotate(const std::string &snapshot_type);
    void do_scale(const std::string &snapshot_type);
    void do_mirror(const std::string &snapshot_type);
    void do_reset_skew(const std::string &snapshot_type);

    void update_gizmos_on_off_state();
    void reset_all_gizmos() { m_gizmos.reset_all_states(); }

    void handle_sidebar_focus_event(const std::string &opt_key, bool focus_on);
    void handle_layers_data_focus_event(const t_layer_height_range range, const EditorType type);

    void update_ui_from_settings();

    int get_move_volume_id() const { return m_mouse.drag.move_volume_idx; }
    int get_first_hover_volume_idx() const { return m_hover_volume_idxs.empty() ? -1 : m_hover_volume_idxs.front(); }
    void set_selected_extruder(int extruder) { m_selected_extruder = extruder; }

    class WipeTowerInfo
    {
    protected:
        Vec2d m_pos = {NaNd, NaNd};
        double m_rotation = 0.;
        BoundingBoxf m_bb;
        int m_bed_index{0};
        friend class GLCanvas3D;

    public:
        inline operator bool() const { return !std::isnan(m_pos.x()) && !std::isnan(m_pos.y()); }

        inline const Vec2d &pos() const { return m_pos; }
        inline double rotation() const { return m_rotation; }
        inline const Vec2d bb_size() const { return m_bb.size(); }
        inline const BoundingBoxf &bounding_box() const { return m_bb; }
        inline const int bed_index() const { return m_bed_index; }

        static void apply_wipe_tower(Model &model, Vec2d pos, double rot, int bed_index);
    };

    std::vector<WipeTowerInfo> get_wipe_tower_infos() const;

    // Returns the view ray line, in world coordinate, at the given mouse position.
    Linef3 mouse_ray(const Point &mouse_pos);

    bool is_mouse_dragging() const { return m_mouse.dragging; }

    double get_size_proportional_to_max_bed_size(double factor) const;

    void set_cursor(CursorType type);
    void msw_rescale() { m_gcode_viewer.invalidate_legend(); }

    void request_extra_frame() { m_extra_frame_requested = true; }

    void schedule_extra_frame(int miliseconds);

    float get_main_toolbar_height() const { return m_main_toolbar.get_height(); }
    float get_undoredo_toolbar_height() const { return m_undoredo_toolbar.get_height(); }
    std::pair<float, float> get_measure_button_position() const
    {
        return m_undoredo_toolbar.get_item_center_position("measure", *this);
    }
    std::pair<float, float> get_layersediting_button_position() const
    {
        return m_main_toolbar.get_item_center_position("layersediting", *this);
    }
    std::pair<float, float> get_brimears_button_position() const
    {
        return m_main_toolbar.get_item_center_position("brimears", *this);
    }
    int get_main_toolbar_item_id(const std::string &name) const { return m_main_toolbar.get_item_id(name); }
    void force_main_toolbar_left_action(int item_id) { m_main_toolbar.force_left_action(item_id, *this); }
    void force_main_toolbar_right_action(int item_id) { m_main_toolbar.force_right_action(item_id, *this); }
    void update_tooltip_for_settings_item_in_main_toolbar();

    bool has_toolpaths_to_export() const { return m_gcode_viewer.can_export_toolpaths(); }
    void export_toolpaths_to_obj(const char *filename) const { m_gcode_viewer.export_toolpaths_to_obj(filename); }

    void mouse_up_cleanup();

    // preFlight: Returns true when GPU power-saving optimizations should be active.
    // Only enabled on the Preview tab where GPU usage matters; the Platter uses
    // minimal GPU and needs full responsiveness for gizmos, hover, slicing progress, etc.
    bool is_gpu_power_saving() const;

    bool are_labels_shown() const { return m_labels.is_shown(); }
    void show_labels(bool show) { m_labels.show(show); }

    bool is_legend_shown() const { return m_gcode_viewer.is_legend_shown(); }
    void show_legend(bool show)
    {
        m_gcode_viewer.show_legend(show);
        m_dirty = true;
    }

    bool is_using_slope() const { return m_slope.is_used(); }
    void use_slope(bool use) { m_slope.use(use); }
    void set_slope_normal_angle(float angle_in_deg) { m_slope.set_normal_angle(angle_in_deg); }
    void set_slope_color(float r, float g, float b) { m_volumes.set_slope_color(r, g, b); }
    void highlight_toolbar_item(const std::string &item_name);
    void highlight_gizmo(const std::string &gizmo_name);

    // Timestamp for FPS calculation and notification fade-outs.
    static int64_t timestamp_now()
    {
#ifdef _WIN32
        // Cheaper on Windows, calls GetSystemTimeAsFileTime()
        return wxGetUTCTimeMillis().GetValue();
#else
        // calls clock()
        return wxGetLocalTimeMillis().GetValue();
#endif
    }

    const Print *fff_print() const;
    const SLAPrint *sla_print() const;

    void reset_old_size() { m_old_size = {0, 0}; }

    bool is_object_sinking(int object_idx) const;

    void apply_retina_scale(Vec2d &screen_coordinate) const;

    std::pair<SlicingParameters, const std::vector<double>> get_layers_height_data(int object_id);

    void detect_sla_view_type();
    void set_sla_view_type(ESLAViewType type);
    void set_sla_view_type(const GLVolume::CompositeID &id, ESLAViewType type);
    void enable_sla_view_type_detection() { m_sla_view_type_detection_active = true; }

private:
    bool _is_shown_on_screen() const;

    bool _init_toolbars();
    bool _init_main_toolbar();
    bool _init_undoredo_toolbar();
    bool _init_view_toolbar();
    bool _init_collapse_toolbar();

    bool _set_current();
    void _resize(unsigned int w, unsigned int h);

    BoundingBoxf3 _max_bounding_box(bool include_bed_model) const;

    void _zoom_to_box(const BoundingBoxf3 &box, double margin_factor = DefaultCameraZoomToBoxMarginFactor);
    void _update_camera_zoom(double zoom);

    void _refresh_if_shown_on_screen();

    void _picking_pass();
    void _rectangular_selection_picking_pass();
    void _render_background();
    void _render_bed(const Transform3d &view_matrix, const Transform3d &projection_matrix, bool bottom);
    void _render_bed_axes();
    void _render_objects(GLVolumeCollection::ERenderType type);
    void _render_gcode();
    void _render_gcode_cog() { m_gcode_viewer.render_cog(); }
    void _render_selection();
    bool check_toolbar_icon_size(float init_scale, float &new_scale_to_save, bool is_custom, int counter = 3);
#if ENABLE_RENDER_SELECTION_CENTER
    void _render_selection_center() { m_selection.render_center(m_gizmos.is_dragging()); }
#endif // ENABLE_RENDER_SELECTION_CENTER
    void _check_and_update_toolbar_icon_scale();
    void _render_overlays();
    void _render_bed_selector();
    void _render_current_gizmo() const { m_gizmos.render_current_gizmo(); }
    void _render_gizmos_overlay();
    void _render_main_toolbar();
    void _render_undoredo_toolbar();
    void _render_collapse_toolbar() const;
    void _render_view_toolbar() const;
    void _render_sidebar_toggle_imgui();
#if ENABLE_SHOW_CAMERA_TARGET
    void _render_camera_target();
    void _render_camera_target_validation_box();
#endif // ENABLE_SHOW_CAMERA_TARGET
    void _render_sla_slices();
    void _render_selection_sidebar_hints() { m_selection.render_sidebar_hints(m_sidebar_field); }
    bool _render_undo_redo_stack(const bool is_undo, float pos_x);
    bool _render_arrange_menu(float pos_x, bool current_bed);
    void _render_thumbnail_internal(ThumbnailData &thumbnail_data, const ThumbnailsParams &thumbnail_params,
                                    const GLVolumeCollection &volumes, Camera::EType camera_type);
    // render thumbnail using an off-screen framebuffer
    void _render_thumbnail_framebuffer(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                       const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                                       Camera::EType camera_type);
    // render thumbnail using an off-screen framebuffer when GL_EXT_framebuffer_object is supported
    void _render_thumbnail_framebuffer_ext(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                           const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                                           Camera::EType camera_type);
    // render thumbnail using the default framebuffer
    void _render_thumbnail_legacy(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                  const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                                  Camera::EType camera_type);

    void _update_volumes_hover_state();

    void _perform_layer_editing_action(const MouseInput *mouse = nullptr);

    // Convert the screen space coordinate to an object space coordinate.
    // If the Z screen space coordinate is not provided, a depth buffer value is substituted.
    Vec3d _mouse_to_3d(const Point &mouse_pos, const float *z = nullptr, bool use_ortho = false);

    // Convert the screen space coordinate to world coordinate on the bed.
    Vec3d _mouse_to_bed_3d(const Point &mouse_pos);

    void _start_timer()
    {
        if (m_timer)
            m_timer->start(100, false);
    }
    void _stop_timer()
    {
        if (m_timer)
            m_timer->stop();
    }

    // Load SLA objects and support structures for objects, for which the slaposSliceSupports step has been finished.
    void _load_sla_shells();
    void _update_sla_shells_outside_state() { check_volumes_outside_state(); }
    void _set_warning_notification_if_needed(EWarning warning);

    // generates a warning notification containing the given message
    void _set_warning_notification(EWarning warning, bool state);

    std::pair<bool, const GLVolume *> _is_any_volume_outside() const;
    bool _is_sequential_print_enabled() const;

    // updates the selection from the content of m_hover_volume_idxs
    void _update_selection_from_hover();

    bool _deactivate_undo_redo_toolbar_items();
    bool _deactivate_collapse_toolbar_items();
    bool _deactivate_arrange_menu();

    float get_overlay_window_width() { return LayersEditing::get_overlay_window_width(); }

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
    void show_binary_gcode_debug_window();
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
};

const ModelVolume *get_model_volume(const GLVolume &v, const Model &model);
ModelVolume *get_model_volume(const ObjectID &volume_id, const ModelObjectPtrs &objects);
ModelVolume *get_model_volume(const GLVolume &v, const ModelObjectPtrs &objects);
ModelVolume *get_model_volume(const GLVolume &v, const ModelObject &object);

GLVolume *get_first_hovered_gl_volume(const GLCanvas3D &canvas);
GLVolume *get_selected_gl_volume(const GLCanvas3D &canvas);

ModelObject *get_model_object(const GLVolume &gl_volume, const Model &model);
ModelObject *get_model_object(const GLVolume &gl_volume, const ModelObjectPtrs &objects);

ModelInstance *get_model_instance(const GLVolume &gl_volume, const Model &model);
ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObjectPtrs &objects);
ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObject &object);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
