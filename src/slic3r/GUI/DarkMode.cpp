///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "DarkMode.hpp"
#include "Widgets/UIColors.hpp"

#ifdef _WIN32

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <commctrl.h>

// Header control messages - define if not available
#ifndef HDM_SETBKCOLOR
#define HDM_SETBKCOLOR (HDM_FIRST + 8)
#endif
#ifndef HDM_SETTEXTCOLOR
#define HDM_SETTEXTCOLOR (HDM_FIRST + 9)
#endif

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

// Undocumented UAH (User Accessible Handle) messages for menu theming
// These are used by Windows for custom menu rendering
// Reference: https://github.com/adzm/win32-custom-menubar-aero-theme
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092

// UAH menu structures (undocumented)
// These must match the internal Windows structures exactly
typedef struct tagUAHMENU
{
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags; // observed values: 0x00000a00, 0x00000a10
} UAHMENU;

typedef struct tagUAHMENUITEM
{
    int iPosition; // 0-based position of menu item
    UINT state;    // menu item state
    HMENU hMenu;
} UAHMENUITEM;

typedef struct tagUAHDRAWMENUITEM
{
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHDRAWMENUITEM;

// Undocumented Windows APIs for dark mode
// These are used by Windows itself and other apps like Notepad++
extern "C"
{
    // ordinal 132 in uxtheme.dll - Windows 10 1809+
    typedef bool(WINAPI *fnShouldAppsUseDarkMode)();
    // ordinal 135 in uxtheme.dll - Windows 10 1903+
    typedef void(WINAPI *fnSetPreferredAppMode)(int mode);
    // ordinal 136 in uxtheme.dll
    typedef void(WINAPI *fnFlushMenuThemes)();
    // ordinal 133 in uxtheme.dll
    typedef bool(WINAPI *fnAllowDarkModeForWindow)(HWND hwnd, bool allow);
    // ordinal 104 in uxtheme.dll
    typedef void(WINAPI *fnRefreshImmersiveColorPolicyState)();
}

// App mode values for SetPreferredAppMode
enum PreferredAppMode
{
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3
};

// DWMWA_USE_IMMERSIVE_DARK_MODE - works on Windows 10 20H1+ and Windows 11
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Pre-20H1 value (Windows 10 1903-1909)
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

// Windows 11 custom caption color support (build 22000+)
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace NppDarkMode
{

// Global state
static bool g_darkModeEnabled = false;
static bool g_darkModeSupported = false;
static HMODULE g_uxtheme = nullptr;

// Function pointers
static fnShouldAppsUseDarkMode g_shouldAppsUseDarkMode = nullptr;
static fnSetPreferredAppMode g_setPreferredAppMode = nullptr;
static fnFlushMenuThemes g_flushMenuThemes = nullptr;
static fnAllowDarkModeForWindow g_allowDarkModeForWindow = nullptr;
static fnRefreshImmersiveColorPolicyState g_refreshImmersiveColorPolicyState = nullptr;

// Check Windows build number
static DWORD GetWindowsBuildNumber()
{
    HKEY hKey;
    DWORD buildNumber = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS)
    {
        DWORD size = sizeof(DWORD);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, &type, nullptr, &size) == ERROR_SUCCESS)
        {
            wchar_t buildStr[32];
            size = sizeof(buildStr);
            type = REG_SZ;
            if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, &type, (LPBYTE) buildStr, &size) ==
                ERROR_SUCCESS)
            {
                buildNumber = _wtoi(buildStr);
            }
        }
        RegCloseKey(hKey);
    }
    return buildNumber;
}

// Initialize dark mode function pointers
static bool InitDarkModeApis()
{
    if (g_uxtheme)
        return g_darkModeSupported;

    DWORD buildNumber = GetWindowsBuildNumber();
    // Dark mode requires Windows 10 1809 (build 17763) or later
    if (buildNumber < 17763)
        return false;

    g_uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_uxtheme)
        return false;

    // Get function pointers by ordinal
    g_shouldAppsUseDarkMode = (fnShouldAppsUseDarkMode) GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(132));
    g_allowDarkModeForWindow = (fnAllowDarkModeForWindow) GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(133));
    g_refreshImmersiveColorPolicyState = (fnRefreshImmersiveColorPolicyState) GetProcAddress(g_uxtheme,
                                                                                             MAKEINTRESOURCEA(104));

    // SetPreferredAppMode is only available on 1903+
    if (buildNumber >= 18362)
    {
        g_setPreferredAppMode = (fnSetPreferredAppMode) GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(135));
        g_flushMenuThemes = (fnFlushMenuThemes) GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(136));
    }

    g_darkModeSupported = g_shouldAppsUseDarkMode != nullptr;
    return g_darkModeSupported;
}

