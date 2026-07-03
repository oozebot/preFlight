///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2020 Benjamin Greiner
///|/ Copyright (c) 2019 John Drake @foxox
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/ Copyright (c) 2019 Thomas Moore
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLCanvas3D.hpp"
#include "InputEvents_wx.hpp"
#include "GLSurface_wx.hpp"
#include "Timer_wx.hpp"

#include <boost/nowide/fstream.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <igl/unproject.h> // IWYU pragma: keep
#include <LocalesUtils.hpp>
#include <nanosvgrast.h>

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Technologies.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "3DBed.hpp"
#include "3DScene.hpp"
#include "CSGPreviewManager.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "GLShader.hpp"
#include "GUI.hpp"
#include "Tab.hpp"
#include "GUI_Preview.hpp"
#include "OpenGLManager.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "Mouse3DController.hpp"
#include "ModernTabBar.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "format.hpp"
#include "libslic3r/ArrangeHelper.hpp"

#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "Widgets/UIColors.hpp"

#if ENABLE_RETINA_GL
#include "slic3r/Utils/RetinaHelper.hpp"
#endif

#if SLIC3R_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

#include <wx/glcanvas.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/tooltip.h>
#include <wx/debug.h>
#include <wx/fontutil.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "wxExtensions.hpp"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/crc.hpp>
#include <boost/regex.hpp>

#include <iostream>
#include <float.h>
#include <algorithm>
#include <cmath>

#include <map>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <slic3r/GUI/Gizmos/TriangleSelectorMmGui.hpp>

extern std::vector<GLuint> s_th_tex_id;

static constexpr const float TRACKBALLSIZE = 0.8f;

// Helper to convert wxColour to ColorRGBA
static Slic3r::ColorRGBA wxColourToColorRGBA(const wxColour &c, float alpha = 1.0f)
{
    return {c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f, alpha};
}

// Canvas background colors - now using centralized UIColors
static Slic3r::ColorRGBA get_bg_color(bool dark_mode)
{
    return dark_mode ? wxColourToColorRGBA(UIColors::CanvasBackgroundDark())
                     : wxColourToColorRGBA(UIColors::CanvasBackgroundLight());
}

static Slic3r::ColorRGBA get_gradient_top_color(bool dark_mode)
{
    return dark_mode ? wxColourToColorRGBA(UIColors::CanvasGradientTopDark())
                     : wxColourToColorRGBA(UIColors::CanvasGradientTopLight());
}

// Error colors (orange-tinted versions of background)
static const Slic3r::ColorRGBA ERROR_BG_DARK_COLOR = {0.478f, 0.192f, 0.039f, 1.0f};
static const Slic3r::ColorRGBA ERROR_BG_LIGHT_COLOR = {0.753f, 0.192f, 0.039f, 1.0f};

#define SHOW_IMGUI_DEMO_WINDOW
#ifdef SHOW_IMGUI_DEMO_WINDOW
static bool show_imgui_demo_window = false;
#endif // SHOW_IMGUI_DEMO_WINDOW

namespace Slic3r
{
namespace GUI
{

void GLCanvas3D::select_bed(int i, bool triggered_by_user)
{
    int old_bed = s_multiple_beds.get_active_bed();
    if ((i == old_bed && !s_multiple_beds.is_autoslicing()) || i == -1)
        return;

    m_process->stop();

    m_event_poster->postEvent(CanvasEventType::EnableActionButtons, is_sliceable(s_print_statuses[i]));

    // The stop call above schedules some events that would be processed after the switch.
    // Among else, on_process_completed would be called, which would stop slicing of
    // the new bed. We need to stop the process, pump all the events out of the queue
    // and then switch the beds.
    if (!m_call_after)
        return;
    m_call_after(
        [this, i, old_bed, triggered_by_user]()
        {
            wxYield();
            s_multiple_beds.set_active_bed(i);
            s_beds_just_switched = true;
            s_beds_switched_since_last_gcode_load = true;
            if (is_preview_shown())
            {
                s_reload_preview_after_switching_beds = true;
                m_event_poster->postEvent(CanvasEventType::ViewToolbarPreview);
                m_camera->translate_world(s_multiple_beds.get_bed_translation(i) -
                                          s_multiple_beds.get_bed_translation(old_bed));
            }
            m_event_poster->postEvent(CanvasEventType::ScheduleBackgroundProcess);
            m_event_poster->postEvent(CanvasEventType::ObjectListChanged);
            if (s_multiple_beds.is_autoslicing() && triggered_by_user)
            {
                s_multiple_beds.stop_autoslice(false);
                m_event_poster->postEvent(CanvasEventType::SwitchFromAutoslicingMode);
            }
        });
}

// Returns extruder model to visualize in the GCodeViewer:
// - nullopt = same as before
// - nullptr = none available, use generic
// - GLModel = the model to use
// The other field is whether the printer is HT90.
std::pair<std::optional<std::unique_ptr<GLModel>>, bool> GLCanvas3D::get_current_marker_model() const
{
    auto out = std::make_pair<std::optional<std::unique_ptr<GLModel>>, bool>(std::nullopt, false);

    static std::string last_printer_notes;
    static double old_r = 0.;
    static double old_h = 0.;
    static bool old_seq = false;

    std::string printer_notes = m_config->opt_string("printer_notes");
    double r = m_config->opt_float("extruder_clearance_radius");
    double h = m_config->opt_float("extruder_clearance_height");
    bool seq = m_config->opt_bool("complete_objects");

    if (last_printer_notes != printer_notes || r != old_r || h != old_h || seq != old_seq)
    {
        last_printer_notes = printer_notes;
        old_r = r;
        old_h = h;
        old_seq = seq;

        out.second = (printer_notes.find("PRINTER_MODEL_HT90") != std::string::npos);

        out.first = std::make_optional(nullptr);
        if (!seq)
            return out;
        try
        {
            boost::nowide::ifstream in(resources_dir() + "/data/printer_gantries/geometries.json");
            boost::property_tree::ptree pt;
            boost::property_tree::read_json(in, pt);
            for (const auto &printer : pt.get_child("printers"))
            {
                std::string printer_notes_match = printer.second.get<std::string>("printer_notes_regex");
                boost::regex rgx(printer_notes_match);
                if (boost::regex_match(printer_notes, rgx))
                {
                    std::string filename = resources_dir() + "/data/printer_gantries/" +
                                           printer.second.get<std::string>("gantry_model_filename");
                    if (boost::filesystem::exists(filename))
                    {
                        std::unique_ptr<GLModel> m = std::make_unique<GLModel>();
                        if (m->init_from_file(filename))
                            out.first = std::make_optional(std::move(m));
                    }
                    break;
                }
            }
        }
        catch (...)
        {
            // Whatever happened, ignore it. We will return nullptr.
        }
        if (*(out.first) == nullptr && seq)
        {
            // Generic sequential extruder model.
            double gantry_height = 10;
            auto mesh = its_make_cylinder(r, h + gantry_height - 0.001);
            double d = 3 * build_volume().bounding_volume2d().size().x();
            auto mesh2 = its_make_cube(d, 2 * r, gantry_height);
            its_translate(mesh2, Vec3f(-d / 2, -r, h));
            its_merge(mesh, mesh2);
            std::unique_ptr<GLModel> m = std::make_unique<GLModel>();
            m->init_from(mesh);
            out.first = std::make_optional(std::move(m));
        }
    }
    return out;
}

#ifdef __WXGTK3__
// wxGTK3 seems to simulate OSX behavior in regard to HiDPI scaling support.
RetinaHelper::RetinaHelper(wxWindow *window) : m_window(window), m_self(nullptr) {}
RetinaHelper::~RetinaHelper() {}
float RetinaHelper::get_scale_factor()
{
    return float(m_window->GetContentScaleFactor());
}
#endif // __WXGTK3__

// Fixed the collision between BuildVolume::Type::Convex and macro Convex defined inside /usr/include/X11/X.h that is included by WxWidgets 3.0.
#if defined(__linux__) && defined(Convex)
#undef Convex
#endif

GLCanvas3D::LayersEditing::~LayersEditing()
{
    if (m_z_texture_id != 0)
    {
        glsafe(::glDeleteTextures(1, &m_z_texture_id));
        m_z_texture_id = 0;
    }
    delete m_slicing_parameters;
}

const float GLCanvas3D::LayersEditing::THICKNESS_BAR_WIDTH = 70.0f;

void GLCanvas3D::LayersEditing::init()
{
    glsafe(::glGenTextures(1, (GLuint *) &m_z_texture_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    if (!OpenGLManager::get_gl_info().is_core_profile() || !OpenGLManager::get_gl_info().is_mesa())
    {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    }
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void GLCanvas3D::LayersEditing::set_config(const DynamicPrintConfig *config)
{
    m_config = config;
    delete m_slicing_parameters;
    m_slicing_parameters = nullptr;
    m_layers_texture.valid = false;

    m_layer_height_profile.clear();
    m_layer_height_profile_modified = false;
}

void GLCanvas3D::LayersEditing::select_object(const Model &model, int object_id)
{
    const ModelObject *model_object_new = (object_id >= 0) ? model.objects[object_id] : nullptr;
    // Maximum height of an object changes when the object gets rotated or scaled.
    // Changing maximum height of an object will invalidate the layer heigth editing profile.
    // m_model_object->bounding_box() is cached, therefore it is cheap even if this method is called frequently.
    const float new_max_z = (model_object_new == nullptr) ? 0.0f : static_cast<float>(model_object_new->max_z());

    if (m_model_object != model_object_new || this->last_object_id != object_id || m_object_max_z != new_max_z ||
        (model_object_new != nullptr && m_model_object->id() != model_object_new->id()))
    {
        m_layer_height_profile.clear();
        m_layer_height_profile_modified = false;
        delete m_slicing_parameters;
        m_slicing_parameters = nullptr;
        m_layers_texture.valid = false;
        this->last_object_id = object_id;
        m_model_object = model_object_new;
        m_object_max_z = new_max_z;
    }
}

bool GLCanvas3D::LayersEditing::is_allowed() const
{
    return m_get_shader && m_get_shader("variable_layer_height") != nullptr && m_z_texture_id > 0;
}

float GLCanvas3D::LayersEditing::s_overlay_window_width;

void GLCanvas3D::LayersEditing::render_overlay(const GLCanvas3D &canvas)
{
    if (!m_enabled)
        return;

    const Size &cnv_size = canvas.get_canvas_size();

    auto button_pos = canvas.get_layersediting_button_position();
    float menu_x = button_pos.first;
    const float style_scaling = canvas.get_imgui()->get_style_scaling();
    float menu_y = canvas.get_main_toolbar_height() + 5.0f * style_scaling;
    ImGuiPureWrap::set_next_window_pos(menu_x, menu_y, ImGuiCond_Always, 0.5f, 0.0f);

    ImGuiPureWrap::begin(_u8L("Variable layer height"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Left mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Add detail"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Right mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Remove detail"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Shift + Left mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Reset to base"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Shift + Right mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Smoothing"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Mouse wheel:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Increase/decrease edit area"));

    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Adaptive")))
        canvas.event_poster()->postEvent(CanvasEventType::AdaptiveLayerHeightProfile, m_adaptive_quality);

    ImGui::SameLine();
    float text_align = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Quality / Speed"));
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(_u8L("Higher print quality versus higher print speed.").c_str());
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    float widget_align = ImGui::GetCursorPosX();
    ImGui::PushItemWidth(style_scaling * 120.0f);
    m_adaptive_quality = std::clamp(m_adaptive_quality, 0.0f, 1.f);
    canvas.get_imgui()->slider_float("", &m_adaptive_quality, 0.0f, 1.f, "%.2f");

    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Smooth")))
        canvas.event_poster()->postEvent(CanvasEventType::SmoothLayerHeightProfile, m_smooth_params);

    ImGui::SameLine();
    ImGui::SetCursorPosX(text_align);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Radius"));
    ImGui::SameLine();
    ImGui::SetCursorPosX(widget_align);
    ImGui::PushItemWidth(style_scaling * 120.0f);
    int radius = (int) m_smooth_params.radius;
    if (ImGui::SliderInt("##1", &radius, 1, 10))
    {
        radius = std::clamp(radius, 1, 10);
        m_smooth_params.radius = (unsigned int) radius;
    }

    ImGui::SetCursorPosX(text_align);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Keep min"));
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() < widget_align) // because of line lenght after localization
        ImGui::SetCursorPosX(widget_align);

    ImGui::PushItemWidth(style_scaling * 120.0f);
    ImGuiPureWrap::checkbox("##2", m_smooth_params.keep_min);

    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Reset")))
        canvas.event_poster()->postEvent(CanvasEventType::ResetLayerHeightProfile);

    GLCanvas3D::LayersEditing::s_overlay_window_width = ImGui::GetWindowSize().x /*+ (float)m_layers_texture.width/4*/;
    ImGuiPureWrap::end();

    render_active_object_annotations(canvas);
    render_profile(canvas);
}

float GLCanvas3D::LayersEditing::get_cursor_z_relative(const GLCanvas3D &canvas)
{
    const Vec2d mouse_pos = canvas.get_local_mouse_position();
    const Rect &rect = get_bar_rect_screen(canvas);
    float x = (float) mouse_pos.x();
    float y = (float) mouse_pos.y();
    float t = rect.get_top();
    float b = rect.get_bottom();

    return (rect.get_left() <= x && x <= rect.get_right() && t <= y && y <= b) ?
                                                                               // Inside the bar.
               (b - y - 1.0f) / (b - t - 1.0f)
                                                                               :
                                                                               // Outside the bar.
               -1000.0f;
}

bool GLCanvas3D::LayersEditing::bar_rect_contains(const GLCanvas3D &canvas, float x, float y)
{
    const Rect &rect = get_bar_rect_screen(canvas);
    return rect.get_left() <= x && x <= rect.get_right() && rect.get_top() <= y && y <= rect.get_bottom();
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_screen(const GLCanvas3D &canvas)
{
    const Size &cnv_size = canvas.get_canvas_size();
    float w = (float) cnv_size.get_width();
    float h = (float) cnv_size.get_height();

    const float bar_w = thickness_bar_width(canvas);
    if (wxGetApp().legacy_prepare_layout())
        return {w - bar_w, 0.0f, w, h};
    return {0.0f, 0.0f, bar_w, h};
}

std::pair<SlicingParameters, const std::vector<double>> GLCanvas3D::LayersEditing::get_layers_height_data()
{
    if (m_slicing_parameters != nullptr)
        return {*m_slicing_parameters, m_layer_height_profile};

    assert(m_model_object != nullptr);
    this->update_slicing_parameters();
    PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile);
    std::pair<SlicingParameters, const std::vector<double>> ret = {*m_slicing_parameters, m_layer_height_profile};
    delete m_slicing_parameters;
    m_slicing_parameters = nullptr;
    return ret;
}

bool GLCanvas3D::LayersEditing::is_initialized() const
{
    return m_get_shader && m_get_shader("variable_layer_height") != nullptr;
}

std::string GLCanvas3D::LayersEditing::get_tooltip(const GLCanvas3D &canvas) const
{
    std::string ret;
    if (m_enabled && m_layer_height_profile.size() >= 4)
    {
        float z = get_cursor_z_relative(canvas);
        if (z != -1000.0f)
        {
            z *= m_object_max_z;

            float h = 0.0f;
            for (size_t i = m_layer_height_profile.size() - 2; i >= 2; i -= 2)
            {
                const float zi = static_cast<float>(m_layer_height_profile[i]);
                const float zi_1 = static_cast<float>(m_layer_height_profile[i - 2]);
                if (zi_1 <= z && z <= zi)
                {
                    float dz = zi - zi_1;
                    h = (dz != 0.0f) ? static_cast<float>(lerp(m_layer_height_profile[i - 1],
                                                               m_layer_height_profile[i + 1], (z - zi_1) / dz))
                                     : static_cast<float>(m_layer_height_profile[i + 1]);
                    break;
                }
            }
            if (h > 0.0f)
                ret = format("%.3f", h);
        }
    }
    return ret;
}

void GLCanvas3D::LayersEditing::render_active_object_annotations(const GLCanvas3D &canvas)
{
    const Size cnv_size = canvas.get_canvas_size();
    const float cnv_width = (float) cnv_size.get_width();
    const float cnv_height = (float) cnv_size.get_height();
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    const float cnv_inv_width = 1.0f / cnv_width;
    GLShaderProgram *shader = canvas.get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    shader->start_using();

    shader->set_uniform("z_to_texture_row",
                        float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * m_object_max_z));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / (float) m_layers_texture.height);
    shader->set_uniform("z_cursor", m_object_max_z * this->get_cursor_z_relative(canvas));
    shader->set_uniform("z_cursor_band_width", band_width);
    shader->set_uniform("object_max_z", m_object_max_z);
    shader->set_uniform("view_model_matrix", Transform3d::Identity());
    shader->set_uniform("projection_matrix", Transform3d::Identity());
    shader->set_uniform("view_normal_matrix", (Matrix3d) Matrix3d::Identity());

    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));

    // Render the color bar
    if (!m_profile.background.is_initialized() || m_profile.old_canvas_width.background != cnv_width)
    {
        m_profile.old_canvas_width.background = cnv_width;
        m_profile.background.reset();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3T2};
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);

        // In legacy layout the gizmo toolbar is on the left, so put the bar on the right edge
        const bool legacy = wxGetApp().legacy_prepare_layout();
        const float bar_ndc_w = 2.0f * THICKNESS_BAR_WIDTH * cnv_inv_width;
        const float l = legacy ? 1.0f - bar_ndc_w : -1.0f;
        const float r = l + bar_ndc_w;
        const float t = 1.0f;
        const float b = -1.0f;
        init_data.add_vertex(Vec3f(l, b, 0.0f), Vec3f::UnitZ(), Vec2f(0.0f, 0.0f));
        init_data.add_vertex(Vec3f(r, b, 0.0f), Vec3f::UnitZ(), Vec2f(1.0f, 0.0f));
        init_data.add_vertex(Vec3f(r, t, 0.0f), Vec3f::UnitZ(), Vec2f(1.0f, 1.0f));
        init_data.add_vertex(Vec3f(l, t, 0.0f), Vec3f::UnitZ(), Vec2f(0.0f, 1.0f));

        // indices
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(2, 3, 0);

        m_profile.background.init_from(std::move(init_data));
    }

    m_profile.background.render();

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    shader->stop_using();
}

void GLCanvas3D::LayersEditing::render_profile(const GLCanvas3D &canvas)
{
    //FIXME show some kind of legend.

    if (!m_slicing_parameters)
        return;

    const Size cnv_size = canvas.get_canvas_size();
    const float cnv_width = (float) cnv_size.get_width();
    const float cnv_height = (float) cnv_size.get_height();
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    // Make the vertical bar a bit wider so the layer height curve does not touch the edge of the bar region.
    const float scale_x = THICKNESS_BAR_WIDTH / float(1.12 * m_slicing_parameters->max_layer_height);
    const float scale_y = cnv_height / m_object_max_z;

    const float cnv_inv_width = 1.0f / cnv_width;
    const float cnv_inv_height = 1.0f / cnv_height;

    const float bar_ndc_w = 2.0f * THICKNESS_BAR_WIDTH * cnv_inv_width;
    const float left = wxGetApp().legacy_prepare_layout() ? 1.0f - bar_ndc_w : -1.0f;

    // Baseline
    if (!m_profile.baseline.is_initialized() || m_profile.old_layer_height_profile != m_layer_height_profile ||
        m_profile.old_canvas_width.baseline != cnv_width)
    {
        m_profile.old_canvas_width.baseline = cnv_width;
        m_profile.baseline.reset();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P2};
        init_data.color = ColorRGBA::BLACK();
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        const float axis_x = left + 2.0f * float(m_slicing_parameters->layer_height) * scale_x * cnv_inv_width;
        init_data.add_vertex(Vec2f(axis_x, -1.0f));
        init_data.add_vertex(Vec2f(axis_x, 1.0f));

        // indices
        init_data.add_line(0, 1);

        m_profile.baseline.init_from(std::move(init_data));
    }

    if (!m_profile.profile.is_initialized() || m_profile.old_layer_height_profile != m_layer_height_profile ||
        m_profile.old_canvas_width.profile != cnv_width)
    {
        m_profile.old_canvas_width.profile = cnv_width;
        m_profile.old_layer_height_profile = m_layer_height_profile;
        m_profile.profile.reset();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P2};
        init_data.color = ColorRGBA::BLUE();
        init_data.reserve_vertices(m_layer_height_profile.size() / 2);
        init_data.reserve_indices(m_layer_height_profile.size() / 2);

        // vertices + indices
        for (unsigned int i = 0; i < (unsigned int) m_layer_height_profile.size(); i += 2)
        {
            init_data.add_vertex(Vec2f(left + 2.0f * float(m_layer_height_profile[i + 1]) * scale_x * cnv_inv_width,
                                       2.0f * (float(m_layer_height_profile[i]) * scale_y * cnv_inv_height - 0.5)));
            init_data.add_index(i / 2);
        }

        m_profile.profile.init_from(std::move(init_data));
    }

#if SLIC3R_OPENGL_ES
    GLShaderProgram *shader = canvas.get_shader("dashed_lines");
#else
    GLShaderProgram *shader = OpenGLManager::get_gl_info().is_core_profile() ? canvas.get_shader("dashed_thick_lines")
                                                                             : canvas.get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr)
    {
        shader->start_using();
        shader->set_uniform("view_model_matrix", Transform3d::Identity());
        shader->set_uniform("projection_matrix", Transform3d::Identity());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile())
        {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4> &viewport = canvas.get_camera().get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.25f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
        m_profile.baseline.render();
        m_profile.profile.render();
        shader->stop_using();
    }
}

void GLCanvas3D::LayersEditing::render_volumes(const GLCanvas3D &canvas, const GLVolumeCollection &volumes)
{
    assert(this->is_allowed());
    assert(this->last_object_id != -1);

    GLShaderProgram *current_shader = canvas.get_current_shader();
    ScopeGuard guard(
        [current_shader]()
        {
            if (current_shader != nullptr)
                current_shader->start_using();
        });
    if (current_shader != nullptr)
        current_shader->stop_using();

    GLShaderProgram *shader = canvas.get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    shader->start_using();

    generate_layer_height_texture();

    // Uniforms were resolved, go ahead using the layer editing shader.
    shader->set_uniform("z_to_texture_row",
                        float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * float(m_object_max_z)));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / float(m_layers_texture.height));
    shader->set_uniform("z_cursor", float(m_object_max_z) * float(this->get_cursor_z_relative(canvas)));
    shader->set_uniform("z_cursor_band_width", float(this->band_width));

    const Camera &camera = canvas.get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    // Initialize the layer height texture mapping.
    const GLsizei w = (GLsizei) m_layers_texture.width;
    const GLsizei h = (GLsizei) m_layers_texture.height;
    const GLsizei half_w = w / 2;
    const GLsizei half_h = h / 2;
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_layers_texture.data.data()));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, half_w, half_h, GL_RGBA, GL_UNSIGNED_BYTE,
                             m_layers_texture.data.data() + m_layers_texture.width * m_layers_texture.height * 4));
    for (GLVolume *glvolume : volumes.volumes)
    {
        // Render the object using the layer editing shader and texture.
        if (!glvolume->is_active || glvolume->composite_id.object_id != this->last_object_id || glvolume->is_modifier)
            continue;

        shader->set_uniform("volume_world_matrix", glvolume->world_matrix());
        shader->set_uniform("object_max_z", 0.0f);
        const Transform3d &view_matrix = camera.get_view_matrix();
        const Transform3d model_matrix = glvolume->world_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                            model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        glvolume->render();
    }
    // Revert back to the previous shader.
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void GLCanvas3D::LayersEditing::adjust_layer_height_profile()
{
    this->update_slicing_parameters();
    PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile);
    Slic3r::adjust_layer_height_profile(*m_slicing_parameters, m_layer_height_profile, this->last_z, this->strength,
                                        this->band_width, this->last_action);
    m_layer_height_profile_modified = true;
    m_layers_texture.valid = false;
}

void GLCanvas3D::LayersEditing::reset_layer_height_profile(GLCanvas3D &canvas)
{
    const_cast<ModelObject *>(m_model_object)->layer_height_profile.clear();
    m_layer_height_profile.clear();
    m_layers_texture.valid = false;
    canvas.m_event_poster->postEvent(CanvasEventType::ScheduleBackgroundProcess);
    canvas.event_poster()->postEvent(CanvasEventType::UpdateInfoItems, last_object_id);
}

void GLCanvas3D::LayersEditing::adaptive_layer_height_profile(GLCanvas3D &canvas, float quality_factor)
{
    this->update_slicing_parameters();
    m_layer_height_profile = layer_height_profile_adaptive(*m_slicing_parameters, *m_model_object, quality_factor);
    const_cast<ModelObject *>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.m_event_poster->postEvent(CanvasEventType::ScheduleBackgroundProcess);
    canvas.event_poster()->postEvent(CanvasEventType::UpdateInfoItems, last_object_id);
}

void GLCanvas3D::LayersEditing::smooth_layer_height_profile(GLCanvas3D &canvas,
                                                            const HeightProfileSmoothingParams &smoothing_params)
{
    this->update_slicing_parameters();
    m_layer_height_profile = smooth_height_profile(m_layer_height_profile, *m_slicing_parameters, smoothing_params);
    const_cast<ModelObject *>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.m_event_poster->postEvent(CanvasEventType::ScheduleBackgroundProcess);
    canvas.event_poster()->postEvent(CanvasEventType::UpdateInfoItems, last_object_id);
}

void GLCanvas3D::LayersEditing::generate_layer_height_texture()
{
    this->update_slicing_parameters();
    // Always try to update the layer height profile.
    bool update = !m_layers_texture.valid;
    if (PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile))
    {
        // Initialized to the default value.
        m_layer_height_profile_modified = false;
        update = true;
    }

    // Update if the layer height profile was changed, or when the texture is not valid.
    if (!update && !m_layers_texture.data.empty() && m_layers_texture.cells > 0)
        // Texture is valid, don't update.
        return;

    if (m_layers_texture.data.empty())
    {
        m_layers_texture.width = 1024;
        m_layers_texture.height = 1024;
        m_layers_texture.levels = 2;
        m_layers_texture.data.assign(m_layers_texture.width * m_layers_texture.height * 5, 0);
    }

    bool level_of_detail_2nd_level = true;
    m_layers_texture.cells = Slic3r::generate_layer_height_texture(
        *m_slicing_parameters, Slic3r::generate_object_layers(*m_slicing_parameters, m_layer_height_profile),
        m_layers_texture.data.data(), m_layers_texture.height, m_layers_texture.width, level_of_detail_2nd_level);
    m_layers_texture.valid = true;
}

void GLCanvas3D::LayersEditing::accept_changes(GLCanvas3D &canvas)
{
    if (last_object_id >= 0)
    {
        if (m_layer_height_profile_modified)
        {
            canvas.event_poster()->postEvent(CanvasEventType::TakeSnapshot,
                                             _u8L("Variable layer height - Manual edit"));
            const_cast<ModelObject *>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
            canvas.event_poster()->postEvent(CanvasEventType::UpdateInfoItems, last_object_id);
            canvas.event_poster()->postEvent(CanvasEventType::ScheduleBackgroundProcess);
        }
    }
    m_layer_height_profile_modified = false;
}

void GLCanvas3D::LayersEditing::update_slicing_parameters()
{
    if (m_slicing_parameters == nullptr)
    {
        m_slicing_parameters = new SlicingParameters();
        *m_slicing_parameters = PrintObject::slicing_parameters(*m_config, *m_model_object, m_object_max_z,
                                                                m_shrinkage_compensation);
    }
}

float GLCanvas3D::LayersEditing::thickness_bar_width(const GLCanvas3D &canvas)
{
    return
#if ENABLE_RETINA_GL
        canvas.get_canvas_size().get_scale_factor()
#else
        canvas.get_wxglcanvas()->GetContentScaleFactor()
#endif
        * THICKNESS_BAR_WIDTH;
}

const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Vec3d GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);
// Note - MoveThresholdPx is intentionally kept as a small fixed value (5px)
// because it's a mouse drag threshold that should remain consistent regardless of DPI.
// The physical screen distance matters for detecting intentional drags vs accidental clicks.
const int GLCanvas3D::Mouse::Drag::MoveThresholdPx = 5;

