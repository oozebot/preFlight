///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GLGizmoBase.hpp"
#include "DSKY/GUI/GLModel.hpp"
#include "DSKY/GUI/MeshUtils.hpp"
#include "DSKY/GUI/CameraUtils.hpp"

#include <chrono>
#include <optional>

namespace DSKY
{
using namespace Luminary;

class GLGizmoAlign : public GLGizmoBase
{
public:
    GLGizmoAlign(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);

    bool on_mouse(const MouseInput &mouse) override;
    void data_changed(bool is_serializing) override;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_render() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    void on_set_state() override;
    void on_register_raycasters_for_picking() override {}
    void on_unregister_raycasters_for_picking() override {}
    CommonGizmosDataID on_get_requirements() const override;

private:
    enum class EState : unsigned char
    {
        Idle,           // Waiting for source face click
        SourceSelected, // Source face picked, waiting for target face click
        Aligned         // Both faces selected, in edit mode
    };

    struct FaceData
    {
        Vec3d normal{Vec3d::Zero()}; // Face normal in world coordinates
        Vec3d center{Vec3d::Zero()}; // Hit point in world coordinates
        int object_idx{-1};          // Index into model objects
        int instance_idx{-1};        // Instance index
        int volume_idx{-1};          // Volume index
        size_t facet_idx{0};         // Triangle index in the volume mesh
    };

    EState m_align_state{EState::Idle};
    FaceData m_source;
    FaceData m_target;

    // The initially selected object when the gizmo opens - this is the target
    int m_target_object_idx{-1};

    // Edit mode parameters
    float m_depth_offset{0.f};        // Push in/out along target normal (Z in plane coords)
    float m_rotation{0.f};            // Rotation around target normal (degrees)
    bool m_mirror_h{false};           // Mirror horizontally on the target plane
    bool m_mirror_v{false};           // Mirror vertically on the target plane
    bool m_inverted{false};           // Flip object to the other side of the target plane
    float m_scale_x{100.f};           // Scale % along target plane X axis (horizontal)
    float m_scale_y{100.f};           // Scale % along target plane Y axis (vertical)
    float m_scale_z{100.f};           // Scale % along target normal (depth)
    bool m_lock_scale{true};          // Lock uniform scaling across all axes
    Vec3d m_orig_size{Vec3d::Zero()}; // Original dimensions in the target plane's coordinate system

    // Stored transform before alignment for undo
    Transform3d m_original_transform{Transform3d::Identity()};

    // Set when accept_as_volume is handling cleanup, prevents on_set_state from interfering
    bool m_accept_in_progress{false};

    // Plane dragging state
    bool m_plane_dragging{false};
    Vec3d m_drag_last_hit{Vec3d::Zero()};
    Vec2d m_plane_offset{Vec2d::Zero()};
    float m_nudge_x{0.f};
    float m_nudge_y{0.f};

    // Saved scale values from when editing began (for uniform scale ratio computation)
    float m_scale_before_x{100.f};
    float m_scale_before_y{100.f};
    float m_scale_before_z{100.f};

    // Panel positioning (measured on first frame, then positioned properly)
    mutable int m_popup_render_count{0};
    mutable float m_popup_height{0.f};

    // Visual feedback
    GLModel m_source_indicator;
    GLModel m_target_indicator;

    // Snap point indicators for face center / feature centers
    struct SnapPoint
    {
        Vec3d position{Vec3d::Zero()};
        Vec3d normal{Vec3d::Zero()};
        float screen_radius{8.0f}; // Screen-space hit detection radius in pixels
        bool is_feature{false};    // True for feature center, false for face center
        bool hovered{false};       // Mouse is within snap distance
    };

    std::vector<SnapPoint> m_snap_points;
    int m_hovered_snap_idx{-1};

    // Drag snap points: target face center/feature points shown during Shift+drag
    std::vector<SnapPoint> m_drag_snap_points;
    int m_drag_snapped_idx{-1};             // Index of snap point currently snapped to (-1 = none)
    Vec2d m_drag_raw_offset{Vec2d::Zero()}; // Unsnapped offset tracking actual mouse position
    FaceData m_hover_face;
    bool m_has_hover{false};
    GLModel m_diamond_model;   // Cached diamond shape for rendering
    GLModel m_snap_ring_model; // Circle ring shown when snap point is hovered

    // Face switch delay: prevents losing snap points when cursor briefly crosses a hole edge
    std::chrono::steady_clock::time_point m_face_switch_time;
    bool m_face_switch_pending{false};
    FaceData m_pending_face;

    // Snap point methods
    void update_hover_snap_points(const Vec2d &mouse_pos);
    Vec3d compute_face_center(const FaceData &face);
    void detect_feature_centers(const FaceData &face, std::vector<SnapPoint> &out);
    void update_hovered_snap_point(const Vec2d &mouse_pos);
    void render_snap_indicator(const Vec3d &position, const Vec3d &normal, const ColorRGBA &color, float size);
    Vec2d project_to_screen(const Vec3d &world_pos) const;

    // Perform the alignment transformation
    void apply_alignment();

    // Reset to idle state
    void reset_state();

    // Try to raycast from mouse position and fill FaceData
    bool raycast_face(const Vec2d &mouse_pos, FaceData &face_out);

    // Draw the ImGui control panel
    void draw_align_panel(float toolbar_x, float icon_y, float toolbar_bottom);

    // Build a consistent 2D coordinate system on a face plane.
    // X+ = rightward, Y+ = upward from the user's perspective.
    static void build_plane_axes(const Vec3d &normal, Vec3d &x_axis, Vec3d &y_axis);

    // Render face highlight indicators
    void render_face_indicator(const FaceData &face, const ColorRGBA &color);

    // Intersect a camera ray with the target face plane, returns hit point in world coords
    bool intersect_target_plane(const Vec2d &mouse_pos, Vec3d &hit_point);

    // Compute the source object's dimensions in the target plane's coordinate system
    void compute_original_size();

    // Snap alignment: 0=min, 1=center, 2=max for each axis on the target plane
    void snap_align(int align_x, int align_y);

    // Convert the aligned source object into a sub-volume of the target
    // volume_type: NEGATIVE_VOLUME for subtract, MODEL_PART for merge
    void accept_as_volume(ModelVolumeType volume_type, const std::string &snapshot_name);
};

} // namespace DSKY

