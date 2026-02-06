///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "FlatStaticBox.hpp"
#include "../GUI_App.hpp"
#include "UIColors.hpp"

#ifdef _WIN32
#include "../DarkMode.hpp"
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#endif

namespace Slic3r
{
namespace GUI
{

// DPI scaling helpers
static int GetScaledEraseWidth()
{
    return wxGetApp().em_unit() / 3; // 3px at 100%
}

static int GetScaledLabelStartPadding()
{
    return (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
}

static int GetScaledLabelEndPadding()
{
    return (wxGetApp().em_unit() * 4) / 10; // 4px at 100%
}

static int GetScaledLabelGap()
{
    return std::max(1, (wxGetApp().em_unit() * 2) / 10); // 2px at 100%, min 1px
}

static int GetScaledBorderWidth()
{
    return std::max(1, wxGetApp().em_unit() / 10); // 1px at 100%, min 1px
}

FlatStaticBox::FlatStaticBox(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos,
                             const wxSize &size, long style, const wxString &name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool FlatStaticBox::Create(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos,
                           const wxSize &size, long style, const wxString &name)
{
    if (!wxStaticBox::Create(parent, id, label, pos, size, style, name))
        return false;

    UpdateTheme();
    return true;
}

void FlatStaticBox::UpdateTheme()
{
#ifdef _WIN32
    if (wxGetApp().dark_mode())
    {
        // Dark mode: use DarkMode_Explorer theme which has built-in flat borders
        NppDarkMode::SetDarkExplorerTheme(GetHWND());
        // Set lighter background for section interiors (#161B22 vs page #0D1117)
        SetBackgroundColour(UIColors::InputBackgroundDark());
        SetForegroundColour(UIColors::InputForegroundDark());
    }
    else
    {
        // Light mode: use classic theme for correct border POSITION (50% label height)
        // We'll paint flat colors over the 3D effect in WM_PAINT
        SetWindowTheme((HWND) GetHWND(), L"", L"");
        SetBackgroundColour(UIColors::InputBackgroundLight());
        SetForegroundColour(UIColors::InputForegroundLight());
    }
#endif
}

void FlatStaticBox::SysColorsChanged()
{
    UpdateTheme();
    Refresh();
}

#ifdef _WIN32
WXLRESULT FlatStaticBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Let Windows paint first (this draws the 3D border in light mode with classic theme)
    WXLRESULT result = wxStaticBox::MSWWindowProc(nMsg, wParam, lParam);

    // Only flatten the border in light mode
    // Dark mode uses DarkMode_Explorer theme which already has flat borders
    bool is_dark = wxGetApp().dark_mode();

    if (nMsg == WM_PAINT && m_drawFlatBorder && m_borderColor.IsOk() && !is_dark)
    {
        HWND hwnd = (HWND) GetHWND();
        HDC hdc = ::GetWindowDC(hwnd);

        // Get window dimensions
        RECT windowRect;
        ::GetWindowRect(hwnd, &windowRect);
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;

        // Get label text and calculate its extent
        wchar_t labelText[256] = {0};
        ::GetWindowTextW(hwnd, labelText, 256);

        // Get the font used by the control
        HFONT hFont = (HFONT)::SendMessage(hwnd, WM_GETFONT, 0, 0);
        if (!hFont)
            hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)::SelectObject(hdc, hFont);

        SIZE textSize = {0, 0};
        if (labelText[0] != 0)
            ::GetTextExtentPoint32W(hdc, labelText, (int) wcslen(labelText), &textSize);

        ::SelectObject(hdc, oldFont);

        // The classic theme 3D border position:
        // - Top line: at textSize.cy / 2, with gap for label
        // - Other sides: at window edges
        // The 3D effect is 2 pixels wide, we'll paint over it with flat color

        int topLineY = textSize.cy / 2;
        int labelStartX = GetScaledLabelStartPadding(); // Standard padding before label (scaled)
        int labelEndX = labelStartX + textSize.cx + GetScaledLabelEndPadding(); // Label width + padding (scaled)
        int eraseWidth = GetScaledEraseWidth();                                 // Width to erase 3D effect (scaled)
        int labelGap = GetScaledLabelGap();   // Gap between label and border line (scaled)
        int borderW = GetScaledBorderWidth(); // Border line width (scaled)

        // Get background color from the control's parent via wxWidgets
        wxWindow *wxParent = GetParent();
        wxColour wxBgColor = wxParent ? wxParent->GetBackgroundColour() : GetBackgroundColour();
        if (!wxBgColor.IsOk())
            wxBgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        COLORREF bgColor = RGB(wxBgColor.Red(), wxBgColor.Green(), wxBgColor.Blue());

        HBRUSH bgBrush = ::CreateSolidBrush(bgColor);
        HBRUSH borderBrush = ::CreateSolidBrush(RGB(m_borderColor.Red(), m_borderColor.Green(), m_borderColor.Blue()));

        RECT rc;

        // First, erase the 3D border by painting background color over it
        // Left edge - erase
        rc.left = 0;
        rc.top = topLineY - borderW;
        rc.right = eraseWidth;
        rc.bottom = height;
        ::FillRect(hdc, &rc, bgBrush);

        // Bottom edge - erase
        rc.left = 0;
        rc.top = height - eraseWidth;
        rc.right = width;
        rc.bottom = height;
        ::FillRect(hdc, &rc, bgBrush);

        // Right edge - erase
        rc.left = width - eraseWidth;
        rc.top = topLineY - borderW;
        rc.right = width;
        rc.bottom = height;
        ::FillRect(hdc, &rc, bgBrush);

        // Top edge - erase (skip label area)
        if (labelText[0] != 0)
        {
            rc.left = 0;
            rc.top = topLineY - borderW;
            rc.right = labelStartX - labelGap;
            rc.bottom = topLineY + eraseWidth;
            ::FillRect(hdc, &rc, bgBrush);

            rc.left = labelEndX + labelGap;
            rc.top = topLineY - borderW;
            rc.right = width;
            rc.bottom = topLineY + eraseWidth;
            ::FillRect(hdc, &rc, bgBrush);
        }
        else
        {
            rc.left = 0;
            rc.top = topLineY - borderW;
            rc.right = width;
            rc.bottom = topLineY + eraseWidth;
            ::FillRect(hdc, &rc, bgBrush);
        }

        // Now draw the border (scaled width)
        // Left edge
        rc.left = 0;
        rc.top = topLineY;
        rc.right = borderW;
        rc.bottom = height;
        ::FillRect(hdc, &rc, borderBrush);

        // Bottom edge
        rc.left = 0;
        rc.top = height - borderW;
        rc.right = width;
        rc.bottom = height;
        ::FillRect(hdc, &rc, borderBrush);

        // Right edge
        rc.left = width - borderW;
        rc.top = topLineY;
        rc.right = width;
        rc.bottom = height;
        ::FillRect(hdc, &rc, borderBrush);

        // Top edge - in two segments, skipping the label area
        if (labelText[0] != 0)
        {
            rc.left = 0;
            rc.top = topLineY;
            rc.right = labelStartX - labelGap;
            rc.bottom = topLineY + borderW;
            ::FillRect(hdc, &rc, borderBrush);

            rc.left = labelEndX + labelGap;
            rc.top = topLineY;
            rc.right = width;
            rc.bottom = topLineY + borderW;
            ::FillRect(hdc, &rc, borderBrush);
        }
        else
        {
            rc.left = 0;
            rc.top = topLineY;
            rc.right = width;
            rc.bottom = topLineY + borderW;
            ::FillRect(hdc, &rc, borderBrush);
        }

        ::DeleteObject(bgBrush);
        ::DeleteObject(borderBrush);
        ::ReleaseDC(hwnd, hdc);
    }

    return result;
}
#endif

} // namespace GUI
} // namespace Slic3r
