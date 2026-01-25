///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_Slider_hpp_
#define slic3r_GUI_Slider_hpp_

#include <wx/panel.h>

// Simple horizontal slider widget with orange theme
// Reusable for any slider needs in the application
class Slider : public wxPanel
{
public:
    Slider(wxWindow *parent, wxWindowID id = wxID_ANY, int value = 0, int minValue = 0, int maxValue = 100,
           const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    void SetValue(int value);
    int GetValue() const { return m_value; }

    void SetRange(int minValue, int maxValue);
    int GetMin() const { return m_min; }
    int GetMax() const { return m_max; }

    void SetToolTip(const wxString &tip);

    void sys_color_changed() { Refresh(); }

private:
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);
    void OnMouseWheel(wxMouseEvent &event);
    void OnSize(wxSizeEvent &event);

    int ValueFromPosition(int x) const;
    int PositionFromValue() const;
    wxRect GetThumbRect() const;

    void NotifyValueChanged();

    int m_value;
    int m_min;
    int m_max;
    bool m_dragging;

    // Brand color #EAA032 (RGB: 234, 160, 50)
    static const wxColour BRAND_COLOR;
    static const wxColour BRAND_COLOR_DARK;

    wxDECLARE_EVENT_TABLE();
};

#endif // !slic3r_GUI_Slider_hpp_