void AllowDarkModeForApp()
{
    if (!InitDarkModeApis())
        return;

    if (g_setPreferredAppMode)
    {
        g_setPreferredAppMode(AllowDark);
    }
}

void InitDarkMode(bool darkMode, bool /*fixDarkScrollbar*/)
{
    g_darkModeEnabled = darkMode;

    if (!InitDarkModeApis())
        return;

    // Set preferred app mode at init, before any windows are created.
    // Note: Common dialogs (Open/Save) follow Windows system theme on Windows 11,
    // not the app's SetPreferredAppMode setting. This is a known limitation.
    if (g_setPreferredAppMode)
    {
        g_setPreferredAppMode(darkMode ? ForceDark : ForceLight);

        if (g_flushMenuThemes)
            g_flushMenuThemes();

        if (g_refreshImmersiveColorPolicyState)
            g_refreshImmersiveColorPolicyState();
    }
}

void SetDarkMode(bool darkMode)
{
    g_darkModeEnabled = darkMode;

    if (!g_darkModeSupported)
        return;

    // Update preferred app mode when theme changes.
    // Note: This may not affect already-open windows or common dialogs.
    if (g_setPreferredAppMode)
    {
        g_setPreferredAppMode(darkMode ? ForceDark : ForceLight);

        if (g_flushMenuThemes)
            g_flushMenuThemes();

        if (g_refreshImmersiveColorPolicyState)
            g_refreshImmersiveColorPolicyState();
    }
}

bool IsDarkModeEnabled()
{
    return g_darkModeEnabled;
}

void SetDarkTitleBar(HWND hwnd)
{
    if (!hwnd)
        return;

    BOOL darkMode = g_darkModeEnabled ? TRUE : FALSE;

    // Try the standard attribute first (Windows 10 20H1+, Windows 11)
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // If that fails, try the pre-20H1 attribute (Windows 10 1903-1909)
    if (FAILED(hr))
    {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &darkMode, sizeof(darkMode));
    }

    // Windows 11 (build 22000+): Set custom caption color to match our theme
    // Note: preFlight always uses dark mode APIs - light mode is just a different color palette
    DWORD buildNumber = GetWindowsBuildNumber();
    if (buildNumber >= 22000)
    {
        COLORREF captionColor = UIColorsWin::TitleBarBackground();
        COLORREF textColor = UIColorsWin::TitleBarText();
        COLORREF borderColor = UIColorsWin::TitleBarBorder();

        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    }
}

void AllowDarkModeForWindow(HWND hwnd)
{
    if (!hwnd || !g_darkModeSupported)
        return;

    if (g_allowDarkModeForWindow)
    {
        // UNIFIED THEMING: Always allow dark mode for windows.
        // This enables DarkMode_Explorer theme to work regardless of our color palette.
        // The actual colors come from WM_CTLCOLOREDIT handlers based on UIColors.
        g_allowDarkModeForWindow(hwnd, true);
    }
}

void SetDarkExplorerTheme(HWND hwnd)
{
    if (!hwnd)
        return;

    // Title bar color follows our theme (light/dark)
    BOOL darkAttr = g_darkModeEnabled ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkAttr, sizeof(darkAttr));

    // Windows 11: Set custom caption color to match our theme
    // Note: preFlight always uses dark mode APIs - light mode is just a different color palette
    DWORD buildNumber = GetWindowsBuildNumber();
    if (buildNumber >= 22000)
    {
        COLORREF captionColor = UIColorsWin::TitleBarBackground();
        COLORREF textColor = UIColorsWin::TitleBarText();
        COLORREF borderColor = UIColorsWin::TitleBarBorder();

        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    }

    if (g_darkModeEnabled)
    {
        // Dark mode: use DarkMode_Explorer for proper scrollbar/control theming
        if (g_allowDarkModeForWindow)
            g_allowDarkModeForWindow(hwnd, true);
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }
    else
    {
        // Light mode: use regular Explorer theme, don't enable dark mode APIs
        if (g_allowDarkModeForWindow)
            g_allowDarkModeForWindow(hwnd, false);
        SetWindowTheme(hwnd, L"Explorer", nullptr);
    }
}

