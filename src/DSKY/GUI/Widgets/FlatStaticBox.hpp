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
    // The sizer asks on every layout, and wx measures the label's font through a new DC each time;
    // the answer is kept until the label, the font or the DPI changes
    void GetBordersForSizer(int *borderTop, int *borderOther) const override;

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
#ifdef _WIN32
    // GetBordersForSizer's last answer and what it was computed from
    mutable int m_borderTop{-1};
    mutable int m_borderOther{0};
    mutable wxFont m_borderFont;
    mutable wxString m_borderLabel;
    mutable double m_borderScale{0.};

    wxColour BandColour() const;
    // Paints a box with a blank label in one pass; false leaves the paint to wx
    bool MSWPaintBlank();
#endif

    void UpdateTheme();
};

} // namespace DSKY