void GLCanvas3D::Labels::render(const std::vector<const ModelInstance *> &sorted_instances) const
{
    if (!m_enabled || !is_shown())
        return;

    const Camera &camera = m_canvas.get_camera();
    const Model *model = m_canvas.get_model();
    if (model == nullptr)
        return;

    Transform3d world_to_eye = camera.get_view_matrix();
    Transform3d world_to_screen = camera.get_projection_matrix() * world_to_eye;
    const std::array<int, 4> &viewport = camera.get_viewport();

    struct Owner
    {
        int obj_idx;
        int inst_idx;
        size_t model_instance_id;
        BoundingBoxf3 world_box;
        double eye_center_z;
        std::string title;
        std::string label;
        std::string print_order;
        bool selected;
    };

    // collect owners world bounding boxes and data from volumes
    std::vector<Owner> owners;
    const GLVolumeCollection &volumes = m_canvas.get_volumes();
    for (const GLVolume *volume : volumes.volumes)
    {
        int obj_idx = volume->object_idx();
        if (0 <= obj_idx && obj_idx < (int) model->objects.size())
        {
            int inst_idx = volume->instance_idx();
            std::vector<Owner>::iterator it =
                std::find_if(owners.begin(), owners.end(), [obj_idx, inst_idx](const Owner &owner)
                             { return (owner.obj_idx == obj_idx) && (owner.inst_idx == inst_idx); });
            if (it != owners.end())
            {
                it->world_box.merge(volume->transformed_bounding_box());
                it->selected &= volume->selected;
            }
            else
            {
                const ModelObject *model_object = model->objects[obj_idx];
                Owner owner;
                owner.obj_idx = obj_idx;
                owner.inst_idx = inst_idx;
                owner.model_instance_id = model_object->instances[inst_idx]->id().id;
                owner.world_box = volume->transformed_bounding_box();
                owner.title = "object" + std::to_string(obj_idx) + "_inst##" + std::to_string(inst_idx);
                owner.label = model_object->name;
                if (model_object->instances.size() > 1)
                    owner.label += " (" + std::to_string(inst_idx + 1) + ")";
                owner.selected = volume->selected;
                owners.emplace_back(owner);
            }
        }
    }

    // updates print order strings
    if (sorted_instances.size() > 1)
    {
        for (size_t i = 0; i < sorted_instances.size(); ++i)
        {
            size_t id = sorted_instances[i]->id().id;
            std::vector<Owner>::iterator it = std::find_if(owners.begin(), owners.end(), [id](const Owner &owner)
                                                           { return owner.model_instance_id == id; });
            if (it != owners.end())
                it->print_order = _u8L("Seq.") + "#: " + std::to_string(i + 1);
        }
    }

    // calculate eye bounding boxes center zs
    for (Owner &owner : owners)
    {
        owner.eye_center_z = (world_to_eye * owner.world_box.center())(2);
    }

    // sort owners by center eye zs and selection
    std::sort(owners.begin(), owners.end(),
              [](const Owner &owner1, const Owner &owner2)
              {
                  if (!owner1.selected && owner2.selected)
                      return true;
                  else if (owner1.selected && !owner2.selected)
                      return false;
                  else
                      return (owner1.eye_center_z < owner2.eye_center_z);
              });

    // render info windows
    for (const Owner &owner : owners)
    {
        Vec3d screen_box_center = world_to_screen * owner.world_box.center();
        float x = 0.0f;
        float y = 0.0f;
        if (camera.get_type() == Camera::EType::Perspective)
        {
            x = (0.5f + 0.001f * 0.5f * (float) screen_box_center(0)) * viewport[2];
            y = (0.5f - 0.001f * 0.5f * (float) screen_box_center(1)) * viewport[3];
        }
        else
        {
            x = (0.5f + 0.5f * (float) screen_box_center(0)) * viewport[2];
            y = (0.5f - 0.5f * (float) screen_box_center(1)) * viewport[3];
        }

        if (x < 0.0f || viewport[2] < x || y < 0.0f || viewport[3] < y)
            continue;

        const float scale = m_canvas.get_imgui()->get_style_scaling();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, (owner.selected ? 3.0f : 1.5f) * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, owner.selected ? ImGuiPureWrap::COL_ORANGE_LIGHT // themed accent
                                                              : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.5f);
        ImGuiPureWrap::begin(owner.title, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize |
                                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        float win_w = ImGui::GetWindowWidth();
        float label_len = ImGui::CalcTextSize(owner.label.c_str()).x;
        ImGui::SetCursorPosX(0.5f * (win_w - label_len));
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(owner.label);

        if (!owner.print_order.empty())
        {
            ImGui::Separator();
            float po_len = ImGui::CalcTextSize(owner.print_order.c_str()).x;
            ImGui::SetCursorPosX(0.5f * (win_w - po_len));
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(owner.print_order);
        }

        // force re-render while the windows gets to its final size (it takes several frames)
        if (ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x !=
            ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
            m_canvas.get_imgui()->set_requires_extra_frame();

        ImGuiPureWrap::end();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

static float get_cursor_height()
{
    float ret = 16.0f;
#ifdef _WIN32
    // see: https://forums.codeguru.com/showthread.php?449040-get-the-system-current-cursor-size
    // this code is not perfect because it returns a maximum height equal to 31 even if the cursor bitmap shown on screen is bigger
    // but at least it gives the same result as wxWidgets in the settings tabs
    ICONINFO ii;
    if (::GetIconInfo((HICON) GetCursor(), &ii) != 0)
    {
        BITMAP bitmap;
        ::GetObject(ii.hbmMask, sizeof(BITMAP), &bitmap);
        const int width = bitmap.bmWidth;
        const int height = (ii.hbmColor == nullptr) ? bitmap.bmHeight / 2 : bitmap.bmHeight;
        HDC dc = ::CreateCompatibleDC(nullptr);
        if (dc != nullptr)
        {
            if (::SelectObject(dc, ii.hbmMask) != nullptr)
            {
                for (int i = 0; i < width; ++i)
                {
                    for (int j = 0; j < height; ++j)
                    {
                        if (::GetPixel(dc, i, j) != RGB(255, 255, 255))
                        {
                            if (ret < float(j))
                                ret = float(j);
                        }
                    }
                }
                ::DeleteDC(dc);
            }
        }
        ::DeleteObject(ii.hbmColor);
        ::DeleteObject(ii.hbmMask);
    }
#endif //  _WIN32
    return ret;
}

void GLCanvas3D::Tooltip::set_text(const std::string &text)
{
    // If the mouse is inside an ImGUI dialog, then the tooltip is suppressed.
    const std::string &new_text = m_in_imgui ? std::string() : text;
    if (m_text != new_text)
    { // To avoid calling the expensive call to get_cursor_height.
        m_text = new_text;
        m_cursor_height = get_cursor_height();
    }
}

void GLCanvas3D::Tooltip::render(const Vec2d &mouse_position, GLCanvas3D &canvas)
{
    static ImVec2 size(0.0f, 0.0f);

    auto validate_position = [this](const Vec2d &position, const GLCanvas3D &canvas, const ImVec2 &wnd_size)
    {
        const Size cnv_size = canvas.get_canvas_size();
        const float x = std::clamp(float(position.x()), 0.0f, float(cnv_size.get_width()) - wnd_size.x);
        const float y = std::clamp(float(position.y()) + m_cursor_height, 0.0f,
                                   float(cnv_size.get_height()) - wnd_size.y);
        return Vec2f(x, y);
    };

    if (m_text.empty())
    {
        m_start_time = std::chrono::steady_clock::now();
        return;
    }

    // draw the tooltip as hidden until the delay is expired
    // use a value of alpha slightly different from 0.0f because newer imgui does not calculate properly the window size if alpha == 0.0f
    const float alpha = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                               m_start_time)
                             .count() < 500)
                            ? 0.01f
                            : 1.0f;

    const Vec2f position = validate_position(mouse_position, canvas, size);

    ImGuiWrapper &imgui = *canvas.get_imgui();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGuiPureWrap::set_next_window_pos(position.x(), position.y(), ImGuiCond_Always, 0.0f, 0.0f);

    ImGuiPureWrap::begin("canvas_tooltip", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMouseInputs |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                                               ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::TextUnformatted(m_text.c_str());

    // force re-render while the windows gets to its final size (it may take several frames) or while hidden
    if (alpha < 1.0f || ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x !=
                            ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
        imgui.set_requires_extra_frame();

    size = ImGui::GetWindowSize();

    ImGuiPureWrap::end();
    ImGui::PopStyleVar(2);
}

wxDEFINE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_FORCE_INVALIDATE_SLICE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, RBtnEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE_CURRENT_BED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESET_SKEW, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MIRRORED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_FORCE_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_WIPETOWER_TOUCHED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESETGIZMOS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SLIDERS_MANIPULATION, wxKeyEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UNDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, Event<float>);
wxDEFINE_EVENT(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, HeightProfileSmoothEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RELOAD_FROM_DISK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAKE_SNAPSHOT, StringEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAKE_SNAPSHOT_SELECTION, StringEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_INFO_ITEMS, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJ_LIST_SELECTION_CHANGED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_SELECTIONS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_HIDE_SLICE_BUTTON, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SHOW_SLICE_BUTTON, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TOGGLE_RENDER_STATISTIC_DIALOG, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SHOW_AUTOSLICING_ACTION_BUTTONS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SWITCH_TO_AUTOSLICING, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SWITCH_FROM_AUTOSLICING, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJECT_LIST_CHANGED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR_TOGGLE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MINIMIZE_WINDOW, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAKE_GIZMO_SNAPSHOT, StringEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MANIPULATION_DIRTY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MANIPULATION_UPDATE_AND_SHOW, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_CHANGED_OBJECT, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_SET_PLATER_DIRTY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_PLATER_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAB_UPDATE_DIRTY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJ_LIST_UPDATE_AFTER_UNDO_REDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJ_LIST_ADD_OBJECT, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJ_LIST_UPDATE_ITEM_ERROR_ICON, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_SCALE_SELECTION_TO_FIT_PRINT_VOLUME, SimpleEvent);

const double GLCanvas3D::DefaultCameraZoomToBoxMarginFactor = 1.25;

static std::unique_ptr<ITimer> s_virtual_bed_timer;
static GLCanvas3D *s_vbt_owner = nullptr;

static bool composite_id_match(const GLVolume::CompositeID &id1, const GLVolume::CompositeID &id2)
{
    return id1.object_id == id2.object_id && id1.instance_id == id2.instance_id;
}

static bool object_contains_negative_volumes(const Model &model, int obj_id)
{
    return (0 <= obj_id && obj_id < (int) model.objects.size()) ? model.objects[obj_id]->has_negative_volume_mesh()
                                                                : false;
}

void GLCanvas3D::SLAView::detect_type_from_volumes(const GLVolumePtrs &volumes) {}

void GLCanvas3D::SLAView::set_type(ESLAViewType new_type) {}

void GLCanvas3D::SLAView::set_type(const GLVolume::CompositeID &id, ESLAViewType new_type) {}

void GLCanvas3D::SLAView::update_volumes_visibility(GLVolumePtrs &volumes)
{
    for (GLVolume *v : volumes)
    {
        v->is_active = true;
    }
}

void GLCanvas3D::SLAView::update_instances_cache(
    const std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>> &new_to_old_ids_map)
{
}

void GLCanvas3D::SLAView::render_switch_button() {}

GLCanvas3D::SLAView::InstancesCacheItem *GLCanvas3D::SLAView::find_instance_item(const GLVolume::CompositeID &id)
{
    return nullptr;
}

void GLCanvas3D::SLAView::select_full_instance(const GLVolume::CompositeID &id) {}

PrinterTechnology GLCanvas3D::current_printer_technology() const
{
    return m_process ? m_process->current_printer_technology() : ptFFF;
}

bool GLCanvas3D::is_arrange_alignment_enabled() const
{
    if (m_config == nullptr)
    {
        return false;
    }
    if (!is_XL_printer(*m_config))
    {
        return false;
    }
    if (this->m_wipe_tower_bounding_boxes[s_multiple_beds.get_active_bed()])
    {
        return false;
    }
    return true;
}

GLCanvas3D::GLCanvas3D(wxGLCanvas *canvas, Bed3D &bed, AppConfig *app_config, ImGuiWrapper *imgui)
    : m_canvas(canvas)
    , m_bed(bed)
#if ENABLE_RETINA_GL
    , m_retina_helper(nullptr)
#endif // ENABLE_RETINA_GL
    , m_in_render(false)
    , m_csg_preview(std::make_unique<CSGPreviewManager>())
    , m_main_toolbar(GLToolbar::Normal, "Main")
    , m_undoredo_toolbar(GLToolbar::Normal, "Undo_Redo")
    , m_gizmos(*this)
    , m_use_clipping_planes(false)
    , m_sidebar_field("")
    , m_extra_frame_requested(false)
    , m_config(nullptr)
    , m_process(nullptr)
    , m_model(nullptr)
    , m_dirty(true)
    , m_initialized(false)
    , m_apply_zoom_to_volumes_filter(false)
    , m_picking_enabled(false)
    , m_moving_enabled(false)
    , m_dynamic_background_enabled(false)
    , m_multisample_allowed(false)
    , m_moving(false)
    , m_tab_down(false)
    , m_cursor_type(CursorType::Standard)
    , m_reload_delayed(false)
    , m_render_sla_auxiliaries(true)
    , m_labels(*this)
    , m_slope(m_volumes)
    , m_sla_view(*this)
    , m_arrange_settings_db{app_config}
    , m_arrange_settings_dialog{imgui, &m_arrange_settings_db}
{
    if (m_canvas != nullptr)
    {
        m_timer = std::make_unique<Timer_wx>();
        m_timer->setCallback([this]() { on_timer_internal(); });
        m_render_timer = std::make_unique<Timer_wx>();
        m_render_timer->setCallback([this]() { on_render_timer_internal(); });
#if ENABLE_RETINA_GL
        m_retina_helper.reset(new RetinaHelper(canvas));
#endif // ENABLE_RETINA_GL
    }

    m_selection.set_volumes(&m_volumes.volumes);
    m_arrange_settings_dialog.show_xl_align_combo([this]() { return this->is_arrange_alignment_enabled(); });
    m_arrange_settings_dialog.on_arrange_btn([this] { m_event_poster->postEvent(CanvasEventType::Arrange); });
    m_arrange_settings_dialog.on_arrange_bed_btn([this]
                                                 { m_event_poster->postEvent(CanvasEventType::ArrangeCurrentBed); });
}

GLCanvas3D::~GLCanvas3D()
{
    if (s_vbt_owner == this)
    {
        s_virtual_bed_timer->stop();
        s_virtual_bed_timer.reset();
        s_vbt_owner = nullptr;
    }
    reset_volumes();
}

wxWindow *GLCanvas3D::get_wxglcanvas_parent()
{
    return m_canvas->GetParent();
}

void GLCanvas3D::set_shader_getters(ShaderGetterFn get_shader, CurrentShaderGetterFn get_current_shader)
{
    m_get_shader = get_shader;
    m_get_current_shader = get_current_shader;
    m_selection.set_shader_getter(get_shader);
    m_gcode_viewer.set_shader_getters(get_shader, get_current_shader);
    m_layers_editing.set_shader_getter(get_shader);
}

static bool should_use_phong(const AppConfig *config)
{
    if (!config)
        return true;
    return OpenGLManager::get_gl_info().should_use_phong(config->get("canvas_lighting_quality"));
}

GLShaderProgram *GLCanvas3D::get_model_shader() const
{
    if (should_use_phong(m_app_config))
    {
        GLShaderProgram *shader = get_shader("phong");
        if (shader)
            return shader;
    }
    return get_shader("gouraud");
}

GLShaderProgram *GLCanvas3D::get_utility_shader() const
{
    if (should_use_phong(m_app_config))
    {
        GLShaderProgram *shader = get_shader("phong_light");
        if (shader)
            return shader;
    }
    return get_shader("gouraud_light");
}

std::vector<std::unique_ptr<Print>> &GLCanvas3D::get_fff_prints()
{
    static std::vector<std::unique_ptr<Print>> empty;
    return m_get_fff_prints ? m_get_fff_prints() : empty;
}

const BuildVolume &GLCanvas3D::build_volume() const
{
    return m_get_build_volume ? m_get_build_volume() : m_bed.build_volume();
}

void GLCanvas3D::take_gizmo_snapshot(const std::string &name)
{
    take_typed_snapshot(name, static_cast<int>(UndoRedo::SnapshotType::GizmoAction));
}

GLRenderContext GLCanvas3D::build_render_context() const
{
    GLRenderContext ctx;
    ctx.get_shader = m_get_shader;
    ctx.get_current_shader = m_get_current_shader;
    ctx.camera = m_camera;
    ctx.model = m_model;
    ctx.app_config = m_app_config;
    ctx.imgui = m_imgui;
    ctx.extruders_edited_cnt = extruders_edited_cnt();
    ctx.extruder_colors = m_get_extruder_colors ? m_get_extruder_colors() : std::vector<ColorRGBA>{};
    return ctx;
}

void GLCanvas3D::set_app_state(std::function<bool()> is_editor, std::function<bool()> is_gcode_viewer,
                               std::function<int()> em_unit, std::function<bool()> dark_mode,
                               std::function<int()> get_mode, std::function<int()> extruders_edited_cnt,
                               std::function<int()> printer_technology)
{
    m_is_editor = std::move(is_editor);
    m_is_gcode_viewer = std::move(is_gcode_viewer);
    m_em_unit = std::move(em_unit);
    m_dark_mode = std::move(dark_mode);
    m_get_mode = std::move(get_mode);
    m_extruders_edited_cnt = std::move(extruders_edited_cnt);
    m_printer_technology = printer_technology;
    m_selection.set_printer_technology_getter(printer_technology);
    m_gcode_viewer.set_app_state(m_is_editor, m_is_gcode_viewer, m_em_unit);
}

bool GLCanvas3D::init()
{
    if (m_initialized)
        return true;

    if (m_canvas == nullptr || m_surface == nullptr)
        return false;

    glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
#if SLIC3R_OPENGL_ES
    glsafe(::glClearDepthf(1.0f));
#else
    glsafe(::glClearDepth(1.0f));
#endif // SLIC3R_OPENGL_ES

    glsafe(::glDepthFunc(GL_LESS));

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    if (m_multisample_allowed)
        glsafe(::glEnable(GL_MULTISAMPLE));

    if (m_main_toolbar.is_enabled())
        m_layers_editing.init();

    if (m_gizmos.is_enabled() && !m_gizmos.init())
        std::cout << "Unable to initialize gizmos: please, check that all the required textures are available"
                  << std::endl;

    if (!_init_toolbars())
        return false;

    if (m_selection.is_enabled() && !m_selection.init())
        return false;

    m_initialized = true;

    return true;
}

void GLCanvas3D::reset_volumes()
{
    if (!m_initialized)
        return;

    if (m_volumes.empty())
        return;

    _set_current();

    m_selection.clear();
    m_volumes.clear();
    m_dirty = true;

    _set_warning_notification(EWarning::ObjectOutside, false);
}

ModelInstanceEPrintVolumeState GLCanvas3D::check_volumes_outside_state(bool selection_only) const
{
    ModelInstanceEPrintVolumeState state = ModelInstanceEPrintVolumeState::ModelInstancePVS_Inside;
    if (m_initialized && !m_volumes.empty())
        check_volumes_outside_state(const_cast<GLVolumeCollection &>(m_volumes), &state, selection_only);
    return state;
}

bool GLCanvas3D::check_volumes_outside_state(GLVolumeCollection &volumes, ModelInstanceEPrintVolumeState *out_state,
                                             bool selection_only) const
{
    auto volume_below = [](GLVolume &volume) -> bool
    {
        return volume.object_idx() != -1 && volume.volume_idx() != -1 && volume.is_below_printbed();
    };
    // Volume is partially below the print bed, thus a pre-calculated convex hull cannot be used.
    auto volume_sinking = [](GLVolume &volume) -> bool
    {
        return volume.object_idx() != -1 && volume.volume_idx() != -1 && volume.is_sinking();
    };
    // Cached bounding box of a volume above the print bed.
    auto volume_bbox = [volume_sinking](GLVolume &volume) -> BoundingBoxf3
    {
        return volume_sinking(volume) ? volume.transformed_non_sinking_bounding_box()
                                      : volume.transformed_convex_hull_bounding_box();
    };
    // Cached 3D convex hull of a volume above the print bed.
    static const TriangleMesh s_empty_mesh;
    auto volume_convex_mesh = [this, volume_sinking](GLVolume &volume) -> const TriangleMesh &
    {
        if (volume_sinking(volume))
        {
            if (m_model)
            {
                const int oidx = volume.object_idx();
                const int vidx = volume.volume_idx();
                if (oidx >= 0 && oidx < int(m_model->objects.size()))
                {
                    const ModelObject *obj = m_model->objects[oidx];
                    if (vidx >= 0 && vidx < int(obj->volumes.size()))
                        return obj->volumes[vidx]->mesh();
                }
            }
            return s_empty_mesh;
        }
        return *volume.convex_hull();
    };

    auto volumes_to_process_idxs = [this, &volumes, selection_only]()
    {
        std::vector<unsigned int> ret;
        if (!selection_only || m_selection.is_empty())
        {
            ret = std::vector<unsigned int>(volumes.volumes.size());
            std::iota(ret.begin(), ret.end(), 0);
        }
        else
        {
            const GUI::Selection::IndicesList &selected_volume_idxs = m_selection.get_volume_idxs();
            ret.assign(selected_volume_idxs.begin(), selected_volume_idxs.end());
        }
        return ret;
    };

    ModelInstanceEPrintVolumeState overall_state = ModelInstancePVS_Inside;
    bool contained_min_one = false;

    const Slic3r::BuildVolume &build_volume = m_bed.build_volume();

    const std::vector<unsigned int> volumes_idxs = volumes_to_process_idxs();
    for (unsigned int vol_idx : volumes_idxs)
    {
        GLVolume *volume = volumes.volumes[vol_idx];
        if (!volume->is_modifier && (volume->shader_outside_printer_detection_enabled ||
                                     (!volume->is_wipe_tower() && volume->composite_id.volume_id >= 0)))
        {
            BuildVolume::ObjectState state;
            int bed_idx = -1;
            if (volume_below(*volume))
                state = BuildVolume::ObjectState::Below;
            else
            {
                switch (build_volume.type())
                {
                case BuildVolume::Type::Rectangle:
                    //FIXME this test does not evaluate collision of a build volume bounding box with non-convex objects.
                    state = build_volume.volume_state_bbox(volume_bbox(*volume), true, &bed_idx);
                    break;
                case BuildVolume::Type::Circle:
                case BuildVolume::Type::Convex:
                //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
                case BuildVolume::Type::Custom:
                    state = build_volume.object_state(volume_convex_mesh(*volume).its,
                                                      volume->world_matrix().cast<float>(), volume_sinking(*volume),
                                                      true, &bed_idx);
                    break;
                default:
                    // Ignore, don't produce any collision.
                    state = BuildVolume::ObjectState::Inside;
                    break;
                }
                assert(state != BuildVolume::ObjectState::Below);
            }
            volume->is_outside = state != BuildVolume::ObjectState::Inside;
            if (volume->printable)
            {
                if (overall_state == ModelInstancePVS_Inside && volume->is_outside)
                    overall_state = ModelInstancePVS_Fully_Outside;
                if (overall_state == ModelInstancePVS_Fully_Outside && volume->is_outside &&
                    state == BuildVolume::ObjectState::Colliding)
                {
                    overall_state = ModelInstancePVS_Partly_Outside;
                }
                contained_min_one |= !volume->is_outside;

                if (bed_idx != -1 && bed_idx == s_multiple_beds.get_number_of_beds())
                    s_multiple_beds.request_next_bed(true);
            }
        }
        else if (volume->is_modifier)
            volume->is_outside = false;
    }

    for (unsigned int vol_idx = 0; vol_idx < volumes.volumes.size(); ++vol_idx)
    {
        if (std::find(volumes_idxs.begin(), volumes_idxs.end(), vol_idx) == volumes_idxs.end())
        {
            if (!volumes.volumes[vol_idx]->is_outside)
            {
                contained_min_one = true;
                break;
            }
        }
    }

    if (out_state != nullptr)
        *out_state = overall_state;

    return contained_min_one;
}

void GLCanvas3D::toggle_sla_auxiliaries_visibility(bool visible, const ModelObject *mo, int instance_idx) {}

void GLCanvas3D::toggle_model_objects_visibility(bool visible, const ModelObject *mo, int instance_idx,
                                                 const ModelVolume *mv)
{
    std::vector<std::shared_ptr<SceneRaycasterItem>> *raycasters = get_raycasters_for_picking(
        SceneRaycaster::EType::Volume);
    for (GLVolume *vol : m_volumes.volumes)
    {
        if (vol->is_wipe_tower())
            vol->is_active = (visible && mo == nullptr);
        else
        {
            if ((mo == nullptr || m_model->objects[vol->composite_id.object_id] == mo) &&
                (instance_idx == -1 || vol->composite_id.instance_id == instance_idx) &&
                (mv == nullptr ||
                 m_model->objects[vol->composite_id.object_id]->volumes[vol->composite_id.volume_id] == mv))
            {
                vol->is_active = visible;
                if (!vol->is_modifier)
                    vol->color.a(1.f);

                if (instance_idx == -1)
                {
                    vol->force_native_color = false;
                    vol->force_neutral_color = false;
                }
                else
                {
                    const GLGizmosManager &gm = get_gizmos_manager();
                    auto gizmo_type = gm.get_current_type();
                    if ((gizmo_type == GLGizmosManager::FdmSupports || gizmo_type == GLGizmosManager::Seam ||
                         gizmo_type == GLGizmosManager::Cut || gizmo_type == GLGizmosManager::FuzzySkin) &&
                        !vol->is_modifier)
                    {
                        vol->force_neutral_color = true;
                    }
                    else if (gizmo_type == GLGizmosManager::ColorMixing)
                        vol->is_active = false;
                    else
                        vol->force_native_color = true;
                }
            }
        }

        auto it = std::find_if(raycasters->begin(), raycasters->end(), [vol](std::shared_ptr<SceneRaycasterItem> item)
                               { return item->get_raycaster() == vol->mesh_raycaster.get(); });
        if (it != raycasters->end())
            (*it)->set_active(vol->is_active);
    }

    if (!mo && !visible && !m_model->objects.empty() &&
        (m_model->objects.size() > 1 || m_model->objects.front()->instances.size() > 1))
        _set_warning_notification(EWarning::SomethingNotShown, true);

    if (!mo && visible)
        _set_warning_notification(EWarning::SomethingNotShown, false);
}

void GLCanvas3D::update_instance_printable_state_for_object(const size_t obj_idx)
{
    ModelObject *model_object = m_model->objects[obj_idx];
    for (int inst_idx = 0; inst_idx < (int) model_object->instances.size(); ++inst_idx)
    {
        ModelInstance *instance = model_object->instances[inst_idx];

        for (GLVolume *volume : m_volumes.volumes)
        {
            if (volume->object_idx() == (int) obj_idx && volume->instance_idx() == inst_idx)
                volume->printable = instance->printable;
        }
    }
}

void GLCanvas3D::update_instance_printable_state_for_objects(const std::vector<size_t> &object_idxs)
{
    for (size_t obj_idx : object_idxs)
        update_instance_printable_state_for_object(obj_idx);
}

void GLCanvas3D::set_config(const DynamicPrintConfig *config)
{
    m_config = config;
    m_layers_editing.set_config(config);

    const PrinterTechnology ptech = current_printer_technology();
    if (const Print *print = fff_print(); ptech == ptFFF && print != nullptr)
        m_layers_editing.set_shrinkage_compensation(fff_print()->shrinkage_compensation());

    if (config)
    {
        auto slot = ArrangeSettingsDb_AppCfg::slotFFF;

        auto co_opt = config->option<ConfigOptionBool>("complete_objects");
        if (co_opt && co_opt->value)
            slot = ArrangeSettingsDb_AppCfg::slotFFFSeqPrint;
        else
            slot = ArrangeSettingsDb_AppCfg::slotFFF;

        m_arrange_settings_db.set_active_slot(slot);

        double objdst = min_object_distance(*config);
        double min_obj_dst = slot == ArrangeSettingsDb_AppCfg::slotFFFSeqPrint ? objdst : 0.;
        m_arrange_settings_db.set_distance_from_obj_range(slot, min_obj_dst, 100.);

        if (std::abs(m_arrange_settings_db.get_defaults(slot).d_obj - objdst) > EPSILON)
        {
            m_arrange_settings_db.get_defaults(slot).d_obj = objdst;

            // Defaults have changed, so let's sync with the app config and fill
            // in the missing values with the new defaults.
            m_arrange_settings_db.sync();
        }
    }
}

void GLCanvas3D::set_model(Model *model)
{
    m_model = model;
    m_selection.set_model(m_model);
}

void GLCanvas3D::bed_shape_changed()
{
    refresh_camera_scene_box();
    get_camera().requires_zoom_to_bed = true;
    m_dirty = true;
}

void GLCanvas3D::refresh_camera_scene_box()
{
    get_camera().set_scene_box(scene_bounding_box());
}

BoundingBoxf3 GLCanvas3D::volumes_bounding_box() const
{
    BoundingBoxf3 bb;
    for (const GLVolume *volume : m_volumes.volumes)
    {
        if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes))
            bb.merge(volume->transformed_bounding_box());
    }
    return bb;
}

BoundingBoxf3 GLCanvas3D::scene_bounding_box() const
{
    BoundingBoxf3 bb = volumes_bounding_box();
    bb.merge(m_bed.extended_bounding_box());
    double h = m_bed.build_volume().max_print_height();
    //FIXME why -h?
    bb.min.z() = std::min(bb.min.z(), -h);
    bb.max.z() = std::max(bb.max.z(), h);
    return bb;
}

void GLCanvas3D::reset_layer_height_profile()
{
    m_event_poster->postEvent(CanvasEventType::TakeSnapshot, _u8L("Variable layer height - Reset"));
    m_layers_editing.reset_layer_height_profile(*this);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::adaptive_layer_height_profile(float quality_factor)
{
    m_event_poster->postEvent(CanvasEventType::TakeSnapshot, _u8L("Variable layer height - Adaptive"));
    m_layers_editing.adaptive_layer_height_profile(*this, quality_factor);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::smooth_layer_height_profile(const HeightProfileSmoothingParams &smoothing_params)
{
    m_event_poster->postEvent(CanvasEventType::TakeSnapshot, _u8L("Variable layer height - Smooth all"));
    m_layers_editing.smooth_layer_height_profile(*this, smoothing_params);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::enable_layers_editing(bool enable)
{
    m_layers_editing.set_enabled(enable);
    set_as_dirty();
}

void GLCanvas3D::zoom_to_bed()
{
    BoundingBoxf3 box = m_bed.build_volume().bounding_volume();
    box.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
    box.min.z() = 0.0;
    box.max.z() = 0.0;

    _zoom_to_box(box);
}

void GLCanvas3D::zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_box(volumes_bounding_box());
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::zoom_to_selection()
{
    if (!m_selection.is_empty())
        _zoom_to_box(m_selection.get_bounding_box());
}

void GLCanvas3D::zoom_to_gcode()
{
    _zoom_to_box(m_gcode_viewer.get_paths_bounding_box(), 1.05);
}

void GLCanvas3D::select_view(const std::string &direction)
{
    get_camera().select_view(direction);
    // Recenter on the bed. Side views need Z height for a non-degenerate bounding box;
    // iso/top/bottom use the flat bed box (matches startup view).
    const bool is_side_view = (direction == "front" || direction == "rear" || direction == "left" ||
                               direction == "right");
    BoundingBoxf3 box = m_bed.build_volume().bounding_volume();
    box.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
    box.min.z() = 0.0;
    box.max.z() = is_side_view ? std::max(box.size().x(), box.size().y()) * 0.5 : 0.0;
    _zoom_to_box(box);
    if (m_surface != nullptr)
        m_surface->refresh();
}

void GLCanvas3D::update_volumes_colors_by_extruder()
{
    if (m_config != nullptr)
        m_volumes.update_colors_by_extruder(m_config);
}

using PerBedStatistics =
    std::vector<std::pair<std::size_t, std::optional<std::reference_wrapper<const PrintStatistics>>>>;

PerBedStatistics get_statistics(std::vector<std::unique_ptr<Print>> &prints)
{
    PerBedStatistics result;
    for (int bed_index = 0; bed_index < s_multiple_beds.get_number_of_beds(); ++bed_index)
    {
        const Print *print = prints[bed_index].get();
        if (print->empty() || !print->finished())
        {
            result.emplace_back(bed_index, std::nullopt);
        }
        else
        {
            result.emplace_back(bed_index, std::optional{std::ref(print->print_statistics())});
        }
    }
    return result;
}

struct StatisticsSum
{
    float cost{};
    float filement_weight{};
    float filament_length{};
    float normal_print_time{};
    float silent_print_time{};
};

StatisticsSum get_statistics_sum(std::vector<std::unique_ptr<Print>> &prints)
{
    StatisticsSum result;
    for (const auto &[_, statistics] : get_statistics(prints))
    {
        if (!statistics)
        {
            continue;
        }
        result.cost += statistics->get().total_cost;
        result.filement_weight += statistics->get().total_weight;
        result.filament_length += statistics->get().total_used_filament;
        result.normal_print_time += statistics->get().normal_print_time_seconds;
        result.silent_print_time += statistics->get().silent_print_time_seconds;
    }

    return result;
}

// retur width of table
float project_overview_table(float scale, std::vector<std::unique_ptr<Print>> &prints)
{
    const float width_gap = 10.f * scale;
    float total_width{width_gap};

    ImGui::Text("%s", _u8L("Project overview").c_str());
    if (ImGui::BeginTable("project_overview_table", 6))
    {
        float width = std::max<float>(ImGui::CalcTextSize(format(_u8L("Bed %1%"), 1).c_str()).x,
                                      ImGui::CalcTextSize(_u8L("Total").c_str()).x) +
                      width_gap;
        total_width += width;
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, width);

        std::string name = _u8L("Cost");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);

        name = _u8L("Filament (g)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);

        name = _u8L("Filament (m)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);

        // TRN %1% is one "Stealth mode" or "Normal mode"
        name = format(_u8L("Estimated Time (%1%)"), _u8L("Stealth mode"));
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);

        name = format(_u8L("Estimated Time (%1%)"), _u8L("Normal mode"));
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);
        ImGui::TableHeadersRow();

        for (const auto &[bed_index, optional_statistics] : get_statistics(prints))
        {
            if (optional_statistics)
            {
                const std::reference_wrapper<const PrintStatistics> statistics{*optional_statistics};
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // TRN %1% is a number of the Bed
                ImGui::Text("%s", format(_u8L("Bed %1%"), bed_index + 1).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_cost);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_weight);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_used_filament / 1000);
                ImGui::TableNextColumn();
                ImGui::Text("%s", statistics.get().estimated_silent_print_time.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", statistics.get().estimated_normal_print_time.c_str());
            }
            else
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", format(_u8L("Bed %1%"), bed_index + 1).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiPureWrap::COL_ORANGE_LIGHT);

        const StatisticsSum statistics_sum{get_statistics_sum(prints)};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", _u8L("Total").c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.cost);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.filement_weight);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.filament_length / 1000);
        ImGui::TableNextColumn();
        ImGui::Text("%s", get_time_dhms(statistics_sum.silent_print_time).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%s", get_time_dhms(statistics_sum.normal_print_time).c_str());

        ImGui::PopStyleColor();

        ImGui::EndTable();
    }

    return total_width + 2.f * width_gap;
}

struct ExtruderStatistics
{
    float filament_weight{};
    float filament_length{};
};

using PerExtruderStatistics = std::map<std::size_t, ExtruderStatistics>;

PerExtruderStatistics get_extruder_statistics(std::vector<std::unique_ptr<Print>> &prints,
                                              const std::vector<ExtruderFilaments> &extruders_filaments)
{
    PerExtruderStatistics result;
    for (int bed_index = 0; bed_index < s_multiple_beds.get_number_of_beds(); ++bed_index)
    {
        const Print *print = prints[bed_index].get();
        if (print->empty() || !print->finished())
        {
            continue;
        }
        print->print_statistics();
        for (const auto &[filament_id, filament_volume] : print->print_statistics().filament_stats)
        {
            const Preset *preset = extruders_filaments[filament_id].get_selected_preset();
            if (preset == nullptr)
            {
                continue;
            }

            const double filament_density = preset->config.opt_float("filament_density", 0);
            const double diameter = preset->config.opt_float("filament_diameter", filament_id);
            result[filament_id].filament_weight += filament_volume * filament_density / 1000.0f;
            result[filament_id].filament_length += filament_volume / (M_PI * diameter * diameter / 4.0) / 1000.0;
        }
    }
    return result;
}

ExtruderStatistics sum_extruder_statistics(const PerExtruderStatistics &per_extruder_statistics)
{
    ExtruderStatistics result;
    for (const auto &[_, statistics] : per_extruder_statistics)
    {
        result.filament_weight += statistics.filament_weight;
        result.filament_length += statistics.filament_length;
    }

    return result;
}

void extruder_usage_table(const PerExtruderStatistics &extruder_statistics, const float scale)
{
    ImGui::Text("%s", _u8L("Extruders usage breakdown").c_str());
    if (ImGui::BeginTable("extruder_usage_table", 3))
    {
        const float width_gap = 10.f * scale;
        float width = width_gap + std::max<float>(ImGui::CalcTextSize(format(_u8L("Extruder %1%"), 1).c_str()).x,
                                                  ImGui::CalcTextSize(_u8L("Total").c_str()).x);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, width);

        std::string name = _u8L("Filament (g)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);

        name = _u8L("Filament (m)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, width);
        ImGui::TableHeadersRow();

        for (const auto &[extruder_index, statistics] : extruder_statistics)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", format(_u8L("Extruder %1%"), extruder_index + 1).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", statistics.filament_weight);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", statistics.filament_length);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiPureWrap::COL_ORANGE_LIGHT);

        const ExtruderStatistics extruder_statistics_sum{sum_extruder_statistics(extruder_statistics)};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", _u8L("Total").c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", extruder_statistics_sum.filament_weight);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", extruder_statistics_sum.filament_length);

        ImGui::PopStyleColor();

        ImGui::EndTable();
    }
}

void begin_statistics(const char *window_name)
{
    ImGuiWindowFlags windows_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_HorizontalScrollbar;

    const ImVec2 center{ImGui::GetMainViewport()->GetCenter()};
    const float y_postion{std::max(0.5f * center.y, 150.0f)};
    const ImVec2 position{center.x, y_postion};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{0.5f, 0.f});

    ImGui::Begin(window_name, nullptr, windows_flags);
}

static float content_size_x = 0.0f;
void render_print_statistics(float scale, std::vector<std::unique_ptr<Print>> &prints,
                             const std::vector<ExtruderFilaments> &extruders_filaments)
{
    ImGui::SetNextWindowContentSize(ImVec2(content_size_x, 0.0f));

    begin_statistics(_u8L("Statistics").c_str());
    ImGui::Spacing();
    content_size_x = project_overview_table(scale, prints);
    ImGui::Separator();

    const PerExtruderStatistics extruder_statistics{get_extruder_statistics(prints, extruders_filaments)};
    if (extruder_statistics.size() > 1)
    {
        ImGui::NewLine();
        extruder_usage_table(extruder_statistics, scale);
        ImGui::Separator();
    }
    ImGui::End();
}

