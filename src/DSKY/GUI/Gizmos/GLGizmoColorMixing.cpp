///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GLGizmoColorMixing.hpp"
#include "TriangleSelectorMmGui.hpp"

#include "luminary/model/scene/Model.hpp"
#include "luminary/colour/mixing/ColorMixer.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "DSKY/GUI/Camera.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/GUI/ImGuiWrapper.hpp"
#include "DSKY/GUI/NotificationManager.hpp"
#include "DSKY/GUI/format.hpp"
#include "DSKY/GUI/EventTypes.hpp"
#include "DSKY/GUI/EventBridge.hpp"

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace DSKY
{
using namespace Luminary;

TriangleStateType GLGizmoColorMixing::get_left_button_state_type() const
{
    // Eraser mode overrides the left-click state to NONE so painting clears existing color
    // assignments back to "unpainted" rather than applying a new color.
    if (m_eraser_mode)
        return TriangleStateType::NONE;
    return TriangleStateType(m_first_selected_color_idx + 1);
}

TriangleStateType GLGizmoColorMixing::get_right_button_state_type() const
{
    if (m_eraser_mode)
        return TriangleStateType::NONE;
    return TriangleStateType(m_second_selected_color_idx + 1);
}

bool GLGizmoColorMixing::on_init()
{
    m_shortcut_key = WXK_CONTROL_Y;

    m_desc["clipping_of_view"] = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"] = _u8L("Reset direction");
    m_desc["cursor_size"] = _u8L("Brush size") + ": ";
    m_desc["cursor_type"] = _u8L("Brush shape");
    m_desc["remove_all"] = _u8L("Clear all");
    m_desc["circle"] = _u8L("Circle");
    m_desc["sphere"] = _u8L("Sphere");
    m_desc["pointer"] = _u8L("Triangles");
    m_desc["tool_type"] = _u8L("Tool type");
    m_desc["tool_brush"] = _u8L("Brush");
    m_desc["tool_smart_fill"] = _u8L("Smart fill");
    m_desc["tool_bucket_fill"] = _u8L("Bucket fill");
    m_desc["tool_height_range"] = _u8L("Height range");
    m_desc["smart_fill_angle"] = _u8L("Smart fill angle");
    m_desc["bucket_fill_angle"] = _u8L("Bucket fill angle");
    m_desc["height_range_z_range"] = _u8L("Height range");
    m_desc["split_triangles"] = _u8L("Split triangles");

    // Color-coded direction hints
    m_desc["paint_caption"] = _u8L("Left mouse button") + ": ";
    m_desc["paint_action"] = _u8L("Paint color");
    m_desc["erase_caption"] =
#ifdef __APPLE__
        _u8L("Cmd + Left mouse button") + ": ";
#else
        _u8L("Ctrl + Left mouse button") + ": ";
#endif
    m_desc["erase_action"] = _u8L("Remove color");
    m_desc["alt_caption"] = _u8L("Alt + Mouse wheel") + ": ";
    m_desc["alt_brush"] = _u8L("Change brush size");
    m_desc["alt_fill"] = _u8L("Change angle");
    m_desc["alt_height_range"] = _u8L("Change height range");
    m_desc["pick_caption"] = _u8L("Alt + Left mouse button") + ": ";
    m_desc["pick_action"] = _u8L("Pick color");

    return true;
}

void GLGizmoColorMixing::on_opening()
{
    // Generate palette on first open or when filament settings / layer height changed
    if (m_filament_optics.empty() || palette_inputs_changed())
        init_palette();

    m_old_mo_id = ObjectID();
}

void GLGizmoColorMixing::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoColorMixing::on_get_name() const
{
    // Toolbar hover tooltip. Detailed help lives in the swatch tooltips inside the popup.
    return _u8L("Color mixing painting");
}

PainterGizmoType GLGizmoColorMixing::get_painter_type() const
{
    return PainterGizmoType::COLOR_MIXING;
}

void GLGizmoColorMixing::init_palette()
{
    if (!m_parent.preset_bundle())
        return;

    m_filament_optics = get_current_filament_optics();
    m_layer_height = get_current_layer_height();
    m_original_colors.clear();
    m_original_colors.reserve(m_filament_optics.size());
    for (const FilamentOptics &fo : m_filament_optics)
        m_original_colors.emplace_back(fo.color.r(), fo.color.g(), fo.color.b(), 1.0f);

    m_palette.clear();
    if (m_filament_optics.size() >= 2)
        m_palette.auto_generate(m_filament_optics, m_layer_height, 12, 10);

    // Preserve the user's picked swatch indices across regeneration so a mid-paint filament
    // edit doesn't reset their brush selection. rebuild_modified_colors uses find_best_match
    // for recipe-bound positions, so the swatch the user had picked still shows the closest
    // achievable color at the same position. Only fall back to defaults if the saved indices
    // would be out of range after regeneration.
    const size_t saved_first = m_first_selected_color_idx;
    const size_t saved_second = m_second_selected_color_idx;

    rebuild_modified_colors();

    if (m_modified_colors.empty())
    {
        m_first_selected_color_idx = 0;
        m_second_selected_color_idx = 0;
    }
    else
    {
        m_first_selected_color_idx = (saved_first < m_modified_colors.size()) ? saved_first : 0;
        m_second_selected_color_idx = (saved_second < m_modified_colors.size())
                                          ? saved_second
                                          : std::min((size_t) 1, m_modified_colors.size() - 1);
    }
}

// Read per-extruder color + TD directly from the selected filament presets. Avoids
// PresetBundle::full_config(), which rebuilds a full merged DynamicPrintConfig on every call
// -- expensive enough to matter when polled from render paths. TD for an unconfigured slot
// falls back to DEFAULT_FILAMENT_TD.
// The color is the filament preset's filament_colour, the same input the slicer's palette is
// built from: mixing physics belongs to the filament, and the printer's extruder_colour is a
// display override that would make the gizmo predict blends of colors that are never loaded.
// The plater color (extruder_colour override, else filament_colour) is only the fallback for
// a slot whose filament preset carries no decodable color.
std::vector<FilamentOptics> GLGizmoColorMixing::get_current_filament_optics() const
{
    std::vector<FilamentOptics> optics;
    if (!m_parent.preset_bundle())
        return optics;

    const auto &plater_colors = m_parent.get_extruder_colors_from_plater_config();
    const auto &extruders_filaments = m_parent.preset_bundle()->extruders_filaments;

    optics.reserve(plater_colors.size());
    for (size_t i = 0; i < plater_colors.size(); ++i)
    {
        const auto &rgba = plater_colors[i];
        ColorRGB color(rgba.r(), rgba.g(), rgba.b());
        float td = DEFAULT_FILAMENT_TD;
        if (i < extruders_filaments.size())
        {
            if (const Preset *preset = extruders_filaments[i].get_selected_preset())
            {
                const auto *td_opt = preset->config.option<ConfigOptionFloats>("filament_transmission_distance");
                if (td_opt && !td_opt->values.empty())
                    td = (float) td_opt->values[0];
                const auto *color_opt = preset->config.option<ConfigOptionStrings>("filament_colour");
                ColorRGB preset_color;
                if (color_opt && !color_opt->values.empty() && decode_color(color_opt->values[0], preset_color))
                    color = preset_color;
            }
        }
        optics.emplace_back(color, td);
    }
    return optics;
}

// Active print preset's layer_height. Drives the dither-stack opacity simulation so the
// predicted_color in the picker matches what the printer lays down. Reads the edited preset
// config directly -- no full_config merge, cheap to call from event handlers. First-layer
// height is intentionally ignored: it's almost always thicker than the rest and treating it
// specially would only skew predictions for one layer out of hundreds.
float GLGizmoColorMixing::get_current_layer_height() const
{
    if (!m_parent.preset_bundle())
        return 0.2f;
    const auto *opt = m_parent.preset_bundle()->prints.get_edited_preset().config.option<ConfigOptionFloat>(
        "layer_height");
    return (opt && opt->value > 0.0) ? (float) opt->value : 0.2f;
}

bool GLGizmoColorMixing::palette_inputs_changed() const
{
    auto current = get_current_filament_optics();
    if (current.size() != m_filament_optics.size())
        return true;
    for (size_t i = 0; i < current.size(); ++i)
        if (current[i] != m_filament_optics[i])
            return true;
    if (std::abs(get_current_layer_height() - m_layer_height) > 1e-4f)
        return true;
    return false;
}

void GLGizmoColorMixing::data_changed(bool is_serializing)
{
    GLGizmoPainterBase::data_changed(is_serializing);

    if (m_state != On)
        return;

    if (palette_inputs_changed())
    {
        this->init_palette();
        this->init_model_triangle_selectors();
    }
}

// Eyedropper: Alt+Left / Alt+Right picks the state under the cursor into the first/second
// brush slot. Falls through to the base for every other action so the existing paint, line,
// eraser, and fill paths keep working.
bool GLGizmoColorMixing::gizmo_event(GizmoEventType action, const Vec2d &mouse_position, bool shift_down,
                                     bool alt_down, bool control_down)
{
    const bool is_pick = alt_down && !shift_down && !control_down &&
                         (action == GizmoEventType::LeftDown || action == GizmoEventType::RightDown);
    if (!is_pick)
        return GLGizmoPainterBase::gizmo_event(action, mouse_position, shift_down, alt_down, control_down);

    if (m_triangle_selectors.empty())
        return false;

    const Selection &selection = m_parent.get_selection();
    const ModelObject *mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
    if (!mo || selection.get_instance_idx() < 0)
        return false;
    const ModelInstance *mi = mo->instances[selection.get_instance_idx()];

    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume *mv : mo->volumes)
        if (mv->is_model_part())
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());

    update_raycast_cache(mouse_position, m_parent.get_camera(), trafo_matrices);
    if (m_rr.mesh_id < 0 || m_rr.mesh_id >= (int) m_triangle_selectors.size())
        return false;

    const TriangleStateType state = m_triangle_selectors[m_rr.mesh_id]->get_triangle_leaf_state(int(m_rr.facet));
    if (state == TriangleStateType::NONE)
        // Nothing painted under the cursor; ignore the click so the user can try another spot.
        return true;

    const size_t idx = size_t(state) - 1;
    if (idx >= m_modified_colors.size())
        return true;

    if (action == GizmoEventType::LeftDown)
        m_first_selected_color_idx = idx;
    else
        m_second_selected_color_idx = idx;

    m_parent.set_as_dirty();
    return true;
}

