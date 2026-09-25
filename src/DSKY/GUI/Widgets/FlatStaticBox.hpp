///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/statbox.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace DSKY
{

/// FlatStaticBox - A wxStaticBox that draws flat borders in both light and dark mode
///
/// Windows: Uses DarkMode_Explorer theme (dark) or custom WM_PAINT overlay (light)
/// GTK3:    Hooks the GtkFrame "draw" signal to custom-draw via cairo
///
class FlatStaticBox : public wxStaticBox
{
public:
    FlatStaticBox() = default;

    FlatStaticBox(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos = wxDefaultPosition,
                  const wxSize &size = wxDefaultSize, long style = 0, const wxString &name = wxStaticBoxNameStr);

    bool Create(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize, long style = 0, const wxString &name = wxStaticBoxNameStr);

    void SetBorderColor(const wxColour &color)
    {
        m_borderColor = color;
        Refresh();
    }
    wxColour GetBorderColor() const { return m_borderColor; }

    void SetDrawFlatBorder(bool draw)
    {
        m_drawFlatBorder = draw;
        Refresh();
    }
    bool GetDrawFlatBorder() const { return m_drawFlatBorder; }

    // Call when system colors change (dark/light mode switch)
    void SysColorsChanged();

    // Call when DPI changes
    void msw_rescale() { Refresh(); }

#ifdef __WXGTK__
    // GTK: set header panel so the draw handler can redraw it unclipped
    void SetHeaderPanel(wxWindow *panel) { m_headerPanel = panel; }
    wxWindow *GetHeaderPanel() const { return m_headerPanel; }
#endif

#ifdef _WIN32
protected:
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

#ifdef __WXOSX__
    // macOS custom paint handler: draws borders and title text
    // since the native NSBox is made transparent.
    void OnPaintMac(wxPaintEvent &evt);
#endif

private:
    wxColour m_borderColor{0, 0, 0}; // Black for light mode
    bool m_drawFlatBorder{true};
#ifdef __WXGTK__
    wxWindow *m_headerPanel{nullptr};
#endif

    void UpdateTheme();
};

} // namespace DSKY