void render_autoslicing_wait(ImGuiWrapper *imgui)
{
    const float scale = imgui->get_style_scaling();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f * scale, 30.f * scale));
    begin_statistics((_u8L("Generating statistics") + " ...").c_str());
    ImGui::Text("%s", _u8L("Statistics will be available once all beds are sliced").c_str());
    ImGui::PopStyleVar();
    ImGui::End();
}

void GLCanvas3D::render()
{
    if (m_in_render)
    {
        // if called recursively, return
        m_dirty = true;
        return;
    }

    m_in_render = true;
    Slic3r::ScopeGuard in_render_guard([this]() { m_in_render = false; });
    (void) in_render_guard;

    if (m_surface == nullptr)
        return;

    // ensures this canvas is current and initialized
    if (!_is_shown_on_screen() || !_set_current() || (m_init_opengl && !m_init_opengl()))
        return;

    if (!is_initialized() && !init())
        return;

    wxGetApp().compile_pending_shaders();

    if (!m_main_toolbar.is_enabled())
        m_gcode_viewer.init();

    if (!m_bed.build_volume().valid())
    {
        // this happens at startup when no data is still saved under <>\AppData\Roaming\Slic3rPE
        m_event_poster->postEvent(CanvasEventType::UpdateBedShape);
        return;
    }

#if ENABLE_ENVIRONMENT_MAP
    if (is_editor())
        if (m_init_environment_texture)
            m_init_environment_texture();
#endif // ENABLE_ENVIRONMENT_MAP

#if ENABLE_GLMODEL_STATISTICS
    GLModel::reset_statistics_counters();
#endif // ENABLE_GLMODEL_STATISTICS

    // Check for completed boolean preview computations (only when Align gizmo is active).
    if (m_csg_preview && m_gizmos.get_current_type() == GLGizmosManager::EType::Align &&
        m_csg_preview->process_completed())
    {
        reload_scene(true);
        if (wxIsBusy())
            wxEndBusyCursor();
    }

    const Size &cnv_size = get_canvas_size();
    // Probably due to different order of events on Linux/GTK2, when one switched from 3D scene
    // to preview, this was called before canvas had its final size. It reported zero width
    // and the viewport was set incorrectly, leading to tripping glAsserts further down
    // the road (in apply_projection). That's why the minimum size is forced to 10.
    Camera &camera = get_camera();
    camera.set_viewport(0, 0, std::max(10u, (unsigned int) cnv_size.get_width()),
                        std::max(10u, (unsigned int) cnv_size.get_height()));
    camera.apply_viewport();

    // Update ImGui display size and font scaling whenever canvas size changes.
    // _resize() has an internal guard (m_old_size == new_size) so this is a no-op
    // on frames where the size hasn't changed. This is needed because DPI changes
    // (e.g. dragging across monitors) resize the canvas but _resize() was previously
    // only called from zoom-to-bed, leaving ImGui with stale display size and fonts.
    _resize((unsigned int) cnv_size.get_width(), (unsigned int) cnv_size.get_height());

    if (camera.requires_zoom_to_bed)
    {
        zoom_to_bed();
        camera.requires_zoom_to_bed = false;
    }

    camera.apply_projection(_max_bounding_box(true));

    const int curr_active_bed_id = s_multiple_beds.get_active_bed();
    if (m_last_active_bed_id != curr_active_bed_id)
    {
        const Vec3d bed_offset = s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed());
        const Vec2d bed_center = m_bed.build_volume().bed_center() + Vec2d(bed_offset.x(), bed_offset.y());
        m_last_active_bed_id = curr_active_bed_id;
    }

    get_imgui()->new_frame();

    if (m_picking_enabled)
    {
        if (m_rectangle_selection.is_dragging() && !m_rectangle_selection.is_empty())
            // picking pass using rectangle selection
            _rectangular_selection_picking_pass();
        else if (!m_volumes.empty())
            // regular picking pass
            _picking_pass();
#if ENABLE_RAYCAST_PICKING_DEBUG
        else
        {
            ImGuiWrapper &imgui = *get_imgui();
            ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
            ImGuiPureWrap::text("Picking disabled");
            ImGuiPureWrap::end();
        }
#endif // ENABLE_RAYCAST_PICKING_DEBUG
    }

#ifdef SHOW_IMGUI_DEMO_WINDOW
    if (show_imgui_demo_window)
        ImGui::ShowDemoWindow();
#endif // SHOW_IMGUI_DEMO_WINDOW

    const bool is_looking_downward = camera.is_looking_downward();

    // draw scene
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    _render_background();

    if (!s_multiple_beds.is_autoslicing())
    {
        _render_objects(GLVolumeCollection::ERenderType::Opaque);
        _render_selection();
        _render_bed_axes();
        if (is_looking_downward)
            _render_bed(camera.get_view_matrix(), camera.get_projection_matrix(), false);
        if (!m_main_toolbar.is_enabled())
            _render_gcode();
        _render_objects(GLVolumeCollection::ERenderType::Transparent);

#if ENABLE_RENDER_SELECTION_CENTER
        _render_selection_center();
#endif // ENABLE_RENDER_SELECTION_CENTER
        if (!m_main_toolbar.is_enabled())
            _render_gcode_cog();

        // we need to set the mouse's scene position here because the depth buffer
        // could be invalidated by the following gizmo render methods
        // this position is used later into on_mouse() to drag the objects
        if (m_picking_enabled)
            m_mouse.scene_position = _mouse_to_3d(m_mouse.position.cast<coord_t>());

        // sidebar hints need to be rendered before the gizmos because the depth buffer
        // could be invalidated by the following gizmo render methods
        _render_selection_sidebar_hints();
        _render_current_gizmo();
        m_gizmos.render_always_visible_overlays();
        if (!is_looking_downward)
            _render_bed(camera.get_view_matrix(), camera.get_projection_matrix(), true);

#if ENABLE_RAYCAST_PICKING_DEBUG
        if (m_picking_enabled && !m_mouse.dragging && !m_gizmos.is_dragging() && !m_rectangle_selection.is_dragging())
            m_scene_raycaster.render_hit(camera);
#endif // ENABLE_RAYCAST_PICKING_DEBUG

#if ENABLE_SHOW_CAMERA_TARGET
        _render_camera_target();
        _render_camera_target_validation_box();
#endif // ENABLE_SHOW_CAMERA_TARGET

        if (m_picking_enabled && m_rectangle_selection.is_dragging())
            m_rectangle_selection.render(*this);
    }
    else
    {
        const auto &prints{get_fff_prints()};

        bool all_finished{true};
        for (std::size_t bed_index{}; bed_index < s_multiple_beds.get_number_of_beds(); ++bed_index)
        {
            const std::unique_ptr<Print> &print{prints[bed_index]};
            if (!print->finished() && is_sliceable(s_print_statuses[bed_index]))
            {
                all_finished = false;
                break;
            }
        }

        if (!all_finished)
        {
            render_autoslicing_wait(get_imgui());
            if (fff_print()->finished() || !is_sliceable(s_print_statuses[s_multiple_beds.get_active_bed()]))
            {
                s_multiple_beds.autoslice_next_bed();
                wxYield();
            }
            else
            {
                m_event_poster->postEvent(CanvasEventType::ScheduleBackgroundProcess);
            }
        }
        else
        {
            m_event_poster->postEvent(CanvasEventType::ShowAutoslicingActionButtons);
#if ENABLE_RETINA_GL
            const float scale = m_retina_helper->get_scale_factor();
#else
            const float scale = 0.1f * em_unit();
#endif // ENABLE_RETINA_GL
            if (m_preset_bundle && m_get_fff_prints)
                render_print_statistics(scale, get_fff_prints(), m_preset_bundle->extruders_filaments);
        }
    }

    _render_overlays();

    _render_bed_selector();

    if (is_render_statistic_dialog_visible())
    {
        ImGuiPureWrap::begin(std::string("Render statistics"), ImGuiWindowFlags_AlwaysAutoResize |
                                                                   ImGuiWindowFlags_NoResize |
                                                                   ImGuiWindowFlags_NoCollapse);
        ImGuiPureWrap::text("FPS (SwapBuffers() calls per second):");
        ImGui::SameLine();
        ImGuiPureWrap::text(std::to_string(m_render_stats.get_fps_and_reset_if_needed()));
        ImGui::Separator();
        ImGuiPureWrap::text("Compressed textures:");
        ImGui::SameLine();
        ImGuiPureWrap::text(OpenGLManager::are_compressed_textures_supported() ? "supported" : "not supported");
        ImGuiPureWrap::text("Max texture size:");
        ImGui::SameLine();
        ImGuiPureWrap::text(std::to_string(OpenGLManager::get_gl_info().get_max_tex_size()));
        ImGuiPureWrap::end();
    }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    if (is_editor() && is_view3D_shown())
        if (m_render_project_state_debug)
            m_render_project_state_debug();
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

#if ENABLE_CAMERA_STATISTICS
    camera.debug_render();
#endif // ENABLE_CAMERA_STATISTICS
#if ENABLE_GLMODEL_STATISTICS
    GLModel::render_statistics();
#endif // ENABLE_GLMODEL_STATISTICS
#if ENABLE_OBJECT_MANIPULATION_DEBUG
    if (m_render_obj_manipul_debug)
        m_render_obj_manipul_debug();
#endif // ENABLE_OBJECT_MANIPULATION_DEBUG

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
    if (is_view3D_shown() && fff_print()->config().binary_gcode)
        show_binary_gcode_debug_window();
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW

    std::string tooltip;

    // Negative coordinate means out of the window, likely because the window was deactivated.
    // In that case the tooltip should be hidden.
    if (m_mouse.position.x() >= 0. && m_mouse.position.y() >= 0.)
    {
        if (tooltip.empty())
            tooltip = m_layers_editing.get_tooltip(*this);

        if (tooltip.empty())
            tooltip = m_gizmos.get_tooltip();

        if (tooltip.empty())
            tooltip = m_main_toolbar.get_tooltip();

        if (tooltip.empty())
            tooltip = m_undoredo_toolbar.get_tooltip();

        if (tooltip.empty())
            tooltip = m_collapse_toolbar->get_tooltip();

        /*
	    if (tooltip.empty())
            tooltip = m_view_toolbar->get_tooltip();
        */
    }

    set_tooltip(tooltip);

    if (m_tooltip_enabled)
        m_tooltip.render(m_mouse.position, *this);

    m_mouse3d_controller->render_settings_dialog(*this);

    m_notification_manager->render_notifications(*this, get_overlay_window_width());
    render_notes_dialog();

    if (!s_multiple_beds.is_autoslicing())
        render_sliders();

    get_imgui()->render();

    m_surface->swapBuffers();
    m_render_stats.increment_fps_counter();
}

void GLCanvas3D::render_thumbnail(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                  const ThumbnailsParams &thumbnail_params, Camera::EType camera_type)
{
    render_thumbnail(thumbnail_data, w, h, thumbnail_params, m_volumes, camera_type);
}

void GLCanvas3D::render_thumbnail(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                  const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                                  Camera::EType camera_type)
{
    switch (OpenGLManager::get_framebuffers_type())
    {
    case OpenGLManager::EFramebufferType::Arb:
    {
        _render_thumbnail_framebuffer(thumbnail_data, w, h, thumbnail_params, volumes, camera_type);
        break;
    }
    default:
    {
        _render_thumbnail_legacy(thumbnail_data, w, h, thumbnail_params, volumes, camera_type);
        break;
    }
    }
}

void GLCanvas3D::select_all()
{
    m_selection.add_all();
    m_dirty = true;
    obj_manipul_set_dirty();
    m_gizmos.reset_all_states();
    m_gizmos.update_data();
    m_event_poster->postEvent(CanvasEventType::ObjectSelect);
}

void GLCanvas3D::deselect_all()
{
    if (m_selection.is_empty())
        return;

    // close actual opened gizmo before deselection(m_selection.remove_all()) write to undo/redo snapshot
    if (GLGizmosManager::EType current_type = m_gizmos.get_current_type(); current_type != GLGizmosManager::Undefined)
        m_gizmos.open_gizmo(current_type);

    m_selection.remove_all();
    obj_manipul_set_dirty();
    m_gizmos.reset_all_states();
    m_gizmos.update_data();
    m_event_poster->postEvent(CanvasEventType::ObjectSelect);
}

void GLCanvas3D::ensure_on_bed(unsigned int object_idx, bool allow_negative_z)
{
    if (allow_negative_z)
        return;

    typedef std::map<std::pair<int, int>, double> InstancesToZMap;
    InstancesToZMap instances_min_z;

    for (GLVolume *volume : m_volumes.volumes)
    {
        if (volume->object_idx() == (int) object_idx && !volume->is_modifier)
        {
            double min_z = volume->transformed_convex_hull_bounding_box().min.z();
            std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
            InstancesToZMap::iterator it = instances_min_z.find(instance);
            if (it == instances_min_z.end())
                it = instances_min_z.insert(InstancesToZMap::value_type(instance, DBL_MAX)).first;

            it->second = std::min(it->second, min_z);
        }
    }

    for (GLVolume *volume : m_volumes.volumes)
    {
        std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
        InstancesToZMap::iterator it = instances_min_z.find(instance);
        if (it != instances_min_z.end())
            volume->set_instance_offset(Z, volume->get_instance_offset(Z) - it->second);
    }
}

void GLCanvas3D::set_toolpaths_z_range(const std::array<unsigned int, 2> &range)
{
    if (m_gcode_viewer.has_data())
        m_gcode_viewer.set_layers_z_range(range);
}

std::vector<int> GLCanvas3D::load_object(const ModelObject &model_object, int obj_idx, std::vector<int> instance_idxs)
{
    if (instance_idxs.empty())
    {
        for (unsigned int i = 0; i < model_object.instances.size(); ++i)
        {
            instance_idxs.emplace_back(i);
        }
    }
    return m_volumes.load_object(&model_object, obj_idx, instance_idxs);
}

std::vector<int> GLCanvas3D::load_object(const Model &model, int obj_idx)
{
    if (0 <= obj_idx && obj_idx < (int) model.objects.size())
    {
        const ModelObject *model_object = model.objects[obj_idx];
        if (model_object != nullptr)
            return load_object(*model_object, obj_idx, std::vector<int>());
    }

    return std::vector<int>();
}

void GLCanvas3D::mirror_selection(Axis axis)
{
    TransformationType transformation_type;
    if (is_local_coordinates())
        transformation_type.set_local();
    else if (is_instance_coordinates())
        transformation_type.set_instance();

    transformation_type.set_relative();

    m_selection.setup_cache();
    m_selection.mirror(axis, transformation_type);

    do_mirror(L("Mirror Object"));
    obj_manipul_set_dirty();
}

// Reload the 3D scene of
// 1) Model / ModelObjects / ModelInstances / ModelVolumes
// 2) Print bed
// 3) SLA support meshes for their respective ModelObjects / ModelInstances
// 4) Wipe tower preview
// 5) Out of bed collision status & message overlay (texture)
void GLCanvas3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas == nullptr || m_config == nullptr || m_model == nullptr)
        return;

    if (!m_initialized)
        return;

    _set_current();

    // Flush MMU paint render cache so colors are rebuilt from current config
    m_volumes.flush_mm_paint_cache();

    m_hover_volume_idxs.clear();

    struct ModelVolumeState
    {
        ModelVolumeState(const GLVolume *volume)
            : model_volume(nullptr), geometry_id(volume->geometry_id), volume_idx(-1)
        {
        }
        ModelVolumeState(const ModelVolume *model_volume, const ObjectID &instance_id,
                         const GLVolume::CompositeID &composite_id)
            : model_volume(model_volume)
            , geometry_id(std::make_pair(model_volume->id().id, instance_id.id))
            , composite_id(composite_id)
            , volume_idx(-1)
        {
        }
        ModelVolumeState(const ObjectID &volume_id, const ObjectID &instance_id)
            : model_volume(nullptr), geometry_id(std::make_pair(volume_id.id, instance_id.id)), volume_idx(-1)
        {
        }
        bool new_geometry() const { return this->volume_idx == size_t(-1); }
        const ModelVolume *model_volume;
        // ObjectID of ModelVolume + ObjectID of ModelInstance
        // or timestamp of an SLAPrintObjectStep + ObjectID of ModelInstance
        std::pair<size_t, size_t> geometry_id;
        GLVolume::CompositeID composite_id;
        // Volume index in the new GLVolume vector.
        size_t volume_idx;
    };
    std::vector<ModelVolumeState> model_volume_state;
    std::vector<ModelVolumeState> aux_volume_state;

    struct GLVolumeState
    {
        GLVolumeState() : volume_idx(size_t(-1)) {}
        GLVolumeState(const GLVolume *volume, unsigned int volume_idx)
            : composite_id(volume->composite_id), volume_idx(volume_idx)
        {
        }
        GLVolumeState(const GLVolume::CompositeID &composite_id) : composite_id(composite_id), volume_idx(size_t(-1)) {}

        GLVolume::CompositeID composite_id;
        // Volume index in the old GLVolume vector.
        size_t volume_idx;
    };

    std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>> new_to_old_ids_map;

    std::vector<size_t> instance_ids_selected;
    std::vector<size_t> map_glvolume_old_to_new(m_volumes.volumes.size(), size_t(-1));
    std::vector<GLVolumeState> deleted_volumes;
    std::vector<GLVolume *> glvolumes_new;
    glvolumes_new.reserve(m_volumes.volumes.size());
    auto model_volume_state_lower = [](const ModelVolumeState &m1, const ModelVolumeState &m2)
    {
        return m1.geometry_id < m2.geometry_id;
    };

    m_reload_delayed = (m_surface == nullptr || !m_surface->isShown()) && !refresh_immediately &&
                       !force_full_scene_refresh;

    PrinterTechnology printer_technology = current_printer_technology();
    std::map<size_t, size_t> volume_idxs_wipe_towers_old; // map from geometry_id.second to volume_id

    // Release invalidated volumes to conserve GPU memory in case of delayed refresh (see m_reload_delayed).
    // First initialize model_volumes_new_sorted & model_instances_new_sorted.
    for (int object_idx = 0; object_idx < (int) m_model->objects.size(); ++object_idx)
    {
        const ModelObject *model_object = m_model->objects[object_idx];
        for (int instance_idx = 0; instance_idx < (int) model_object->instances.size(); ++instance_idx)
        {
            const ModelInstance *model_instance = model_object->instances[instance_idx];
            for (int volume_idx = 0; volume_idx < (int) model_object->volumes.size(); ++volume_idx)
            {
                const ModelVolume *model_volume = model_object->volumes[volume_idx];
                model_volume_state.emplace_back(model_volume, model_instance->id(),
                                                GLVolume::CompositeID(object_idx, volume_idx, instance_idx));
            }
        }
    }

    std::sort(model_volume_state.begin(), model_volume_state.end(), model_volume_state_lower);
    // Release all ModelVolume based GLVolumes not found in the current Model.
    for (size_t volume_id = 0; volume_id < m_volumes.volumes.size(); ++volume_id)
    {
        GLVolume *volume = m_volumes.volumes[volume_id];
        ModelVolumeState key(volume);
        ModelVolumeState *mvs = nullptr;
        if (volume->volume_idx() >= 0)
        {
            auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key,
                                       model_volume_state_lower);
            if (it != model_volume_state.end() && it->geometry_id == key.geometry_id)
                mvs = &(*it);
        }
        // Emplace instance ID of the volume. Both the aux volumes and model volumes share the same instance ID.
        // The wipe tower has its own wipe_tower_instance_id().
        if (m_selection.contains_volume(volume_id))
            instance_ids_selected.emplace_back(volume->geometry_id.second);
        if (mvs == nullptr || force_full_scene_refresh)
        {
            // This GLVolume will be released.
            if (volume->is_wipe_tower())
            {
#if SLIC3R_OPENGL_ES
                m_wipe_tower_meshes.clear();
#endif // SLIC3R_OPENGL_ES
                volume_idxs_wipe_towers_old.emplace(std::make_pair(volume->geometry_id.second, volume_id));
            }
            if (!m_reload_delayed)
            {
                deleted_volumes.emplace_back(volume, volume_id);
                delete volume;
            }
        }
        else
        {
            // This GLVolume will be reused.
            map_glvolume_old_to_new[volume_id] = glvolumes_new.size();
            mvs->volume_idx = glvolumes_new.size();
            glvolumes_new.emplace_back(volume);
            // Update color of the volume based on the current extruder.
            if (mvs->model_volume != nullptr)
            {
                int extruder_id = mvs->model_volume->extruder_id();
                if (extruder_id != -1)
                    volume->extruder_id = extruder_id;

                volume->is_modifier = !mvs->model_volume->is_model_part();
                volume->shader_outside_printer_detection_enabled = mvs->model_volume->is_model_part();
                // Alignment Box modifiers are never scene-selectable
                if (mvs->model_volume->is_modifier() && mvs->model_volume->name == "Alignment Box")
                    volume->disabled = true;
                volume->set_color(color_from_model_volume(*mvs->model_volume));
                // force update of render_color alpha channel
                volume->set_render_color(volume->color.is_transparent());

                // updates volumes transformations
                volume->set_instance_transformation(
                    mvs->model_volume->get_object()->instances[mvs->composite_id.instance_id]->get_transformation());
                volume->set_volume_transformation(mvs->model_volume->get_transformation());

                // updates volumes convex hull
                if (mvs->model_volume->is_model_part() && !volume->convex_hull())
                    // Model volume was likely changed from modifier or support blocker / enforcer to a model part.
                    // Only model parts require convex hulls.
                    volume->set_convex_hull(mvs->model_volume->get_convex_hull_shared_ptr());
            }
        }
    }
    sort_remove_duplicates(instance_ids_selected);
    auto deleted_volumes_lower = [](const GLVolumeState &v1, const GLVolumeState &v2)
    {
        return v1.composite_id < v2.composite_id;
    };
    std::sort(deleted_volumes.begin(), deleted_volumes.end(), deleted_volumes_lower);

    if (m_reload_delayed)
        return;

    bool update_object_list = false;
    if (m_volumes.volumes != glvolumes_new)
        update_object_list = true;
    m_volumes.volumes = std::move(glvolumes_new);
    for (unsigned int obj_idx = 0; obj_idx < (unsigned int) m_model->objects.size(); ++obj_idx)
    {
        const ModelObject &model_object = *m_model->objects[obj_idx];
        for (int volume_idx = 0; volume_idx < (int) model_object.volumes.size(); ++volume_idx)
        {
            const ModelVolume &model_volume = *model_object.volumes[volume_idx];
            for (int instance_idx = 0; instance_idx < (int) model_object.instances.size(); ++instance_idx)
            {
                const ModelInstance &model_instance = *model_object.instances[instance_idx];
                ModelVolumeState key(model_volume.id(), model_instance.id());
                auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key,
                                           model_volume_state_lower);
                assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
                if (it->new_geometry())
                {
                    // New volume.
                    auto it_old_volume = std::lower_bound(deleted_volumes.begin(), deleted_volumes.end(),
                                                          GLVolumeState(it->composite_id), deleted_volumes_lower);
                    if (it_old_volume != deleted_volumes.end() && it_old_volume->composite_id == it->composite_id)
                        // If a volume changed its ObjectID, but it reuses a GLVolume's CompositeID, maintain its selection.
                        map_glvolume_old_to_new[it_old_volume->volume_idx] = m_volumes.volumes.size();
                    // Note the index of the loaded volume, so that we can reload the main model GLVolume with the hollowed mesh
                    // later in this function.
                    it->volume_idx = m_volumes.volumes.size();
                    m_volumes.load_object_volume(&model_object, obj_idx, volume_idx, instance_idx);
                    m_volumes.volumes.back()->geometry_id = key.geometry_id;
                    update_object_list = true;
                }
                else
                {
                    // Recycling an old GLVolume.
                    GLVolume &existing_volume = *m_volumes.volumes[it->volume_idx];
                    assert(existing_volume.geometry_id == key.geometry_id);
                    // Update the Object/Volume/Instance indices into the current Model.
                    if (existing_volume.composite_id != it->composite_id)
                    {
                        new_to_old_ids_map.push_back(std::make_pair(it->composite_id, existing_volume.composite_id));
                        existing_volume.composite_id = it->composite_id;
                        update_object_list = true;
                    }
                }
            }
        }
    }

    if (m_config->has("nozzle_diameter"))
    {
        // Should the wipe tower be visualized ?
        unsigned int extruders_count =
            (unsigned int) dynamic_cast<const ConfigOptionFloats *>(m_config->option("nozzle_diameter"))->values.size();

        const bool wt = dynamic_cast<const ConfigOptionBool *>(m_config->option("wipe_tower"))->value;
        const bool co = dynamic_cast<const ConfigOptionBool *>(m_config->option("complete_objects"))->value;
        const float w = dynamic_cast<const ConfigOptionFloat *>(m_config->option("wipe_tower_width"))->value;
        const float bw = dynamic_cast<const ConfigOptionFloat *>(m_config->option("wipe_tower_brim_width"))->value;
        const float ca = dynamic_cast<const ConfigOptionFloat *>(m_config->option("wipe_tower_cone_angle"))->value;

        if (extruders_count > 1 && wt && !co)
        {
            for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx)
            {
                const Print *print = get_fff_prints()[bed_idx].get();

                const float x = m_model->get_wipe_tower_vector()[bed_idx].position.x();
                const float y = m_model->get_wipe_tower_vector()[bed_idx].position.y();
                const float a = m_model->get_wipe_tower_vector()[bed_idx].rotation;
                const float depth = print->wipe_tower_data(extruders_count).depth;
                const std::vector<std::pair<float, float>> z_and_depth_pairs =
                    print->wipe_tower_data(extruders_count).z_and_depth_pairs;
                const float height_real = print->wipe_tower_data(extruders_count).height; // -1.f = unknown
                const bool is_wipe_tower_step_done = print->is_step_done(psWipeTower);

                // Height of a print (Show at least a slab).
                const double height = height_real < 0.f ? std::max(m_model->max_z(), 10.0) : height_real;
                if (depth != 0.)
                {
#if SLIC3R_OPENGL_ES
                    if (bed_idx >= m_wipe_tower_meshes.size())
                        m_wipe_tower_meshes.resize(bed_idx + 1);
                    GLVolume *volume = m_volumes.load_wipe_tower_preview(x, y, w, depth, z_and_depth_pairs,
                                                                         (float) height, ca, a,
                                                                         !is_wipe_tower_step_done, bw, bed_idx,
                                                                         &m_wipe_tower_meshes[bed_idx]);
#else
                    GLVolume *volume = m_volumes.load_wipe_tower_preview(x, y, w, depth, z_and_depth_pairs,
                                                                         (float) height, ca, a,
                                                                         !is_wipe_tower_step_done, bw, bed_idx);
#endif // SLIC3R_OPENGL_ES
                    const BoundingBoxf3 &bb = volume->bounding_box();
                    m_wipe_tower_bounding_boxes[bed_idx] = BoundingBoxf{to_2d(bb.min), to_2d(bb.max)};
                    if (static_cast<int>(bed_idx) < s_multiple_beds.get_number_of_beds())
                    {
                        m_volumes.volumes.emplace_back(volume);
                        const auto volume_idx_wipe_tower_new{static_cast<int>(m_volumes.volumes.size() - 1)};
                        auto it = volume_idxs_wipe_towers_old.find(m_volumes.volumes.back()->geometry_id.second);
                        if (it != volume_idxs_wipe_towers_old.end())
                            map_glvolume_old_to_new[it->second] = volume_idx_wipe_tower_new;
                        m_volumes.volumes.back()->set_volume_offset(m_volumes.volumes.back()->get_volume_offset() +
                                                                    s_multiple_beds.get_bed_translation(bed_idx));
                    }
                    else
                    {
                        delete volume;
                    }
                }
                else
                {
                    m_wipe_tower_bounding_boxes[bed_idx] = std::nullopt;
                }
            }
            s_multiple_beds.ensure_wipe_towers_on_beds(*m_model, get_fff_prints());
        }
        else
        {
            m_wipe_tower_bounding_boxes.fill(std::nullopt);
        }
    }
    else
    {
        m_wipe_tower_bounding_boxes.fill(std::nullopt);
    }

    update_volumes_colors_by_extruder();
    // Update selection indices based on the old/new GLVolumeCollection.
    if (m_selection.get_mode() == Selection::Instance)
        m_selection.instances_changed(instance_ids_selected);
    else
        m_selection.volumes_changed(map_glvolume_old_to_new);

    // @Enrico suggest this solution to preven accessing pointer on caster without data
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::Volume);
    m_gizmos.update_data();
    m_gizmos.refresh_on_off_state();

    // Update the toolbar
    if (update_object_list)
        m_event_poster->postEvent(CanvasEventType::ObjectSelect);

    // checks for geometry outside the print volume to render it accordingly
    if (!m_volumes.empty())
    {
        ModelInstanceEPrintVolumeState state;
        check_volumes_outside_state(m_volumes, &state, !force_full_scene_refresh);
        const bool partlyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Partly_Outside);
        const bool fullyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Fully_Outside);

        _set_warning_notification(EWarning::ObjectClashed, partlyOut);
        _set_warning_notification(EWarning::ObjectOutside, fullyOut);
    }
    else
    {
        _set_warning_notification(EWarning::ObjectOutside, false);
        _set_warning_notification(EWarning::ObjectClashed, false);
    }

    refresh_camera_scene_box();

    if (m_selection.is_empty())
    {
        // If no object is selected, deactivate the active gizmo, if any
        // Otherwise it may be shown after cleaning the scene (if it was active while the objects were deleted)
        m_gizmos.reset_all_states();

        // If no object is selected, reset the objects manipulator on the sidebar
        // to force a reset of its cache
        obj_manipul_set_dirty();
    }

    // CSG boolean preview: only active when the Align gizmo is open.
    // Outside the gizmo, negative volumes must remain visible and editable.
    const bool csg_preview_active = m_csg_preview && m_model != nullptr &&
                                    m_gizmos.get_current_type() == GLGizmosManager::EType::Align;

    if (csg_preview_active)
    {
        for (size_t obj_idx = 0; obj_idx < m_model->objects.size(); ++obj_idx)
        {
            const ModelObject *obj = m_model->objects[obj_idx];
            if (obj != nullptr)
                m_csg_preview->request_update(*obj, static_cast<int>(obj_idx));
        }

        if (m_csg_preview->needs_scene_rebuild())
        {
            m_csg_preview->clear_rebuild_flag();
            reload_scene(true, true);
            return;
        }
    }
    else if (m_csg_preview && m_csg_preview->needs_scene_rebuild())
    {
        // Leaving the Align gizmo with stale CSG data - rebuild to restore original meshes
        m_csg_preview->clear_rebuild_flag();
        m_csg_preview->invalidate_all();
        reload_scene(true, true);
        return;
    }

    // Apply boolean preview results BEFORE registering raycasters.
    // This ensures raycasters point to the correct (possibly replaced) mesh data.
    if (csg_preview_active)
    {
        std::set<int> replaced_objects;

        for (GLVolume *v : m_volumes.volumes)
        {
            if (v == nullptr)
                continue;

            int obj_idx = v->object_idx();
            int vol_idx = v->volume_idx();
            if (obj_idx < 0 || !m_csg_preview->has_preview(obj_idx))
                continue;

            if (replaced_objects.find(obj_idx) == replaced_objects.end())
            {
                bool is_positive = true;
                if (obj_idx < static_cast<int>(m_model->objects.size()))
                {
                    const ModelObject *obj = m_model->objects[obj_idx];
                    if (obj != nullptr && vol_idx >= 0 && vol_idx < static_cast<int>(obj->volumes.size()))
                        is_positive = obj->volumes[vol_idx]->is_model_part();
                }

                if (is_positive)
                {
                    const TriangleMesh *preview = m_csg_preview->get_preview_mesh(obj_idx);
                    if (preview != nullptr && !preview->empty())
                    {
                        // The boolean result is in object-local space (all volume transforms
                        // already baked in). But this GLVolume will apply its own volume
                        // transform on top. To cancel that out, pre-transform the mesh
                        // by the inverse of this volume's transform.
                        TriangleMesh adjusted = *preview;
                        Transform3d vol_inv = v->get_volume_transformation().get_matrix().inverse();
                        adjusted.transform(vol_inv, true);

                        v->model.reset();
                        v->model.init_from(adjusted);
                        v->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(
                            std::make_shared<const TriangleMesh>(std::move(adjusted)));
                        replaced_objects.insert(obj_idx);
                    }
                }
            }
            else
            {
                v->is_active = false;
            }
        }
    }

    // refresh volume raycasters for picking
    for (size_t i = 0; i < m_volumes.volumes.size(); ++i)
    {
        const GLVolume *v = m_volumes.volumes[i];
        assert(v->mesh_raycaster != nullptr);
        std::shared_ptr<SceneRaycasterItem> raycaster = add_raycaster_for_picking(SceneRaycaster::EType::Volume, i,
                                                                                  *v->mesh_raycaster,
                                                                                  v->world_matrix());
        raycaster->set_active(v->is_active && !v->disabled);
    }

    // refresh gizmo elements raycasters for picking
    GLGizmoBase *curr_gizmo = m_gizmos.get_current();
    if (curr_gizmo != nullptr)
        curr_gizmo->unregister_raycasters_for_picking();
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::Gizmo);
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::FallbackGizmo);
    if (curr_gizmo != nullptr && !m_selection.is_empty())
        curr_gizmo->register_raycasters_for_picking();

    // and force this canvas to be redrawn.
    m_dirty = true;
}

void GLCanvas3D::load_gcode_shells()
{
    m_gcode_viewer.load_shells(*this->fff_print());
    m_gcode_viewer.update_shells_color_by_extruder(m_config);
    m_gcode_viewer.set_force_shells_visible(true);
}

