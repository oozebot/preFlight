///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/textctrl.h>
#include <wx/brush.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace DSKY
{

/// ThemedTextCtrl - A wxTextCtrl that properly supports custom background colors on Windows
///
/// On Windows, native EDIT controls (wrapped by wxTextCtrl) ignore SetBackgroundColour()
/// after they become visible. This class intercepts WM_CTLCOLOREDIT and WM_CTLCOLORSTATIC
/// messages to force our desired colors, bypassing Windows theme limitations.
///
/// Usage:
///   auto* text = new ThemedTextCtrl(parent, wxID_ANY, "initial text");
///   text->SetThemedColors(bgColor, fgColor);
///   // Colors will work even after theme switch!
///
class ThemedTextCtrl : public wxTextCtrl
{
public:
    ThemedTextCtrl();

    ThemedTextCtrl(wxWindow *parent, wxWindowID id, const wxString &value = wxEmptyString,
                   const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0,
                   const wxValidator &validator = wxDefaultValidator, const wxString &name = wxTextCtrlNameStr);

    bool Create(wxWindow *parent, wxWindowID id, const wxString &value = wxEmptyString,
                const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0,
                const wxValidator &validator = wxDefaultValidator, const wxString &name = wxTextCtrlNameStr);

    virtual ~ThemedTextCtrl();

    /// Set both background and foreground colors
    /// These colors WILL be applied even on Windows, even after the control is visible
    void SetThemedColors(const wxColour &bgColor, const wxColour &fgColor);

    /// Set just the background color
    void SetThemedBackgroundColour(const wxColour &color);

    /// Set just the foreground color
    void SetThemedForegroundColour(const wxColour &color);

    /// Get the current themed background color
    wxColour GetThemedBackgroundColour() const { return m_themedBgColor; }

    /// Get the current themed foreground color
    wxColour GetThemedForegroundColour() const { return m_themedFgColor; }

    /// Force refresh of the control with current themed colors
    void RefreshThemedColors();

#ifdef _WIN32
    /// Windows message handler - intercepts WM_ERASEBKGND to force our colors
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;

    /// wxWidgets control color handler - called when parent receives WM_CTLCOLOREDIT/STATIC
    /// This is the KEY method for setting edit control background color in wxWidgets
    virtual WXHBRUSH MSWControlColor(WXHDC pDC, WXHWND hWnd) override;
#endif

private:
    void UpdateBrush();

    wxColour m_themedBgColor;
    wxColour m_themedFgColor;
    wxBrush m_bgBrush;
#ifdef _WIN32
    HBRUSH m_hBgBrush = NULL; // Native GDI brush for MSWControlColor
#endif
    bool m_hasThemedColors = false;
    bool m_wheelScrollActive = false; // True after a click inside multiline, cleared on mouse-leave

public:
    /// Allow TextInput to activate/deactivate wheel scrolling for multiline controls
    void SetWheelScrollActive(bool active) { m_wheelScrollActive = active; }
    bool GetWheelScrollActive() const { return m_wheelScrollActive; }
};

} // namespace DSKY
