///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GLGizmoCounterboreBridge.hpp"

#include "luminary/model/scene/Model.hpp"
#include "luminary/layer/print/Print.hpp"

#include "DSKY/GUI/Camera.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/GUI/GUI.hpp"
#include "DSKY/GUI/EventTypes.hpp"
#include "DSKY/GUI/EventBridge.hpp"
#include "DSKY/GUI/ImGuiWrapper.hpp"
#include "DSKY/GUI/MsgDialog.hpp"
#include "DSKY/GUI/Tab.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "DSKY/Utils/UndoRedo.hpp"

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif
#include <algorithm>

namespace DSKY
{
using namespace Luminary;

void GLGizmoCounterboreBridge::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoCounterboreBridge::on_get_name() const
{
    return _u8L("Counterbore bridge");
}

bool GLGizmoCounterboreBridge::on_init()
{
    m_shortcut_key = WXK_CONTROL_K;

    // Force smart fill mode - no brush/circle tools for counterbore painting
    m_tool_type = ToolType::SMART_FILL;

    // Only allow painting on overhang faces (counterbore ceilings)
    m_paint_on_overhangs_only = true;
    m_highlight_by_angle_threshold_deg = 90.f;

    m_desc["clipping_of_view"] = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"] = _u8L("Reset direction");
    m_desc["add_bridge_caption"] = _u8L("Left mouse button") + ": ";
    m_desc["add_bridge"] = _u8L("Mark counterbore");
    m_desc["remove_bridge_caption"] = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove_bridge"] = _u8L("Remove marking");
    m_desc["remove_all"] = _u8L("Remove all selection");
    m_desc["smart_fill_angle"] = _u8L("Smart fill angle");

    return true;
}

void GLGizmoCounterboreBridge::render_painter_gizmo()
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

void GLGizmoCounterboreBridge::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    // Measure-then-position popup pattern (same as FuzzySkin gizmo)
    if (m_popup_render_count == 0 && m_popup_height <= 0.0f)
    {
        set_side_flyout_pos(x, -500.0f);
    }
    else
    {
        float menu_y = y - m_popup_height * 0.5f;
        menu_y = std::max(0.0f, menu_y);
        set_side_flyout_pos(x, menu_y);
    }

    m_popup_render_count++;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse;
    if (m_popup_render_count == 1 && m_popup_height <= 0.0f)
    {
        window_flags |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    }
    ImGuiPureWrap::begin(get_name(), window_flags);

