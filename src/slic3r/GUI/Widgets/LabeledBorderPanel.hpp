///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_LabeledBorderPanel_hpp_
#define slic3r_GUI_LabeledBorderPanel_hpp_

#include <wx/panel.h>
#include <wx/sizer.h>

namespace Slic3r
{
namespace GUI
{

// A panel that draws a 1px border with a centered label overlaying the top border
class LabeledBorderPanel : public wxPanel
{
public:
    LabeledBorderPanel(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos = wxDefaultPosition,
                       const wxSize &size = wxDefaultSize);

    void SetLabel(const wxString &label);
    wxString GetLabel() const { return m_label; }

    // Get the inner sizer where child controls should be added
    wxBoxSizer *GetInnerSizer() { return m_inner_sizer; }

    // Update colors for theme change
    void sys_color_changed();

    // Update layout for DPI change
    void msw_rescale();

private:
    void OnPaint(wxPaintEvent &event);
    void UpdateColors();

    wxString m_label;
    wxBoxSizer *m_inner_sizer;

    wxColour m_border_color;
    wxColour m_text_color;
    wxColour m_bg_color;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_LabeledBorderPanel_hpp_
