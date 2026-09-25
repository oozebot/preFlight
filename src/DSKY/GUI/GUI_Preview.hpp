///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/panel.h>
#include <wx/timer.h>

#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/model/custom_gcode/CustomGCode.hpp"

#include <memory>
#include <string>
#include "luminary/gcode/interpret/GCodeProcessor.hpp"

class wxGLCanvas;
class wxBoxSizer;
class wxStaticText;
class wxComboBox;
class wxComboCtrl;
class wxCheckBox;

namespace DoubleSlider
{
class DSForGcode;
class DSForLayers;
}; // namespace DoubleSlider

namespace Luminary
{
class DynamicPrintConfig;
class Print;
class BackgroundSlicingProcess;
class Model;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class GLCanvas3D;
class GLSurface_wx;
class GLToolbar;
class Bed3D;
struct Camera;
class Plater;
#ifdef _WIN32
class BitmapComboBox;
#endif

class View3D : public wxPanel
{
    wxGLCanvas *m_canvas_widget;
    GLCanvas3D *m_canvas;
    std::unique_ptr<GLSurface_wx> m_gl_surface;

public:
    View3D(wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config, BackgroundSlicingProcess *process);
    virtual ~View3D();

    wxGLCanvas *get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D *get_canvas3d() { return m_canvas; }

    void set_as_dirty();
    void bed_shape_changed();

    void select_view(const std::string &direction);
    void select_all();
    void deselect_all();
    void delete_selected();
    void mirror_selection(Axis axis);

    bool is_layers_editing_enabled() const;
    bool is_layers_editing_allowed() const;
    void enable_layers_editing(bool enable);

    bool is_dragging() const;
    bool is_reload_delayed() const;

    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);
    void render();

private:
    bool init(wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config,
              BackgroundSlicingProcess *process);
};

class Preview : public wxPanel
{
    wxGLCanvas *m_canvas_widget{nullptr};
    GLCanvas3D *m_canvas{nullptr};
    std::unique_ptr<GLSurface_wx> m_gl_surface;
    wxBoxSizer *m_left_sizer{nullptr};

    DynamicPrintConfig *m_config;
    BackgroundSlicingProcess *m_process;
    std::vector<GCodeProcessorResult> *m_gcode_results;

    GCodeProcessorResult *active_gcode_result();

    // Calling this function object forces Plater::schedule_background_process.
    std::function<void()> m_schedule_background_process;

    unsigned int m_number_extruders{1};
    bool m_keep_current_preview_type{false};

    bool m_loaded{false};
    bool m_preview_gpu_loaded{false};
    // Bed whose result the GPU holds; another bed's freed result cannot be shown from memory.
    int m_preview_gpu_bed{-1};
    int m_last_layer_pos{-1};

    std::unique_ptr<DoubleSlider::DSForLayers> m_layers_slider{nullptr};
    std::unique_ptr<DoubleSlider::DSForGcode> m_moves_slider{nullptr};
    std::vector<float> m_cached_layer_times;

public:
    enum class OptionType : unsigned int
    {
        Travel,
        Wipe,
        Retractions,
        Unretractions,
        Seams,
        ToolChanges,
        ColorChanges,
        PausePrints,
        CustomGCodes,
        CenterOfGravity,
        Shells,
        ToolMarker,
    };

    Preview(
        wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config, BackgroundSlicingProcess *process,
        std::vector<GCodeProcessorResult> *gcode_results, std::function<void()> schedule_background_process = []() {});
    virtual ~Preview();

    wxGLCanvas *get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D *get_canvas3d() { return m_canvas; }

    void set_as_dirty();

    void bed_shape_changed();
    void select_view(const std::string &direction);
    void set_drop_target(wxDropTarget *target);

    void load_print(bool keep_z_range = false);
    void reload_print();

    void msw_rescale();

    void render_sliders(GLCanvas3D &canvas);
    float get_layers_slider_width(bool disregard_visibility = false) const;
    float get_moves_slider_height() const;

    bool is_loaded() const { return m_loaded; }

    void update_moves_slider(std::optional<int> visible_range_min = std::nullopt,
                             std::optional<int> visible_range_max = std::nullopt);
    void enable_moves_slider(bool enable);
    void hide_layers_slider();

    void set_keep_current_preview_type(bool value) { m_keep_current_preview_type = value; }

    void set_layers_slider_values_range(int bottom, int top);

private:
    bool init(wxWindow *parent, Bed3D &bed, Model *model);

    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent &evt);

    // Create/Update/Reset double slider on 3dPreview
    void create_sliders();
    void check_layers_slider_values(std::vector<CustomGCode::Item> &ticks_from_model,
                                    const std::vector<double> &layers_z);
    void reset_layers_slider();
    void update_layers_slider(const std::vector<double> &layers_z, bool keep_z_range = false);
    void update_layers_slider_mode();
    // update vertical DoubleSlider after keyDown in canvas
    void update_sliders_from_canvas(wxKeyEvent &event);
    void on_key_up(wxKeyEvent &event);
    void on_arrow_repeat_timer(wxTimerEvent &event);
    void do_arrow_repeat();
    void move_slider_by_key(int arrow_key, int delta,
                            bool from_gcode_scroll = false); // Helper to avoid code duplication
    wxTimer m_arrow_repeat_timer;
    int m_held_arrow_key{0};
    long m_last_arrow_keydown_time{0}; // Track when last KEY_DOWN received

    void load_print_as_fff(bool keep_z_range = false);

    void on_layers_slider_scroll_changed();
    void on_moves_slider_scroll_changed();
    std::string get_moves_slider_hover_label(int pos);
};

} // namespace DSKY