void SetSystemMenuForApp(bool enabled)
{
    if (!g_darkModeSupported)
        return;

    if (g_flushMenuThemes)
    {
        g_flushMenuThemes();
    }
}

void RefreshTitleBarThemeColor(HWND hwnd)
{
    SetDarkTitleBar(hwnd);

    // Force a redraw of the non-client area
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

COLORREF GetSofterBackgroundColor()
{
    // A slightly lighter dark background for disabled controls
    return g_darkModeEnabled ? UIColorsWin::SofterBackgroundDark() : GetSysColor(COLOR_3DFACE);
}

COLORREF GetBackgroundColor()
{
    return g_darkModeEnabled ? UIColorsWin::WindowBackgroundDark() : GetSysColor(COLOR_WINDOW);
}

COLORREF GetTextColor()
{
    return g_darkModeEnabled ? UIColorsWin::WindowTextDark() : GetSysColor(COLOR_WINDOWTEXT);
}

// UAH menu drawing subclass procedure
static LRESULT CALLBACK UAHMenuSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    switch (uMsg)
    {
    case WM_UAHDRAWMENU:
    {
        if (!g_darkModeEnabled)
            break;

        UAHMENU *pUDM = (UAHMENU *) lParam;
        if (pUDM && pUDM->hdc)
        {
            MENUBARINFO mbi = {sizeof(mbi)};
            if (GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hWnd, &rcWindow);

                // Convert screen coordinates to window coordinates
                RECT rc = mbi.rcBar;
                OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

                int windowWidth = rcWindow.right - rcWindow.left;
                rc.right = windowWidth;
                rc.bottom += 2;
                // Fill the entire menu bar background
                HBRUSH hBrush = CreateSolidBrush(UIColorsWin::MenuBackground());
                FillRect(pUDM->hdc, &rc, hBrush);
                DeleteObject(hBrush);
            }
        }
        return 0;
    }

    case WM_UAHDRAWMENUITEM:
    {
        if (!g_darkModeEnabled)
            break;

        UAHDRAWMENUITEM *pUDMI = (UAHDRAWMENUITEM *) lParam;
        if (pUDMI)
        {
            DRAWITEMSTRUCT &dis = pUDMI->dis;

            // Determine colors based on state
            COLORREF bgColor = UIColorsWin::MenuBackground();
            COLORREF textColor = UIColorsWin::MenuText();

            bool isHot = (dis.itemState & ODS_HOTLIGHT) != 0;
            bool isSelected = (dis.itemState & ODS_SELECTED) != 0;
            bool isDisabled = (dis.itemState & (ODS_INACTIVE | ODS_DISABLED | ODS_GRAYED)) != 0;

            if (isHot || isSelected)
            {
                bgColor = UIColorsWin::MenuHotBackground();
            }
            if (isDisabled)
            {
                textColor = UIColorsWin::MenuDisabledText();
            }

            // Fill background
            HBRUSH hBrush = CreateSolidBrush(bgColor);
            FillRect(dis.hDC, &dis.rcItem, hBrush);
            DeleteObject(hBrush);

            // Draw a subtle border when hot
            if (isHot || isSelected)
            {
                HBRUSH hBorderBrush = CreateSolidBrush(RGB(0x50, 0x50, 0x50));
                FrameRect(dis.hDC, &dis.rcItem, hBorderBrush);
                DeleteObject(hBorderBrush);
            }

            // Get menu item text
            wchar_t menuText[256] = {0};
            MENUITEMINFOW mii = {sizeof(mii)};
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = menuText;
            mii.cch = _countof(menuText);
            if (GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii))
            {
                // Draw text centered
                SetBkMode(dis.hDC, TRANSPARENT);
                SetTextColor(dis.hDC, textColor);

                DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
                // Hide accelerator prefix if requested
                if (dis.itemState & ODS_NOACCEL)
                {
                    dwFlags |= DT_HIDEPREFIX;
                }
                DrawTextW(dis.hDC, menuText, -1, &dis.rcItem, dwFlags);
            }
        }
        return 0;
    }

    case WM_NCPAINT:
    case WM_NCACTIVATE:
    {
        // Let the default handler run first
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);

        if (g_darkModeEnabled)
        {
            // Draw a line at the bottom of the menu bar to cover the light separator line
            MENUBARINFO mbi = {sizeof(mbi)};
            if (GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hWnd, &rcWindow);

                // Convert screen coordinates to window coordinates
                RECT rc = mbi.rcBar;
                OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

                int windowWidth = rcWindow.right - rcWindow.left;
                RECT rcLine = {rc.left, rc.bottom, windowWidth, rc.bottom + 2};
                HDC hdc = GetWindowDC(hWnd);
                if (hdc)
                {
                    HBRUSH hBrush = CreateSolidBrush(UIColorsWin::MenuBackground());
                    FillRect(hdc, &rcLine, hBrush);
                    DeleteObject(hBrush);
                    ReleaseDC(hWnd, hdc);
                }
            }
        }
        return result;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass ID for menu theming