void GLCanvas3D::load_gcode_preview(const GCodeProcessorResult &gcode_result,
                                    const std::vector<std::string> &str_tool_colors,
                                    const std::vector<std::string> &str_color_print_colors)
{
    _set_current();

    m_gcode_viewer.enable_legend(false);
    m_event_poster->postEvent(CanvasEventType::HideSliceButton);
    m_gcode_viewer.enable_view_type_cache_write(true);
    m_gcode_viewer.enable_view_type_cache_load(true);
    m_gcode_viewer.set_view_type(m_gcode_viewer.get_view_type());
    m_gcode_viewer.load_as_gcode(gcode_result, *this->fff_print(), str_tool_colors, str_color_print_colors);
    m_gcode_layers_times_cache = m_gcode_viewer.get_layers_times();
    m_gcode_viewer.set_force_shells_visible(false);
    m_gcode_viewer.enable_legend(true);
    m_event_poster->postEvent(CanvasEventType::ShowSliceButton);
    if (is_editor())
    {
        _set_warning_notification_if_needed(EWarning::ToolpathOutside);
        _set_warning_notification_if_needed(EWarning::GCodeConflict);
        _set_warning_notification_if_needed(EWarning::SequentialCollision);
    }

    set_as_dirty();
    request_extra_frame();
}

void GLCanvas3D::load_sla_preview() {}

void GLCanvas3D::load_preview(const std::vector<std::string> &str_tool_colors,
                              const std::vector<std::string> &str_color_print_colors,
                              const std::vector<CustomGCode::Item> &color_print_values)
{
    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    _set_current();

    libvgcode::GCodeInputData data = libvgcode::convert(*print, str_tool_colors, str_color_print_colors,
                                                        color_print_values,
                                                        static_cast<size_t>(extruders_edited_cnt()));

    // send data to the viewer
    m_gcode_viewer.enable_legend(false);
    m_gcode_viewer.enable_view_type_cache_write(false);
    m_gcode_viewer.enable_view_type_cache_load(false);
    m_gcode_viewer.set_view_type(libvgcode::EViewType::FeatureType);
    m_gcode_viewer.load_as_preview(std::move(data));
    m_gcode_viewer.set_force_shells_visible(false);
    _set_warning_notification_if_needed(EWarning::ToolpathOutside);
}

void GLCanvas3D::bind_event_handlers()
{
    if (m_canvas != nullptr)
    {
        m_canvas->Bind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Bind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Bind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Bind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);

        m_toolbar_highlighter.invalidate();
        auto toolbar_timer = std::make_unique<Timer_wx>();
        toolbar_timer->setCallback([this]() { m_toolbar_highlighter.blink(); });
        m_toolbar_highlighter.set_timer(std::move(toolbar_timer));

        m_gizmo_highlighter.invalidate();
        auto gizmo_timer = std::make_unique<Timer_wx>();
        gizmo_timer->setCallback([this]() { m_gizmo_highlighter.blink(); });
        m_gizmo_highlighter.set_timer(std::move(gizmo_timer));

        m_canvas->Bind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Bind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);

        m_event_handlers_bound = true;
    }
}

void GLCanvas3D::unbind_event_handlers()
{
    if (m_canvas != nullptr && m_event_handlers_bound)
    {
        m_canvas->Unbind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Unbind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Unbind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Unbind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Unbind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Unbind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);

        m_event_handlers_bound = false;
    }
}

void GLCanvas3D::on_idle(wxIdleEvent &evt)
{
    if (!m_initialized || m_rendering_paused)
        return;

    if (!m_event_poster)
        return;

    // preFlight: Process state updates (toolbars, notifications, gizmos).
    m_dirty |= m_main_toolbar.update_items_state();
    m_dirty |= m_undoredo_toolbar.update_items_state();
    if (m_collapse_toolbar)
        m_dirty |= m_collapse_toolbar->update_items_state();
    bool mouse3d_controller_applied = m_mouse3d_controller ? m_mouse3d_controller->apply(get_camera()) : false;
    m_dirty |= mouse3d_controller_applied;
    if (m_notification_manager)
        m_dirty |= m_notification_manager->update_notifications(*this);
    auto gizmo = m_gizmos.get_current();
    if (gizmo != nullptr)
        m_dirty |= gizmo->update_items_state();

    bool imgui_requires_extra_frame = get_imgui()->requires_extra_frame();
    m_dirty |= imgui_requires_extra_frame;

    if (!m_dirty)
        return;

    get_imgui()->reset_requires_extra_frame();

    _refresh_if_shown_on_screen();

    if (is_gpu_power_saving())
    {
        // preFlight: In power-saving mode (Preview tab), only continue the idle loop
        // when explicitly requested or when ImGui needs layout recalculation.
        if (m_extra_frame_requested || get_imgui()->requires_extra_frame())
            evt.RequestMore();
        else
            m_dirty = false;
    }
    else
    {
        // Keep the idle loop alive for mouse3d,
        // imgui extra frames, and explicit frame requests.
        if (m_extra_frame_requested || mouse3d_controller_applied || imgui_requires_extra_frame ||
            get_imgui()->requires_extra_frame())
        {
            m_extra_frame_requested = false;
            evt.RequestMore();
        }
        else
            m_dirty = false;
    }
    m_extra_frame_requested = false;
}

void GLCanvas3D::on_char(wxKeyEvent &evt)
{
    assert(m_event_poster);
    KeyInput key = key_from_wx(evt);
    on_char_internal(key);
    if (key.propagate)
        evt.Skip();
}

// key.key_code uses WXK_* values from wx/defs.h.
void GLCanvas3D::on_char_internal(KeyInput &key)
{
    if (!m_initialized)
        return;

#ifdef SHOW_IMGUI_DEMO_WINDOW
    static int cur = 0;
    if (get_logging_level() >= 3 && wxString("demo")[cur] == key.unicode_key)
        ++cur;
    else
        cur = 0;
    if (cur == 4)
    {
        show_imgui_demo_window = !show_imgui_demo_window;
        cur = 0;
    }
#endif // SHOW_IMGUI_DEMO_WINDOW

    auto imgui = get_imgui();
    if (imgui->update_key_data(key))
    {
        render();
        return;
    }

    int keyCode = key.key_code;
    if (keyCode == WXK_ESCAPE && (_deactivate_undo_redo_toolbar_items() || _deactivate_arrange_menu()))
        return;

    if (m_gizmos.on_char(key))
    {
        if (m_gizmos.get_current_type() == GLGizmosManager::EType::Scale &&
            m_gizmos.get_current()->get_state() == GLGizmoBase::EState::On)
        {
            // Update selection from object list to check selection of the cut objects
            // It's not allowed to scale separate ct parts
            m_event_poster->postEvent(CanvasEventType::ObjListSelectionChanged);
        }
        return;
    }

    if (key.ctrl)
    {
        switch (keyCode)
        {
#ifdef __APPLE__
        case 'a':
        case 'A':
#else  /* __APPLE__ */
        case WXK_CONTROL_A:
#endif /* __APPLE__ */
            m_event_poster->postEvent(CanvasEventType::SelectAll);
            break;
#ifdef __APPLE__
        case 'c':
        case 'C':
#else  /* __APPLE__ */
        case WXK_CONTROL_C:
#endif /* __APPLE__ */
            m_event_poster->postEvent(CanvasEventType::ToolbarCopy);
            break;
#ifdef __APPLE__
        case 'm':
        case 'M':
#else  /* __APPLE__ */
        case WXK_CONTROL_M:
#endif /* __APPLE__ */
        {
#ifdef _WIN32
            if (m_app_config->get_bool("use_legacy_3DConnexion"))
            {
#endif //_WIN32
#ifdef __APPLE__
                // On OSX use Cmd+Shift+M to "Show/Hide 3Dconnexion devices settings dialog"
                if (key.shift)
                {
#endif // __APPLE__
                    m_mouse3d_controller->show_settings_dialog(!m_mouse3d_controller->is_settings_dialog_shown());
                    m_dirty = true;
#ifdef __APPLE__
                }
                else
                    // and Cmd+M to minimize application
                    m_event_poster->postEvent(CanvasEventType::MinimizeWindow);
#endif // __APPLE__
#ifdef _WIN32
            }
#endif //_WIN32
            break;
        }
#ifdef __APPLE__
        case 'v':
        case 'V':
#else  /* __APPLE__ */
        case WXK_CONTROL_V:
#endif /* __APPLE__ */
            m_event_poster->postEvent(CanvasEventType::ToolbarPaste);
            break;
#ifdef __APPLE__
        case 'y':
        case 'Y':
#else  /* __APPLE__ */
        case WXK_CONTROL_Y:
#endif /* __APPLE__ */
            m_event_poster->postEvent(CanvasEventType::Redo);
            break;
#ifdef __APPLE__
        case 'z':
        case 'Z':
#else  /* __APPLE__ */
        case WXK_CONTROL_Z:
#endif /* __APPLE__ */
            m_event_poster->postEvent(CanvasEventType::Undo);
            break;

        case WXK_BACK:
        case WXK_DELETE:
            m_event_poster->postEvent(CanvasEventType::ToolbarDeleteAll);
            break;
#ifdef __APPLE__
        case 'e':
        case 'E':
#else  /* __APPLE__ */
        case WXK_CONTROL_E:
#endif /* __APPLE__ */
            m_labels.show(!m_labels.is_shown());
            m_dirty = true;
            break;
#ifdef __APPLE__
        case 'l':
        case 'L':
#else  /* __APPLE__ */
        case WXK_CONTROL_L:
#endif /* __APPLE__ */
            if (!m_main_toolbar.is_enabled())
                show_legend(!is_legend_shown());
            break;
        default:
            key.propagate = true;
        }
    }
    else
    {
        switch (keyCode)
        {
        case WXK_BACK:
        case WXK_DELETE:
        {
            m_event_poster->postEvent(CanvasEventType::ToolbarDelete);
            break;
        }
        case WXK_ESCAPE:
        {
            deselect_all();
            break;
        }
        case WXK_F5:
        {
            if ((is_editor() && !m_model->objects.empty()) ||
                (is_gcode_viewer() && m_has_loaded_gcode && m_has_loaded_gcode()))
                m_event_poster->postEvent(CanvasEventType::ReloadFromDisk);
            break;
        }
        // Support both regular number keys and numpad keys
        case '0':
        case WXK_NUMPAD0:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("iso");
            break;
        }
        case '1':
        case WXK_NUMPAD1:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("top");
            break;
        }
        case '2':
        case WXK_NUMPAD2:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("bottom");
            break;
        }
        case '3':
        case WXK_NUMPAD3:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("front");
            break;
        }
        case '4':
        case WXK_NUMPAD4:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("rear");
            break;
        }
        case '5':
        case WXK_NUMPAD5:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("left");
            break;
        }
        case '6':
        case WXK_NUMPAD6:
        {
            if (m_canvas_role == CanvasRole::Preview || m_canvas_role == CanvasRole::View3D)
                select_view("right");
            break;
        }
        case '+':
        {
            if (m_canvas_role == CanvasRole::Preview)
                m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, key.key_code,
                                             key_modifiers_to_wx(key));
            else
                m_event_poster->postEvent(CanvasEventType::IncreaseInstances, +1);
            break;
        }
        case '-':
        {
            if (m_canvas_role == CanvasRole::Preview)
                m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, key.key_code,
                                             key_modifiers_to_wx(key));
            else
                m_event_poster->postEvent(CanvasEventType::IncreaseInstances, -1);
            break;
        }
        case WXK_F1:
        {
            m_event_poster->postEvent(CanvasEventType::QuestionMark);
            break;
        }
        case 'A':
        case 'a':
        {
            m_event_poster->postEvent(CanvasEventType::Arrange);
            break;
        }
        case 'B':
        case 'b':
        {
            zoom_to_bed();
            break;
        }
        case 'C':
        case 'c':
        {
            m_gcode_viewer.toggle_gcode_window_visibility();
            m_dirty = true;
            request_extra_frame();
            break;
        }
        case 'D':
        case 'd':
        {
            m_event_poster->postEvent(CanvasEventType::ArrangeCurrentBed);
            break;
        }
        case 'G':
        case 'g':
        {
            if (key.shift)
            {
                if (m_canvas_role == CanvasRole::Preview)
                    m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, key.key_code,
                                                 key_modifiers_to_wx(key));
            }
            break;
        }
        case 'I':
        case 'i':
        {
            _update_camera_zoom(1.0);
            break;
        }
        case 'K':
        case 'k':
        {
            get_camera().select_next_type();
            m_dirty = true;
            break;
        }
        case 'O':
        case 'o':
        {
            _update_camera_zoom(-1.0);
            break;
        }
        case 'Z':
        case 'z':
        {
            if (!m_selection.is_empty())
                zoom_to_selection();
            else
            {
                if (!m_volumes.empty())
                    zoom_to_volumes();
                else
                    _zoom_to_box(m_gcode_viewer.get_paths_bounding_box());
            }
            break;
        }
        default:
        {
            key.propagate = true;
            break;
        }
        }
    }
}

class TranslationProcessor
{
    using UpAction = std::function<void(void)>;
    using DownAction = std::function<void(const Vec3d &, bool, bool)>;

    UpAction m_up_action{nullptr};
    DownAction m_down_action{nullptr};

    bool m_running{false};
    Vec3d m_direction{Vec3d::UnitX()};

public:
    TranslationProcessor(UpAction up_action, DownAction down_action)
        : m_up_action(up_action), m_down_action(down_action)
    {
    }

    void process(const KeyInput &key)
    {
        const int keyCode = key.key_code;
        if (key.type == KeyEventType::KeyUp)
        {
            switch (keyCode)
            {
            case WXK_NUMPAD_LEFT:
            case WXK_LEFT:
            case WXK_NUMPAD_RIGHT:
            case WXK_RIGHT:
            case WXK_NUMPAD_UP:
            case WXK_UP:
            case WXK_NUMPAD_DOWN:
            case WXK_DOWN:
            {
                m_running = false;
                m_up_action();
                break;
            }
            default:
            {
                break;
            }
            }
        }
        else if (key.type == KeyEventType::KeyDown)
        {
            bool apply = false;

            switch (keyCode)
            {
            case WXK_SHIFT:
            {
                if (m_running)
                    apply = true;

                break;
            }
            case WXK_NUMPAD_LEFT:
            case WXK_LEFT:
            {
                m_direction = -Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_RIGHT:
            case WXK_RIGHT:
            {
                m_direction = Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_UP:
            case WXK_UP:
            {
                m_direction = Vec3d::UnitY();
                apply = true;
                break;
            }
            case WXK_NUMPAD_DOWN:
            case WXK_DOWN:
            {
                m_direction = -Vec3d::UnitY();
                apply = true;
                break;
            }
            default:
            {
                break;
            }
            }

            if (apply)
            {
                m_running = true;
                m_down_action(m_direction, key.shift, key.cmd);
            }
        }
    }
};

void GLCanvas3D::on_key(wxKeyEvent &evt)
{
    assert(m_event_poster);
    KeyInput key = key_from_wx(evt);
    on_key_internal(key);
    if (key.propagate)
        evt.Skip();
}

// key.key_code uses WXK_* values from wx/defs.h.
void GLCanvas3D::on_key_internal(KeyInput &key)
{
    static TranslationProcessor translationProcessor(
        [this]()
        {
            do_move(L("Gizmo-Move"));
            m_gizmos.update_data();

            obj_manipul_set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            m_event_poster->postEvent(CanvasEventType::MouseDraggingFinished);
            // updates camera target constraints
            refresh_camera_scene_box();
            m_dirty = true;
        },
        [this](const Vec3d &direction, bool slow, bool camera_space)
        {
            m_selection.setup_cache();
            double multiplier = slow ? 1.0 : 10.0;

            Vec3d displacement;
            if (camera_space)
            {
                Eigen::Matrix<double, 3, 3, Eigen::DontAlign> inv_view_3x3 =
                    get_camera().get_view_matrix().inverse().matrix().block(0, 0, 3, 3);
                displacement = multiplier * (inv_view_3x3 * direction);
                displacement.z() = 0.0;
            }
            else
                displacement = multiplier * direction;

            TransformationType trafo_type;
            trafo_type.set_relative();
            m_selection.translate(displacement, trafo_type);
            m_dirty = true;
        });

    const int keyCode = key.key_code;

    auto imgui = get_imgui();
    if (imgui->update_key_data(key))
        render();
    else
    {
        if (!m_gizmos.on_key(key))
        {
            if (key.type == KeyEventType::KeyUp)
            {
                if (get_logging_level() >= 3 && key.shift && key.ctrl && keyCode == WXK_SPACE)
                {
                    m_event_poster->postEvent(CanvasEventType::ToggleRenderStatisticDialog);
                    m_dirty = true;
                }
                if (m_tab_down && keyCode == WXK_TAB && !key.has_any_modifiers())
                {
                    // Enable switching between 3D and Preview with Tab
                    // m_canvas->HandleAsNavigationKey(evt);   // XXX: Doesn't work in some cases / on Linux
                    m_event_poster->postEvent(CanvasEventType::Tab);
                }
                else if (!is_gcode_viewer() && keyCode == WXK_TAB &&
                         // Use strong condition for modifiers state to avoid cases when Shift can be combined with other modifiers
                         // (Fix for model visibility issue)
                         key.shift && !key.ctrl && !key.alt && !key.meta)
                {
                    // Collapse side-panel with Shift+Tab
                    m_event_poster->postEvent(CanvasEventType::CollapseSidebar);
                }
                else if (keyCode == WXK_SHIFT)
                {
                    translationProcessor.process(key);

                    if (m_picking_enabled && m_rectangle_selection.is_dragging())
                    {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                    }
                    m_shift_kar_filter.reset_count();
                    m_dirty = true;
                    //                    set_cursor(Standard);
                }
                else if (keyCode == WXK_ALT)
                {
                    if (m_picking_enabled && m_rectangle_selection.is_dragging())
                    {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                        m_dirty = true;
                    }
                    //                    set_cursor(Standard);
                }
                else if (keyCode == WXK_CONTROL)
                {
                    if (m_mouse.dragging && !m_moving)
                    {
                        // if the user releases CTRL while rotating the 3D scene
                        // prevent from moving the selected volume
                        m_mouse.drag.move_volume_idx = -1;
                        m_mouse.set_start_position_3D_as_invalid();
                    }
                    m_ctrl_kar_filter.reset_count();
                    m_dirty = true;
                }
                else if (m_gizmos.is_enabled() && !m_selection.is_empty())
                {
                    translationProcessor.process(key);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:
                    case WXK_PAGEUP:
                    case WXK_NUMPAD_PAGEDOWN:
                    case WXK_PAGEDOWN:
                    {
                        do_rotate(L("Gizmo-Rotate"));
                        m_gizmos.update_data();

                        obj_manipul_set_dirty();
                        // Let the plater know that the dragging finished, so a delayed refresh
                        // of the scene with the background processing data should be performed.
                        m_event_poster->postEvent(CanvasEventType::MouseDraggingFinished);
                        // updates camera target constraints
                        refresh_camera_scene_box();
                        m_dirty = true;

                        break;
                    }
                    default:
                    {
                        break;
                    }
                    }
                }
            }
            else if (key.type == KeyEventType::KeyDown)
            {
                m_tab_down = keyCode == WXK_TAB && !key.has_any_modifiers();
                if (keyCode == WXK_SHIFT)
                {
                    translationProcessor.process(key);

                    if (m_picking_enabled)
                    {
                        m_mouse.ignore_left_up = false;
                    }
                    if (m_shift_kar_filter.is_first())
                        m_dirty = true;

                    m_shift_kar_filter.increase_count();
                }
                else if (keyCode == WXK_ALT)
                {
                    if (m_picking_enabled)
                    {
                        m_mouse.ignore_left_up = false;
                    }
                }
                else if (keyCode == WXK_CONTROL)
                {
                    if (m_ctrl_kar_filter.is_first())
                        m_dirty = true;

                    m_ctrl_kar_filter.increase_count();
                }
                else if (m_gizmos.is_enabled() && !m_selection.is_empty())
                {
                    auto do_rotate = [this](double angle_z_rad)
                    {
                        m_selection.setup_cache();
                        m_selection.rotate(Vec3d(0.0, 0.0, angle_z_rad),
                                           TransformationType(TransformationType::World_Relative_Joint));
                        m_dirty = true;
                        //                        obj_manipul_set_dirty();
                    };

                    translationProcessor.process(key);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:
                    case WXK_PAGEUP:
                    {
                        do_rotate(0.25 * M_PI);
                        break;
                    }
                    case WXK_NUMPAD_PAGEDOWN:
                    case WXK_PAGEDOWN:
                    {
                        do_rotate(-0.25 * M_PI);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                    }
                }
                else if (!m_gizmos.is_enabled())
                {
                    // DoubleSlider navigation in Preview
                    if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT || keyCode == WXK_UP || keyCode == WXK_DOWN)
                    {
                        if (m_canvas_role == CanvasRole::Preview)
                            m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, key.key_code,
                                                         key_modifiers_to_wx(key));
                    }
                }

                // Delete/Backspace: these are "special keys" excluded from the menu
                // accelerator table (see wxExtensions.cpp), and on Windows they don't
                // generate wxEVT_CHAR, so forward them to the char handler on KeyDown.
                if (keyCode == WXK_BACK || keyCode == WXK_DELETE)
                    on_char_internal(key);
            }
        }
    }

    const GLGizmosManager::EType gizmo_type = m_gizmos.get_current_type();
    if (keyCode == WXK_ALT && (gizmo_type == GLGizmosManager::FdmSupports || gizmo_type == GLGizmosManager::Seam ||
                               gizmo_type == GLGizmosManager::ColorMixing || gizmo_type == GLGizmosManager::FuzzySkin ||
                               gizmo_type == GLGizmosManager::CounterboreBridge))
    {
        // Prevents focusing on the menu bar when ALT is pressed in painting gizmos.
        key.propagate = false;
    }
    else if (keyCode != WXK_TAB && keyCode != WXK_LEFT && keyCode != WXK_UP && keyCode != WXK_RIGHT &&
             keyCode != WXK_DOWN)
    {
        // Propagate so KEY_DOWN generates CHAR event
        key.propagate = true;
    }
}

void GLCanvas3D::on_mouse_wheel(wxMouseEvent &evt)
{
    assert(m_event_poster);
#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif
    MouseInput mouse = mouse_from_wx(evt);
    on_mouse_wheel_internal(mouse);
    if (mouse.propagate)
        evt.Skip();
}

void GLCanvas3D::on_mouse_wheel_internal(MouseInput &mouse)
{
#ifdef WIN32
    if (m_mouse3d_controller->process_mouse_wheel())
        return;
#endif

    if (!m_initialized)
        return;

    if (mouse.middle_down)
        return;

    if (get_imgui()->update_mouse_data(mouse))
    {
        m_dirty = true;
        return;
    }

#ifdef __WXMSW__
    wxWakeUpIdle();
#endif

    if (is_layers_editing_enabled())
    {
        int object_idx_selected = m_selection.get_object_idx();
        if (object_idx_selected != -1)
        {
            if (m_layers_editing.bar_rect_contains(*this, (float) mouse.x, (float) mouse.y))
            {
                m_layers_editing.band_width = std::max(std::min(m_layers_editing.band_width *
                                                                    (1.0f + 0.1f * (float) mouse.wheel_rotation /
                                                                                (float) mouse.wheel_delta),
                                                                10.0f),
                                                       1.5f);
                if (m_surface != nullptr)
                    m_surface->refresh();
                return;
            }
        }
    }

    if (m_undoredo_toolbar.is_item_pressed("undo") || m_undoredo_toolbar.is_item_pressed("redo"))
    {
        m_mouse_wheel = int((double) mouse.wheel_rotation / (double) mouse.wheel_delta);
        return;
    }

    if (m_gizmos.on_mouse_wheel(mouse))
        return;

    const double direction_factor = m_app_config->get_bool("reverse_mouse_wheel_zoom") ? -1.0 : 1.0;
    const double delta = direction_factor * (double) mouse.wheel_rotation / (double) mouse.wheel_delta;
    if (mouse.shift)
    {
        const auto cnv_size = get_canvas_size();
        const Vec3d screen_center_3d_pos = _mouse_to_3d({cnv_size.get_width() * 0.5, cnv_size.get_height() * 0.5});
        const Vec3d mouse_3d_pos = _mouse_to_3d({mouse.x, mouse.y});
        const Vec3d displacement = mouse_3d_pos - screen_center_3d_pos;
        get_camera().translate_world(displacement);
        const double origin_zoom = get_camera().get_zoom();
        _update_camera_zoom(delta);
        const double new_zoom = get_camera().get_zoom();
        get_camera().translate_world((-displacement) / (new_zoom / origin_zoom));
    }
    else
        _update_camera_zoom(delta);
}

void GLCanvas3D::on_timer_internal()
{
    if (m_layers_editing.state == LayersEditing::Editing)
        _perform_layer_editing_action();
}

void GLCanvas3D::on_render_timer_internal()
{
    m_dirty = true;
}

void GLCanvas3D::schedule_extra_frame(int miliseconds)
{
    // preFlight: Ignore schedule requests made during the render cycle when power saving.
    // Code inside render() calls schedule_extra_frame(0) which originally started a
    // 33ms timer, creating an infinite loop. All legitimate render triggers are handled
    // by on_idle's update checks or by event handlers.
    if (m_in_render && is_gpu_power_saving())
        return;

    // Schedule idle event right now
    if (miliseconds == 0)
    {
        m_dirty = true;
        if (!is_gpu_power_saving())
        {
            // Wake up the idle handler immediately
            // so the frame is rendered without waiting for the next natural idle event.
            wxWakeUpIdle();
        }
        // preFlight power-saving: Just set m_dirty without wxWakeUpIdle(). The notification
        // system calls schedule_extra_frame(0) from timer callbacks and from
        // update_notifications(). If we post wxWakeUpIdle here, it triggers another
        // idle cycle, which calls update_notifications, which calls schedule_extra_frame(0)
        // again - infinite loop. Setting m_dirty is sufficient: the next natural idle
        // event will render it.
        return;
    }
    int remaining_time = m_render_timer->getInterval();
    if (!m_render_timer->isRunning())
    {
        m_render_timer->start(miliseconds, true);
    }
    else
    {
        if (miliseconds + 20 < remaining_time)
        {
            m_render_timer->stop();
            m_render_timer->start(miliseconds, true);
        }
    }
}

void GLCanvas3D::on_mouse(wxMouseEvent &evt)
{
    assert(m_event_poster);
#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif
    MouseInput mouse = mouse_from_wx(evt);
    on_mouse_internal(mouse);
    if (mouse.propagate)
        evt.Skip();
}