void GLGizmoColorMixing::init_model_triangle_selectors()
{
    const ModelObject *mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
    if (!mo)
        return;

    m_triangle_selectors.clear();

    for (const ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;

        const TriangleMesh *mesh = &mv->mesh();
        const int extruders_count = (int) m_original_colors.size();
        const size_t extruder_idx = ModelVolume::get_extruder_color_idx(*mv, extruders_count);
        ColorRGBA default_color = (extruder_idx < m_original_colors.size()) ? m_original_colors[extruder_idx]
                                                                            : ColorRGBA(0.5f, 0.5f, 0.5f, 1.0f);

        m_triangle_selectors.emplace_back(
            std::make_unique<TriangleSelectorMmGui>(*mesh, m_modified_colors, default_color));
        m_triangle_selectors.back()->deserialize(mv->color_mixing_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

void GLGizmoColorMixing::update_from_model_object()
{
    wxBusyCursor wait;

    if (palette_inputs_changed())
        this->init_palette();

    // Defensive: seed recipes from the current palette for any painted volume that arrives
    // without a recipe table (foreign 3MF). find_best_match later snaps each painted region
    // to the closest achievable color.
    if (const ModelObject *mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr)
    {
        for (ModelVolume *mv : const_cast<ModelObject *>(mo)->volumes)
        {
            if (!mv->is_model_part())
                continue;
            if (mv->is_color_mixing_painted() && mv->color_mixing_palette.empty())
                ensure_color_mixing_recipes_for_used_states(m_palette, *mv);
        }
    }

    this->init_model_triangle_selectors();
}

void GLGizmoColorMixing::update_model_object() const
{
    bool updated = false;
    ModelObject *mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;
        ++idx;
        const bool facets_updated = mv->color_mixing_facets.set(*m_triangle_selectors[idx]);
        updated |= facets_updated;
        // After a paint commit, sync every currently-used state's recipe to the current
        // palette's predicted_color so recipe.rgb tracks the swatch the user just clicked.
        if (facets_updated && !mv->color_mixing_facets.empty())
            ensure_color_mixing_recipes_for_used_states(m_palette, *mv);
    }

    if (updated)
    {
        const ModelObjectPtrs &mos = m_parent.get_model()->objects;
        m_parent.event_poster()->postEvent(CanvasEventType::UpdateInfoItems,
                                           int(std::find(mos.begin(), mos.end(), mo) - mos.begin()));
        m_parent.event_poster()->postEvent(CanvasEventType::ScheduleBackgroundProcess);

        // First-paint welcome notification, once per app installation. The persistent banner in
        // the gizmo popup carries the same message at lower volume; this one fires on the user's
        // very first paint so they read it at least once before forming expectations.
        if (m_parent.app_config() && m_parent.app_config()->get("color_mixing_warning_shown") != "1")
        {
            m_parent.get_notification_manager()->push_notification(
                _u8L("Color mixing painted. Blends look best on near-vertical walls. Top surfaces, "
                     "steep overhangs, and the base layers may show a single filament rather than the "
                     "blended target. Base layer behavior is configurable in Print Settings -> Layer "
                     "height."));
            m_parent.app_config()->set("color_mixing_warning_shown", "1");
        }
    }
}

void GLGizmoColorMixing::render_painter_gizmo()
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

void GLGizmoColorMixing::render_triangles(const Selection &selection) const
{
    ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    auto *shader = m_parent.get_shader("mm_color_preview");
    if (!shader)
        shader = m_parent.get_shader("mm_gouraud"); // fallback
    if (!shader)
        return;
    shader->start_using();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    ScopeGuard guard(
        [shader]()
        {
            if (shader)
                shader->stop_using();
        });

    const ModelObject *mo = m_c->selection_info()->model_object();
    int mesh_id = -1;
    for (const ModelVolume *mv : mo->volumes)
    {
        if (!mv->is_model_part())
            continue;
        ++mesh_id;

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

        const bool is_left_handed = trafo_matrix.matrix().determinant() < 0.0;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        const Camera &camera = m_parent.get_camera();
        const Transform3d &view_matrix = camera.get_view_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * trafo_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                            trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        shader->set_uniform("volume_world_matrix", trafo_matrix);
        shader->set_uniform("volume_mirrored", is_left_handed);

        m_triangle_selectors[mesh_id]->set_shader_getters([this](const std::string &name)
                                                          { return m_parent.get_shader(name); },
                                                          [this]() { return m_parent.get_current_shader(); });
        m_triangle_selectors[mesh_id]->render(m_imgui, trafo_matrix, m_parent.get_camera());

        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}

ColorRGBA GLGizmoColorMixing::get_cursor_sphere_left_button_color() const
{
    if (m_first_selected_color_idx < m_modified_colors.size())
    {
        ColorRGBA color = m_modified_colors[m_first_selected_color_idx];
        color.a(0.25f);
        return color;
    }
    return {0.0f, 0.0f, 1.0f, 0.25f};
}

ColorRGBA GLGizmoColorMixing::get_cursor_sphere_right_button_color() const
{
    if (m_second_selected_color_idx < m_modified_colors.size())
    {
        ColorRGBA color = m_modified_colors[m_second_selected_color_idx];
        color.a(0.25f);
        return color;
    }
    return {1.0f, 0.0f, 0.0f, 0.25f};
}

void GLGizmoColorMixing::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    // Anchor the popup's bottom-right corner at (x, bottom_limit) -- pivot (1.0, 1.0) tells
    // ImGui to treat the given point as the bottom-right of the window rather than the
    // top-left. bottom_limit is the viewport's usable bottom, which coincides with the
    // bottom of the vertical gizmo toolbar. This keeps the popup visually glued to the
    // color mixing icon (which lives at the bottom of the toolbar) regardless of popup
    // height. ImGui handles the "size not known yet" first-frame case because the pivot is
    // applied after auto-resize measures the window's content.
    set_side_flyout_pos(x, bottom_limit, 1.0f);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse;
    ImGuiPureWrap::begin(get_name(), window_flags);

    // Scale the swatch size with the font so it stays proportional across DPI settings.
    // Hardcoding 24.0f made swatches render as tiny black squares on macOS Retina because
    // the rest of the popup scales with m_imgui->scaled() and ImGui's ColorButton fell
    // below a usable size. 1.5 * font_size ~= 24 px at typical Windows DPI.
    const float swatch_size = m_imgui->scaled(1.5f);
    const float slider_width = m_imgui->scaled(10.f);

    // --- Color-coded direction hints ---
    {
        float caption_max = 0.f;
        for (const std::string &t : {"paint", "erase", "pick", "alt"})
            caption_max = std::max(caption_max, ImGuiPureWrap::calc_text_size(m_desc[t + "_caption"]).x);
        caption_max += m_imgui->scaled(1.f);

        auto draw_hint = [&](const std::string &caption, const std::string &text)
        {
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, caption);
            ImGui::SameLine(caption_max);
            ImGuiPureWrap::text(text);
        };

        draw_hint(m_desc.at("paint_caption"), m_desc.at("paint_action"));
        draw_hint(m_desc.at("erase_caption"), m_desc.at("erase_action"));
        draw_hint(m_desc.at("pick_caption"), m_desc.at("pick_action"));
        std::string alt_text = (m_tool_type == ToolType::BRUSH)          ? m_desc.at("alt_brush")
                               : (m_tool_type == ToolType::HEIGHT_RANGE) ? m_desc.at("alt_height_range")
                                                                         : m_desc.at("alt_fill");
        draw_hint(m_desc.at("alt_caption"), alt_text);
    }

    ImGui::Separator();

    // --- Limitation banner. Sets expectations every time the gizmo opens so users don't paint
    // a top face or shallow overhang and then think the dither is broken when it shows a single
    // filament instead of the blend. The base-layer behavior is configured separately in Print
    // Settings, so it isn't called out here. ---
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", _u8L("Color blends best on near-vertical walls. Top faces and steep "
                                   "overhangs may show a single filament instead of the blend.")
                                  .c_str());
    ImGui::PopTextWrapPos();

    ImGui::Separator();

    // --- Achievable color swatches (auto-fill width) ---
    if (!m_modified_colors.empty())
    {
        // Eraser toggle sits on the same line as the swatches header. Painting with eraser
        // active overrides the brush state to NONE so strokes clear color back to unpainted.
        ImGuiPureWrap::text(_u8L("Achievable Colors"));
        ImGui::SameLine();
        ImGui::Checkbox(_u8L("Eraser").c_str(), &m_eraser_mode);

        // Reserve space for the vertical scrollbar so the rightmost swatch isn't clipped.
        const float scrollbar_w = ImGui::GetStyle().ScrollbarSize;
        const float avail_width = ImGui::GetContentRegionAvail().x - scrollbar_w;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int cols = std::max(1, (int) ((avail_width + spacing) / (swatch_size + spacing)));

        float max_palette_height = swatch_size * 6.0f + ImGui::GetStyle().ItemSpacing.y * 6.0f;
        ImGui::BeginChild("##palette_scroll", ImVec2(0, max_palette_height), false);

        // Nudge the grid in by 2 px so the 2-px selection outline (drawn 1 px outside each
        // swatch) has room to render on the leftmost swatch in a row without getting clipped
        // by the child window's content edge.
        ImGui::Indent(2.0f);

        // Tier each entry by unique-filament count in the layer_pattern of the palette entry
        // the swatch shows (a recipe-bound slot shows its closest achievable color, which can
        // sit at another palette index than the slot itself). 1 = pure filament, 2 = 2-way
        // blend, 3+ = 3-way blend. Entries without a palette match fall back to tier 1 so
        // they're always visible.
        auto entry_tier = [this](size_t idx) -> int
        {
            const int pidx = idx < m_modified_palette_idx.size() ? m_modified_palette_idx[idx] : -1;
            if (pidx < 0 || pidx >= (int) m_palette.colors().size())
                return 1;
            const auto &pat = m_palette.colors()[pidx].layer_pattern;
            if (pat.empty())
                return 1;
            std::vector<int> uniq(pat.begin(), pat.end());
            std::sort(uniq.begin(), uniq.end());
            uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
            return std::min(3, (int) uniq.size());
        };

        // Bucket indices into the three tiers, preserving generation order within each tier.
        std::array<std::vector<size_t>, 3> tiers;
        for (size_t i = 0; i < m_modified_colors.size(); ++i)
            tiers[entry_tier(i) - 1].push_back(i);

        const std::array<std::string, 3> tier_labels = {_u8L("Single colors"), _u8L("2-way colors"),
                                                        _u8L("3-way colors")};

        bool first_section = true;
        for (int t = 0; t < 3; ++t)
        {
            if (tiers[t].empty())
                continue;
            if (!first_section)
                ImGui::Spacing();
            first_section = false;
            ImGui::TextDisabled("%s", tier_labels[t].c_str());

            for (size_t k = 0; k < tiers[t].size(); ++k)
            {
                const size_t i = tiers[t][k];
                const auto &rgba = m_modified_colors[i];
                ImVec4 col(rgba.r(), rgba.g(), rgba.b(), 1.0f);
                ImGui::PushID((int) i);

                if (m_first_selected_color_idx == i)
                {
                    ImVec2 cursor = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRect(ImVec2(cursor.x - 1, cursor.y - 1),
                                                        ImVec2(cursor.x + swatch_size + 1, cursor.y + swatch_size + 1),
                                                        IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
                }

                if (ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(swatch_size, swatch_size)))
                    m_first_selected_color_idx = i;

                const int shown_idx = i < m_modified_palette_idx.size() ? m_modified_palette_idx[i] : -1;
                if (ImGui::IsItemHovered() && shown_idx >= 0 && shown_idx < (int) m_palette.colors().size())
                {
                    const auto &mc = m_palette.colors()[shown_idx];
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", mc.name.c_str());
                    if (mc.layer_pattern.size() <= 1)
                        ImGui::TextDisabled("%s", _u8L("Single filament -- no swaps").c_str());
                    else
                        ImGui::TextDisabled("%s", DSKY::format(_L("Repeats every %1% layers (%2% swaps per cycle)"),
                                                               (int) mc.layer_pattern.size(),
                                                               (int) mc.layer_pattern.size() - 1)
                                                      .c_str());
                    ImGui::EndTooltip();
                }

                ImGui::PopID();

                if (((int) (k + 1) % cols) != 0 && k < tiers[t].size() - 1)
                    ImGui::SameLine();
            }
        }

        ImGui::EndChild();
    }

    ImGui::Separator();

    // --- Tool type (label above, centered two-row layout) ---
    ImGuiPureWrap::text(m_desc.at("tool_type"));
    ImGui::NewLine();

    auto set_tool = [&](ToolType t)
    {
        m_tool_type = t;
        for (auto &ts : m_triangle_selectors)
        {
            ts->seed_fill_unselect_all_triangles();
            ts->request_update_render_data();
        }
    };

    // First row: Brush, Smart fill, Bucket fill (centered)
    {
        float row1_w = ImGuiPureWrap::calc_text_size(m_desc["tool_brush"]).x +
                       ImGuiPureWrap::calc_text_size(m_desc["tool_smart_fill"]).x +
                       ImGuiPureWrap::calc_text_size(m_desc["tool_bucket_fill"]).x + m_imgui->scaled(7.5f);
        float offset = (ImGui::GetContentRegionAvail().x - row1_w) * 0.5f;
        ImGui::SameLine(std::max(0.f, offset));
    }
    if (ImGuiPureWrap::radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH))
        set_tool(ToolType::BRUSH);
    ImGui::SameLine();
    if (ImGuiPureWrap::radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL))
        set_tool(ToolType::SMART_FILL);
    ImGui::SameLine();
    if (ImGuiPureWrap::radio_button(m_desc["tool_bucket_fill"], m_tool_type == ToolType::BUCKET_FILL))
        set_tool(ToolType::BUCKET_FILL);

    // Second row: Height range (centered)
    {
        float hr_w = ImGuiPureWrap::calc_text_size(m_desc["tool_height_range"]).x + m_imgui->scaled(2.5f);
        float offset = (ImGui::GetContentRegionAvail().x - hr_w) * 0.5f;
        ImGui::NewLine();
        ImGui::SameLine(std::max(0.f, offset));
        if (ImGuiPureWrap::radio_button(m_desc["tool_height_range"], m_tool_type == ToolType::HEIGHT_RANGE))
            set_tool(ToolType::HEIGHT_RANGE);
    }

    ImGui::Separator();

    // --- Tool-specific settings ---
    if (m_tool_type == ToolType::BRUSH)
    {
        // Brush shape (label above, centered)
        ImGuiPureWrap::text(m_desc.at("cursor_type"));
        ImGui::NewLine();
        {
            float row_w = ImGuiPureWrap::calc_text_size(m_desc["sphere"]).x +
                          ImGuiPureWrap::calc_text_size(m_desc["circle"]).x +
                          ImGuiPureWrap::calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(7.5f);
            float offset = (ImGui::GetContentRegionAvail().x - row_w) * 0.5f;
            ImGui::SameLine(std::max(0.f, offset));
        }
        if (ImGuiPureWrap::radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
            m_cursor_type = TriangleSelector::CursorType::SPHERE;
        ImGui::SameLine();
        if (ImGuiPureWrap::radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
            m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        ImGui::SameLine();
        if (ImGuiPureWrap::radio_button(m_desc["pointer"], m_cursor_type == TriangleSelector::CursorType::POINTER))
            m_cursor_type = TriangleSelector::CursorType::POINTER;

        m_imgui->disabled_begin(m_cursor_type != TriangleSelector::CursorType::SPHERE &&
                                m_cursor_type != TriangleSelector::CursorType::CIRCLE);

        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("cursor_size"));
        ImGui::SameLine();
        ImGui::PushItemWidth(slider_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true,
                              _u8L("Alt + Mouse wheel"));

        ImGuiPureWrap::checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        m_imgui->disabled_end();

        ImGui::Separator();
    }
    else if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL)
    {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_tool_type == ToolType::SMART_FILL ? m_desc.at("smart_fill_angle")
                                                                : m_desc.at("bucket_fill_angle"));
        ImGui::SameLine();
        ImGui::PushItemWidth(slider_width);
        float &fill_angle = (m_tool_type == ToolType::SMART_FILL) ? m_smart_fill_angle : m_bucket_fill_angle;
        if (m_imgui->slider_float("##fill_angle", &fill_angle, SmartFillAngleMin, SmartFillAngleMax, "%.f\xC2\xB0",
                                  1.0f, true, _u8L("Alt + Mouse wheel")))
        {
            for (auto &ts : m_triangle_selectors)
            {
                ts->seed_fill_unselect_all_triangles();
                ts->request_update_render_data();
            }
        }

        ImGui::Separator();
    }
    else if (m_tool_type == ToolType::HEIGHT_RANGE)
    {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("height_range_z_range"));
        ImGui::SameLine();
        ImGui::PushItemWidth(slider_width);
        if (m_imgui->slider_float("##height_range_z_range", &m_height_range_z_range, HeightRangeZRangeMin,
                                  HeightRangeZRangeMax, "%.2f mm", 1.0f, true, _u8L("Alt + Mouse wheel")))
        {
            for (auto &ts : m_triangle_selectors)
            {
                ts->seed_fill_unselect_all_triangles();
                ts->request_update_render_data();
            }
        }

        ImGui::Separator();
    }

    ImGui::Separator();
    const float clip_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                     ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) +
                            m_imgui->scaled(1.5f);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("clipping_of_view"));
    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(clip_left);
    ImGui::PushItemWidth(slider_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, _u8L("Ctrl + Mouse wheel")))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    ImGui::Separator();
    if (ImGuiPureWrap::button(m_desc.at("remove_all")))
    {
        m_parent.take_gizmo_snapshot(_u8L("Clear color mixing"));
        ModelObject *mo = m_c->selection_info()->model_object();
        int idx = -1;
        for (ModelVolume *mv : mo->volumes)
        {
            if (!mv->is_model_part())
                continue;
            ++idx;
            m_triangle_selectors[idx]->reset();
            m_triangle_selectors[idx]->request_update_render_data();
            // Wipe the recipe table too, not just the facets. Keeping a stale recipe
            // table around after "Clear all" caused new paint to land on recipe slots
            // populated by a previous session's palette, which rendered as wrong colors
            // even though the gizmo's find_best_match view still looked correct.
            mv->color_mixing_palette.clear();
        }
        update_model_object();
        m_parent.set_as_dirty();
    }

    ImGuiPureWrap::end();
}

