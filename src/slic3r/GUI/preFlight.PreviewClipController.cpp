///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "preFlight.PreviewClipController.hpp"
#include "GCodeViewer.hpp"
#include "GLCanvas3D.hpp"
#include "Camera.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "ImGuiPureWrap.hpp"
#include "3DScene.hpp"

#include <imgui/imgui.h>
#include <float.h>

namespace Slic3r
{
namespace GUI
{

// Helper to get the GCodeViewer from the current canvas
static GCodeViewer *get_current_gcode_viewer()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return nullptr;
    GLCanvas3D *canvas = plater->get_current_canvas3D();
    if (canvas == nullptr)
        return nullptr;
    return &canvas->get_gcode_viewer();
}

void PreviewClipController::activate(int object_id)
{
    if (m_active)
        deactivate();

    GCodeViewer *viewer = get_current_gcode_viewer();
    if (viewer == nullptr)
        return;

    GLVolumeCollection &shells = viewer->get_shells_volumes();
    if (shells.volumes.empty())
        return;

    m_object_id = object_id;
    m_active = true;

    // Save current shell visibility state
    SavedState state;
    state.shells_visible = viewer->are_shells_visible();
    for (const GLVolume *v : shells.volumes)
        state.shell_visibility.push_back(v->is_active);
    m_saved_state = state;

    // Compute bounding box of the selected object's shell volumes
    m_object_bbox = BoundingBoxf3();
    for (GLVolume *v : shells.volumes)
    {
        if (v->composite_id.object_id == object_id)
            m_object_bbox.merge(v->transformed_bounding_box());
    }

    // Capture camera forward direction as initial clip normal
    reset_direction();

    // Start at 50%
    m_clip_ratio = 0.5;

    apply_clipping_plane();
}

void PreviewClipController::deactivate()
{
    if (!m_active)
        return;

    GCodeViewer *viewer = get_current_gcode_viewer();
    if (viewer != nullptr)
    {
        GLVolumeCollection &shells = viewer->get_shells_volumes();

        // Restore shell visibility
        if (m_saved_state.has_value())
        {
            viewer->set_shells_visible(m_saved_state->shells_visible);
            for (size_t i = 0; i < shells.volumes.size() && i < m_saved_state->shell_visibility.size(); ++i)
                shells.volumes[i]->is_active = m_saved_state->shell_visibility[i];
        }

        // Reset clipping planes
        viewer->reset_preview_clipping_plane();
        viewer->get_libvgcode_viewer().reset_clipping_plane();
    }

    m_active = false;
    m_object_id = -1;
    m_saved_state.reset();
}

void PreviewClipController::set_position(double ratio)
{
    m_clip_ratio = std::clamp(ratio, 0.0, 1.0);
    apply_clipping_plane();
}

void PreviewClipController::reset_direction()
{
    const Camera &camera = wxGetApp().plater()->get_camera();
    // Use negative camera forward (camera looks along -Z in view space)
    m_clip_normal = camera.get_dir_forward();

    // Normalize
    double len = m_clip_normal.norm();
    if (len > 1e-6)
        m_clip_normal /= len;
    else
        m_clip_normal = Vec3d(0.0, 0.0, 1.0);

    apply_clipping_plane();
}

void PreviewClipController::apply_clipping_plane()
{
    if (!m_active || !m_object_bbox.defined)
        return;

    GCodeViewer *viewer = get_current_gcode_viewer();
    if (viewer == nullptr)
        return;

    // Project the object bounding box corners onto the clip normal to find the range
    double min_proj = DBL_MAX;
    double max_proj = -DBL_MAX;
    for (int i = 0; i < 8; ++i)
    {
        Vec3d corner;
        corner.x() = (i & 1) ? m_object_bbox.max.x() : m_object_bbox.min.x();
        corner.y() = (i & 2) ? m_object_bbox.max.y() : m_object_bbox.min.y();
        corner.z() = (i & 4) ? m_object_bbox.max.z() : m_object_bbox.min.z();
        double proj = m_clip_normal.dot(corner);
        min_proj = std::min(min_proj, proj);
        max_proj = std::max(max_proj, proj);
    }

    // Offset along the normal based on ratio
    // At ratio=0.0: clip plane at min_proj (nothing clipped - everything visible)
    // At ratio=1.0: clip plane at max_proj (everything clipped)
    double offset = min_proj + m_clip_ratio * (max_proj - min_proj);

    // The clipping plane equation: dot(vec4(pos, 1.0), plane) >= 0 is visible
    // plane = (nx, ny, nz, -offset)
    // This means: nx*x + ny*y + nz*z - offset >= 0, i.e., dot(normal, pos) >= offset
    // Fragments with dot < 0 are discarded, so we need dot(pos, normal) - offset >= 0
    // => plane = (nx, ny, nz, -offset)

    // Apply to shell rendering (when shells are visible via legend toggle)
    std::array<double, 4> clip_plane = {m_clip_normal.x(), m_clip_normal.y(), m_clip_normal.z(), -offset};
    viewer->set_preview_clipping_plane(clip_plane);

    // Apply to toolpath rendering (libvgcode uses float)
    viewer->get_libvgcode_viewer().set_clipping_plane(static_cast<float>(m_clip_normal.x()),
                                                      static_cast<float>(m_clip_normal.y()),
                                                      static_cast<float>(m_clip_normal.z()),
                                                      static_cast<float>(-offset));
}

std::string PreviewClipController::get_object_name() const
{
    if (m_object_id < 0)
        return "Object";

    const Model &model = wxGetApp().plater()->model();
    const ModelObjectPtrs &objects = model.objects;
    if (m_object_id < static_cast<int>(objects.size()))
        return objects[m_object_id]->name;

    return "Object";
}

void PreviewClipController::render_imgui()
{
    if (!m_active)
        return;

    ImGuiWrapper &imgui = *wxGetApp().imgui();

    const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();

    // Position in the top-center area of the canvas (only on first appearance)
    ImGuiPureWrap::set_next_window_pos(static_cast<float>(cnv_size.get_width()) * 0.5f, 10.0f, ImGuiCond_Once, 0.5f,
                                       0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse;

    std::string title = "Clipping Plane - " + get_object_name() + "###PreviewClipController";
    ImGuiPureWrap::begin(title, flags);

    // Slider for clipping position
    float ratio_f = static_cast<float>(m_clip_ratio);
    ImGuiPureWrap::text("Position");
    ImGui::SameLine();
    if (imgui.slider_float("##clip_pos", &ratio_f, 0.0f, 1.0f, "%.2f"))
        set_position(static_cast<double>(ratio_f));

    // Reset Direction button
    if (ImGui::Button("Reset Direction"))
        reset_direction();

    ImGui::SameLine();

    // Close button
    if (ImGui::Button("Close"))
        deactivate();

    ImGuiPureWrap::end();
}

} // namespace GUI
} // namespace Slic3r