void GLCanvas3D::on_mouse_internal(MouseInput &mouse)
{
    if (!m_initialized || !_set_current())
        return;

    Point pos((int) mouse.x, (int) mouse.y);

    // Timer for virtual bed switching when dragging objects outside the bed.
    if (s_vbt_owner != this)
    {
        s_virtual_bed_timer = std::make_unique<Timer_wx>();
        s_virtual_bed_timer->setCallback(
            [this]()
            {
                s_multiple_beds.request_next_bed(true);
                schedule_extra_frame(100);
            });
        s_vbt_owner = this;
    }

    ImGuiWrapper *imgui = get_imgui();
    if (m_tooltip.is_in_imgui() && mouse.type == MouseEventType::LeftUp)
        // ignore left up events coming from imgui windows and not processed by them
        m_mouse.ignore_left_up = true;
    m_tooltip.set_in_imgui(false);
    if (imgui->update_mouse_data(mouse))
    {
        m_mouse.position = (mouse.type == MouseEventType::Leave) ? Vec2d(-1.0, -1.0) : pos.cast<double>();
        m_tooltip.set_in_imgui(true);
        render();
        m_dirty = true;
        // Let Moving events through to gizmos that need them for hover tracking
        // (ColorMixing for seed fill, Align for snap point indicators)
        if (!m_mouse.dragging && m_tooltip.is_empty() &&
            ((m_gizmos.get_current_type() != GLGizmosManager::ColorMixing &&
              m_gizmos.get_current_type() != GLGizmosManager::Align) ||
             mouse.type != MouseEventType::Motion))
            return;
    }

#ifdef __WXMSW__
    bool on_enter_workaround = false;
    if (mouse.type != MouseEventType::Enter && mouse.type != MouseEventType::Leave && m_mouse.position.x() == -1.0)
    {
        m_mouse.position = pos.cast<double>();
        render();
        on_enter_workaround = true;
    }
    else
#endif /* __WXMSW__ */
    {
    }

    if (m_main_toolbar.on_mouse(mouse, *this))
    {
        if (mouse.is_any_button_up())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    if (m_undoredo_toolbar.on_mouse(mouse, *this))
    {
        if (mouse.is_any_button_up())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    if (m_collapse_toolbar->on_mouse(mouse, *this))
    {
        if (mouse.is_any_button_up())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    for (GLVolume *volume : m_volumes.volumes)
    {
        volume->force_sinking_contours = false;
    }

    auto show_sinking_contours = [this]()
    {
        const Selection::IndicesList &idxs = m_selection.get_volume_idxs();
        for (unsigned int idx : idxs)
        {
            m_volumes.volumes[idx]->force_sinking_contours = true;
        }
        m_dirty = true;
    };

    if (m_gizmos.on_mouse(mouse))
    {
        if (m_surface && !m_surface->hasFocus())
            m_surface->setFocus();

        if (mouse.is_any_button_up())
            mouse_up_cleanup();

        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.position = pos.cast<double>();

        // It should be detection of volume change
        // Not only detection of some modifiers !!!
        if (mouse.dragging)
        {
            GLGizmosManager::EType c = m_gizmos.get_current_type();
            if (c == GLGizmosManager::EType::Move || c == GLGizmosManager::EType::Scale ||
                c == GLGizmosManager::EType::Rotate)
            {
                show_sinking_contours();
            }
        }
        else if ((mouse.type == MouseEventType::LeftUp) &&
                 m_gizmos.get_current_type() == GLGizmosManager::EType::Scale &&
                 m_gizmos.get_current()->get_state() == GLGizmoBase::EState::On)
        {
            // Update selection from object list to check selection of the cut objects
            // It's not allowed to scale separate ct parts
            m_event_poster->postEvent(CanvasEventType::ObjListSelectionChanged);
        }

        return;
    }

    bool any_gizmo_active = m_gizmos.get_current() != nullptr;

    int selected_object_idx = m_selection.get_object_idx();
    int layer_editing_object_idx = is_layers_editing_enabled() ? selected_object_idx : -1;
    m_layers_editing.select_object(*m_model, layer_editing_object_idx);

    if (m_mouse.drag.move_requires_threshold && m_mouse.is_move_start_threshold_position_2D_defined() &&
        m_mouse.is_move_threshold_met(pos))
    {
        m_mouse.drag.move_requires_threshold = false;
        m_mouse.set_move_start_threshold_position_2D_as_invalid();
    }

    if (mouse.is_any_button_down() && m_surface && !m_surface->hasFocus())
        m_surface->setFocus();

    if ((mouse.type == MouseEventType::Enter))
    {
        if (m_mouse.dragging && !mouse.left_down && !mouse.right_down && !mouse.middle_down)
        {
            // ensure to stop layers editing if enabled
            if (m_layers_editing.state != LayersEditing::Unknown)
            {
                m_layers_editing.state = LayersEditing::Unknown;
                _stop_timer();
                m_layers_editing.accept_changes(*this);
            }
            mouse_up_cleanup();
        }

        //#if defined(__WXMSW__) || defined(__linux__)
        //        // On Windows and Linux needs focus in order to catch key events
        if (m_surface != nullptr)
        {
            if (!m_surface->hasFocus())
            {
                wxTopLevelWindow *tlw = find_toplevel_parent(m_canvas);
                if (tlw->IsActive())
                {
                    auto *text_ctrl = dynamic_cast<wxTextCtrl *>(tlw->FindFocus());
                    if (text_ctrl == nullptr)
                        m_surface->setFocus();
                }
            }

            m_mouse.position = pos.cast<double>();
            m_tooltip_enabled = false;
            // 1) forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is shown, ensuring it to disappear if the mouse is outside any volume and to
            // change the volume hover state if any is under the mouse
            // 2) when switching between 3d view and preview the size of the canvas changes if the side panels are visible,
            // so forces a resize to avoid multiple renders with different sizes (seen as flickering)
            _refresh_if_shown_on_screen();
            m_tooltip_enabled = true;
        }
        m_mouse.set_start_position_2D_as_invalid();
        //#endif
    }
    else if ((mouse.type == MouseEventType::Leave))
    {
        _deactivate_undo_redo_toolbar_items();

        if (m_layers_editing.state != LayersEditing::Unknown)
            m_layers_editing.state = LayersEditing::Paused;

        // to remove hover on objects when the mouse goes out of this canvas
        m_mouse.position = Vec2d(-1.0, -1.0);
        m_dirty = true;
    }
    else if (mouse.is_any_button_down())
    {
#ifndef _WIN32
        // preFlight: Track genuine LeftDown events on the canvas so we can filter
        // phantom LeftUp events that GTK/Cocoa deliver after popup menus close.
        if ((mouse.type == MouseEventType::LeftDown))
            m_mouse.left_down_on_canvas = true;
#endif
        if (_deactivate_undo_redo_toolbar_items() || _deactivate_arrange_menu())
            return;

        // If user pressed left or right button we first check whether this happened
        // on a volume or not.
        m_layers_editing.state = LayersEditing::Unknown;
        if (layer_editing_object_idx != -1 && m_layers_editing.bar_rect_contains(*this, pos(0), pos(1)))
        {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&mouse);
        }
        else
        {
            const bool rectangle_selection_dragging = m_rectangle_selection.is_dragging();
            if ((mouse.type == MouseEventType::LeftDown) && (mouse.shift || mouse.alt) && m_picking_enabled)
            {
                if (m_gizmos.get_current_type() != GLGizmosManager::FdmSupports &&
                    m_gizmos.get_current_type() != GLGizmosManager::Seam &&
                    m_gizmos.get_current_type() != GLGizmosManager::Cut &&
                    m_gizmos.get_current_type() != GLGizmosManager::Measure &&
                    m_gizmos.get_current_type() != GLGizmosManager::ColorMixing &&
                    m_gizmos.get_current_type() != GLGizmosManager::FuzzySkin &&
                    m_gizmos.get_current_type() != GLGizmosManager::CounterboreBridge)
                {
                    m_rectangle_selection.start_dragging(m_mouse.position,
                                                         mouse.shift ? GLSelectionRectangle::EState::Select
                                                                     : GLSelectionRectangle::EState::Deselect);
                    m_dirty = true;
                }
            }

            // Select volume in this 3D canvas.
            // Don't deselect a volume if layer editing is enabled or any gizmo is active. We want the object to stay selected
            // during the scene manipulation.

            if (m_picking_enabled && (!any_gizmo_active || !mouse.cmd) &&
                (!m_hover_volume_idxs.empty() || !is_layers_editing_enabled()) && !rectangle_selection_dragging)
            {
                if ((mouse.type == MouseEventType::LeftDown) && !m_hover_volume_idxs.empty())
                {
                    int volume_idx = get_first_hover_volume_idx();
                    bool already_selected = m_selection.contains_volume(volume_idx);
                    bool shift_down = mouse.shift;

                    Selection::IndicesList curr_idxs = m_selection.get_volume_idxs();

                    if (already_selected && shift_down)
                        m_selection.remove(volume_idx);
                    else
                    {
                        m_selection.add(volume_idx, !shift_down, true);
                        m_mouse.drag.move_requires_threshold = !already_selected;
                        if (already_selected)
                            m_mouse.set_move_start_threshold_position_2D_as_invalid();
                        else
                            m_mouse.drag.move_start_threshold_position_2D = pos;
                    }

                    // propagate event through callback
                    if (curr_idxs != m_selection.get_volume_idxs())
                    {
                        if (m_selection.is_empty())
                            m_gizmos.reset_all_states();
                        else
                            m_gizmos.refresh_on_off_state();

                        m_gizmos.update_data();
                        m_event_poster->postEvent(CanvasEventType::ObjectSelect);
                        m_dirty = true;
                    }
                }
                else if ((mouse.type == MouseEventType::LeftDown))
                {
                    select_bed(s_multiple_beds.get_last_hovered_bed(), true);
                }
            }

            if (!m_hover_volume_idxs.empty() && !m_rectangle_selection.is_dragging())
            {
                if ((mouse.type == MouseEventType::LeftDown) && m_moving_enabled && m_mouse.drag.move_volume_idx == -1)
                {
                    // Only accept the initial position, if it is inside the volume bounding box.
                    const int volume_idx = get_first_hover_volume_idx();
                    BoundingBoxf3 volume_bbox = m_volumes.volumes[volume_idx]->transformed_bounding_box();
                    volume_bbox.offset(1.0);
                    const bool is_cut_connector_selected = m_selection.is_any_connector();
                    if ((!any_gizmo_active || !mouse.cmd) && volume_bbox.contains(m_mouse.scene_position) &&
                        !is_cut_connector_selected)
                    {
                        m_volumes.volumes[volume_idx]->hover = GLVolume::HS_None;
                        // The dragging operation is initiated.
                        m_mouse.drag.move_volume_idx = volume_idx;
                        m_selection.setup_cache();
                        if (!mouse.cmd)
                            m_mouse.drag.start_position_3D = m_mouse.scene_position;
                    }
                }
            }
        }
    }
    else if (mouse.dragging && mouse.left_down && !mouse.cmd && m_layers_editing.state == LayersEditing::Unknown &&
             m_mouse.drag.move_volume_idx != -1 && m_mouse.is_start_position_3D_defined())
    {
        if (!m_mouse.drag.move_requires_threshold)
        {
            static bool was_dragging = false;
            was_dragging = m_mouse.dragging;
            m_mouse.dragging = true;
            Vec3d cur_pos = m_mouse.drag.start_position_3D;
            // we do not want to translate objects if the user just clicked on an object while pressing shift to remove it from the selection and then drag
            if (m_selection.contains_volume(get_first_hover_volume_idx()))
            {
                const Camera &camera = get_camera();
                if (std::abs(camera.get_dir_forward().z()) < EPSILON)
                {
                    // side view -> move selected volumes orthogonally to camera view direction
                    const Linef3 ray = mouse_ray(pos);
                    const Vec3d dir = ray.unit_vector();
                    // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
                    // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
                    // in our case plane normal and ray direction are the same (orthogonal view)
                    // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
                    const Vec3d inters = ray.a +
                                         (m_mouse.drag.start_position_3D - ray.a).dot(dir) / dir.squaredNorm() * dir;
                    // vector from the starting position to the found intersection
                    const Vec3d inters_vec = inters - m_mouse.drag.start_position_3D;

                    const Vec3d camera_right = camera.get_dir_right();
                    const Vec3d camera_up = camera.get_dir_up();

                    // finds projection of the vector along the camera axes
                    const double projection_x = inters_vec.dot(camera_right);
                    const double projection_z = inters_vec.dot(camera_up);

                    // apply offset
                    cur_pos = m_mouse.drag.start_position_3D + projection_x * camera_right + projection_z * camera_up;
                }
                else
                {
                    // Generic view
                    // Get new position at the same Z of the initial click point.
                    cur_pos = mouse_ray(pos).intersect_plane(m_mouse.drag.start_position_3D.z());
                }
            }

            m_moving = true;
            TransformationType trafo_type;
            trafo_type.set_relative();
            m_selection.translate(cur_pos - m_mouse.drag.start_position_3D, trafo_type);
            obj_manipul_set_dirty();
            m_dirty = true;

            const Selection::IndicesList &list = m_selection.get_volume_idxs();
            static bool was_outside = true;
            bool is_outside = std::any_of(list.begin(), list.end(),
                                          [this](unsigned int i) { return m_volumes.volumes[i]->is_outside; });
            if (is_outside && (!was_dragging || !was_outside))
                s_virtual_bed_timer->start(1000, true);
            was_outside = is_outside;
        }
    }
    else if (mouse.dragging && mouse.left_down && m_picking_enabled && m_rectangle_selection.is_dragging())
    {
        // keeps the mouse position updated while dragging the selection rectangle
        m_mouse.position = pos.cast<double>();
        m_rectangle_selection.dragging(m_mouse.position);
        m_dirty = true;
    }
    else if (mouse.dragging)
    {
        m_mouse.dragging = true;

        if (m_layers_editing.state != LayersEditing::Unknown && layer_editing_object_idx != -1)
        {
            if (m_layers_editing.state == LayersEditing::Editing)
            {
                _perform_layer_editing_action(&mouse);
                m_mouse.position = pos.cast<double>();
            }
        }
        // do not process the dragging if the left mouse was set down in another canvas
        else if (mouse.left_down)
        {
            // if dragging over blank area with left button, rotate
            if (!m_moving)
            {
                if ((any_gizmo_active || mouse.cmd || m_hover_volume_idxs.empty()) &&
                    m_mouse.is_start_position_3D_defined())
                {
                    const Vec3d rot = (Vec3d(pos.x(), pos.y(), 0.0) - m_mouse.drag.start_position_3D) *
                                      (PI * TRACKBALLSIZE / 180.0);
                    if (m_app_config->get_bool("use_free_camera"))
                        // Virtual track ball (similar to the 3DConnexion mouse).
                        get_camera().rotate_local_around_target(Vec3d(rot.y(), rot.x(), 0.0));
                    else
                    {
                        // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                        // It is cheaper to call this function right away instead of testing m_mouse3d_controller->connected(),
                        // which checks an atomics (flushes CPU caches).
                        // See GH issue #3816.
                        Camera &camera = get_camera();
                        camera.recover_from_free_camera();
                        camera.rotate_on_sphere(rot.x(), rot.y(), true);
                    }

                    m_dirty = true;
                }
                m_mouse.drag.start_position_3D = Vec3d((double) pos.x(), (double) pos.y(), 0.0);
            }
        }
        else if (mouse.middle_down || mouse.right_down)
        {
            Camera &camera = get_camera();
            // If dragging over blank area with right/middle button, pan.
            if (m_mouse.is_start_position_2D_defined())
            {
                // get point in model space at Z = 0
                const float z = 0.0f;
                const Vec3d cur_pos = _mouse_to_3d(pos, &z, true);
                const Vec3d orig = _mouse_to_3d(m_mouse.drag.start_position_2D, &z, true);
                if (!m_app_config->get_bool("use_free_camera"))
                    // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                    // It is cheaper to call this function right away instead of testing m_mouse3d_controller->connected(),
                    // which checks an atomics (flushes CPU caches).
                    // See GH issue #3816.
                    camera.recover_from_free_camera();

                camera.set_target(m_mouse.drag.camera_start_target + orig - cur_pos);
                m_dirty = true;
            }
            else
            {
                m_mouse.drag.start_position_2D = pos;
                m_mouse.drag.camera_start_target = camera.get_target();
            }
        }
    }
    else if (mouse.is_any_button_up())
    {
        m_mouse.position = pos.cast<double>();

        if ((mouse.type == MouseEventType::RightUp) && m_mouse.is_start_position_2D_defined())
        {
            // forces camera target to be on the plane z = 0
            Camera &camera = get_camera();
            if (std::abs(camera.get_dir_forward().dot(Vec3d::UnitZ())) > EPSILON)
            {
                const Vec3d old_pos = camera.get_position();
                const double old_distance = camera.get_distance();
                const Vec3d old_target = camera.get_target();
                const Linef3 ray(old_pos, old_target);
                const Vec3d new_target = ray.intersect_plane(0.0);
                const BoundingBoxf3 validation_box = camera.get_target_validation_box();
                if (validation_box.contains(new_target))
                {
                    const double new_distance = (new_target - old_pos).norm();
                    camera.set_target(new_target);
                    camera.set_distance(new_distance);
                    if (camera.get_type() == Camera::EType::Perspective)
                        camera.set_zoom(camera.get_zoom() * old_distance / new_distance);
                }
            }
        }

        if (m_layers_editing.state != LayersEditing::Unknown)
        {
            m_layers_editing.state = LayersEditing::Unknown;
            _stop_timer();
            m_layers_editing.accept_changes(*this);
        }
        else if (m_mouse.drag.move_volume_idx != -1 && m_mouse.dragging)
        {
            s_multiple_beds.request_next_bed(false);
            s_virtual_bed_timer->stop();
            do_move(L("Move Object"));
            obj_manipul_set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            m_event_poster->postEvent(CanvasEventType::MouseDraggingFinished);
        }
        else if ((mouse.type == MouseEventType::LeftUp) && m_picking_enabled && m_rectangle_selection.is_dragging())
        {
            if (mouse.shift || mouse.alt)
                _update_selection_from_hover();

            m_rectangle_selection.stop_dragging();
        }
        else if ((mouse.type == MouseEventType::LeftUp) && !m_mouse.ignore_left_up && !m_mouse.dragging &&
                 m_hover_volume_idxs.empty() && !is_layers_editing_enabled())
        {
            // deselect and propagate event through callback
            if (!mouse.shift && (!any_gizmo_active || !mouse.cmd) &&
                m_picking_enabled
#ifndef _WIN32
                // preFlight: GTK/Cocoa deliver phantom LeftUp events to the canvas after popup menus
                // (e.g. Add Shape) close. Only deselect if a genuine LeftDown preceded this LeftUp.
                && m_mouse.left_down_on_canvas
#endif
            )
                deselect_all();
        }
        else if ((mouse.type == MouseEventType::RightUp))
        {
            // forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is already shown
            render();
            if (!m_hover_volume_idxs.empty())
            {
                // if right clicking on volume, propagate event through callback (shows context menu)
                int volume_idx = get_first_hover_volume_idx();
                if (!m_volumes.volumes[volume_idx]->is_wipe_tower() // no context menu for the wipe tower
                    && m_gizmos.get_current_type() !=
                           GLGizmosManager::Measure) // disable context menu when the gizmo is open
                {
                    // forces the selection of the volume
                    /* m_selection.add(volume_idx); // #et_FIXME_if_needed
                     * To avoid extra "Add-Selection" snapshots,
                     * call add() with check_for_already_contained=true
                     * */
                    m_selection.add(volume_idx, true, true);
                    m_gizmos.refresh_on_off_state();
                    m_event_poster->postEvent(CanvasEventType::ObjectSelect);
                    m_gizmos.update_data();
                    obj_manipul_set_dirty();
                    // forces a frame render to update the view before the context menu is shown
                    render();
                }
            }
            Vec2d logical_pos = pos.cast<double>();
#if ENABLE_RETINA_GL
            const float factor = m_retina_helper->get_scale_factor();
            logical_pos = logical_pos.cwiseQuotient(Vec2d(factor, factor));
#endif // ENABLE_RETINA_GL
            if (!m_mouse.dragging)
            {
                // do not post the event if the user is panning the scene
                // or if right click was done over the wipe tower
                const bool post_right_click_event =
                    (m_hover_volume_idxs.empty() ||
                     !m_volumes.volumes[get_first_hover_volume_idx()]->is_wipe_tower()) &&
                    m_gizmos.get_current_type() != GLGizmosManager::Measure;
                if (post_right_click_event)
                    m_event_poster->postEvent(CanvasEventType::RightClick,
                                              std::make_pair(logical_pos, m_hover_volume_idxs.empty()));
            }
        }

        mouse_up_cleanup();
    }
    else if ((mouse.type == MouseEventType::Motion))
    {
        m_mouse.position = pos.cast<double>();

        // updates gizmos overlay
        if (m_selection.is_empty())
            m_gizmos.reset_all_states();

        m_dirty = true;
    }
    else
        mouse.propagate = true;

    // Detection of doubleclick on text to open emboss edit window
    auto type = m_gizmos.get_current_type();
    if ((mouse.type == MouseEventType::LeftDClick) && !m_hover_volume_idxs.empty() &&
        (type == GLGizmosManager::EType::Undefined || type == GLGizmosManager::EType::Move ||
         type == GLGizmosManager::EType::Rotate || type == GLGizmosManager::EType::Scale ||
         type == GLGizmosManager::EType::Emboss || type == GLGizmosManager::EType::Svg))
    {
        for (int hover_volume_id : m_hover_volume_idxs)
        {
            const GLVolume &hover_gl_volume = *m_volumes.volumes[hover_volume_id];
            int object_idx = hover_gl_volume.object_idx();
            if (object_idx < 0 || static_cast<size_t>(object_idx) >= m_model->objects.size())
                continue;
            const ModelObject *hover_object = m_model->objects[object_idx];
            int hover_volume_idx = hover_gl_volume.volume_idx();
            if (hover_volume_idx < 0 || static_cast<size_t>(hover_volume_idx) >= hover_object->volumes.size())
                continue;
            const ModelVolume *hover_volume = hover_object->volumes[hover_volume_idx];

            if (hover_volume->text_configuration.has_value())
            {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type != GLGizmosManager::EType::Emboss)
                    m_gizmos.open_gizmo(GLGizmosManager::EType::Emboss);
                m_event_poster->postEvent(CanvasEventType::UpdateSelections);
                return;
            }
            else if (hover_volume->emboss_shape.has_value())
            {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type != GLGizmosManager::EType::Svg)
                    m_gizmos.open_gizmo(GLGizmosManager::EType::Svg);
                m_event_poster->postEvent(CanvasEventType::UpdateSelections);
                return;
            }
        }
    }

    if (m_moving)
        show_sinking_contours();

#ifdef __WXMSW__
    if (on_enter_workaround)
        m_mouse.position = Vec2d(-1., -1.);
#endif /* __WXMSW__ */
}

void GLCanvas3D::on_paint(wxPaintEvent &evt)
{
    // preFlight: wxPaintDC validates the paint region, preventing WM_PAINT from being
    // re-sent after every SwapBuffers().
    wxPaintDC dc(m_canvas);

    if (!m_initialized)
        // Call render directly, so it gets initialized immediately, not from On Idle handler.
        this->render();
    else
        m_dirty = true;
}

void GLCanvas3D::on_set_focus(wxFocusEvent &evt)
{
    m_tooltip_enabled = false;
    _refresh_if_shown_on_screen();
    m_tooltip_enabled = true;
}

Size GLCanvas3D::get_canvas_size() const
{
    int w = 0;
    int h = 0;

    if (m_surface != nullptr)
    {
        auto [sw, sh] = m_surface->getSize();
        w = sw;
        h = sh;
    }

#if ENABLE_RETINA_GL
    const float factor = m_retina_helper->get_scale_factor();
    w *= factor;
    h *= factor;
#else
    const float factor = 1.0f;
#endif

    return Size(w, h, factor);
}

Vec2d GLCanvas3D::get_local_mouse_position() const
{
    if (m_surface == nullptr)
        return Vec2d::Zero();

    wxPoint global_pos = wxGetMousePosition();
    auto [cx, cy] = m_surface->screenToClient(global_pos.x, global_pos.y);
    const double factor =
#if ENABLE_RETINA_GL
        m_retina_helper->get_scale_factor();
#else
        1.0;
#endif
    return Vec2d(factor * cx, factor * cy);
}

void GLCanvas3D::set_tooltip(const std::string &tooltip)
{
    if (m_canvas != nullptr)
        m_tooltip.set_text(tooltip);
}

void GLCanvas3D::do_move(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        m_event_poster->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_type));

    std::set<std::pair<int, int>> done; // keeps track of modified instances
    bool object_moved = false;
    std::vector<Vec3d> wipe_tower_origin = std::vector<Vec3d>(s_multiple_beds.get_max_beds(), Vec3d::Zero());

    Selection::EMode selection_mode = m_selection.get_mode();
    int vol_id = -1;

    for (const GLVolume *v : m_volumes.volumes)
    {
        ++vol_id;
        int object_idx = v->object_idx();
        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        std::pair<int, int> done_id(object_idx, instance_idx);

        if (0 <= object_idx && object_idx < (int) m_model->objects.size())
        {
            done.insert(done_id);

            // Move instances/volumes
            ModelObject *model_object = m_model->objects[object_idx];
            if (model_object != nullptr)
            {
                if (selection_mode == Selection::Instance)
                    model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                else if (selection_mode == Selection::Volume)
                    model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());

                object_moved = true;
                model_object->invalidate_bounding_box();
            }
        }
        else if (m_selection.is_wipe_tower() && v->is_wipe_tower() && m_selection.contains_volume(vol_id))
        {
            // Move a wipe tower proxy.
            for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx)
            {
                if (v->geometry_id.second == wipe_tower_instance_id(bed_idx).id)
                {
                    wipe_tower_origin[bed_idx] = v->get_volume_offset();
                    break;
                }
            }
        }
    }

    // Fixes flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int> &i : done)
    {
        ModelObject *m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        if (shift_z > SINKING_Z_THRESHOLD)
        {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        m_event_poster->postEvent(CanvasEventType::UpdateInfoItems, id);

    // if the selection is not valid to allow for layer editing after the move, we need to turn off the tool if it is running
    // similar to void Plater::priv::selection_changed()
    if (!can_layers_editing() && is_layers_editing_enabled())
        m_event_poster->postEvent(CanvasEventType::ToolbarLayersEditing);

    if (object_moved)
        m_event_poster->postEvent(CanvasEventType::InstanceMoved);

    if (auto it = std::find_if(wipe_tower_origin.begin(), wipe_tower_origin.end(),
                               [](const Vec3d &pos) { return pos != Vec3d::Zero(); });
        it != wipe_tower_origin.end())
    {
        size_t bed_idx = it - wipe_tower_origin.begin();
        m_model->get_wipe_tower_vector()[bed_idx].position =
            Vec2d((*it)[0] - s_multiple_beds.get_bed_translation(bed_idx).x(),
                  (*it)[1] - s_multiple_beds.get_bed_translation(bed_idx).y());
        m_event_poster->postEvent(CanvasEventType::WipeTowerTouched);
    }

    m_dirty = true;
}

void GLCanvas3D::do_rotate(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        m_event_poster->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i)
    {
        const ModelObject *obj = m_model->objects[i];
        for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j)
        {
            if (snapshot_type == L("Gizmo-Place on Face") && m_selection.get_object_idx() == i)
            {
                // This means we are flattening this object. In that case pretend
                // that it is not sinking (even if it is), so it is placed on bed
                // later on (whatever is sinking will be left sinking).
                min_zs[{i, j}] = SINKING_Z_THRESHOLD;
            }
            else
                min_zs[{i, j}] = obj->instance_bounding_box(j).min.z();
        }
    }

    std::set<std::pair<int, int>> done; // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();
    int v_id = -1;

    for (const GLVolume *v : m_volumes.volumes)
    {
        ++v_id;
        if (v->is_wipe_tower())
        {
            if (m_selection.contains_volume(v_id))
            {
                for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx)
                {
                    if (v->geometry_id.second == wipe_tower_instance_id(bed_idx).id)
                    {
                        const Vec3d offset = v->get_volume_offset() - s_multiple_beds.get_bed_translation(bed_idx);
                        Vec3d rot_unit_x = v->get_volume_transformation().get_matrix().linear() * Vec3d::UnitX();
                        double z_rot = std::atan2(rot_unit_x.y(), rot_unit_x.x());
                        m_model->get_wipe_tower_vector()[bed_idx].position = Vec2d(offset.x(), offset.y());
                        m_model->get_wipe_tower_vector()[bed_idx].rotation = (180. / M_PI) * z_rot;
                        break;
                    }
                }
            }
        }
        const int object_idx = v->object_idx();
        if (object_idx < 0 || (int) m_model->objects.size() <= object_idx)
            continue;

        const int instance_idx = v->instance_idx();
        const int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes.
        ModelObject *model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int> &i : done)
    {
        ModelObject *m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.find({i.first, i.second})->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD)
        {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }

        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        m_event_poster->postEvent(CanvasEventType::UpdateInfoItems, id);

    if (!done.empty())
        m_event_poster->postEvent(CanvasEventType::InstanceRotated);

    m_dirty = true;
}

void GLCanvas3D::do_scale(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        m_event_poster->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty())
    {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i)
        {
            const ModelObject *obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j)
            {
                min_zs[{i, j}] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done; // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume *v : m_volumes.volumes)
    {
        const int object_idx = v->object_idx();
        if (object_idx < 0 || (int) m_model->objects.size() <= object_idx)
            continue;

        const int instance_idx = v->instance_idx();
        const int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes
        ModelObject *model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
            {
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            }
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int> &i : done)
    {
        ModelObject *m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({i.first, i.second})->second >= SINKING_Z_THRESHOLD ||
            shift_z > SINKING_Z_THRESHOLD)
        {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        m_event_poster->postEvent(CanvasEventType::UpdateInfoItems, id);

    if (!done.empty())
        m_event_poster->postEvent(CanvasEventType::InstanceScaled);

    m_dirty = true;
}

void GLCanvas3D::do_mirror(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        m_event_poster->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty())
    {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i)
        {
            const ModelObject *obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j)
            {
                min_zs[{i, j}] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done; // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume *v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int) m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject *model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int> &i : done)
    {
        ModelObject *m = m_model->objects[i.first];
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({i.first, i.second})->second >= SINKING_Z_THRESHOLD ||
            shift_z > SINKING_Z_THRESHOLD)
        {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        m_event_poster->postEvent(CanvasEventType::UpdateInfoItems, id);

    m_event_poster->postEvent(CanvasEventType::InstanceMirrored);

    m_dirty = true;
}

void GLCanvas3D::do_reset_skew(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        m_event_poster->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty())
    {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i)
        {
            const ModelObject *obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j)
            {
                min_zs[{i, j}] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done; // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume *v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int) m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject *model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int> &i : done)
    {
        ModelObject *m = m_model->objects[i.first];
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({i.first, i.second})->second >= SINKING_Z_THRESHOLD ||
            shift_z > SINKING_Z_THRESHOLD)
        {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        m_event_poster->postEvent(CanvasEventType::UpdateInfoItems, id);

    m_event_poster->postEvent(CanvasEventType::ResetSkew);

    m_dirty = true;
}

void GLCanvas3D::update_gizmos_on_off_state()
{
    set_as_dirty();
    m_gizmos.update_data();
    m_gizmos.refresh_on_off_state();
}

void GLCanvas3D::handle_sidebar_focus_event(const std::string &opt_key, bool focus_on)
{
    m_sidebar_field = focus_on ? opt_key : "";
    if (!m_sidebar_field.empty())
        m_gizmos.reset_all_states();

    m_dirty = true;
}

void GLCanvas3D::handle_layers_data_focus_event(const t_layer_height_range range, const EditorType type)
{
    std::string field = "layer_" + std::to_string(type) + "_" + float_to_string_decimal_point(range.first) + "_" +
                        float_to_string_decimal_point(range.second);
    handle_sidebar_focus_event(field, true);
}

void GLCanvas3D::update_ui_from_settings()
{
    m_dirty = true;

#if __APPLE__
    // Update OpenGL scaling on OSX after the user toggled the "use_retina_opengl" settings in Preferences dialog.
    const float orig_scaling = m_retina_helper->get_scale_factor();

    const bool use_retina = m_app_config->get_bool("use_retina_opengl");
    BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Use Retina OpenGL: " << use_retina;
    m_retina_helper->set_use_retina(use_retina);
    const float new_scaling = m_retina_helper->get_scale_factor();

    if (new_scaling != orig_scaling)
    {
        BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Scaling factor: " << new_scaling;

        Camera &camera = get_camera();
        camera.set_zoom(camera.get_zoom() * new_scaling / orig_scaling);
        _refresh_if_shown_on_screen();
    }
#endif // ENABLE_RETINA_GL

    if (is_editor())
        m_collapse_toolbar->set_enabled(m_app_config->get_bool("show_collapse_button") || is_sidebar_collapsed());
}

std::vector<GLCanvas3D::WipeTowerInfo> GLCanvas3D::get_wipe_tower_infos() const
{
    std::vector<WipeTowerInfo> result;

    for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx)
    {
        if (m_wipe_tower_bounding_boxes[bed_idx])
        {
            const ModelWipeTower &wipe_tower{m_model->wipe_tower(bed_idx)};
            WipeTowerInfo wti;
            wti.m_pos = Vec2d(wipe_tower.position.x(), wipe_tower.position.y());
            wti.m_rotation = (M_PI / 180.) * wipe_tower.rotation;
            wti.m_bb = *m_wipe_tower_bounding_boxes[bed_idx];
            wti.m_bed_index = bed_idx;
            result.push_back(std::move(wti));
        }
    }

    return result;
}

Linef3 GLCanvas3D::mouse_ray(const Point &mouse_pos)
{
    float z0 = 0.0f;
    float z1 = 1.0f;
    return Linef3(_mouse_to_3d(mouse_pos, &z0), _mouse_to_3d(mouse_pos, &z1));
}

double GLCanvas3D::get_size_proportional_to_max_bed_size(double factor) const
{
    const BoundingBoxf &bbox = m_bed.build_volume().bounding_volume2d();
    return factor * std::max(bbox.size()[0], bbox.size()[1]);
}

void GLCanvas3D::set_cursor(CursorType type)
{
    if ((m_surface != nullptr) && (m_cursor_type != type))
    {
        m_surface->setCursor(type);
        m_cursor_type = type;
    }
}

void GLCanvas3D::update_tooltip_for_settings_item_in_main_toolbar()
{
    std::string new_tooltip = _u8L("Switch to Settings") + "\n" + "[" + GUI::shortkey_ctrl_prefix() + "2] - " +
                              _u8L("Print Settings Tab") + "\n" + "[" + GUI::shortkey_ctrl_prefix() + "3] - " +
                              (current_printer_technology() == ptFFF ? _u8L("Filament Settings Tab")
                                                                     : _u8L("Material Settings Tab")) +
                              "\n" + "[" + GUI::shortkey_ctrl_prefix() + "4] - " + _u8L("Printer Settings Tab");

    m_main_toolbar.set_tooltip(get_main_toolbar_item_id("settings"), new_tooltip);
}

void GLCanvas3D::mouse_up_cleanup()
{
    m_moving = false;
    m_mouse.drag.move_volume_idx = -1;
    m_mouse.set_start_position_3D_as_invalid();
    m_mouse.set_start_position_2D_as_invalid();
    m_mouse.dragging = false;

    m_mouse.ignore_left_up = false;
#ifndef _WIN32
    m_mouse.left_down_on_canvas = false;
#endif
    m_dirty = true;

    if (m_surface && m_surface->hasCapture())
        m_surface->releaseMouse();

    // preFlight: Flush the GPU pipeline and release the GL context after a drag ends
    // (power-saving only). glFinish() completes all pending GPU work. Then releasing
    // the context via wglMakeCurrent(NULL, NULL) tells the GPU driver we're done with
    // GL entirely, allowing it to drop to an idle power state. Without this, the driver
    // keeps the GPU clocked high expecting more work, even though we're rendering 0 frames.
    // The context is re-acquired automatically by _set_current() before the next render.
    if (is_gpu_power_saving())
    {
        glsafe(::glFinish());
#ifdef _WIN32
        wglMakeCurrent(NULL, NULL);
#endif
    }
}

bool GLCanvas3D::is_gpu_power_saving() const
{
    // Power saving only when Preview is shown AND slicing is not in progress.
    // During slicing, the shell progress animation and notification progress bar
    // need continuous rendering for smooth updates.
    return is_preview_shown() && !is_slicing();
}

bool GLCanvas3D::is_object_sinking(int object_idx) const
{
    for (const GLVolume *v : m_volumes.volumes)
    {
        if (v->object_idx() == object_idx && (v->is_sinking() || (!v->is_modifier && v->is_below_printbed())))
            return true;
    }
    return false;
}

void GLCanvas3D::release_gl_context()
{
    if (m_surface)
        m_surface->releaseCurrent();
}

void GLCanvas3D::apply_retina_scale(Vec2d &screen_coordinate) const
{
#if ENABLE_RETINA_GL
    double scale = static_cast<double>(m_retina_helper->get_scale_factor());
    screen_coordinate *= scale;
#endif // ENABLE_RETINA_GL
}

std::pair<SlicingParameters, const std::vector<double>> GLCanvas3D::get_layers_height_data(int object_id)
{
    m_layers_editing.select_object(*m_model, object_id);
    std::pair<SlicingParameters, const std::vector<double>> ret = m_layers_editing.get_layers_height_data();
    m_layers_editing.select_object(*m_model, -1);
    return ret;
}

void GLCanvas3D::detect_sla_view_type()
{
    m_sla_view.detect_type_from_volumes(m_volumes.volumes);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

void GLCanvas3D::set_sla_view_type(ESLAViewType type)
{
    m_sla_view.set_type(type);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

void GLCanvas3D::set_sla_view_type(const GLVolume::CompositeID &id, ESLAViewType type)
{
    m_sla_view.set_type(id, type);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

bool GLCanvas3D::_is_shown_on_screen() const
{
    return m_surface != nullptr && m_surface->isShownOnScreen();
}

static std::function<bool(bool, int, const char **)> s_undo_redo_string_getter;
static bool string_getter(const bool is_undo, int idx, const char **out_text)
{
    return s_undo_redo_string_getter(is_undo, idx, out_text);
}

bool GLCanvas3D::_render_undo_redo_stack(const bool is_undo, float pos_x)
{
    bool action_taken = false;

    ImGuiPureWrap::set_next_window_pos(pos_x, m_undoredo_toolbar.get_height(), ImGuiCond_Always, 0.5f, 0.0f);
    std::string title = is_undo ? _u8L("Undo History") : _u8L("Redo History");
    ImGuiPureWrap::begin(title, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    int hovered = m_imgui_undo_redo_hovered_pos;
    int selected = -1;
    float em = static_cast<float>(em_unit());
#if ENABLE_RETINA_GL
    em *= m_retina_helper->get_scale_factor();
#endif

    s_undo_redo_string_getter = m_undo_redo_string_getter;
    if (ImGuiPureWrap::undo_redo_list(ImVec2(18 * em, 26 * em), is_undo, &string_getter, hovered, selected,
                                      m_mouse_wheel))
        m_imgui_undo_redo_hovered_pos = hovered;
    else
        m_imgui_undo_redo_hovered_pos = -1;

    if (selected >= 0)
    {
        is_undo ? undo_to(selected) : redo_to(selected);
        action_taken = true;
    }

    ImGuiPureWrap::text(GUI::format(is_undo ? _L_PLURAL("Undo %1$d Action", "Undo %1$d Actions", hovered + 1)
                                            : _L_PLURAL("Redo %1$d Action", "Redo %1$d Actions", hovered + 1),
                                    hovered + 1));

    ImGuiPureWrap::end();

    return action_taken;
}

bool GLCanvas3D::_render_arrange_menu(float pos_x, bool current_bed)
{
    m_arrange_settings_dialog.render(pos_x, m_main_toolbar.get_height(), current_bed);
    return true;
}

void GLCanvas3D::_render_thumbnail_internal(ThumbnailData &thumbnail_data, const ThumbnailsParams &thumbnail_params,
                                            const GLVolumeCollection &volumes, Camera::EType camera_type)
{
    auto is_visible = [](const GLVolume &v)
    {
        bool ret = v.printable;
        ret &= (!v.shader_outside_printer_detection_enabled || !v.is_outside);
        return ret;
    };

    GLVolumePtrs visible_volumes;

    for (GLVolume *vol : volumes.volumes)
    {
        if (!vol->is_modifier && !vol->is_wipe_tower() &&
            (!thumbnail_params.parts_only || vol->composite_id.volume_id >= 0))
        {
            if (!thumbnail_params.printable_only || is_visible(*vol))
            {
                if (s_multiple_beds.is_glvolume_on_thumbnail_bed(*m_model, vol->composite_id.object_id,
                                                                 vol->composite_id.instance_id))
                    visible_volumes.emplace_back(vol);
            }
        }
    }

    BoundingBoxf3 volumes_box;
    if (!visible_volumes.empty())
    {
        for (const GLVolume *vol : visible_volumes)
        {
            volumes_box.merge(vol->transformed_bounding_box());
        }
    }
    else
        // This happens for empty projects
        volumes_box = m_bed.extended_bounding_box();

    Camera camera;
    camera.set_type(camera_type);
    camera.set_scene_box(scene_bounding_box());
    camera.set_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);
    camera.apply_viewport();
    camera.zoom_to_box(volumes_box);

    const Transform3d &view_matrix = camera.get_view_matrix();

    double near_z = -1.0;
    double far_z = -1.0;

    if (thumbnail_params.show_bed)
    {
        // extends the near and far z of the frustrum to avoid the bed being clipped

        // box in eye space
        BoundingBoxf3 t_bed_box = m_bed.extended_bounding_box();
        if (s_multiple_beds.get_thumbnail_bed_idx() != -1)
        {
            BoundingBoxf3 bed_bb = m_bed.build_volume().bounding_volume();
            bed_bb.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_thumbnail_bed_idx()));
            t_bed_box.merge(bed_bb);
        }
        t_bed_box = t_bed_box.transformed(view_matrix);
        near_z = -t_bed_box.max.z();
        far_z = -t_bed_box.min.z();
    }

    camera.apply_projection(volumes_box, near_z, far_z);

    const ModelObjectPtrs &model_objects = m_model->objects;
    std::vector<ColorRGBA> extruders_colors = get_extruder_colors_from_plater_config();
    const bool is_enabled_painted_thumbnail = !model_objects.empty() && !extruders_colors.empty();

    if (thumbnail_params.transparent_background)
    {
        // Use themed canvas background color for transparent thumbnails
        const ColorRGBA bg = get_bg_color(dark_mode());
        glsafe(::glClearColor(bg.r(), bg.g(), bg.b(), 0.0f));
    }

    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glCullFace(GL_BACK));

    const Transform3d &projection_matrix = camera.get_projection_matrix();

    // Build a per-volume painted palette for any real object volume. State 0 always renders
    // as default_color; state s (1-based) looks up palette[s-1]. Covers three cases:
    //   - unpainted volume: empty palette, every triangle is state 0 -> default color
    //   - MMU painted: palette = extruder filament colors, state == extruder index
    //   - color mixing painted: palette = per-volume recipe rgb (already resolved)
    // If both paint types are present, color_mixing wins (active gizmo's output).
    auto build_painted_palette = [&extruders_colors](const ModelVolume &mv) -> std::vector<ColorRGBA>
    {
        if (!mv.color_mixing_facets.empty() && !mv.color_mixing_palette.empty())
        {
            std::vector<ColorRGBA> palette;
            palette.reserve(mv.color_mixing_palette.size());
            for (const ColorMixingRecipe &r : mv.color_mixing_palette)
            {
                palette.emplace_back(float((r.rgb >> 16) & 0xFF) / 255.f, float((r.rgb >> 8) & 0xFF) / 255.f,
                                     float(r.rgb & 0xFF) / 255.f, 1.0f);
            }
            return palette;
        }
        if (!mv.mm_segmentation_facets.empty())
            return extruders_colors;
        return {};
    };

    const int extruders_count = extruders_edited_cnt();
    for (GLVolume *vol : visible_volumes)
    {
        const int obj_idx = vol->object_idx();
        const int vol_idx = vol->volume_idx();
        const bool has_model = is_enabled_painted_thumbnail && obj_idx >= 0 && vol_idx >= 0 &&
                               model_objects[obj_idx]->volumes[vol_idx]->is_model_part();

        // Real object volumes always go through the painted path -- mm_gouraud handles both
        // unpainted (single-color) and painted cases uniformly. Wipe tower and modifier
        // volumes (obj_idx < 0 or non-model-part) fall through to vol->render().
        GLShaderProgram *shader = get_shader(has_model ? "mm_gouraud" : "gouraud_light");
        if (shader == nullptr)
            continue;

        shader->start_using();
        const std::array<float, 4> clp_data = {0.0f, 0.0f, 1.0f, FLT_MAX};
        const std::array<float, 2> z_range = {-FLT_MAX, FLT_MAX};
        const bool is_left_handed = vol->is_left_handed();
        if (has_model)
        {
            shader->set_uniform("volume_world_matrix", vol->world_matrix());
            shader->set_uniform("volume_mirrored", is_left_handed);
            shader->set_uniform("clipping_plane", clp_data);
            shader->set_uniform("z_range", z_range);
        }
        else
        {
            shader->set_uniform("emission_factor", 0.0f);
            vol->model.set_color((vol->printable && !vol->is_outside) ? vol->color : ColorRGBA::GRAY());
        }

        // the volume may have been deactivated by an active gizmo
        const bool is_active = vol->is_active;
        vol->is_active = true;
        const Transform3d model_matrix = vol->world_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                            model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        if (has_model)
        {
            const ModelVolume &mv = *model_objects[obj_idx]->volumes[vol_idx];
            const size_t extruder_idx = ModelVolume::get_extruder_color_idx(mv, extruders_count);
            const std::vector<ColorRGBA> palette = build_painted_palette(mv);
            // preFlight: a single-color (unpainted) object renders in the themed "selected" accent color so
            // the thumbnail matches the active theme. Painted/MMU objects (non-empty palette) keep their
            // per-region painted colors, including their unpainted base filament color.
            const ColorRGBA default_color = palette.empty() ? GLVolume::selected_color()
                                                            : ((vol->printable && !vol->is_outside &&
                                                                extruder_idx < extruders_colors.size())
                                                                   ? extruders_colors[extruder_idx]
                                                                   : ColorRGBA::GRAY());
            TriangleSelectorMmGui ts(mv.mesh(), palette, default_color);
            // Prefer color_mixing_facets when present (active painting wins over legacy MMU).
            if (!mv.color_mixing_facets.empty() && !mv.color_mixing_palette.empty())
                ts.deserialize(mv.color_mixing_facets.get_data(), true);
            else if (!mv.mm_segmentation_facets.empty())
                ts.deserialize(mv.mm_segmentation_facets.get_data(), true);
            ts.request_update_render_data();
            ts.set_shader_getters([this](const std::string &name) { return get_shader(name); },
                                  [this]() { return get_current_shader(); });
            ts.render(nullptr, model_matrix, get_camera());
        }
        else
            vol->render();

        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));

        shader->stop_using();

        vol->is_active = is_active;
    }

    glsafe(::glDisable(GL_DEPTH_TEST));

    if (thumbnail_params.show_bed)
        _render_bed(view_matrix, projection_matrix, !camera.is_looking_downward());

    // restore background color
    if (thumbnail_params.transparent_background)
        glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
}