    const float clipping_slider_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                                ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) +
                                       m_imgui->scaled(1.5f);
    const float smart_fill_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("smart_fill_angle")).x +
                                         m_imgui->scaled(1.f);

    const float button_width = ImGuiPureWrap::calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width = m_imgui->scaled(0.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max = 0.f;
    float total_text_max = 0.f;
    for (const std::string t : {"add_bridge", "remove_bridge"})
    {
        caption_max = std::max(caption_max, ImGuiPureWrap::calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, ImGuiPureWrap::calc_text_size(m_desc[t]).x);
    }

    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(smart_fill_slider_left, clipping_slider_left);
    const float slider_icon_width = ImGuiPureWrap::get_slider_icon_size().x;
    float window_width = minimal_slider_width + sliders_left_width + slider_icon_width;
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);
    window_width = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [&caption_max](const std::string &caption, const std::string &text)
    {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        ImGuiPureWrap::text(text);
    };

    for (const std::string t : {"add_bridge", "remove_bridge"})
    {
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));
    }

    ImGui::Separator();

    std::string format_str =
        std::string("%.f") +
        I18N::translate_utf8("°", "Degree sign to use in the respective slider in counterbore bridge gizmo,"
                                  "placed after the number with no whitespace in between.");

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc["smart_fill_angle"] + ":");

    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax,
                              format_str.data(), 1.0f, true, _L("Alt + Mouse wheel")))
        for (auto &triangle_selector : m_triangle_selectors)
        {
            triangle_selector->seed_fill_unselect_all_triangles();
            triangle_selector->request_update_render_data();
        }

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
            m_parent.call_after([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true,
                              from_u8(DSKY::shortkey_ctrl_prefix()) + _L("Mouse wheel")))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    ImGui::Separator();

    // Bridge layers slider - per-painted-area value, encoded in triangle state.
    // When changed after clicking a painted region, repaint that region with the new value.
    {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(_u8L("Bridge layers") + ": ");
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (ImGui::SliderInt("##cb_layers", &m_bridge_layers, 2, 9))
        {
            auto new_state = static_cast<TriangleStateType>(m_bridge_layers);

            if (m_last_cb_mesh_id >= 0 && m_last_cb_mesh_id < int(m_triangle_selectors.size()))
            {
                // Have a cached click - repaint that specific region
                const ModelObject *mo = m_c->selection_info()->model_object();
                if (mo)
                {
                    const Selection &selection = m_parent.get_selection();
                    const ModelInstance *mi = mo->instances[selection.get_instance_idx()];
                    int vi = -1;
                    for (const ModelVolume *mv : mo->volumes)
                    {
                        if (!mv->is_model_part())
                            continue;
                        if (++vi == m_last_cb_mesh_id)
                        {
                            Transform3d trafo = mi->get_transformation().get_matrix() * mv->get_matrix();
                            Transform3d trafo_no_offset = mi->get_transformation().get_matrix_no_offset() *
                                                          mv->get_matrix_no_offset();
                            const TriangleSelector::ClippingPlane &clp = get_clipping_plane_in_volume_coordinates(
                                trafo);

                            m_triangle_selectors[m_last_cb_mesh_id]->seed_fill_select_triangles(
                                m_last_cb_hit, m_last_cb_facet, trafo_no_offset, clp, m_smart_fill_angle,
                                SmartFillGapArea, 0.f, TriangleSelector::ForceReselection::YES);
                            m_triangle_selectors[m_last_cb_mesh_id]->seed_fill_apply_on_triangles(new_state);
                            m_triangle_selectors[m_last_cb_mesh_id]->seed_fill_select_triangles(
                                m_last_cb_hit, m_last_cb_facet, trafo_no_offset, clp, m_smart_fill_angle,
                                SmartFillGapArea, 0.f, TriangleSelector::ForceReselection::YES);
                            break;
                        }
                    }
                }
                m_triangle_selectors[m_last_cb_mesh_id]->request_update_render_data();
            }
            else
            {
                // No cached click (e.g., re-entered gizmo after slicing).
                // Update all painted triangles across all volumes to the new layer count.
                const ModelObject *mo = m_c->selection_info()->model_object();
                if (mo)
                {
                    int idx = -1;
                    for (const ModelVolume *mv : mo->volumes)
                    {
                        if (!mv->is_model_part())
                            continue;
                        ++idx;
                        if (idx >= int(m_triangle_selectors.size()))
                            break;
                        auto &ts = m_triangle_selectors[idx];
                        int num_orig = int(mv->mesh().its.indices.size());
                        for (int fi = 0; fi < num_orig; ++fi)
                            if (ts->get_triangle_leaf_state(fi) != TriangleStateType::NONE)
                                ts->set_facet(fi, new_state);
                        ts->request_update_render_data();
                    }
                }
            }

            update_model_object();
            m_parent.set_as_dirty();
        }
    }

    ImGui::Separator();
    if (ImGuiPureWrap::button(m_desc.at("remove_all")))
    {
        m_parent.take_gizmo_snapshot(_u8L("Reset selection"));
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

bool GLGizmoCounterboreBridge::gizmo_event(GizmoEventType action, const Vec2d &mouse_position, bool shift_down,
                                           bool alt_down, bool control_down)
{
    // On left-click (paint), check if the hit triangle is already painted.
    // If so, adopt its bridge_layers value so the slider reflects the existing setting.
    if (action == GizmoEventType::LeftDown && !shift_down)
    {
        const ModelObject *mo = m_c->selection_info()->model_object();
        if (mo)
        {
            const Camera &camera = m_parent.get_camera();
            const Selection &selection = m_parent.get_selection();
            const ModelInstance *mi = mo->instances[selection.get_instance_idx()];

            // Raycast each model-part volume to find the closest hit triangle
            int vol_idx = -1;
            size_t best_facet = 0;
            int best_vol = -1;
            double best_dist_sq = std::numeric_limits<double>::max();
            for (const ModelVolume *mv : mo->volumes)
            {
                if (!mv->is_model_part())
                    continue;
                ++vol_idx;
                Transform3d trafo = mi->get_transformation().get_matrix() * mv->get_matrix();
                Vec3f hit, normal;
                size_t facet = 0;
                if (m_c->raycaster()->raycasters()[vol_idx]->unproject_on_mesh(
                        mouse_position, trafo, camera, hit, normal, m_c->object_clipper()->get_clipping_plane(), &facet,
                        false))
                {
                    double d = (camera.get_position() - trafo * hit.cast<double>()).squaredNorm();
                    if (d < best_dist_sq)
                    {
                        best_dist_sq = d;
                        best_facet = facet;
                        best_vol = vol_idx;
                    }
                }
            }

            if (best_vol >= 0 && best_vol < int(m_triangle_selectors.size()))
            {
                // If clicking an already-painted area, adopt its bridge_layers value
                int state_val = static_cast<int>(
                    m_triangle_selectors[best_vol]->get_triangle_leaf_state(int(best_facet)));
                if (state_val >= 2 && state_val <= 9)
                    m_bridge_layers = state_val;

                // Always track the hit for slider-driven adjustment (initial paint or repaint)
                m_last_cb_mesh_id = best_vol;
                m_last_cb_facet = int(best_facet);
                const ModelVolume *hit_mv = nullptr;
                int vi = -1;
                for (const ModelVolume *v : mo->volumes)
                {
                    if (!v->is_model_part())
                        continue;
                    if (++vi == best_vol)
                    {
                        hit_mv = v;
                        break;
                    }
                }
                if (hit_mv)
                {
                    Transform3d trafo = mi->get_transformation().get_matrix() * hit_mv->get_matrix();
                    Vec3f hit_tmp, normal_tmp;
                    size_t facet_tmp = 0;
                    m_c->raycaster()->raycasters()[best_vol]->unproject_on_mesh(
                        mouse_position, trafo, camera, hit_tmp, normal_tmp, m_c->object_clipper()->get_clipping_plane(),
                        &facet_tmp, false);
                    m_last_cb_hit = hit_tmp;
                }
            }
        }
    }

    return GLGizmoPainterBase::gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
}

void GLGizmoCounterboreBridge::update_model_object() const
{
    bool updated = false;
    ModelObject *mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;

        ++idx;
        updated |= mv->counterbore_bridge_facets.set(*m_triangle_selectors[idx]);
    }

    if (updated)
    {
        const ModelObjectPtrs &mos = m_parent.get_model()->objects;
        m_parent.event_poster()->postEvent(CanvasEventType::UpdateInfoItems,
                                           int(std::find(mos.begin(), mos.end(), mo) - mos.begin()));

        m_parent.event_poster()->postEvent(CanvasEventType::ScheduleBackgroundProcess);
    }
}

void GLGizmoCounterboreBridge::update_from_model_object()
{
    wxBusyCursor wait;

    // Invalidate cached click state - volume indices may have shifted
    m_last_cb_mesh_id = -1;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;

        ++volume_id;

        const TriangleMesh *mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        m_triangle_selectors.back()->deserialize(mv->counterbore_bridge_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

PainterGizmoType GLGizmoCounterboreBridge::get_painter_type() const
{
    return PainterGizmoType::COUNTERBORE_BRIDGE;
}

wxString GLGizmoCounterboreBridge::handle_snapshot_action_name(bool control_down,
                                                               GLGizmoPainterBase::Button button_down) const
{
    return control_down ? _L("Remove counterbore bridge") : _L("Add counterbore bridge");
}

} // namespace DSKY
