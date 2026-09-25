///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "DSKY/GUI/SurfaceDrag.hpp"
#include "DSKY/GUI/GLTexture.hpp"
#include "DSKY/Utils/RaycastManager.hpp"
#include "DSKY/GUI/IconManager.hpp"

#include <optional>
#include <memory>
#include <atomic>

#include "luminary/gizmo/emboss/Emboss.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/model/scene/Model.hpp"

#include <imgui/imgui.h>
#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace Luminary
{
class ModelVolume;
enum class ModelVolumeType : int;
} // namespace Luminary
namespace DSKY::Emboss
{
struct CreateVolumeParams;
}

namespace DSKY
{
using namespace Luminary;

struct Texture
{
    unsigned id{0};
    unsigned width{0};
    unsigned height{0};
};

class GLGizmoSVG : public GLGizmoBase
{
public:
    explicit GLGizmoSVG(GLCanvas3D &parent);

    /// <summary>
    /// Create new embossed text volume by type on position of mouse
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <param name="mouse_pos">Define position of new volume</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    bool create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos); // first open file dialog

    /// <summary>
    /// Create new text without given position
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    bool create_volume(ModelVolumeType volume_type); // first open file dialog

    /// <summary>
    /// Create volume from already selected svg file
    /// </summary>
    /// <param name="svg_file">File path</param>
    /// <param name="mouse_pos">Position on screen where to create volume</param>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    bool create_volume(std::string_view svg_file, const Vec2d &mouse_pos,
                       ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);
    bool create_volume(std::string_view svg_file, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);

    /// <summary>
    /// Check whether volume is object containing only emboss volume
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when object otherwise False</returns>
    static bool is_svg_object(const ModelVolume &volume);

    /// <summary>
    /// Check whether volume has emboss data
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when constain emboss data otherwise False</returns>
    static bool is_svg(const ModelVolume &volume);

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    bool on_is_activable() const override { return true; }
    bool on_is_selectable() const override { return false; }
    void on_set_state() override;
    void data_changed(bool is_serializing) override; // selection changed
    void on_set_hover_id() override { m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;

    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    bool on_mouse(const MouseInput &mouse) override;

    bool wants_enter_leave_snapshots() const override;
    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    std::string get_action_snapshot_name() const override;

private:
    void set_volume_by_selection();
    void reset_volume();

    // create volume from text - main functionality
    bool process(bool make_snapshot = true);
    void close();
    void draw_window();
    void draw_preview();
    void draw_filename();
    void draw_depth();
    void draw_size();
    void draw_use_surface();
    void draw_distance();
    void draw_rotation();
    void draw_mirroring();
    void draw_face_the_camera();
    void draw_model_type();

    // process mouse event
    bool on_mouse_for_rotation(const MouseInput &mouse);
    bool on_mouse_for_translate(const MouseInput &mouse);
    void on_mouse_confirm_edit(const MouseInput &mouse);

    void volume_transformation_changed();

    Emboss::CreateVolumeParams create_input(ModelVolumeType volume_type, std::string_view svg_filepath = "");

    struct GuiCfg;
    std::unique_ptr<const GuiCfg> m_gui_cfg;

    // actual selected only one volume - with emboss data
    ModelVolume *m_volume = nullptr;

    // Is used to edit eboss and send changes to job
    // Inside volume is current state of shape WRT Volume
    EmbossShape m_volume_shape; // copy from m_volume for edit

    // same index as volumes in
    std::vector<std::string> m_shape_warnings;

    // When work with undo redo stack there could be situation that
    // m_volume point to unexisting volume so One need also objectID
    ObjectID m_volume_id;

    // cancel for previous update of volume to cancel finalize part
    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;

    // Rotation gizmo
    GLGizmoRotate m_rotate_gizmo;
    std::optional<float> m_angle;
    std::optional<float> m_distance;

    // Value is set only when dragging rotation to calculate actual angle
    std::optional<float> m_rotate_start_angle;

    RaycastManager m_raycast_manager;

    // When true keep up vector otherwise relative rotation
    bool m_keep_up = true;

    // Keep size aspect ratio when True.
    bool m_keep_ratio = true;

    // setted only when wanted to use - not all the time
    std::optional<ImVec2> m_set_window_offset;

    // Keep data about dragging only during drag&drop
    std::optional<SurfaceDrag> m_surface_drag;

    // For volume on scaled objects
    std::optional<float> m_scale_width;
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    void calculate_scale();
    float get_scale_for_tolerance();

    // keep SVG data rendered on GPU
    Texture m_texture;

    // bounding box of shape
    // Note: Scaled mm to int value by m_volume_shape.scale
    BoundingBox m_shape_bb;

    std::string m_filename_preview;

    IconManager m_icon_manager;
    IconManager::Icons m_icons;

    // only temporary solution
    static const std::string M_ICON_FILENAME;
};
} // namespace DSKY