void GLCanvas3D::_render_thumbnail_framebuffer(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                               const ThumbnailsParams &thumbnail_params,
                                               const GLVolumeCollection &volumes, Camera::EType camera_type)
{
    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    const bool multisample = ::glIsEnabled(GL_MULTISAMPLE);
    glcheck();
    if (m_multisample_allowed && !multisample)
        glsafe(::glEnable(GL_MULTISAMPLE));

    GLint max_samples;
    glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
    GLsizei num_samples = max_samples / 2;

    GLuint render_fbo;
    glsafe(::glGenFramebuffers(1, &render_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));

    GLuint render_tex = 0;
    GLuint render_tex_buffer = 0;
    if (multisample)
    {
        // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
        glsafe(::glGenRenderbuffers(1, &render_tex_buffer));
        glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_tex_buffer));
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_RGBA8, w, h));
        glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_tex_buffer));
    }
    else
    {
        glsafe(::glGenTextures(1, &render_tex));
        glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_tex, 0));
    }

    GLuint render_depth;
    glsafe(::glGenRenderbuffers(1, &render_depth));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
    if (multisample)
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_DEPTH_COMPONENT24, w, h));
    else
        glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h));

    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));

    GLenum drawBufs[] = {GL_COLOR_ATTACHMENT0};
    glsafe(::glDrawBuffers(1, drawBufs));

    if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        _render_thumbnail_internal(thumbnail_data, thumbnail_params, volumes, camera_type);

        if (multisample)
        {
            GLuint resolve_fbo;
            glsafe(::glGenFramebuffers(1, &resolve_fbo));
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo));

            GLuint resolve_tex;
            glsafe(::glGenTextures(1, &resolve_tex));
            glsafe(::glBindTexture(GL_TEXTURE_2D, resolve_tex));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0));

            glsafe(::glDrawBuffers(1, drawBufs));

            if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            {
                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, render_fbo));
                glsafe(::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo));
                glsafe(::glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo));
                glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *) thumbnail_data.pixels.data()));
            }

            glsafe(::glDeleteTextures(1, &resolve_tex));
            glsafe(::glDeleteFramebuffers(1, &resolve_fbo));
        }
        else
            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *) thumbnail_data.pixels.data()));
    }

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glDeleteRenderbuffers(1, &render_depth));
    if (render_tex_buffer != 0)
        glsafe(::glDeleteRenderbuffers(1, &render_tex_buffer));
    if (render_tex != 0)
        glsafe(::glDeleteTextures(1, &render_tex));
    glsafe(::glDeleteFramebuffers(1, &render_fbo));

    if (!multisample)
        glsafe(::glDisable(GL_MULTISAMPLE));
}

void GLCanvas3D::_render_thumbnail_framebuffer_ext(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                                   const ThumbnailsParams &thumbnail_params,
                                                   const GLVolumeCollection &volumes, Camera::EType camera_type)
{
    // This path should never be reached - OpenGL 3.2+ always has ARB framebuffer
    assert(false && "EXT framebuffer path called - should not happen with OpenGL 3.2+");
    thumbnail_data.set(w, h);
}

void GLCanvas3D::_render_thumbnail_legacy(ThumbnailData &thumbnail_data, unsigned int w, unsigned int h,
                                          const ThumbnailsParams &thumbnail_params, const GLVolumeCollection &volumes,
                                          Camera::EType camera_type)
{
    // check that thumbnail size does not exceed the default framebuffer size
    const Size &cnv_size = get_canvas_size();
    unsigned int cnv_w = (unsigned int) cnv_size.get_width();
    unsigned int cnv_h = (unsigned int) cnv_size.get_height();
    if (w > cnv_w || h > cnv_h)
    {
        float ratio = std::min((float) cnv_w / (float) w, (float) cnv_h / (float) h);
        w = (unsigned int) (ratio * (float) w);
        h = (unsigned int) (ratio * (float) h);
    }

    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    _render_thumbnail_internal(thumbnail_data, thumbnail_params, volumes, camera_type);

    glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *) thumbnail_data.pixels.data()));

    // restore the default framebuffer size to avoid flickering on the 3D scene
    get_camera().apply_viewport();
}

bool GLCanvas3D::_init_toolbars()
{
    if (!_init_main_toolbar())
        return false;

    if (!_init_undoredo_toolbar())
        return false;

    if (!_init_view_toolbar())
        return false;

    if (!_init_collapse_toolbar())
        return false;

    return true;
}

bool GLCanvas3D::_init_main_toolbar()
{
    //if (!m_main_toolbar.is_enabled())
    //    return true;

    BackgroundTexture::Metadata background_data;
    background_data.filename = "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!m_main_toolbar.init(background_data))
    {
        // unable to init the toolbar texture, disable it
        m_main_toolbar.set_enabled(false);
        return true;
    }
    // init arrow
    if (!m_main_toolbar.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Main toolbar failed to load arrow texture.";

    // m_gizmos is created at constructor, thus we can init arrow here.
    if (!m_gizmos.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Gizmos manager failed to load arrow texture.";

    //    m_main_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    m_main_toolbar.set_layout_type(GLToolbar::Layout::Horizontal);
    m_main_toolbar.set_horizontal_orientation(GLToolbar::Layout::HO_Right);
    m_main_toolbar.set_vertical_orientation(GLToolbar::Layout::VO_Top);
    //m_main_toolbar.set_border(5.0f);
    //m_main_toolbar.set_separator_size(5.f);
    //m_main_toolbar.set_gap_size(5.f);

    GLToolbarItem::Data item;

    unsigned int sprite_id = 0;

    item.name = "add";
    item.icon_filename = "add.svg";
    item.tooltip = _u8L("Add...") + " [" + GUI::shortkey_ctrl_prefix() + "I]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarAdd);
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "delete";
    item.icon_filename = "remove.svg";
    item.tooltip = _u8L("Delete") + " [Del]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarDelete);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_delete && m_can_delete();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "deleteall";
    item.icon_filename = "delete_all.svg";
    item.tooltip = _u8L("Delete all") + " [" + GUI::shortkey_ctrl_prefix() + "Del]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarDeleteAll);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_delete_all && m_can_delete_all();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "arrange";
    item.icon_filename = "arrange.svg";
    item.tooltip = _u8L("Arrange") + " [A]\n" + _u8L("Arrange selection") + " [Shift+A]\n" +
                   _u8L("Click right mouse button to show arrangement options");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarArrange);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_arrange && m_can_arrange();
    };
    item.right.toggable = true;
    item.right.render_callback = [this](float left, float right, float, float)
    {
        if (m_canvas != nullptr)
            _render_arrange_menu(0.5f * (left + right), false);
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "arrangecurrent";
    item.icon_filename = "arrange_current.svg";
    item.tooltip = _u8L("Arrange current bed") + " [D]\n" + _u8L("Arrange selection on current bed") + " [Shift+D]\n";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarArrangeCurrentBed);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_arrange && m_can_arrange();
    };
    item.right.toggable = true;
    item.right.render_callback = [this](float left, float right, float, float)
    {
        if (m_canvas != nullptr)
            _render_arrange_menu(0.5f * (left + right), true);
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.right.toggable = false;
    item.right.render_callback = GLToolbarItem::Default_Render_Callback;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "copy";
    item.icon_filename = "copy.svg";
    item.tooltip = _u8L("Copy") + " [" + GUI::shortkey_ctrl_prefix() + "C]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarCopy);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_copy_to_clipboard && m_can_copy_to_clipboard();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "paste";
    item.icon_filename = "paste.svg";
    item.tooltip = _u8L("Paste") + " [" + GUI::shortkey_ctrl_prefix() + "V]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarPaste);
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_paste_from_clipboard && m_can_paste_from_clipboard();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "more";
    item.icon_filename = "instance_add.svg";
    item.tooltip = _u8L("Add instance") + " [+]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarMore);
    };
    item.visibility_callback = [this]() -> bool
    {
        return get_mode() != comSimple;
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_increase_instances && m_can_increase_instances();
    };

    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "fewer";
    item.icon_filename = "instance_remove.svg";
    item.tooltip = _u8L("Remove instance") + " [-]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarFewer);
    };
    item.visibility_callback = [this]() -> bool
    {
        return get_mode() != comSimple;
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_decrease_instances && m_can_decrease_instances();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "splitobjects";
    item.icon_filename = "split_objects.svg";
    item.tooltip = _u8L("Split to objects");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarSplitObjects);
    };
    item.visibility_callback = GLToolbarItem::Default_Visibility_Callback;
    item.enabling_callback = [this]() -> bool
    {
        return m_can_split_to_objects && m_can_split_to_objects();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "splitvolumes";
    item.icon_filename = "split_parts.svg";
    item.tooltip = _u8L("Split to parts");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarSplitVolumes);
    };
    item.visibility_callback = [this]() -> bool
    {
        return get_mode() != comSimple;
    };
    item.enabling_callback = [this]() -> bool
    {
        return m_can_split_to_volumes && m_can_split_to_volumes();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;
    /*
    item.name = "settings";
    item.icon_filename = "settings.svg";
    item.tooltip = _u8L("Switch to Settings") + "\n" + "[" + GUI::shortkey_ctrl_prefix() + "2] - " + _u8L("Print Settings Tab")    + 
                                                "\n" + "[" + GUI::shortkey_ctrl_prefix() + "3] - " + (current_printer_technology() == ptFFF ? _u8L("Filament Settings Tab") : _u8L("Material Settings Tab") +
                                                "\n" + "[" + GUI::shortkey_ctrl_prefix() + "4] - " + _u8L("Printer Settings Tab")) ;
    item.sprite_id = sprite_id++;
    item.enabling_callback    = GLToolbarItem::Default_Enabling_Callback;
    item.visibility_callback  = [this]() { return m_app_config->get_bool("dlg_settings_layout_mode"); };
    item.left.action_callback = [this]() { m_event_poster->postEvent(CanvasEventType::Tab); };
    if (!m_main_toolbar.add_item(item))
        return false;
*/
    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "brimears";
    item.icon_filename = "brim_ears.svg";
    item.tooltip = _u8L("Brim Ears") + " [E]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        // Close Variable Layer Height if it's open
        if (is_layers_editing_enabled())
        {
            enable_layers_editing(false);
        }
        m_gizmos.open_gizmo(GLGizmosManager::EType::BrimEars);
    };
    item.visibility_callback = [this]() -> bool
    {
        return current_printer_technology() == ptFFF;
    };
    item.enabling_callback = [this]() -> bool
    {
        const Selection &selection = m_selection;
        return selection.is_single_full_instance();
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "layersediting";
    item.icon_filename = "layers_white.svg";
    item.tooltip = _u8L("Variable layer height");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        if (m_canvas != nullptr)
            m_event_poster->postEvent(CanvasEventType::ToolbarLayersEditing);
    };
    item.visibility_callback = [this]() -> bool
    {
        bool res = current_printer_technology() == ptFFF;
        // turns off if changing printer technology
        if (!res && m_main_toolbar.is_item_visible("layersediting") && m_main_toolbar.is_item_pressed("layersediting"))
            force_main_toolbar_left_action(get_main_toolbar_item_id("layersediting"));

        return res;
    };
    item.enabling_callback = [this]() -> bool
    {
        return can_layers_editing();
    };
    item.left.render_callback = GLToolbarItem::Default_Render_Callback;
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.generate_icons_texture())
        return false;

    return true;
}

bool GLCanvas3D::_init_undoredo_toolbar()
{
    //if (!m_undoredo_toolbar.is_enabled())
    //    return true;

    BackgroundTexture::Metadata background_data;
    background_data.filename = "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!m_undoredo_toolbar.init(background_data))
    {
        // unable to init the toolbar texture, disable it
        m_undoredo_toolbar.set_enabled(false);
        return true;
    }

    // init arrow
    if (!m_undoredo_toolbar.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Undo/Redo toolbar failed to load arrow texture.";

    //    m_undoredo_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    m_undoredo_toolbar.set_layout_type(GLToolbar::Layout::Horizontal);
    m_undoredo_toolbar.set_horizontal_orientation(GLToolbar::Layout::HO_Left);
    m_undoredo_toolbar.set_vertical_orientation(GLToolbar::Layout::VO_Top);
    //m_undoredo_toolbar.set_border(5.f);
    //m_undoredo_toolbar.set_separator_size(5.f);
    //m_undoredo_toolbar.set_gap_size(5.f);

    GLToolbarItem::Data item;

    unsigned int sprite_id = 0;

    item.name = "measure";
    item.icon_filename = "measure.svg";
    item.tooltip = _u8L("Measure") + " [M]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        // Close Variable Layer Height if it's open
        if (is_layers_editing_enabled())
        {
            enable_layers_editing(false);
        }
        m_gizmos.open_gizmo(GLGizmosManager::EType::Measure);
    };
    item.visibility_callback = GLToolbarItem::Default_Visibility_Callback;
    item.enabling_callback = [this]() -> bool
    {
        const Selection &selection = m_selection;
        return selection.is_single_full_instance() || selection.is_single_volume() || selection.is_single_modifier();
    };
    if (!m_undoredo_toolbar.add_item(item))
        return false;

    if (!m_undoredo_toolbar.add_separator())
        return false;

    item.name = "undo";
    item.icon_filename = "undo_toolbar.svg";
    item.tooltip = _u8L("Undo") + " [" + GUI::shortkey_ctrl_prefix() + "Z]\n" +
                   _u8L("Click right mouse button to open/close History");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        m_event_poster->postEvent(CanvasEventType::Undo);
    };
    item.right.toggable = true;
    item.right.action_callback = [this]()
    {
        m_imgui_undo_redo_hovered_pos = -1;
    };
    item.right.render_callback = [this](float left, float right, float, float)
    {
        if (m_canvas != nullptr)
        {
            if (_render_undo_redo_stack(true, 0.5f * (left + right)))
                _deactivate_undo_redo_toolbar_items();
        }
    };
    item.enabling_callback = [this]() -> bool
    {
        bool _can_undo = can_undo();
        int id = m_undoredo_toolbar.get_item_id("undo");

        std::string curr_additional_tooltip;
        m_undoredo_toolbar.get_additional_tooltip(id, curr_additional_tooltip);

        std::string new_additional_tooltip;
        if (_can_undo)
        {
            std::string action;
            undo_redo_topmost_string_getter(true, action);
            new_additional_tooltip = format(_L("Next Undo action: %1%"), action);
        }

        if (new_additional_tooltip != curr_additional_tooltip)
        {
            m_undoredo_toolbar.set_additional_tooltip(id, new_additional_tooltip);
            set_tooltip("");
        }
        return _can_undo;
    };

    if (!m_undoredo_toolbar.add_item(item))
        return false;

    item.name = "redo";
    item.icon_filename = "redo_toolbar.svg";
    item.tooltip = _u8L("Redo") + " [" + GUI::shortkey_ctrl_prefix() + "Y]\n" +
                   _u8L("Click right mouse button to open/close History");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]()
    {
        m_event_poster->postEvent(CanvasEventType::Redo);
    };
    item.right.action_callback = [this]()
    {
        m_imgui_undo_redo_hovered_pos = -1;
    };
    item.right.render_callback = [this](float left, float right, float, float)
    {
        if (m_canvas != nullptr)
        {
            if (_render_undo_redo_stack(false, 0.5f * (left + right)))
                _deactivate_undo_redo_toolbar_items();
        }
    };
    item.enabling_callback = [this]() -> bool
    {
        bool _can_redo = can_redo();
        int id = m_undoredo_toolbar.get_item_id("redo");

        std::string curr_additional_tooltip;
        m_undoredo_toolbar.get_additional_tooltip(id, curr_additional_tooltip);

        std::string new_additional_tooltip;
        if (_can_redo)
        {
            std::string action;
            undo_redo_topmost_string_getter(false, action);
            new_additional_tooltip = format(_L("Next Redo action: %1%"), action);
        }

        if (new_additional_tooltip != curr_additional_tooltip)
        {
            m_undoredo_toolbar.set_additional_tooltip(id, new_additional_tooltip);
            set_tooltip("");
        }
        return _can_redo;
    };

    if (!m_undoredo_toolbar.add_item(item))
        return false;
    /*
    if (!m_undoredo_toolbar.add_separator())
        return false;
        */

    if (!m_undoredo_toolbar.generate_icons_texture())
        return false;

    return true;
}

bool GLCanvas3D::_init_view_toolbar()
{
    return m_init_view_toolbar ? m_init_view_toolbar() : true;
}

bool GLCanvas3D::_init_collapse_toolbar()
{
    return m_init_collapse_toolbar ? m_init_collapse_toolbar() : true;
}

bool GLCanvas3D::_set_current()
{
    return m_surface != nullptr && m_surface->makeCurrent();
}

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if (m_surface == nullptr)
        return;

    const std::array<unsigned int, 2> new_size = {w, h};
    if (m_old_size == new_size)
        return;

    m_old_size = new_size;

    auto *imgui = get_imgui();
    ImGuiPureWrap::set_display_size(static_cast<float>(w), static_cast<float>(h));
    const float font_size = 1.5f * em_unit();
#if ENABLE_RETINA_GL
    imgui->set_scaling(font_size, 1.0f, m_retina_helper->get_scale_factor());
#else
    imgui->set_scaling(font_size, m_surface->getScaleFactor(), 1.0f);
#endif

    this->request_extra_frame();

    // ensures that this canvas is current
    _set_current();
}

BoundingBoxf3 GLCanvas3D::_max_bounding_box(bool include_bed_model) const
{
    const bool is_preview = is_preview_shown();

    BoundingBoxf3 bb = volumes_bounding_box();

    // The following is a workaround for gizmos not being taken in account when calculating the tight camera frustrum
    // A better solution would ask the gizmo manager for the bounding box of the current active gizmo, if any
    if (!is_preview && m_gizmos.is_running())
    {
        const BoundingBoxf3 sel_bb = m_selection.get_bounding_box();
        const Vec3d sel_bb_center = sel_bb.center();
        const Vec3d extend_by = sel_bb.max_size() * Vec3d::Ones();
        bb.merge(BoundingBoxf3(sel_bb_center - extend_by, sel_bb_center + extend_by));
    }

    const BoundingBoxf3 first_bed_bb = include_bed_model ? m_bed.extended_bounding_box()
                                                         : m_bed.build_volume().bounding_volume();
    BoundingBoxf3 bed_bb;

    for (int i = 0; i < s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()); ++i)
    {
        if (!is_preview || i == s_multiple_beds.get_active_bed())
        {
            BoundingBoxf3 this_bed = first_bed_bb;
            this_bed.translate(s_multiple_beds.get_bed_translation(i));
            bed_bb.merge(this_bed);
        }
    }
    bb.merge(bed_bb);

    if (is_preview)
    {
        BoundingBoxf3 paths_bb = m_gcode_viewer.get_max_bounding_box();
        paths_bb.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
        bb.merge(paths_bb);
    }

    // clamp max bb size with respect to bed bb size
    if (!m_picking_enabled)
    {
        static const double max_scale_factor = 2.0;
        const Vec3d bb_size = bb.size();
        const Vec3d bed_bb_size = m_bed.build_volume().bounding_volume().size();

        if ((bed_bb_size.x() > 0.0 && bb_size.x() > max_scale_factor * bed_bb_size.x()) ||
            (bed_bb_size.y() > 0.0 && bb_size.y() > max_scale_factor * bed_bb_size.y()) ||
            (bed_bb_size.z() > 0.0 && bb_size.z() > max_scale_factor * bed_bb_size.z()))
        {
            const Vec3d bed_bb_center = bed_bb.center();
            const Vec3d extend_by = max_scale_factor * bed_bb_size;
            bb = BoundingBoxf3(bed_bb_center - extend_by, bed_bb_center + extend_by);
        }
    }

    return bb;
}

void GLCanvas3D::_zoom_to_box(const BoundingBoxf3 &box, double margin_factor)
{
    get_camera().zoom_to_box(box, margin_factor);
    m_dirty = true;
}

void GLCanvas3D::_update_camera_zoom(double zoom)
{
    get_camera().update_zoom(zoom);
    m_dirty = true;
}

void GLCanvas3D::_refresh_if_shown_on_screen()
{
    if (_is_shown_on_screen())
    {
        const Size &cnv_size = get_canvas_size();
        _resize((unsigned int) cnv_size.get_width(), (unsigned int) cnv_size.get_height());

        // When the application starts the following call to render() triggers the opengl initialization.
        // We need to ask for an extra call to reload_scene() to force the generation of the model for wipe tower
        // for printers using it, which is skipped by all the previous calls to reload_scene() because m_initialized == false
        const bool requires_reload_scene = !m_initialized;

        // Because of performance problems on macOS, where PaintEvents are not delivered
        // frequently enough, we call render() here directly when we can.
        render();
        assert(m_initialized);
        if (requires_reload_scene)
        {
            if (is_view3D_shown())
                reload_scene(true);
        }
    }
}