static const UINT_PTR SUBCLASS_ID_DARKMENUS = 0x1001;

void EnableDarkMenuForWindow(HWND hwnd)
{
    if (!hwnd || !g_darkModeSupported)
        return;

    // Allow dark mode for this window
    if (g_allowDarkModeForWindow)
    {
        g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
    }

    // Subclass the window to handle UAH menu messages
    SetWindowSubclass(hwnd, UAHMenuSubclassProc, SUBCLASS_ID_DARKMENUS, 0);

    // Force menu bar to redraw
    DrawMenuBar(hwnd);
}

void DisableDarkMenuForWindow(HWND hwnd)
{
    if (!hwnd)
        return;

    RemoveWindowSubclass(hwnd, UAHMenuSubclassProc, SUBCLASS_ID_DARKMENUS);
}

// Header subclass for background
static LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/,
                                           DWORD_PTR /*dwRefData*/)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC) wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);

        COLORREF bgColor = g_darkModeEnabled ? UIColorsWin::HeaderBackgroundDark()
                                             : UIColorsWin::HeaderBackgroundLight();
        HBRUSH hBrush = CreateSolidBrush(bgColor);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        return 1;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, HeaderSubclassProc, 0);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Parent subclass to handle NM_CUSTOMDRAW for header items - draw everything ourselves