wxString GLGizmoColorMixing::handle_snapshot_action_name(bool control_down, Button button_down) const
{
    return control_down ? _L("Remove color mixing") : _L("Paint color mixing");
}

void GLGizmoColorMixing::rebuild_modified_colors()
{
    ModelObject *mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;

    // Determine the size needed. m_modified_colors must be large enough to index every painted
    // state across all volumes, otherwise TriangleSelectorMmGui silently falls back to color 0
    // (extruder 0) for out-of-range states, which manifests as paint rendering as the wrong
    // color when the palette shrinks (e.g., after removing a filament).
    size_t total = m_palette.colors().size();
    if (mo)
    {
        for (const ModelVolume *mv : mo->volumes)
        {
            if (!mv->is_model_part())
                continue;
            total = std::max(total, mv->color_mixing_palette.size());
        }
    }

    m_modified_colors.clear();
    m_modified_colors.reserve(total);
    m_modified_palette_idx.assign(total, -1);

    // Find the first volume with a recipe table. All painted volumes share the same recipe
    // table contents (the gizmo mirrors the runtime palette into each), so picking any
    // painted volume's table is equivalent for swatch rendering.
    const std::vector<ColorMixingRecipe> *recipes = nullptr;
    if (mo)
    {
        for (const ModelVolume *mv : mo->volumes)
        {
            if (mv->is_model_part() && !mv->color_mixing_palette.empty())
            {
                recipes = &mv->color_mixing_palette;
                break;
            }
        }
    }

    for (size_t i = 0; i < total; ++i)
    {
        // Prefer the recipe-snapped color so painted intent survives palette resizes. If no
        // recipe exists for this index yet (pre-paint or picker-only slot), fall back to the
        // runtime palette entry.
        if (recipes && i < recipes->size())
        {
            const ColorMixingRecipe &rec = (*recipes)[i];
            if (rec.is_locked() && (int) rec.extruder_lock < (int) m_filament_optics.size() && rec.extruder_lock >= 0)
            {
                const ColorRGB &c = m_filament_optics[rec.extruder_lock].color;
                m_modified_colors.emplace_back(c.r(), c.g(), c.b(), 1.0f);
                m_modified_palette_idx[i] = rec.extruder_lock;
                continue;
            }
            int best = m_palette.find_best_match(rec.rgb);
            if (best >= 0 && best < (int) m_palette.colors().size())
            {
                const MixedColor &mc = m_palette.colors()[best];
                m_modified_colors.emplace_back(mc.predicted_color.r(), mc.predicted_color.g(), mc.predicted_color.b(),
                                               1.0f);
                m_modified_palette_idx[i] = best;
                continue;
            }
            // No palette match (palette empty) -- render the recipe rgb literally.
            float r = float((rec.rgb >> 16) & 0xFF) / 255.f;
            float g = float((rec.rgb >> 8) & 0xFF) / 255.f;
            float b = float(rec.rgb & 0xFF) / 255.f;
            m_modified_colors.emplace_back(r, g, b, 1.0f);
        }
        else if (i < m_palette.colors().size())
        {
            const MixedColor &mc = m_palette.colors()[i];
            m_modified_colors.emplace_back(mc.predicted_color.r(), mc.predicted_color.g(), mc.predicted_color.b(),
                                           1.0f);
            m_modified_palette_idx[i] = (int) i;
        }
        else
        {
            // Defensive: unreachable under normal flow.
            m_modified_colors.emplace_back(0.5f, 0.5f, 0.5f, 1.0f);
        }
    }

    if (m_modified_colors.empty())
        m_modified_colors.emplace_back(0.5f, 0.5f, 0.5f, 1.0f);

    m_first_selected_color_idx = std::min(m_first_selected_color_idx, m_modified_colors.size() - 1);
    m_second_selected_color_idx = std::min(m_second_selected_color_idx, m_modified_colors.size() - 1);
}

} // namespace DSKY