void GLCanvas3D::_picking_pass()
{
    if (!m_picking_enabled || m_mouse.dragging || m_mouse.position == Vec2d(DBL_MAX, DBL_MAX) || m_gizmos.is_dragging())
    {
#if ENABLE_RAYCAST_PICKING_DEBUG
        ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
        ImGuiPureWrap::text("Picking disabled");
        ImGuiPureWrap::end();
#endif // ENABLE_RAYCAST_PICKING_DEBUG
        return;
    }

    m_hover_volume_idxs.clear();

    const ClippingPlane clipping_plane = m_gizmos.get_clipping_plane().inverted_normal();
    const SceneRaycaster::HitResult hit = m_scene_raycaster.hit(m_mouse.position, get_camera(), &clipping_plane);
    if (hit.is_valid())
    {
        switch (hit.type)
        {
        case SceneRaycaster::EType::Volume:
        {
            if (0 <= hit.raycaster_id && hit.raycaster_id < (int) m_volumes.volumes.size())
            {
                const GLVolume *volume = m_volumes.volumes[hit.raycaster_id];
                if (volume->is_active && !volume->disabled &&
                    (volume->composite_id.volume_id >= 0 || m_render_sla_auxiliaries))
                {
                    // do not add the volume id if any gizmo is active and CTRL is pressed
                    if (m_gizmos.get_current_type() == GLGizmosManager::EType::Undefined || !wxGetKeyState(WXK_CONTROL))
                        m_hover_volume_idxs.emplace_back(hit.raycaster_id);
                    m_gizmos.set_hover_id(-1);
                }
            }
            else
                assert(false);

            break;
        }
        case SceneRaycaster::EType::Gizmo:
        case SceneRaycaster::EType::FallbackGizmo:
        {
            const Size &cnv_size = get_canvas_size();
            const bool inside = 0 <= m_mouse.position.x() && m_mouse.position.x() < cnv_size.get_width() &&
                                0 <= m_mouse.position.y() && m_mouse.position.y() < cnv_size.get_height();
            m_gizmos.set_hover_id(inside ? hit.raycaster_id : -1);
            break;
        }
        case SceneRaycaster::EType::Bed:
        {
            m_gizmos.set_hover_id(-1);
            break;
        }
        default:
        {
            assert(false);
            break;
        }
        }
    }
    else
        m_gizmos.set_hover_id(-1);

    _update_volumes_hover_state();

#if ENABLE_RAYCAST_PICKING_DEBUG
    ImGuiWrapper &imgui = *get_imgui();
    ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
    std::string object_type = "None";
    switch (hit.type)
    {
    case SceneRaycaster::EType::Bed:
    {
        object_type = "Bed";
        break;
    }
    case SceneRaycaster::EType::Gizmo:
    {
        object_type = "Gizmo element";
        break;
    }
    case SceneRaycaster::EType::FallbackGizmo:
    {
        object_type = "Gizmo2 element";
        break;
    }
    case SceneRaycaster::EType::Volume:
    {
        if (m_volumes.volumes[hit.raycaster_id]->is_wipe_tower)
            object_type = "Volume (Wipe tower)";
        else if (m_volumes.volumes[hit.raycaster_id]->is_modifier)
            object_type = "Volume (Modifier)";
        else
            object_type = "Volume (Part)";
        break;
    }
    default:
    {
        break;
    }
    }

    auto add_strings_row_to_table = [](const std::string &col_1, const ImVec4 &col_1_color, const std::string &col_2,
                                       const ImVec4 &col_2_color, const std::string &col_3 = "",
                                       const ImVec4 &col_3_color = ImGui::GetStyleColorVec4(ImGuiCol_Text))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(col_1_color, col_1.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGuiPureWrap::text_colored(col_2_color, col_2.c_str());
        if (!col_3.empty())
        {
            ImGui::TableSetColumnIndex(2);
            ImGuiPureWrap::text_colored(col_3_color, col_3.c_str());
        }
    };

    char buf[1024];
    if (hit.type != SceneRaycaster::EType::None)
    {
        if (ImGui::BeginTable("Hit", 2))
        {
            add_strings_row_to_table("Object ID", ImGuiPureWrap::COL_ORANGE_LIGHT, std::to_string(hit.raycaster_id),
                                     ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table("Type", ImGuiPureWrap::COL_ORANGE_LIGHT, object_type,
                                     ImGui::GetStyleColorVec4(ImGuiCol_Text));
            sprintf(buf, "%.3f, %.3f, %.3f", hit.position.x(), hit.position.y(), hit.position.z());
            add_strings_row_to_table("Position", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                     ImGui::GetStyleColorVec4(ImGuiCol_Text));
            sprintf(buf, "%.3f, %.3f, %.3f", hit.normal.x(), hit.normal.y(), hit.normal.z());
            add_strings_row_to_table("Normal", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                     ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::EndTable();
        }
    }
    else
        ImGuiPureWrap::text("NO HIT");

    ImGui::Separator();
    ImGuiPureWrap::text("Registered for picking:");
    if (ImGui::BeginTable("Raycasters", 2))
    {
        sprintf(buf, "%d (%d)", (int) m_scene_raycaster.beds_count(), (int) m_scene_raycaster.active_beds_count());
        add_strings_row_to_table("Beds", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                 ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int) m_scene_raycaster.volumes_count(),
                (int) m_scene_raycaster.active_volumes_count());
        add_strings_row_to_table("Volumes", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                 ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int) m_scene_raycaster.gizmos_count(), (int) m_scene_raycaster.active_gizmos_count());
        add_strings_row_to_table("Gizmo elements", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                 ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int) m_scene_raycaster.fallback_gizmos_count(),
                (int) m_scene_raycaster.active_fallback_gizmos_count());
        add_strings_row_to_table("Gizmo2 elements", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf),
                                 ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::EndTable();
    }

    std::vector<std::shared_ptr<SceneRaycasterItem>> *gizmo_raycasters = m_scene_raycaster.get_raycasters(
        SceneRaycaster::EType::Gizmo);
    if (gizmo_raycasters != nullptr && !gizmo_raycasters->empty())
    {
        ImGui::Separator();
        ImGuiPureWrap::text("Gizmo raycasters IDs:");
        if (ImGui::BeginTable("GizmoRaycasters", 3))
        {
            for (size_t i = 0; i < gizmo_raycasters->size(); ++i)
            {
                add_strings_row_to_table(
                    std::to_string(i), ImGuiPureWrap::COL_ORANGE_LIGHT,
                    std::to_string(
                        SceneRaycaster::decode_id(SceneRaycaster::EType::Gizmo, (*gizmo_raycasters)[i]->get_id())),
                    ImGui::GetStyleColorVec4(ImGuiCol_Text),
                    to_string(Geometry::Transformation((*gizmo_raycasters)[i]->get_transform()).get_offset()),
                    ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            ImGui::EndTable();
        }
    }

    std::vector<std::shared_ptr<SceneRaycasterItem>> *gizmo2_raycasters = m_scene_raycaster.get_raycasters(
        SceneRaycaster::EType::FallbackGizmo);
    if (gizmo2_raycasters != nullptr && !gizmo2_raycasters->empty())
    {
        ImGui::Separator();
        ImGuiPureWrap::text("Gizmo2 raycasters IDs:");
        if (ImGui::BeginTable("Gizmo2Raycasters", 3))
        {
            for (size_t i = 0; i < gizmo2_raycasters->size(); ++i)
            {
                add_strings_row_to_table(
                    std::to_string(i), ImGuiPureWrap::COL_ORANGE_LIGHT,
                    std::to_string(SceneRaycaster::decode_id(SceneRaycaster::EType::FallbackGizmo,
                                                             (*gizmo2_raycasters)[i]->get_id())),
                    ImGui::GetStyleColorVec4(ImGuiCol_Text),
                    to_string(Geometry::Transformation((*gizmo2_raycasters)[i]->get_transform()).get_offset()),
                    ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            ImGui::EndTable();
        }
    }

    ImGuiPureWrap::end();
#endif // ENABLE_RAYCAST_PICKING_DEBUG
}

void GLCanvas3D::_rectangular_selection_picking_pass()
{
    m_gizmos.set_hover_id(-1);

    std::set<int> idxs;

    if (m_picking_enabled)
    {
        // preFlight: detect which volumes fall inside the selection rectangle by projecting their
        // geometry through the camera (CPU test), NOT by reading back rendered pixel colors. Object
        // render colors change with theming, so they must never be the basis for hit detection.
        std::vector<Vec3d> points;     // representative world-space points
        std::vector<int> point_volume; // parallel to points: owning volume index

        for (size_t i = 0; i < m_volumes.volumes.size(); ++i)
        {
            const GLVolume *volume = m_volumes.volumes[i];
            if (volume == nullptr || !volume->is_active || volume->disabled ||
                (volume->composite_id.volume_id < 0 && !m_render_sla_auxiliaries))
                continue;

            const TriangleMesh *hull = volume->convex_hull();
            if (hull != nullptr && !hull->its.vertices.empty())
            {
                // Convex-hull vertices live in the volume's local space; bring them to world space.
                const Transform3d world_matrix = volume->world_matrix();
                for (const Vec3f &v : hull->its.vertices)
                {
                    points.emplace_back(world_matrix * v.cast<double>());
                    point_volume.push_back((int) i);
                }
            }
            else
            {
                // Fallback: the (already world-space) transformed convex-hull bounding box corners.
                const BoundingBoxf3 &bb = volume->transformed_convex_hull_bounding_box();
                for (int c = 0; c < 8; ++c)
                {
                    points.emplace_back((c & 1) ? bb.max.x() : bb.min.x(), (c & 2) ? bb.max.y() : bb.min.y(),
                                        (c & 4) ? bb.max.z() : bb.min.z());
                    point_volume.push_back((int) i);
                }
            }
        }

        // A volume is hit if any of its representative points projects inside the rectangle.
        const std::vector<unsigned int> inside = m_rectangle_selection.contains(points);
        for (unsigned int idx : inside)
            idxs.insert(point_volume[idx]);

        // Also pick whatever sits directly under the rectangle's center and corners. This covers a
        // small rectangle drawn entirely inside a large object's silhouette, where none of that
        // object's hull vertices fall within the rectangle. The ray cast is depth-correct.
        const ClippingPlane clipping_plane = m_gizmos.get_clipping_plane().inverted_normal();
        const double l = m_rectangle_selection.get_left();
        const double r = m_rectangle_selection.get_right();
        const double t = m_rectangle_selection.get_top();
        const double b = m_rectangle_selection.get_bottom();
        const Vec2d samples[5] = {Vec2d(0.5 * (l + r), 0.5 * (t + b)), Vec2d(l, t), Vec2d(r, t), Vec2d(l, b),
                                  Vec2d(r, b)};
        for (const Vec2d &sample : samples)
        {
            const SceneRaycaster::HitResult hit = m_scene_raycaster.hit(sample, get_camera(), &clipping_plane);
            if (hit.is_valid() && hit.type == SceneRaycaster::EType::Volume && 0 <= hit.raycaster_id &&
                hit.raycaster_id < (int) m_volumes.volumes.size())
            {
                const GLVolume *volume = m_volumes.volumes[hit.raycaster_id];
                if (volume->is_active && !volume->disabled &&
                    (volume->composite_id.volume_id >= 0 || m_render_sla_auxiliaries))
                    idxs.insert(hit.raycaster_id);
            }
        }
    }

    m_hover_volume_idxs.assign(idxs.begin(), idxs.end());
    _update_volumes_hover_state();
}

void GLCanvas3D::_render_background()
{
    bool use_error_color = false;
    if (is_editor())
    {
        use_error_color = m_dynamic_background_enabled;

        if (s_multiple_beds.is_autoslicing())
        {
            use_error_color &= std::any_of(s_print_statuses.begin(), s_print_statuses.end(),
                                           [](const PrintStatus status)
                                           { return status == PrintStatus::toolpath_outside; });
        }
        else
        {
            if (!m_volumes.empty())
                use_error_color &= _is_any_volume_outside().first;
            else
                use_error_color &= m_gcode_viewer.has_data() && !m_gcode_viewer.is_contained_in_bed();
        }
    }

    // Draws a bottom to top gradient over the complete screen.
    glsafe(::glDisable(GL_DEPTH_TEST));

    // Use centralized UIColors for themed canvas background
    const bool is_dark = dark_mode();
    const ColorRGBA top_color = use_error_color ? ERROR_BG_LIGHT_COLOR : get_gradient_top_color(is_dark);
    const ColorRGBA bottom_color = use_error_color ? ERROR_BG_DARK_COLOR : get_bg_color(is_dark);

    if (!m_background.is_initialized())
    {
        m_background.reset();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P2T2};
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);

        // vertices
        init_data.add_vertex(Vec2f(-1.0f, -1.0f), Vec2f(0.0f, 0.0f));
        init_data.add_vertex(Vec2f(1.0f, -1.0f), Vec2f(1.0f, 0.0f));
        init_data.add_vertex(Vec2f(1.0f, 1.0f), Vec2f(1.0f, 1.0f));
        init_data.add_vertex(Vec2f(-1.0f, 1.0f), Vec2f(0.0f, 1.0f));

        // indices
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(2, 3, 0);

        m_background.init_from(std::move(init_data));
    }

    GLShaderProgram *shader = get_shader("background");
    if (shader != nullptr)
    {
        shader->start_using();
        shader->set_uniform("top_color", top_color);
        shader->set_uniform("bottom_color", bottom_color);
        m_background.render();
        shader->stop_using();
    }

    glsafe(::glEnable(GL_DEPTH_TEST));
}

void GLCanvas3D::_render_bed(const Transform3d &view_matrix, const Transform3d &projection_matrix, bool bottom)
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    bool show_texture = !bottom || (m_gizmos.get_current_type() != GLGizmosManager::FdmSupports &&
                                    m_gizmos.get_current_type() != GLGizmosManager::Seam &&
                                    m_gizmos.get_current_type() != GLGizmosManager::ColorMixing &&
                                    m_gizmos.get_current_type() != GLGizmosManager::FuzzySkin);

    m_bed.render(*this, view_matrix, projection_matrix, bottom, scale_factor, show_texture);
}

void GLCanvas3D::_render_bed_axes()
{
    m_bed.render_axes();
}

void GLCanvas3D::_render_gcode()
{
    m_gcode_viewer.render();

    // Check for scroll requests from the G-code window
    int scroll_request = m_gcode_viewer.get_sequential_view().gcode_window.get_and_clear_scroll_request();
    if (scroll_request != 0)
    {
        // Track if we're stuck at boundaries
        static size_t last_visible_start = 0;
        static size_t last_visible_end = 0;
        static int stuck_count = 0;
        static int last_scroll_direction = 0;

        const auto &visible_range = m_gcode_viewer.get_gcode_view_visible_range();
        const auto &enabled_range = m_gcode_viewer.get_gcode_view_enabled_range();

        // Check if the PREVIOUS scroll moved us (before we do this one)
        bool position_changed = (visible_range[0] != last_visible_start || visible_range[1] != last_visible_end);

        // Always try to scroll normally first
        int scroll_key = (scroll_request < 0) ? WXK_LEFT : WXK_RIGHT;
        m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, scroll_key, wxMOD_SHIFT);

        // If position didn't change and we're scrolling in the same direction, we might be stuck
        if (!position_changed && scroll_request == last_scroll_direction)
        {
            stuck_count++;

            // If we're stuck even once, immediately transition to next/previous layer
            // This skips unselectable moves at layer boundaries
            if (stuck_count >= 1)
            {
                // Get layer information
                const auto &layers_range = m_gcode_viewer.get_layers_view_range();
                const auto layer_zs = m_gcode_viewer.get_layers_zs();
                size_t total_layers = layer_zs.size();

                // Check if we're at file boundaries based on layer indices
                bool at_first_layer = (layers_range[0] == 0 && layers_range[1] == 0);
                bool at_last_layer = (layers_range[1] >= total_layers - 1);

                if (scroll_request < 0 && !at_first_layer)
                {
                    // Scrolling backward and stuck but NOT at first layer - go to previous layer
                    m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, WXK_DOWN, 0);
                    stuck_count = 0;
                }
                else if (scroll_request > 0 && !at_last_layer && !at_first_layer)
                {
                    // Scrolling forward and stuck but NOT at first/last layer - go to next layer
                    // Skip when at first layer - move_slider_by_key handles the layer 0 skip
                    // wxMOD_CONTROL signals this is from scrubbing
                    m_event_poster->postKeyEvent(CanvasEventType::SlidersManipulation, WXK_UP, wxMOD_CONTROL);
                    stuck_count = 0;
                }
                else
                {
                    // We're at a boundary, don't transition
                    stuck_count = 0;
                }
            }
        }
        else
        {
            // Position changed or direction changed, reset stuck counter
            stuck_count = 0;
        }

        // Remember state for next time
        last_visible_start = visible_range[0];
        last_visible_end = visible_range[1];
        last_scroll_direction = scroll_request;
    }
}

void GLCanvas3D::_render_objects(GLVolumeCollection::ERenderType type)
{
    if (m_volumes.empty())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_camera_clipping_plane = m_gizmos.get_clipping_plane();

    if (m_picking_enabled)
        // Update the layer editing selection to the first object selected, update the current object maximum Z.
        m_layers_editing.select_object(*m_model, this->is_layers_editing_enabled() ? m_selection.get_object_idx() : -1);

    if (const BuildVolume &build_volume = m_bed.build_volume(); build_volume.valid())
    {
        const Vec3d bed_offset = s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed());
        switch (build_volume.type())
        {
        case BuildVolume::Type::Rectangle:
        {
            const BoundingBox3Base<Vec3d> bed_bb = build_volume.bounding_volume().inflated(BuildVolume::SceneEpsilon);
            m_volumes.set_print_volume(
                {0, // rectangle
                 {float(bed_bb.min.x() + bed_offset.x()), float(bed_bb.min.y() + bed_offset.y()),
                  float(bed_bb.max.x() + bed_offset.x()), float(bed_bb.max.y() + bed_offset.y())},
                 {float(0.0 + bed_offset.z()), float(build_volume.max_print_height() + bed_offset.z())}});
            break;
        }
        case BuildVolume::Type::Circle:
        {
            m_volumes.set_print_volume(
                {1, // circle
                 {unscaled<float>(build_volume.circle().center.x() + bed_offset.x()),
                  unscaled<float>(build_volume.circle().center.y() + bed_offset.y()),
                  unscaled<float>(build_volume.circle().radius + BuildVolume::SceneEpsilon), 0.0f},
                 {float(0.0 + bed_offset.z()),
                  float(build_volume.max_print_height() + bed_offset.z() + BuildVolume::SceneEpsilon)}});
            break;
        }
        default:
        case BuildVolume::Type::Convex:
        case BuildVolume::Type::Custom:
        {
            m_volumes.set_print_volume(
                {static_cast<int>(type), {-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, FLT_MAX}});
        }
        }
        if (m_requires_check_outside_state)
        {
            check_volumes_outside_state(m_volumes, nullptr);
            m_requires_check_outside_state = false;
        }
    }

    if (m_use_clipping_planes)
        m_volumes.set_z_range(-m_clipping_planes[0].get_data()[3], m_clipping_planes[1].get_data()[3]);
    else
        m_volumes.set_z_range(-FLT_MAX, FLT_MAX);

    m_volumes.set_clipping_plane(m_camera_clipping_plane.get_data());
    m_volumes.set_show_sinking_contours(!m_gizmos.is_hiding_instances());
    m_volumes.set_show_non_manifold_edges(!m_gizmos.is_hiding_instances() &&
                                          m_gizmos.get_current_type() != GLGizmosManager::Simplify);

    const Camera &camera = get_camera();
    auto trafo = camera.get_view_matrix();
    GLRenderContext render_ctx = build_render_context();

    GLShaderProgram *shader = get_model_shader();
    if (shader != nullptr)
    {
        shader->start_using();

        switch (type)
        {
        default:
        case GLVolumeCollection::ERenderType::Opaque:
        {
            if (m_picking_enabled && !m_gizmos.is_dragging() && m_layers_editing.is_enabled() &&
                (m_layers_editing.last_object_id != -1) && (m_layers_editing.object_max_z() > 0.0f))
            {
                int object_id = m_layers_editing.last_object_id;

                m_volumes.render(type, false, trafo, camera.get_projection_matrix(), render_ctx,
                                 [object_id](const GLVolume &volume)
                                 {
                                     // Which volume to paint without the layer height profile shader?
                                     return volume.is_active &&
                                            (volume.is_modifier || volume.composite_id.object_id != object_id);
                                 });
                // Let LayersEditing handle rendering of the active object using the layer height profile shader.
                // render_volumes() calls GLVolume::render() directly (not via GLVolumeCollection::render()),
                // so we must provide the render context that GLVolume::render() requires.
                GLVolumeCollection::set_render_context(&render_ctx);
                m_layers_editing.render_volumes(*this, m_volumes);
                GLVolumeCollection::set_render_context(nullptr);
            }
            else
            {
                // do not cull backfaces to show broken geometry, if any
                const Camera &camera = get_camera();
                m_volumes.render(type, m_picking_enabled, trafo, camera.get_projection_matrix(), render_ctx,
                                 [this](const GLVolume &volume)
                                 { return (m_render_sla_auxiliaries || volume.composite_id.volume_id >= 0); });
            }

            // In case a painting gizmo is open, it should render the painted triangles
            // before transparent objects are rendered. Otherwise they would not be
            // visible when inside modifier meshes etc.
            {
                GLGizmosManager &gm = get_gizmos_manager();
                //                GLGizmosManager::EType type = gm.get_current_type();
                if (dynamic_cast<GLGizmoPainterBase *>(gm.get_current()))
                {
                    shader->stop_using();
                    gm.render_painter_gizmo();
                    shader->start_using();
                }
            }
            break;
        }
        case GLVolumeCollection::ERenderType::Transparent:
        {
            const Camera &camera = get_camera();
            m_volumes.render(type, false, trafo, camera.get_projection_matrix(), render_ctx);
            break;
        }
        }
        shader->stop_using();
    }

    m_camera_clipping_plane = ClippingPlane::ClipsNothing();
}

void GLCanvas3D::_render_selection()
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    if (!m_gizmos.is_running())
        m_selection.render(scale_factor);

#if ENABLE_MATRICES_DEBUG
    m_selection.render_debug_window();
#endif // ENABLE_MATRICES_DEBUG
}

bool GLCanvas3D::check_toolbar_icon_size(float init_scale, float &new_scale_to_save, bool is_custom,
                                         int counter /* = 3*/)
{
    const Size cnv_size = get_canvas_size();

#if ENABLE_RETINA_GL
    float max_scale = m_retina_helper->get_scale_factor();
#else
    float max_scale = 0.1f * em_unit();
#endif // ENABLE_RETINA_GL

    float scale = init_scale * max_scale;

    int size = int(GLToolbar::Default_Icons_Size * scale);
    int gizmo_size = int(GLGizmosManager::Default_Icons_Size * scale);

    // Set current scale for all top toolbars. It will be used for next calculations

    if (!is_approx(scale, m_main_toolbar.get_scale(), 0.015f))
    {
        m_main_toolbar.set_scale(scale);
        m_undoredo_toolbar.set_scale(scale);
        m_collapse_toolbar->set_scale(scale);
        m_view_toolbar->set_scale(scale);
        m_gizmos.set_overlay_scale(scale);

        m_view_toolbar->set_icons_size(gizmo_size);
    }

    const float top_tb_width = m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() +
                               m_collapse_toolbar->get_width();
    float items_cnt = float(m_main_toolbar.get_visible_items_cnt() + m_undoredo_toolbar.get_visible_items_cnt() +
                            m_collapse_toolbar->get_visible_items_cnt());
    const float noitems_width = top_tb_width -
                                float(size) * items_cnt; // width of separators and borders in top toolbars
    items_cnt += 1.6f;                                   // +1.6 means a place for some minimal margin between toolbars

    // calculate scale needed for items in all top toolbars
    // the std::max() is there because on some Linux dialects/virtual machines this code is called when the canvas has not been properly initialized yet,
    // leading to negative values for the scale.
    // Fix for OpenGL rendering issue
    //      https://github.com/supermerill/SuperSlicer/issues/854
    const float new_h_scale = std::max((cnv_size.get_width() - noitems_width), 1.0f) /
                              (items_cnt * GLToolbar::Default_Icons_Size);

    float gizmos_height = m_gizmos.get_scaled_total_height();
    int giz_items_cnt = m_gizmos.get_selectable_icons_cnt();
    float noitems_height = gizmos_height -
                           gizmo_size * giz_items_cnt; // height of separators and borders in gizmos toolbars

    noitems_height += m_main_toolbar.get_height(); // increase its value to main_toolbar height
    giz_items_cnt += 2;                            // +2 means a place for view toolbar

    const float new_v_scale = std::max((cnv_size.get_height() - noitems_height), 1.0f) /
                              (giz_items_cnt * GLGizmosManager::Default_Icons_Size);

    // set minimum scale as a auto scale for the toolbars
    float new_scale = std::min(new_h_scale, new_v_scale);

    new_scale_to_save = std::min(new_scale / max_scale, 1.f);

    if (is_custom && new_scale_to_save > init_scale)
        return true; // we need to save new value, so return true

    if (is_approx(init_scale, new_scale_to_save, 0.015f) || counter == 0)
        return true;

    // scale is changed by 1.5% and more
    init_scale = new_scale_to_save;
    counter--;
    return check_toolbar_icon_size(init_scale, new_scale_to_save, is_custom, counter);
}

void GLCanvas3D::_check_and_update_toolbar_icon_scale()
{
    // Don't update a toolbar scale, when we are on a Preview
    if (is_preview_shown())
        return;

    bool is_custom;
    if (!m_toolbar_icon_scale)
        return;
    const float init_scale = m_toolbar_icon_scale(is_custom);
    float new_scale_to_save;
    if (check_toolbar_icon_size(init_scale, new_scale_to_save, is_custom) &&
        !is_approx(init_scale, new_scale_to_save, 0.015f))
        if (m_set_auto_toolbar_icon_scale)
            m_set_auto_toolbar_icon_scale(new_scale_to_save);
}

void GLCanvas3D::_render_overlays()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    // main toolbar and undoredo toolbar need to be both updated before rendering because both their sizes are needed
    // to correctly place them
    _check_and_update_toolbar_icon_scale();

    _render_gizmos_overlay();

    _render_main_toolbar();
    _render_undoredo_toolbar();
    _render_view_toolbar();

    // Hamburger menu button in top-left of 3D view (already skips Preview internally)
    _render_sidebar_toggle_imgui();

    if (m_layers_editing.last_object_id >= 0 && m_layers_editing.object_max_z() > 0.0f)
        m_layers_editing.render_overlay(*this);

    std::vector<const ModelInstance *> sorted_instances;
    if (_is_sequential_print_enabled())
    {
        for (ModelObject *model_object : m_model->objects)
            for (ModelInstance *model_instance : model_object->instances)
            {
                if (auto it = s_multiple_beds.get_inst_map().find(model_instance->id());
                    it != s_multiple_beds.get_inst_map().end() && it->second == s_multiple_beds.get_active_bed())
                    sorted_instances.emplace_back(model_instance);
            }
    }
    m_labels.render(sorted_instances);
}

#define use_scrolling 1

std::string get_status_text(PrintStatus status)
{
    switch (status)
    {
    case PrintStatus::idle:
        return _u8L("Unsliced bed");
    case PrintStatus::running:
        return _u8L("Slicing") + "...";
    case PrintStatus::finished:
        return _u8L("Sliced bed");
    case PrintStatus::outside:
        return _u8L("Object at boundary");
    case PrintStatus::invalid:
        return _u8L("Invalid data");
    case PrintStatus::empty:
        return _u8L("Empty bed");
    case PrintStatus::toolpath_outside:
        return _u8L("Toolpath exceeds bounds");
    }
    return {};
}

wchar_t get_raw_status_icon(const PrintStatus status)
{
    switch (status)
    {
    case PrintStatus::finished:
        return ImGui::PrintFinished;
    case PrintStatus::running:
        return ImGui::PrintRunning;
    case PrintStatus::idle:
        return ImGui::PrintIdle;
    case PrintStatus::outside:
        return ImGui::PrintIdle;
    case PrintStatus::invalid:
        return ImGui::PrintIdle;
    case PrintStatus::empty:
        return ImGui::PrintIdle;
    case PrintStatus::toolpath_outside:
        return ImGui::PrintIdle;
    }
    return ImGui::PrintIdle;
}

std::string get_status_icon(const PrintStatus status)
{
    return boost::nowide::narrow(std::wstring{get_raw_status_icon(status)});
}

bool bed_selector_thumbnail(const ImVec2 size, const ImVec2 padding, const float side, const float border,
                            const float scale, const int bed_id, const std::optional<PrintStatus> status)
{
    ImGuiWindow *window = GImGui->CurrentWindow;
    const ImVec2 current_position = GImGui->CurrentWindow->DC.CursorPos;
    const ImVec2 state_pos = current_position + ImVec2(3.f * border, side - 20.f * scale);

    const GLuint texture_id = s_bed_selector_thumbnail_texture_ids[bed_id];
    const bool clicked{
        ImGui::ImageButton((void *) (int64_t) texture_id, size - padding, ImVec2(0, 1), ImVec2(1, 0), border)};

    if (status)
    {
        const std::string icon{get_status_icon(*status)};

        window->DrawList->AddText(GImGui->Font, GImGui->FontSize, state_pos, ImGui::GetColorU32(ImGuiCol_Text),
                                  icon.c_str(), icon.c_str() + icon.size());
    }

    const ImVec2 id_pos = current_position + ImVec2(3.f * border, 1.5f * border);
    const std::string id = std::to_string(bed_id + 1);

    window->DrawList->AddText(GImGui->Font, GImGui->FontSize * 1.5f, id_pos, ImGui::GetColorU32(ImGuiCol_Text),
                              id.c_str(), id.c_str() + id.size());

    return clicked;
}

bool button_with_icon(ImGuiWrapper *imgui, const wchar_t icon, const std::string &tooltip, bool is_active,
                      const ImVec2 size)
{
    std::string btn_name = boost::nowide::narrow(std::wstring{icon});

    ImGuiButtonFlags flags = ImGuiButtonFlags_None;

    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext &g = *GImGui;
    const ImGuiStyle &style = g.Style;
    const ImGuiID id = window->GetID(btn_name.c_str());
    const ImFontAtlasCustomRect *const rect = imgui->GetTextureCustomRect(icon);
    const ImVec2 label_size = ImVec2(rect->Width, rect->Height);

    ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    if (g.CurrentItemFlags & ImGuiItemFlags_ButtonRepeat)
        flags |= ImGuiButtonFlags_Repeat;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    // Render
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiPureWrap::COL_ORANGE_DARK
                                         : hovered         ? ImGuiPureWrap::COL_ORANGE_LIGHT
                                                           : ImGuiPureWrap::COL_GREY_DARK);
    ImGui::RenderNavHighlight(bb, id);
    ImGui::PushStyleColor(ImGuiCol_Border, is_active ? ImGuiPureWrap::COL_BUTTON_ACTIVE : ImGuiPureWrap::COL_GREY_DARK);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);
    ImGui::PopStyleColor();

    if (g.LogEnabled)
        ImGui::LogSetNextTextDecoration("[", "]");
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, btn_name.c_str(), NULL,
                             &label_size, style.ButtonTextAlign, &bb);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.c_str());

    return pressed;
}

void Slic3r::GUI::GLCanvas3D::_render_bed_selector()
{
    bool extra_frame{false};
    static std::array<std::optional<PrintStatus>, MAX_NUMBER_OF_BEDS> previous_print_status;

    if (s_multiple_beds.get_number_of_beds() != 1 && is_preview_shown())
    {
#if ENABLE_RETINA_GL
        float scale = m_retina_helper->get_scale_factor();
#else
        float scale = 0.1f * em_unit();
#endif // ENABLE_RETINA_GL

        const float btn_side = 80.f * scale;
        const float btn_border = 2.f * scale;

        const ImVec2 btn_size = ImVec2(btn_side, btn_side);
        const ImVec2 btn_padding = ImVec2(btn_border, btn_border);

        auto render_bed_button = [btn_side, btn_border, btn_size, btn_padding, this, &extra_frame, scale](int i)
        {
            bool inactive = i != s_multiple_beds.get_active_bed() || s_multiple_beds.is_autoslicing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImGuiPureWrap::COL_GREY_DARK);
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  inactive ? ImGuiPureWrap::COL_GREY_DARK : ImGuiPureWrap::COL_BUTTON_ACTIVE);

            const PrintStatus print_status{s_print_statuses[i]};

            if (current_printer_technology() == ptFFF)
            {
                if (!previous_print_status[i] || print_status != previous_print_status[i])
                {
                    extra_frame = true;
                }
                previous_print_status[i] = print_status;
            }

            if (s_bed_selector_thumbnail_changed[i])
            {
                extra_frame = true;
                s_bed_selector_thumbnail_changed[i] = false;
            }

            if (!is_sliceable(print_status))
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            }

            bool clicked = false;
            if (!is_sliceable(print_status))
            {
                clicked = button_with_icon(get_imgui(), ImGui::WarningMarkerDisabled, get_status_text(print_status),
                                           !inactive, btn_size + btn_padding);
            }
            else if (print_status == PrintStatus::toolpath_outside)
            {
                clicked = button_with_icon(get_imgui(), ImGui::WarningMarker, get_status_text(print_status), !inactive,
                                           btn_size + btn_padding);
            }
            else if (i >= int(s_bed_selector_thumbnail_texture_ids.size()))
            {
                clicked = ImGui::Button(std::to_string(i + 1).c_str(), btn_size + btn_padding);
            }
            else
            {
                clicked = bed_selector_thumbnail(btn_size, btn_padding, btn_side, btn_border, scale, i,
                                                 current_printer_technology() == ptFFF ? std::optional{print_status}
                                                                                       : std::nullopt);
            }

            if (clicked && is_sliceable(print_status))
                select_bed(i, true);

            ImGui::PopStyleColor(2);
            if (!is_sliceable(print_status))
            {
                ImGui::PopItemFlag();
            }

            if (current_printer_technology() == ptFFF)
            {
                const std::string status_text{get_status_text(print_status)};
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", status_text.c_str());
                }
            }
        };

        float win_x_pos = get_canvas_size().get_width();

        float right_shift = 0.f;
        if (m_right_margin_fn)
            right_shift = m_right_margin_fn();
        if (right_shift == 0.f)
        {
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
            // When the application is run as GCodeViewer the collapse toolbar is enabled but invisible, as it is renderer
            // outside of the screen
            const bool is_collapse_btn_shown = is_editor() ? m_collapse_toolbar->is_enabled() : false;
#else
            const bool is_collapse_btn_shown = m_collapse_toolbar->is_enabled();
#endif // ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
            if (is_collapse_btn_shown)
                right_shift = m_collapse_toolbar->get_width();
        }
        win_x_pos -= right_shift;

#if use_scrolling
        static float width{0.f};
        static float height{0.f};
        static float v_pos{1.f};

        ImGui::SetNextWindowPos({win_x_pos - scale * 5.f, v_pos}, ImGuiCond_Always, {1.f, 0.f});
        ImGui::SetNextWindowSize({width, height});
#else
        ImGuiPureWrap::set_next_window_pos(win_x_pos - scale * 5.f, 1.f, ImGuiCond_Always, 1.f);
#endif
        ImGui::Begin("Bed selector", 0,
                     ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, btn_border);

        if (current_printer_technology() == ptFFF &&
            button_with_icon(get_imgui(), ImGui::SliceAllBtnIcon, _u8L("Slice all"), s_multiple_beds.is_autoslicing(),
                             btn_size + btn_padding))
        {
            if (!s_multiple_beds.is_autoslicing())
            {
                s_multiple_beds.start_autoslice([this](int i, bool user) { this->select_bed(i, user); });
                m_event_poster->postEvent(CanvasEventType::SwitchToAutoslicingMode);
                m_event_poster->postEvent(CanvasEventType::ShowAutoslicingActionButtons);
            }
        }

        ImGui::SameLine();

        int beds_num = s_multiple_beds.get_number_of_beds();

        for (int i = 0; i < beds_num; ++i)
        {
            render_bed_button(i);
            if (i < beds_num - 1)
                ImGui::SameLine();
        }

        ImGui::PopStyleVar(3);

#if use_scrolling
        bool is_legend_visible = is_legend_shown() && !s_multiple_beds.is_autoslicing();
        ImVec2 win_size = ImGui::GetCurrentWindow()->ContentSizeIdeal + ImGui::GetCurrentWindow()->WindowPadding * 2.f +
                          ImGui::GetCurrentWindow()->ScrollbarSizes +
                          ImVec2(0.f, is_legend_visible ? ImGui::GetCurrentWindow()->TitleBarHeight() : 0.f);

        if (!is_approx(height, win_size.y))
        {
            height = win_size.y;
            get_imgui()->set_requires_extra_frame();
        }
        m_bed_selector_current_height = height;

        float max_width = win_x_pos;
        if (is_legend_visible)
            max_width -= 400.f * scale; // 400.f is used instead of legend width

        if (max_width < height)
        {
            width = win_x_pos - 5.f * scale;

            v_pos = ImGui::GetCurrentWindow()->CalcFontSize() + GImGui->Style.FramePadding.y * 2.f + 5.f * scale;
            extra_frame = true;
        }
        else
        {
            if (v_pos > 1.f)
            {
                v_pos = 1.f;
                extra_frame = true;
            }

            if (win_size.x > max_width)
            {
                width = max_width;
                extra_frame = true;
            }
            else if (!is_approx(width, win_size.x))
            {
                width = win_size.x;
                extra_frame = true;
            }
        }

        if (extra_frame)
            get_imgui()->set_requires_extra_frame();
#endif
        ImGui::End();
    }
}

void GLCanvas3D::_render_gizmos_overlay()
{
    m_gizmos.render_overlay();

    if (m_gizmo_highlighter.m_render_arrow)
        m_gizmos.render_arrow(*this, m_gizmo_highlighter.m_gizmo_type);
}

void GLCanvas3D::_render_main_toolbar()
{
    if (!m_main_toolbar.is_enabled())
        return;

    const Size cnv_size = get_canvas_size();
    const float top = 0.5f * (float) cnv_size.get_height();

    const float collapse_toolbar_width = m_collapse_toolbar->is_enabled() ? m_collapse_toolbar->get_width() : 0.0f;

    const float left = -0.5f * (m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() + collapse_toolbar_width);

    m_main_toolbar.set_position(top, left);
    m_main_toolbar.render(*this);
    if (m_toolbar_highlighter.m_render_arrow)
        m_main_toolbar.render_arrow(*this, m_toolbar_highlighter.m_toolbar_item);
}

void GLCanvas3D::_render_undoredo_toolbar()
{
    if (!m_undoredo_toolbar.is_enabled())
        return;

    const Size cnv_size = get_canvas_size();
    const float top = 0.5f * (float) cnv_size.get_height();
    const float collapse_toolbar_width = m_collapse_toolbar->is_enabled() ? m_collapse_toolbar->get_width() : 0.0f;
    const float left = m_main_toolbar.get_width() -
                       0.5f * (m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() + collapse_toolbar_width);

    m_undoredo_toolbar.set_position(top, left);
    m_undoredo_toolbar.render(*this);
    if (m_toolbar_highlighter.m_render_arrow)
        m_undoredo_toolbar.render_arrow(*this, m_toolbar_highlighter.m_toolbar_item);
}

void GLCanvas3D::_render_collapse_toolbar() const
{
    const Size cnv_size = get_canvas_size();
    const float band = m_layers_editing.is_enabled()
                           ? (get_imgui()->get_style_scaling() * LayersEditing::THICKNESS_BAR_WIDTH)
                           : 0.0;
    const float top = 0.5f * (float) cnv_size.get_height();
    const bool legacy = wxGetApp().legacy_prepare_layout();
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
    // When the application is run as GCodeViewer, render the collapse toolbar outside of the screen
    const float left = is_gcode_viewer() ? 0.5f * (float) cnv_size.get_width()
                       : legacy          ? -0.5f * (float) cnv_size.get_width()
                                         : 0.5f * (float) cnv_size.get_width() - m_collapse_toolbar->get_width() - band;
#else
    const float left = legacy ? -0.5f * (float) cnv_size.get_width()
                              : 0.5f * (float) cnv_size.get_width() - m_collapse_toolbar->get_width() - band;
#endif // ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC

    m_collapse_toolbar->set_position(top, left);
    m_collapse_toolbar->render(*this);
}

