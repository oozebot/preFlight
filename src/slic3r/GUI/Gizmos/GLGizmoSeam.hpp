///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoSeam_hpp_
#define slic3r_GLGizmoSeam_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI
{

class GLGizmoSeam : public GLGizmoPainterBase
{
public:
    GLGizmoSeam(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id)
    {
    }

    void render_painter_gizmo() override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    wxString handle_snapshot_action_name(bool control_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Seam painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Seam painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on seam editing"); }

private:
    bool on_init() override;

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

    // Seam detection parameter
    float m_seam_detection = 0.05f; // Detection radius in mm (0.01-1.00)

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, std::string> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSeam_hpp_