static LRESULT CALLBACK HeaderParentSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NOTIFY)
    {
        NMHDR *nmhdr = (NMHDR *) lParam;
        if (nmhdr->code == NM_CUSTOMDRAW && nmhdr->hwndFrom == (HWND) dwRefData)
        {
            NMCUSTOMDRAW *nmcd = (NMCUSTOMDRAW *) lParam;

            switch (nmcd->dwDrawStage)
            {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;

            case CDDS_ITEMPREPAINT:
            {
                COLORREF bgColor = g_darkModeEnabled ? UIColorsWin::HeaderBackgroundDark()
                                                     : UIColorsWin::HeaderBackgroundLight();
                COLORREF textColor = g_darkModeEnabled ? UIColorsWin::TextDark() : UIColorsWin::TextLight();
                COLORREF dividerColor = g_darkModeEnabled ? UIColorsWin::HeaderDividerDark()
                                                          : UIColorsWin::HeaderDividerLight();

                // Fill item background
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(nmcd->hdc, &nmcd->rc, hBrush);
                DeleteObject(hBrush);

                // Draw divider line on right edge
                HPEN hPen = CreatePen(PS_SOLID, 1, dividerColor);
                HPEN hOldPen = (HPEN) SelectObject(nmcd->hdc, hPen);
                MoveToEx(nmcd->hdc, nmcd->rc.right - 1, nmcd->rc.top + 2, NULL);
                LineTo(nmcd->hdc, nmcd->rc.right - 1, nmcd->rc.bottom - 2);
                SelectObject(nmcd->hdc, hOldPen);
                DeleteObject(hPen);

                // Get item text
                HWND hHeader = (HWND) dwRefData;
                int itemIndex = (int) nmcd->dwItemSpec;
                HDITEMW hdi = {0};
                wchar_t szText[256] = {0};
                hdi.mask = HDI_TEXT | HDI_FORMAT;
                hdi.pszText = szText;
                hdi.cchTextMax = 256;
                Header_GetItem(hHeader, itemIndex, &hdi);

                // Draw text ourselves
                SetTextColor(nmcd->hdc, textColor);
                SetBkMode(nmcd->hdc, TRANSPARENT);

                RECT rcText = nmcd->rc;
                rcText.left += 6;
                rcText.right -= 6;

                UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
                if (hdi.fmt & HDF_CENTER)
                    format |= DT_CENTER;
                else if (hdi.fmt & HDF_RIGHT)
                    format |= DT_RIGHT;
                else
                    format |= DT_LEFT;

                DrawTextW(nmcd->hdc, szText, -1, &rcText, format);

                // Skip default drawing entirely - we drew everything
                return CDRF_SKIPDEFAULT;
            }
            }
        }
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, HeaderParentSubclassProc, uIdSubclass);
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// TreeView parent subclass to handle NM_CUSTOMDRAW for custom selection colors
static LRESULT CALLBACK TreeViewParentSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NOTIFY)
    {
        NMHDR *nmhdr = (NMHDR *) lParam;
        if (nmhdr->hwndFrom == (HWND) dwRefData && nmhdr->code == NM_CUSTOMDRAW)
        {
            NMTVCUSTOMDRAW *nmcd = (NMTVCUSTOMDRAW *) lParam;

            switch (nmcd->nmcd.dwDrawStage)
            {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;

            case CDDS_ITEMPREPAINT:
            {
                bool is_dark = g_darkModeEnabled;
                bool is_selected = (nmcd->nmcd.uItemState & CDIS_SELECTED) != 0;

                // Colors - always use normal background (no highlight fill)
                COLORREF bgColor = is_dark ? UIColorsWin::InputBackgroundDark() : UIColorsWin::InputBackgroundLight();
                COLORREF textColor = is_dark ? UIColorsWin::TextDark() : UIColorsWin::TextLight();
                COLORREF borderColor = is_dark ? UIColorsWin::SelectionBorderDark()
                                               : UIColorsWin::SelectionBorderLight();

                // Fill entire row background
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(nmcd->nmcd.hdc, &nmcd->nmcd.rc, hBrush);
                DeleteObject(hBrush);

                // Get item text
                HWND hTree = (HWND) dwRefData;
                HTREEITEM hItem = (HTREEITEM) nmcd->nmcd.dwItemSpec;
                wchar_t szText[256] = {0};
                TVITEMW tvi = {0};
                tvi.mask = TVIF_TEXT | TVIF_HANDLE;
                tvi.hItem = hItem;
                tvi.pszText = szText;
                tvi.cchTextMax = 256;
                TreeView_GetItem(hTree, &tvi);

                // Measure text width
                SIZE textSize = {0};
                GetTextExtentPoint32W(nmcd->nmcd.hdc, szText, (int) wcslen(szText), &textSize);

                // Calculate text rect with padding
                int padding = 4;
                RECT rcText = nmcd->nmcd.rc;
                rcText.left += padding;
                int textRight = rcText.left + textSize.cx + padding;
                if (textRight > nmcd->nmcd.rc.right)
                    textRight = nmcd->nmcd.rc.right;

                // Draw 1px border around text area only if selected
                if (is_selected)
                {
                    RECT rcBorder = nmcd->nmcd.rc;
                    rcBorder.left += 1;
                    rcBorder.right = textRight + padding;

                    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
                    HPEN hOldPen = (HPEN) SelectObject(nmcd->nmcd.hdc, hPen);
                    HBRUSH hOldBrush = (HBRUSH) SelectObject(nmcd->nmcd.hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(nmcd->nmcd.hdc, rcBorder.left, rcBorder.top, rcBorder.right, rcBorder.bottom);
                    SelectObject(nmcd->nmcd.hdc, hOldBrush);
                    SelectObject(nmcd->nmcd.hdc, hOldPen);
                    DeleteObject(hPen);
                }

                // Draw text
                SetTextColor(nmcd->nmcd.hdc, textColor);
                SetBkMode(nmcd->nmcd.hdc, TRANSPARENT);

                rcText.right -= 2;
                DrawTextW(nmcd->nmcd.hdc, szText, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                return CDRF_SKIPDEFAULT;
            }
            }
        }
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, TreeViewParentSubclassProc, uIdSubclass);
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Callback for EnumChildWindows to apply dark theme to child controls
static BOOL CALLBACK ApplyDarkThemeToChildProc(HWND hwnd, LPARAM lParam)
{
    (void) lParam;

    if (!hwnd)
        return TRUE;

    // Get the window class name
    wchar_t className[256] = {0};
    GetClassNameW(hwnd, className, _countof(className));

    // Apply theme to header controls
    if (wcscmp(className, L"SysHeader32") == 0 || wcscmp(className, WC_HEADERW) == 0)
    {
        // For headers, disable visual styles so our custom drawing works
        if (g_allowDarkModeForWindow)
        {
            g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
        }

        // Disable visual styles for header
        SetWindowTheme(hwnd, L"", L"");

        // Subclass header for background
        SetWindowSubclass(hwnd, HeaderSubclassProc, 0, 0);

        // Subclass parent to handle NM_CUSTOMDRAW - we draw items ourselves with CDRF_SKIPDEFAULT
        HWND parent = GetParent(hwnd);
        if (parent)
        {
            SetWindowSubclass(parent, HeaderParentSubclassProc, (UINT_PTR) hwnd, (DWORD_PTR) hwnd);
        }

        // Force redraw
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    else if (wcscmp(className, L"SysListView32") == 0)
    {
        if (g_allowDarkModeForWindow)
        {
            g_allowDarkModeForWindow(hwnd, true);
        }

        SetWindowTheme(hwnd, L"DarkMode_ItemsView", nullptr);

        // Set list view colors based on current palette
        COLORREF listBg = g_darkModeEnabled ? UIColorsWin::InputBackgroundDark() : UIColorsWin::InputBackgroundLight();
        COLORREF listText = g_darkModeEnabled ? UIColorsWin::TextDark() : UIColorsWin::TextLight();

        ListView_SetBkColor(hwnd, listBg);
        ListView_SetTextBkColor(hwnd, listBg);
        ListView_SetTextColor(hwnd, listText);

        // Force redraw
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    return TRUE;
}

void SetDarkThemeForDataViewCtrl(HWND hwnd)
{
    if (!hwnd || !g_darkModeSupported)
        return;

    // Allow dark mode for the main window
    if (g_allowDarkModeForWindow)
    {
        g_allowDarkModeForWindow(hwnd, true); // Always allow
    }

    // UNIFIED THEMING: Always use DarkMode_Explorer which respects custom colors
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);

    // Apply dark theme to child controls (header, etc.)
    EnumChildWindows(hwnd, ApplyDarkThemeToChildProc, 0);

    // Force redraw
    InvalidateRect(hwnd, nullptr, TRUE);
}

void SetDarkThemeForTreeCtrl(HWND hwnd)
{
    if (!hwnd)
        return;

    // Allow dark mode for the tree control
    if (g_allowDarkModeForWindow)
    {
        g_allowDarkModeForWindow(hwnd, true); // Always allow
    }

    // Apply theme based on current mode
    // Dark mode: DarkMode_Explorer for dark scrollbars
    // Light mode: Disable theming for classic light scrollbars (items are owner-drawn anyway)
    if (g_darkModeEnabled)
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    else
        SetWindowTheme(hwnd, L"", L""); // Classic theme for light scrollbars

    // Set tree view colors based on current theme
    COLORREF bgColor = g_darkModeEnabled ? UIColorsWin::InputBackgroundDark() : UIColorsWin::InputBackgroundLight();
    COLORREF textColor = g_darkModeEnabled ? UIColorsWin::TextDark() : UIColorsWin::TextLight();

    TreeView_SetBkColor(hwnd, bgColor);
    TreeView_SetTextColor(hwnd, textColor);

    // Subclass parent to handle NM_CUSTOMDRAW for custom selection colors
    // Note: SetWindowSubclass is safe to call multiple times - it updates existing subclass
    HWND parent = GetParent(hwnd);
    if (parent)
    {
        SetWindowSubclass(parent, TreeViewParentSubclassProc, (UINT_PTR) hwnd, (DWORD_PTR) hwnd);
    }

    // Force complete redraw
    RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void PrepareForCommonDialog()
{
    // No-op: Common dialogs (Open/Save) on Windows 11 follow the Windows system
    // theme setting, not the app's SetPreferredAppMode. This is a known Windows
    // limitation - there's no documented way to override it per-dialog.
    // Keeping function for API compatibility.
}

void RestoreAfterCommonDialog()
{
    // No-op: See PrepareForCommonDialog comment.
    // Keeping function for API compatibility.
}

} // namespace NppDarkMode

#endif // _WIN32