/*
void GLCanvas3D::_render_view_toolbar() const
{
    GLToolbar& view_toolbar = *m_view_toolbar;

    // Hide both buttons if nothing is sliced
    // Show only "3D View" button when in Preview mode
    // Show only "Preview" button when in 3D View mode

    bool is_sliced = is_preview_loaded();
    bool preview_shown = is_preview_shown();

    if (!is_sliced) {
        // Nothing sliced - hide both buttons
        view_toolbar.set_item_visible("3D", false);
        view_toolbar.set_item_visible("Preview", false);
    }
    else if (preview_shown) {
        // In Preview mode - show only 3D button
        view_toolbar.set_item_visible("3D", true);
        view_toolbar.set_item_visible("Preview", false);
    }
    else {
        // In 3D View mode - show only Preview button
        view_toolbar.set_item_visible("3D", false);
        view_toolbar.set_item_visible("Preview", true);
    }

    // Don't render if no items are visible
    if (!view_toolbar.is_any_item_visible())
        return;

    const Size cnv_size = get_canvas_size();
    // places the toolbar on the bottom-right corner of the 3d scene
    const float top = -0.5f * (float)cnv_size.get_height() + view_toolbar.get_height();
    // Calculate left position for right-side placement
    // The toolbar expects 'left' position, so we calculate where the left edge should be
    // Add some margin from the right edge
    const float margin = 10.0f;
    const float left = 0.5f * (float)cnv_size.get_width() - view_toolbar.get_width() - margin;

    view_toolbar.set_position(top, left);
    view_toolbar.render(*this);
}
*/

// Empty stub to avoid compilation errors
void GLCanvas3D::_render_view_toolbar() const
{
    // View toolbar rendering suppressed
}

void GLCanvas3D::_render_sidebar_toggle_imgui()
{
    // Only show in 3D view (Prepare mode), not in Preview
    if (is_preview_shown())
    {
        return;
    }

    ImGuiWrapper &imgui = *get_imgui();
    const float scale = imgui.get_style_scaling();

    const Size cnv_size = get_canvas_size();
    const float button_size = 30.0f * scale;

    if (wxGetApp().legacy_prepare_layout())
        ImGuiPureWrap::set_next_window_pos((float) cnv_size.get_width() - button_size, 0.0f, ImGuiCond_Always);
    else
        ImGuiPureWrap::set_next_window_pos(0.0f, 0.0f, ImGuiCond_Always);

    // Create a transparent window for the button
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // Add NoNav flag to prevent this window from affecting other ImGui window positioning
    ImGuiPureWrap::begin("SidebarToggle", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

    // Remove button border by setting FrameBorderSize to 0
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    // Themed borderless button: transparent at rest, subtle themed fill on hover/press, muted themed icon.
    const Slic3r::GUI::ThemePalette &pal = Slic3r::GUI::active_palette();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(pal.imgui_frame_hover.r, pal.imgui_frame_hover.g, pal.imgui_frame_hover.b, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(pal.imgui_frame_bg.r, pal.imgui_frame_bg.g, pal.imgui_frame_bg.b, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(pal.imgui_text_disabled.r, pal.imgui_text_disabled.g,
                                                pal.imgui_text_disabled.b, pal.imgui_text_disabled.a));

    // Draw custom hamburger menu icon
    ImVec2 button_pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##sidebar_toggle", ImVec2(button_size, button_size));

    // Check hover state
    bool hovered = ImGui::IsItemHovered();

    // No backdrop at rest; themed fill only on hover.
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    if (hovered)
        draw_list->AddRectFilled(button_pos, ImVec2(button_pos.x + button_size, button_pos.y + button_size),
                                 ImGui::GetColorU32(ImGuiCol_ButtonHovered));

    // Draw three horizontal lines for hamburger menu
    const float line_height = 2.0f * scale;
    const float line_spacing = 4.0f * scale;
    const float line_width = 18.0f * scale;
    const float margin = (button_size - line_width) / 2.0f;
    const float top_margin = (button_size - (3 * line_height + 2 * line_spacing)) / 2.0f;

    const wxColour &icon_clr = pal.icon_enabled; // match the 3D toolbar icon color
    ImU32 line_color = IM_COL32(icon_clr.Red(), icon_clr.Green(), icon_clr.Blue(), 255);
    for (int i = 0; i < 3; i++)
    {
        float y = button_pos.y + top_margin + i * (line_height + line_spacing);
        draw_list->AddRectFilled(ImVec2(button_pos.x + margin, y),
                                 ImVec2(button_pos.x + margin + line_width, y + line_height), line_color);
    }

    if (clicked)
    {
        m_event_poster->postEvent(CanvasEventType::CollapseSidebarToggle);
    }

    // Cache tooltip string to avoid calling _u8L every frame
    static const std::string tooltip_text = _u8L("Toggle sidebar");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", tooltip_text.c_str());
    }

    ImGui::PopStyleColor(4); // Button colors and text color
    ImGui::PopStyleVar(1);   // FrameBorderSize

    ImGuiPureWrap::end();

    ImGui::PopStyleVar(2);   // WindowBorderSize, WindowPadding
    ImGui::PopStyleColor(1); // WindowBg
}

#if ENABLE_SHOW_CAMERA_TARGET
void GLCanvas3D::_render_camera_target()
{
    static const float half_length = 10.0f;

    glsafe(::glDisable(GL_DEPTH_TEST));
#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f * get_imgui()->get_style_scaling()));
#endif // !SLIC3R_OPENGL_ES

    m_camera_target.target = get_camera().get_target();

    for (int i = 0; i < 3; ++i)
    {
        if (!m_camera_target.axis[i].is_initialized())
        {
            m_camera_target.axis[i].reset();

            GLModel::Geometry init_data;
            init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
            init_data.color = (i == X) ? ColorRGBA::X() : (i == Y) ? ColorRGBA::Y() : ColorRGBA::Z();
            init_data.reserve_vertices(2);
            init_data.reserve_indices(2);

            // vertices
            if (i == X)
            {
                init_data.add_vertex(Vec3f(-half_length, 0.0f, 0.0f));
                init_data.add_vertex(Vec3f(+half_length, 0.0f, 0.0f));
            }
            else if (i == Y)
            {
                init_data.add_vertex(Vec3f(0.0f, -half_length, 0.0f));
                init_data.add_vertex(Vec3f(0.0f, +half_length, 0.0f));
            }
            else
            {
                init_data.add_vertex(Vec3f(0.0f, 0.0f, -half_length));
                init_data.add_vertex(Vec3f(0.0f, 0.0f, +half_length));
            }

            // indices
            init_data.add_line(0, 1);

            m_camera_target.axis[i].init_from(std::move(init_data));
        }
    }

#if SLIC3R_OPENGL_ES
    GLShaderProgram *shader = get_shader("dashed_lines");
#else
    GLShaderProgram *shader = OpenGLManager::get_gl_info().is_core_profile() ? get_shader("dashed_thick_lines")
                                                                             : get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr)
    {
        shader->start_using();
        const Camera &camera = get_camera();
        shader->set_uniform("view_model_matrix",
                            camera.get_view_matrix() * Geometry::translation_transform(m_camera_target.target));
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile())
        {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4> &viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.5f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
        for (int i = 0; i < 3; ++i)
        {
            m_camera_target.axis[i].render();
        }
        shader->stop_using();
    }
}

void GLCanvas3D::_render_camera_target_validation_box()
{
    const BoundingBoxf3 &curr_box = m_target_validation_box.get_bounding_box();
    const BoundingBoxf3 camera_box = get_camera().get_target_validation_box();

    if (!m_target_validation_box.is_initialized() || !is_approx(camera_box.min, curr_box.min) ||
        !is_approx(camera_box.max, curr_box.max))
    {
        m_target_validation_box.reset();

        const Vec3f b_min = camera_box.min.cast<float>();
        const Vec3f b_max = camera_box.max.cast<float>();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        init_data.reserve_vertices(12);
        init_data.reserve_indices(12);

        // vertices
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_max.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_min.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_min.z()));

        // indices
        for (unsigned int i = 0; i < 12; ++i)
        {
            init_data.add_index(i);
        }

        m_target_validation_box.init_from(std::move(init_data));
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

#if SLIC3R_OPENGL_ES
    GLShaderProgram *shader = get_shader("dashed_lines");
#else
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f * get_imgui()->get_style_scaling()));

    GLShaderProgram *shader = OpenGLManager::get_gl_info().is_core_profile() ? get_shader("dashed_thick_lines")
                                                                             : get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader == nullptr)
        return;

    shader->start_using();
    const Camera &camera = get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !SLIC3R_OPENGL_ES
        const std::array<int, 4> &viewport = camera.get_viewport();
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", 1.5f);
        shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
    }
#endif // !SLIC3R_OPENGL_ES
    m_target_validation_box.set_color(to_rgba(ColorRGB::WHITE()));
    m_target_validation_box.render();
    shader->stop_using();
}
#endif // ENABLE_SHOW_CAMERA_TARGET

void GLCanvas3D::_render_sla_slices() {}

void GLCanvas3D::_update_volumes_hover_state()
{
    // skip update if the Gizmo Measure is active
    if (m_gizmos.get_current_type() == GLGizmosManager::Measure)
        return;

    for (GLVolume *v : m_volumes.volumes)
    {
        v->hover = GLVolume::HS_None;
    }

    if (m_hover_volume_idxs.empty())
        return;

    const bool ctrl_pressed = wxGetKeyState(WXK_CONTROL);
    const bool shift_pressed = wxGetKeyState(WXK_SHIFT);
    const bool alt_pressed = wxGetKeyState(WXK_ALT);

    if (alt_pressed && (shift_pressed || ctrl_pressed))
    {
        // illegal combinations of keys
        m_hover_volume_idxs.clear();
        return;
    }

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs)
    {
        if (!m_volumes.volumes[i]->is_modifier)
        {
            hover_modifiers_only = false;
            break;
        }
    }

    std::set<std::pair<int, int>> hover_instances;
    for (int i : m_hover_volume_idxs)
    {
        const GLVolume &v = *m_volumes.volumes[i];
        hover_instances.insert(std::make_pair(v.object_idx(), v.instance_idx()));
    }

    bool hover_from_single_instance = hover_instances.size() == 1;

    if (hover_modifiers_only && !hover_from_single_instance)
    {
        // do not allow to select volumes from different instances
        m_hover_volume_idxs.clear();
        return;
    }

    for (int i : m_hover_volume_idxs)
    {
        GLVolume &volume = *m_volumes.volumes[i];
        if (volume.hover != GLVolume::HS_None)
            continue;

        const bool deselect = volume.selected && ((shift_pressed && m_rectangle_selection.is_empty()) ||
                                                  (alt_pressed && !m_rectangle_selection.is_empty()));
        const bool select = !volume.selected &&
                            (m_rectangle_selection.is_empty() || (shift_pressed && !m_rectangle_selection.is_empty()));

        if (select || deselect)
        {
            const bool as_volume = volume.is_modifier && hover_from_single_instance && !ctrl_pressed &&
                                   (!deselect || (deselect && !m_selection.is_single_full_instance() &&
                                                  volume.object_idx() == m_selection.get_object_idx() &&
                                                  volume.instance_idx() == m_selection.get_instance_idx()));

            if (as_volume)
                volume.hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
            else
            {
                const int object_idx = volume.object_idx();
                const int instance_idx = volume.instance_idx();

                for (GLVolume *v : m_volumes.volumes)
                {
                    if (v->object_idx() == object_idx && v->instance_idx() == instance_idx)
                        v->hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
                }
            }
        }
        else if (volume.selected)
            volume.hover = GLVolume::HS_Hover;
    }
}

void GLCanvas3D::_perform_layer_editing_action(const MouseInput *mouse)
{
    int object_idx_selected = m_layers_editing.last_object_id;
    if (object_idx_selected == -1)
        return;

    if (mouse != nullptr)
    {
        const Rect &rect = LayersEditing::get_bar_rect_screen(*this);
        float b = rect.get_bottom();
        m_layers_editing.last_z = m_layers_editing.object_max_z() * (b - (float) mouse->y - 1.0f) /
                                  (b - rect.get_top());
        m_layers_editing.last_action = mouse->shift ? (mouse->right_down ? LAYER_HEIGHT_EDIT_ACTION_SMOOTH
                                                                         : LAYER_HEIGHT_EDIT_ACTION_REDUCE)
                                                    : (mouse->right_down ? LAYER_HEIGHT_EDIT_ACTION_INCREASE
                                                                         : LAYER_HEIGHT_EDIT_ACTION_DECREASE);
    }

    if (m_layers_editing.state != LayersEditing::Paused)
    {
        m_layers_editing.adjust_layer_height_profile();
        _refresh_if_shown_on_screen();
    }

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

Vec3d GLCanvas3D::_mouse_to_3d(const Point &mouse_pos, const float *z, bool use_ortho)
{
    if (m_canvas == nullptr)
        return Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);

    if (z == nullptr)
    {
        const SceneRaycaster::HitResult hit = m_scene_raycaster.hit(mouse_pos.cast<double>(), get_camera(), nullptr);
        return hit.is_valid() ? hit.position.cast<double>() : _mouse_to_bed_3d(mouse_pos);
    }
    else
    {
        Camera &camera = get_camera();
        const Camera::EType type = camera.get_type();
        const Vec4i viewport(
            Eigen::Map<const Eigen::Matrix<int, 4, 1>>(camera.get_viewport().data()).cast<coord_t>().eval());
        Transform3d projection_matrix;
        if (use_ortho && type != Camera::EType::Ortho)
        {
            const double inv_zoom = camera.get_inv_zoom();
            const double left = -0.5 * inv_zoom * double(viewport[2]);
            const double bottom = -0.5 * inv_zoom * double(viewport[3]);
            const double right = 0.5 * inv_zoom * double(viewport[2]);
            const double top = 0.5 * inv_zoom * double(viewport[3]);
            const double near_z = camera.get_near_z();
            const double far_z = camera.get_far_z();
            const double inv_dx = 1.0 / (right - left);
            const double inv_dy = 1.0 / (top - bottom);
            const double inv_dz = 1.0 / (far_z - near_z);
            projection_matrix.matrix() << 2.0 * near_z * inv_dx, 0.0, (left + right) * inv_dx, 0.0, 0.0,
                2.0 * near_z * inv_dy, (bottom + top) * inv_dy, 0.0, 0.0, 0.0, -(near_z + far_z) * inv_dz,
                -2.0 * near_z * far_z * inv_dz, 0.0, 0.0, -1.0, 0.0;
        }
        else
            projection_matrix = camera.get_projection_matrix();

        Vec3d out;
        igl::unproject(Vec3d(mouse_pos.x(), viewport[3] - mouse_pos.y(), *z), camera.get_view_matrix().matrix(),
                       projection_matrix.matrix(), viewport, out);
        return out;
    }
}

Vec3d GLCanvas3D::_mouse_to_bed_3d(const Point &mouse_pos)
{
    const Linef3 ray = mouse_ray(mouse_pos);
    return (std::abs(ray.unit_vector().z()) < EPSILON) ? ray.a : ray.intersect_plane(0.0);
}

void GLCanvas3D::_load_sla_shells() {}

void GLCanvas3D::_set_warning_notification_if_needed(EWarning warning)
{
    _set_current();
    bool show = false;
    if (!m_volumes.empty())
    {
        show = _is_any_volume_outside().first;
    }
    else
    {
        if (is_editor())
        {
            if (warning == EWarning::ToolpathOutside)
                show = m_gcode_viewer.has_data() && !m_gcode_viewer.is_contained_in_bed();
            else if (warning == EWarning::GCodeConflict)
                show = m_gcode_viewer.has_data() && m_gcode_viewer.is_contained_in_bed() &&
                       m_gcode_viewer.get_conflict_result().has_value();
            else if (warning == EWarning::SequentialCollision)
                show = m_gcode_viewer.has_data() && m_gcode_viewer.get_sequential_collision_detected().has_value();
        }
    }

    _set_warning_notification(warning, show);
}

void GLCanvas3D::_set_warning_notification(EWarning warning, bool state)
{
    enum ErrorType
    {
        PLATER_WARNING,
        PLATER_ERROR,
        SLICING_ERROR
    };
    std::string text;
    ErrorType error = ErrorType::PLATER_WARNING;
    switch (warning)
    {
    case EWarning::ObjectOutside:
        text = _u8L("An object outside the print area was detected.");
        break;
    case EWarning::ToolpathOutside:
        text = _u8L("A toolpath outside the print area was detected.");
        error = ErrorType::SLICING_ERROR;
        break;
    case EWarning::SomethingNotShown:
        text = _u8L("Some objects are not visible during editing.");
        break;
    case EWarning::ObjectClashed:
        text = _u8L("An object outside the print area was detected.\n"
                    "Resolve the current problem to continue slicing.");
        error = ErrorType::PLATER_ERROR;
        break;
    case EWarning::GCodeConflict:
    {
        const ConflictResultOpt &conflict_result = m_gcode_viewer.get_conflict_result();
        if (!conflict_result.has_value())
        {
            break;
        }
        std::string objName1 = conflict_result->_objName1;
        std::string objName2 = conflict_result->_objName2;
        double height = conflict_result->_height;
        int layer = conflict_result->layer;
        // TRN %3% is name of Object1, %4% is name of Object2
        text = format(
            _u8L(
                "Conflicts in G-code paths have been detected at layer %1%, z=%2$.2f mm. Please reposition the conflicting objects (%3% <-> %4%) further apart."),
            layer, height, objName1, objName2);
        error = ErrorType::SLICING_ERROR;
        break;
    }
    case EWarning::SequentialCollision:
    {
        auto conflict = m_gcode_viewer.get_sequential_collision_detected();
        if (!conflict.has_value())
            break;
        // TRN: Placeholders contain names of the colliding objects.
        text = format(_u8L("Extruder will crash into %1% while printing %2%."), conflict->first, conflict->second);
        error = ErrorType::SLICING_ERROR;
        break;
    }
    }
    auto &notification_manager = *m_notification_manager;

    const ConflictResultOpt &conflict_result = m_gcode_viewer.get_conflict_result();
    if (warning == EWarning::GCodeConflict)
    {
        if (conflict_result.has_value())
        {
            const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(conflict_result->_obj2);
            auto mo = obj2->model_object();
            ObjectID id = mo->id();
            int layer_id = conflict_result->layer;
            auto action_fn = [this, id, layer_id](wxEvtHandler *)
            {
                auto &objects = m_model->objects;
                auto iter = id.id ? std::find_if(objects.begin(), objects.end(), [id](auto o) { return o->id() == id; })
                                  : objects.end();
                if (iter != objects.end())
                {
                    const unsigned int obj_idx = std::distance(objects.begin(), iter);
                    if (!m_call_after || !m_set_preview_layer_range)
                        return false;
                    m_call_after(
                        [this, obj_idx, layer_id]()
                        {
                            m_set_preview_layer_range(0, layer_id - 1);
                            m_event_poster->postEvent(CanvasEventType::ViewToolbar3D);
                            reset_all_gizmos();
                            m_selection.add_object(obj_idx, true);
                            m_event_poster->postEvent(CanvasEventType::UpdateSelections);
                        });
                }
                return false;
            };
            auto hypertext = _u8L("Jump to");
            hypertext += std::string(" [") + mo->name + "]";
            notification_manager.push_notification(NotificationType::SlicingError,
                                                   NotificationManager::NotificationLevel::ErrorNotificationLevel,
                                                   _u8L("ERROR:") + "\n" + text, hypertext, action_fn);
        }
        else
            notification_manager.close_slicing_error_notification(text);

        return;
    }

    switch (error)
    {
    case PLATER_WARNING:
        if (state)
            notification_manager.push_plater_warning_notification(text);
        else
            notification_manager.close_plater_warning_notification(text);
        break;
    case PLATER_ERROR:
        if (state)
            notification_manager.push_plater_error_notification(text);
        else
            notification_manager.close_plater_error_notification(text);
        break;
    case SLICING_ERROR:
        if (state)
            notification_manager.push_slicing_error_notification(text);
        else
            notification_manager.close_slicing_error_notification(text);
        break;
    default:
        break;
    }
}

std::pair<bool, const GLVolume *> GLCanvas3D::_is_any_volume_outside() const
{
    for (const GLVolume *volume : m_volumes.volumes)
    {
        if (volume != nullptr && volume->is_outside)
            return std::make_pair(true, volume);
    }

    return std::make_pair(false, nullptr);
}

bool GLCanvas3D::_is_sequential_print_enabled() const
{
    return current_printer_technology() == ptFFF && fff_print()->config().complete_objects;
}

void GLCanvas3D::_update_selection_from_hover()
{
    bool ctrl_pressed = wxGetKeyState(WXK_CONTROL);
    bool selection_changed = false;

    if (m_hover_volume_idxs.empty())
    {
        if (!ctrl_pressed && m_rectangle_selection.get_state() == GLSelectionRectangle::EState::Select)
        {
            selection_changed = !m_selection.is_empty();
            m_selection.remove_all();
        }
    }

    GLSelectionRectangle::EState state = m_rectangle_selection.get_state();

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs)
    {
        if (!m_volumes.volumes[i]->is_modifier)
        {
            hover_modifiers_only = false;
            break;
        }
    }

    if (!m_rectangle_selection.is_empty())
    {
        if (state == GLSelectionRectangle::EState::Select)
        {
            bool contains_all = true;
            for (int i : m_hover_volume_idxs)
            {
                if (!m_selection.contains_volume((unsigned int) i))
                {
                    contains_all = false;
                    break;
                }
            }

            // the selection is going to be modified (Add)
            if (!contains_all)
            {
                m_event_poster->postEvent(CanvasEventType::TakeSnapshotSelection, _u8L("Selection-Add from rectangle"));
                selection_changed = true;
            }
        }
        else
        {
            bool contains_any = false;
            for (int i : m_hover_volume_idxs)
            {
                if (m_selection.contains_volume((unsigned int) i))
                {
                    contains_any = true;
                    break;
                }
            }

            // the selection is going to be modified (Remove)
            if (contains_any)
            {
                m_event_poster->postEvent(CanvasEventType::TakeSnapshotSelection,
                                          _u8L("Selection-Remove from rectangle"));
                selection_changed = true;
            }
        }
    }

    if (!selection_changed)
        return;

    if (m_suppress_snapshots_start)
        m_suppress_snapshots_start();
    struct ScopeGuard
    {
        std::function<void()> fn;
        ~ScopeGuard()
        {
            if (fn)
                fn();
        }
    } suppress{m_suppress_snapshots_end};

    if (state == GLSelectionRectangle::EState::Select && !ctrl_pressed)
        m_selection.clear();

    for (int i : m_hover_volume_idxs)
    {
        if (state == GLSelectionRectangle::EState::Select)
        {
            if (hover_modifiers_only)
            {
                const GLVolume &v = *m_volumes.volumes[i];
                m_selection.add_volume(v.object_idx(), v.volume_idx(), v.instance_idx(), false);
            }
            else
                m_selection.add(i, false);
        }
        else
            m_selection.remove(i);
    }

    if (m_selection.is_empty())
        m_gizmos.reset_all_states();
    else
        m_gizmos.refresh_on_off_state();

    m_gizmos.update_data();
    m_event_poster->postEvent(CanvasEventType::ObjectSelect);
    m_dirty = true;
}

bool GLCanvas3D::_deactivate_undo_redo_toolbar_items()
{
    if (m_undoredo_toolbar.is_item_pressed("undo"))
    {
        m_undoredo_toolbar.force_right_action(m_undoredo_toolbar.get_item_id("undo"), *this);
        return true;
    }
    else if (m_undoredo_toolbar.is_item_pressed("redo"))
    {
        m_undoredo_toolbar.force_right_action(m_undoredo_toolbar.get_item_id("redo"), *this);
        return true;
    }

    return false;
}

bool GLCanvas3D::_deactivate_arrange_menu()
{
    if (m_main_toolbar.is_item_pressed("arrange"))
    {
        m_main_toolbar.force_right_action(m_main_toolbar.get_item_id("arrange"), *this);
        return true;
    }

    if (m_main_toolbar.is_item_pressed("arrangecurrent"))
    {
        m_main_toolbar.force_right_action(m_main_toolbar.get_item_id("arrangecurrent"), *this);
        return true;
    }

    return false;
}

bool GLCanvas3D::_deactivate_collapse_toolbar_items()
{
    if (m_collapse_toolbar->is_item_pressed("print"))
    {
        m_collapse_toolbar->force_left_action(m_collapse_toolbar->get_item_id("print"), *this);
        return true;
    }

    return false;
}

void GLCanvas3D::highlight_toolbar_item(const std::string &item_name)
{
    GLToolbarItem *item = m_main_toolbar.get_item(item_name);
    if (!item)
        item = m_undoredo_toolbar.get_item(item_name);
    if (!item || !item->is_visible())
        return;
    m_toolbar_highlighter.init(item, this);
}

void GLCanvas3D::highlight_gizmo(const std::string &gizmo_name)
{
    GLGizmosManager::EType gizmo = m_gizmos.get_gizmo_from_name(gizmo_name);
    if (gizmo == GLGizmosManager::EType::Undefined)
        return;
    m_gizmo_highlighter.init(&m_gizmos, gizmo, this);
}

const Print *GLCanvas3D::fff_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->fff_print();
}

const SLAPrint *GLCanvas3D::sla_print() const
{
    return nullptr;
}

void GLCanvas3D::WipeTowerInfo::apply_wipe_tower(Model &model, Vec2d pos, double rot, int bed_index)
{
    model.wipe_tower(bed_index).position = pos;
    model.wipe_tower(bed_index).rotation = (180. / M_PI) * rot;
}

void GLCanvas3D::ToolbarHighlighter::init(GLToolbarItem *toolbar_item, GLCanvas3D *canvas)
{
    if (m_timer && m_timer->isRunning())
        invalidate();
    if (!toolbar_item || !canvas)
        return;

    if (m_timer)
        m_timer->start(300, false);

    m_toolbar_item = toolbar_item;
    m_canvas = canvas;
}

void GLCanvas3D::ToolbarHighlighter::invalidate()
{
    if (m_timer)
        m_timer->stop();

    if (m_toolbar_item)
        m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::NotHighlighted);

    m_toolbar_item = nullptr;
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::ToolbarHighlighter::blink()
{
    if (m_toolbar_item)
    {
        char state = m_toolbar_item->get_highlight();
        if (state != (char) GLToolbarItem::EHighlightState::HighlightedShown)
            m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedShown);
        else
            m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedHidden);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

void GLCanvas3D::GizmoHighlighter::init(GLGizmosManager *manager, GLGizmosManager::EType gizmo, GLCanvas3D *canvas)
{
    if (m_timer && m_timer->isRunning())
        invalidate();
    if (gizmo == GLGizmosManager::EType::Undefined || !canvas)
        return;

    if (m_timer)
        m_timer->start(300, false);

    m_gizmo_manager = manager;
    m_gizmo_type = gizmo;
    m_canvas = canvas;
}

void GLCanvas3D::GizmoHighlighter::invalidate()
{
    if (m_timer)
        m_timer->stop();

    if (m_gizmo_manager)
    {
        m_gizmo_manager->set_highlight(GLGizmosManager::EType::Undefined, false);
    }
    m_gizmo_manager = nullptr;
    m_gizmo_type = GLGizmosManager::EType::Undefined;
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::GizmoHighlighter::blink()
{
    if (m_gizmo_manager)
    {
        if (m_blink_counter % 2 == 0)
            m_gizmo_manager->set_highlight(m_gizmo_type, true);
        else
            m_gizmo_manager->set_highlight(m_gizmo_type, false);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
void GLCanvas3D::show_binary_gcode_debug_window()
{
    bgcode::binarize::BinarizerConfig &binarizer_config = GCodeProcessor::get_binarizer_config();

    ImGuiWrapper &imgui = *get_imgui();
    ImGuiPureWrap::begin(std::string("Binary GCode"),
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    using namespace bgcode::core;
    if (ImGui::BeginTable("BinaryGCodeConfig", 2))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "File metadata compression");
        ImGui::TableSetColumnIndex(1);
        std::vector<std::string> options = {"None", "Deflate", "heatshrink 11,4", "heatshrink 12,4"};
        int option_id = (int) binarizer_config.compression.file_metadata;
        if (imgui.combo(std::string("##file_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest,
                        0.0f, 175.0f))
            binarizer_config.compression.file_metadata = (ECompressionType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Printer metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int) binarizer_config.compression.printer_metadata;
        if (imgui.combo(std::string("##printer_metadata_compression"), options, option_id,
                        ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.printer_metadata = (ECompressionType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Print metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int) binarizer_config.compression.print_metadata;
        if (imgui.combo(std::string("##print_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest,
                        0.0f, 175.0f))
            binarizer_config.compression.print_metadata = (ECompressionType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Slicer metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int) binarizer_config.compression.slicer_metadata;
        if (imgui.combo(std::string("##slicer_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest,
                        0.0f, 175.0f))
            binarizer_config.compression.slicer_metadata = (ECompressionType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "GCode compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int) binarizer_config.compression.gcode;
        if (imgui.combo(std::string("##gcode_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f,
                        175.0f))
            binarizer_config.compression.gcode = (ECompressionType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "GCode encoding");
        ImGui::TableSetColumnIndex(1);
        options = {"None", "MeatPack", "MeatPack Comments"};
        option_id = (int) binarizer_config.gcode_encoding;
        if (imgui.combo(std::string("##gcode_encoding"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f,
                        175.0f))
            binarizer_config.gcode_encoding = (EGCodeEncodingType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Metadata encoding");
        ImGui::TableSetColumnIndex(1);
        options = {"INI"};
        option_id = (int) binarizer_config.metadata_encoding;
        if (imgui.combo(std::string("##metadata_encoding"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f,
                        175.0f))
            binarizer_config.metadata_encoding = (EMetadataEncodingType) option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Checksum type");
        ImGui::TableSetColumnIndex(1);
        options = {"None", "CRC32"};
        option_id = (int) binarizer_config.checksum;
        if (imgui.combo(std::string("##4"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.checksum = (EChecksumType) option_id;

        ImGui::EndTable();

        ImGui::Separator();
        ImGuiPureWrap::text("!!! WARNING !!!");
        ImGuiPureWrap::text("Changing values does NOT invalidate the current slice");
    }

    ImGuiPureWrap::end();
}
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW

const ModelVolume *get_model_volume(const GLVolume &v, const Model &model)
{
    const ModelVolume *ret = nullptr;

    if (v.object_idx() < (int) model.objects.size())
    {
        const ModelObject *obj = model.objects[v.object_idx()];
        if (v.volume_idx() < (int) obj->volumes.size())
            ret = obj->volumes[v.volume_idx()];
    }

    return ret;
}

ModelVolume *get_model_volume(const ObjectID &volume_id, const ModelObjectPtrs &objects)
{
    for (const ModelObject *obj : objects)
        for (ModelVolume *vol : obj->volumes)
            if (vol->id() == volume_id)
                return vol;
    return nullptr;
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObject &object)
{
    if (v.volume_idx() < 0)
        return nullptr;

    size_t volume_idx = static_cast<size_t>(v.volume_idx());
    if (volume_idx >= object.volumes.size())
        return nullptr;

    return object.volumes[volume_idx];
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObjectPtrs &objects)
{
    if (v.object_idx() < 0)
        return nullptr;
    size_t objext_idx = static_cast<size_t>(v.object_idx());
    if (objext_idx >= objects.size())
        return nullptr;
    if (objects[objext_idx] == nullptr)
        return nullptr;
    return get_model_volume(v, *objects[objext_idx]);
}

GLVolume *get_first_hovered_gl_volume(const GLCanvas3D &canvas)
{
    int hovered_id_signed = canvas.get_first_hover_volume_idx();
    if (hovered_id_signed < 0)
        return nullptr;

    size_t hovered_id = static_cast<size_t>(hovered_id_signed);
    const GLVolumePtrs &volumes = canvas.get_volumes().volumes;
    if (hovered_id >= volumes.size())
        return nullptr;

    return volumes[hovered_id];
}

GLVolume *get_selected_gl_volume(const GLCanvas3D &canvas)
{
    const GLVolume *gl_volume = get_selected_gl_volume(canvas.get_selection());
    if (gl_volume == nullptr)
        return nullptr;

    const GLVolumePtrs &gl_volumes = canvas.get_volumes().volumes;
    for (GLVolume *v : gl_volumes)
        if (v->composite_id == gl_volume->composite_id)
            return v;
    return nullptr;
}

ModelObject *get_model_object(const GLVolume &gl_volume, const Model &model)
{
    return get_model_object(gl_volume, model.objects);
}

ModelObject *get_model_object(const GLVolume &gl_volume, const ModelObjectPtrs &objects)
{
    if (gl_volume.object_idx() < 0)
        return nullptr;
    size_t objext_idx = static_cast<size_t>(gl_volume.object_idx());
    if (objext_idx >= objects.size())
        return nullptr;
    return objects[objext_idx];
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const Model &model)
{
    return get_model_instance(gl_volume, model.objects);
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObjectPtrs &objects)
{
    if (gl_volume.instance_idx() < 0)
        return nullptr;
    ModelObject *object = get_model_object(gl_volume, objects);
    return get_model_instance(gl_volume, *object);
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObject &object)
{
    if (gl_volume.instance_idx() < 0)
        return nullptr;
    size_t instance_idx = static_cast<size_t>(gl_volume.instance_idx());
    if (instance_idx >= object.instances.size())
        return nullptr;
    return object.instances[instance_idx];
}

} // namespace GUI
} // namespace Slic3r
