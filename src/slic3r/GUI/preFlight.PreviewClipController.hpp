///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_preFlight_PreviewClipController_hpp_
#define slic3r_preFlight_PreviewClipController_hpp_

#include "MeshUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <optional>
#include <vector>

namespace Slic3r
{
namespace GUI
{

class GLCanvas3D;
class GCodeViewer;
class Camera;

// Controller for interactive clipping plane in the G-code preview view.
// Right-click an object in preview -> "Clipping Plane" -> isolates the object
// and shows a clipping plane slider. Closing the dialog restores normal preview.
class PreviewClipController
{
public:
    // Activate clipping for a specific object in the preview.
    // object_id: index into Model.objects matching GLVolume::composite_id.object_id
    void activate(int object_id);

    // Deactivate and restore normal preview.
    void deactivate();

    // Update clipping plane position (0.0 = near face, 1.0 = far face of object bbox).
    void set_position(double ratio);

    // Reset clipping direction to current camera forward vector.
    void reset_direction();

    // Render the ImGui control overlay (slider + reset + close buttons).
    // Returns true if the controller consumed the frame (caller may skip other overlays).
    void render_imgui();

    bool is_active() const { return m_active; }
    int get_object_id() const { return m_object_id; }

private:
    void apply_clipping_plane();
    std::string get_object_name() const;

    bool m_active = false;
    int m_object_id = -1;

    // Clipping state
    Vec3d m_clip_normal{0.0, 0.0, 1.0};
    double m_clip_ratio = 0.5;
    BoundingBoxf3 m_object_bbox; // world-space bounding box of the selected object's shells

    // Saved state for restoration
    struct SavedState
    {
        std::vector<bool> shell_visibility; // per-volume is_active flags
        bool shells_visible;
    };
    std::optional<SavedState> m_saved_state;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_preFlight_PreviewClipController_hpp_
