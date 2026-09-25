///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GLGizmoPainterBase.hpp"

#include "DSKY/GUI/I18N.hpp"

namespace DSKY
{
using namespace Luminary;

class GLGizmoCounterboreBridge : public GLGizmoPainterBase
{
public:
    GLGizmoCounterboreBridge(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id)
    {
    }

    void render_painter_gizmo() override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    wxString handle_snapshot_action_name(bool control_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Counterbore bridge painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Counterbore bridge painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Counterbore bridge editing"); }

    // State value encodes bridge_layers count (2-9). Old ENFORCER (1) is treated as 2 during slicing.
    TriangleStateType get_left_button_state_type() const override
    {
        return static_cast<TriangleStateType>(m_bridge_layers);
    }
    TriangleStateType get_right_button_state_type() const override { return TriangleStateType::NONE; }

    bool gizmo_event(GizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down,
                     bool control_down) override;

private:
    bool on_init() override;
    bool on_is_selectable() const override { return true; }

    void update_model_object() const override;
    void update_from_model_object() override;

    mutable int m_popup_render_count = 0;
    mutable float m_popup_width = 0.0f;
    mutable float m_popup_height = 0.0f;

    void on_opening() override
    {
        m_popup_render_count = 0;
        m_popup_width = 0.0f;
        m_popup_height = 0.0f;
    }
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    int m_bridge_layers = 2;

    // Track last-clicked painted region for slider-driven repainting
    int m_last_cb_mesh_id = -1;
    int m_last_cb_facet = -1;
    Vec3f m_last_cb_hit = Vec3f::Zero();

    std::map<std::string, std::string> m_desc;
};

} // namespace DSKY

