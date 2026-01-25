///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "GLGizmoSeam.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#if SLIC3R_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace Slic3r::GUI
{

void GLGizmoSeam::on_shutdown()
{
    m_parent.toggle_model_objects_visibility(true);
}

bool GLGizmoSeam::on_init()
{
    m_shortcut_key = WXK_CONTROL_P;

    m_desc["clipping_of_view"] = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"] = _u8L("Reset direction");
    m_desc["cursor_size"] = _u8L("Brush size") + ": ";
    m_desc["cursor_type"] = _u8L("Brush shape") + ": ";
    m_desc["enforce_caption"] = _u8L("Left mouse button") + ": ";
    m_desc["enforce"] = _u8L("Enforce seam");
    m_desc["block_caption"] = _u8L("Right mouse button") + ": ";
    m_desc["block"] = _u8L("Block seam");
    m_desc["draw_caption"] = _u8L("Ctrl + Left mouse button") + ": ";
    m_desc["draw"] = _u8L("Draw line");
    m_desc["remove_caption"] = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove"] = _u8L("Remove selection");
    m_desc["remove_all"] = _u8L("Remove all selection");
    m_desc["circle"] = _u8L("Circle");
    m_desc["sphere"] = _u8L("Sphere");
    m_desc["seam_detection"] = _u8L("Seam detection") + ": ";

    // Load saved value from app config
    std::string detection_str = wxGetApp().app_config->get("seam_detection_radius");
    m_seam_detection = detection_str.empty() ? 0.05f : std::stof(detection_str);

    // Set global value for SeamPlacer to use
    SeamGlobalParams::setSeamDetectionRadius(m_seam_detection);

    return true;
}

std::string GLGizmoSeam::on_get_name() const
{
    return _u8L("Seam painting");
}

void GLGizmoSeam::render_painter_gizmo()
{
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoSeam::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    // Need to measure? Position way above screen at correct X
    if (m_popup_render_count == 0 && m_popup_height <= 0.0f)
    {
        ImGuiPureWrap::set_next_window_pos(x, -500.0f, ImGuiCond_Always, 1.0f, 0.0f);
    }
    else
    {
        // Center on button
        float menu_y = y - m_popup_height * 0.5f;

        menu_y = std::max(0.0f, menu_y);

        ImGuiPureWrap::set_next_window_pos(x, menu_y, ImGuiCond_Always, 1.0f, 0.0f);
    }

    m_popup_render_count++;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse;
    if (m_popup_render_count == 1 && m_popup_height <= 0.0f)
    {
        // First frame: make window completely invisible
        window_flags |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    }
    ImGuiPureWrap::begin(get_name(), window_flags);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                                ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) +
                                       m_imgui->scaled(1.5f);
    const float cursor_size_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("cursor_size")).x +
                                          m_imgui->scaled(1.f);
    const float seam_detection_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("seam_detection")).x +
                                             m_imgui->scaled(1.f);

    const float cursor_type_radio_left = ImGuiPureWrap::calc_text_size(m_desc["cursor_type"]).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_sphere = ImGuiPureWrap::calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_circle = ImGuiPureWrap::calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);

    const float button_width = ImGuiPureWrap::calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 4>{"enforce", "block", "draw", "remove"})
    {
        caption_max = std::max(caption_max, ImGuiPureWrap::calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, ImGuiPureWrap::calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(
        {cursor_size_slider_left, clipping_slider_left, seam_detection_slider_left});
    const float slider_icon_width = ImGuiPureWrap::get_slider_icon_size().x;
    float window_width = minimal_slider_width + sliders_left_width + slider_icon_width;
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);
    window_width = std::max(window_width, cursor_type_radio_left + cursor_type_radio_sphere + cursor_type_radio_circle);

    auto draw_text_with_caption = [&caption_max](const std::string &caption, const std::string &text)
    {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        ImGuiPureWrap::text(text);
    };

    for (const auto &t : std::array<std::string, 4>{"enforce", "block", "draw", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("cursor_size"));
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true,
                          _L("Alt + Mouse wheel"));

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("cursor_type"));

    float cursor_type_offset = cursor_type_radio_left +
                               (window_width - cursor_type_radio_left - cursor_type_radio_sphere -
                                cursor_type_radio_circle + m_imgui->scaled(0.5f)) /
                                   2.f;
    ImGui::SameLine(cursor_type_offset);
    ImGui::PushItemWidth(cursor_type_radio_sphere);
    if (ImGuiPureWrap::radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
        m_cursor_type = TriangleSelector::CursorType::SPHERE;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

    ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
    ImGui::PushItemWidth(cursor_type_radio_circle);
    if (ImGuiPureWrap::radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Ignores facets facing away from the camera."), max_tooltip_width);

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f)
    {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("clipping_of_view"));
    }
    else
    {
        if (ImGuiPureWrap::button(m_desc.at("reset_direction")))
        {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true,
                              from_u8(GUI::shortkey_ctrl_prefix()) + _L("Mouse wheel")))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    ImGui::Separator();

    // Seam detection slider (0.01-1.00mm)
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("seam_detection"));
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);

    static bool was_dragging = false;
    static float last_saved_value = -1.0f;

    // Initialize on first run
    if (last_saved_value < 0)
    {
        last_saved_value = m_seam_detection;
    }

    if (m_imgui->slider_float("##seam_detection", &m_seam_detection, 0.01f, 1.0f, "%.2f mm"))
    {
        m_parent.set_as_dirty();
        // Update value immediately for visual feedback
        SeamGlobalParams::setSeamDetectionRadius(m_seam_detection);
    }

    // Just use mouse state since ImGui::IsItemActive() doesn't work properly here
    bool mouse_down = ImGui::IsMouseDown(0);

    // Detect when we STOP dragging (was dragging, now not)
    if (was_dragging && !mouse_down)
    {
        // Save and invalidate ONLY when we release
        wxGetApp().app_config->set("seam_detection_radius", std::to_string(m_seam_detection));
        wxPostEvent(wxGetApp().plater(), SimpleEvent(EVT_FORCE_INVALIDATE_SLICE));
        last_saved_value = m_seam_detection;
    }

    was_dragging = mouse_down && (m_seam_detection != last_saved_value);

    ImGui::Separator();
    if (ImGuiPureWrap::button(m_desc.at("remove_all")))
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
        ModelObject *mo = m_c->selection_info()->model_object();
        int idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part())
            {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data();
            }

        update_model_object();
        m_parent.set_as_dirty();
    }

    const ImVec2 size = ImGui::GetWindowSize();
    if (size.y > 0.0f && m_popup_height != size.y)
    {
        m_popup_height = size.y;
        // Request extra frame if height changed significantly
        if (m_popup_render_count == 1)
        {
            m_imgui->set_requires_extra_frame();
        }
    }

    ImGuiPureWrap::end();

    if (m_popup_render_count == 1 && m_popup_height > 0.0f)
    {
        ImGui::PopStyleVar();
    }
}

void GLGizmoSeam::update_model_object() const
{
    bool updated = false;
    ModelObject *mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->seam_facets.set(*m_triangle_selectors[idx]);
    }

    if (updated)
    {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoSeam::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh *mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->seam_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

PainterGizmoType GLGizmoSeam::get_painter_type() const
{
    return PainterGizmoType::SEAM;
}

wxString GLGizmoSeam::handle_snapshot_action_name(bool control_down, GLGizmoPainterBase::Button button_down) const
{
    wxString action_name;
    if (control_down)
        action_name = _L("Remove selection");
    else
    {
        if (button_down == Button::Left)
            action_name = _L("Enforce seam");
        else
            action_name = _L("Block seam");
    }
    return action_name;
}

} // namespace Slic3r::GUI
