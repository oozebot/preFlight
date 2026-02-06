///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ThemedTextCtrl.hpp"

namespace Slic3r
{
namespace GUI
{

ThemedTextCtrl::ThemedTextCtrl()
    : wxTextCtrl()
    , m_themedBgColor(*wxWHITE)
    , m_themedFgColor(*wxBLACK)
    , m_bgBrush(*wxWHITE_BRUSH)
    , m_hasThemedColors(false)
{
}

ThemedTextCtrl::ThemedTextCtrl(wxWindow *parent, wxWindowID id, const wxString &value, const wxPoint &pos,
                               const wxSize &size, long style, const wxValidator &validator, const wxString &name)
    : wxTextCtrl()
    , m_themedBgColor(*wxWHITE)
    , m_themedFgColor(*wxBLACK)
    , m_bgBrush(*wxWHITE_BRUSH)
    , m_hasThemedColors(false)
{
    Create(parent, id, value, pos, size, style, validator, name);
}

bool ThemedTextCtrl::Create(wxWindow *parent, wxWindowID id, const wxString &value, const wxPoint &pos,
                            const wxSize &size, long style, const wxValidator &validator, const wxString &name)
{
    return wxTextCtrl::Create(parent, id, value, pos, size, style, validator, name);
}

ThemedTextCtrl::~ThemedTextCtrl()
{
#ifdef _WIN32
    if (m_hBgBrush != NULL)
    {
        DeleteObject(m_hBgBrush);
        m_hBgBrush = NULL;
    }
#endif
}

void ThemedTextCtrl::SetThemedColors(const wxColour &bgColor, const wxColour &fgColor)
{
    m_themedBgColor = bgColor;
    m_themedFgColor = fgColor;
    m_hasThemedColors = true;
    UpdateBrush();

    // Also set via wxWidgets API for initial display
    wxTextCtrl::SetBackgroundColour(bgColor);
    wxTextCtrl::SetForegroundColour(fgColor);

    RefreshThemedColors();
}

void ThemedTextCtrl::SetThemedBackgroundColour(const wxColour &color)
{
    m_themedBgColor = color;
    m_hasThemedColors = true;
    UpdateBrush();

    wxTextCtrl::SetBackgroundColour(color);
    RefreshThemedColors();
}

void ThemedTextCtrl::SetThemedForegroundColour(const wxColour &color)
{
    m_themedFgColor = color;
    m_hasThemedColors = true;

    wxTextCtrl::SetForegroundColour(color);
    RefreshThemedColors();
}

void ThemedTextCtrl::UpdateBrush()
{
    if (m_themedBgColor.IsOk())
    {
        m_bgBrush = wxBrush(m_themedBgColor);
#ifdef _WIN32
        // Also update native GDI brush
        if (m_hBgBrush != NULL)
        {
            DeleteObject(m_hBgBrush);
        }
        m_hBgBrush = CreateSolidBrush(RGB(m_themedBgColor.Red(), m_themedBgColor.Green(), m_themedBgColor.Blue()));
#endif
    }
}

void ThemedTextCtrl::RefreshThemedColors()
{
#ifdef _WIN32
    if (GetHWND())
    {
        // Force Windows to fully repaint - use RedrawWindow which is more aggressive
        // RDW_ERASE triggers WM_ERASEBKGND which we handle
        RedrawWindow((HWND) GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
#else
    Refresh();
#endif
}

#ifdef _WIN32
WXLRESULT ThemedTextCtrl::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Handle WM_ERASEBKGND to paint our own background
    if (nMsg == WM_ERASEBKGND && m_hasThemedColors && m_themedBgColor.IsOk())
    {
        HDC hdc = (HDC) wParam;
        RECT rc;
        ::GetClientRect((HWND) GetHWND(), &rc);

        HBRUSH hBrush = CreateSolidBrush(RGB(m_themedBgColor.Red(), m_themedBgColor.Green(), m_themedBgColor.Blue()));
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);

        return 1; // We handled the erase
    }

    return wxTextCtrl::MSWWindowProc(nMsg, wParam, lParam);
}

WXHBRUSH ThemedTextCtrl::MSWControlColor(WXHDC pDC, WXHWND hWnd)
{
    // This is called by wxWidgets when parent receives WM_CTLCOLOREDIT/WM_CTLCOLORSTATIC
    if (m_hasThemedColors && m_themedBgColor.IsOk())
    {
        HDC hdc = (HDC) pDC;

        // Set text background color (area behind each character)
        ::SetBkColor(hdc, RGB(m_themedBgColor.Red(), m_themedBgColor.Green(), m_themedBgColor.Blue()));
        ::SetBkMode(hdc, OPAQUE);

        // Set text foreground color
        if (m_themedFgColor.IsOk())
        {
            ::SetTextColor(hdc, RGB(m_themedFgColor.Red(), m_themedFgColor.Green(), m_themedFgColor.Blue()));
        }

        // Create brush on demand if needed
        if (m_hBgBrush == NULL)
        {
            m_hBgBrush = CreateSolidBrush(RGB(m_themedBgColor.Red(), m_themedBgColor.Green(), m_themedBgColor.Blue()));
        }

        return (WXHBRUSH) m_hBgBrush;
    }

    return wxTextCtrl::MSWControlColor(pDC, hWnd);
}
#endif

} // namespace GUI
} // namespace Slic3r
