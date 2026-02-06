///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_FlatStaticBox_hpp_
#define slic3r_FlatStaticBox_hpp_

#include <wx/statbox.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Slic3r
{
namespace GUI
{

/// FlatStaticBox - A wxStaticBox that draws flat borders in both light and dark mode
///
/// In light mode: Uses Explorer theme (invisible borders) and draws custom flat borders
/// In dark mode: Uses DarkMode_Explorer theme which has built-in flat borders
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

#ifdef _WIN32
protected:
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    wxColour m_borderColor{0, 0, 0}; // Black for light mode
    bool m_drawFlatBorder{true};

    void UpdateTheme();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FlatStaticBox_hpp_
