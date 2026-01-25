///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "DarkMode.hpp"

#ifdef _WIN32

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <commctrl.h>

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
    if (!InitDarkModeApis())
        return;

    AllowDarkModeForApp();

    if (g_refreshImmersiveColorPolicyState)
    {
        g_refreshImmersiveColorPolicyState();
    }

    g_darkModeEnabled = darkMode;
}

void SetDarkMode(bool darkMode)
{
    g_darkModeEnabled = darkMode;

    if (!g_darkModeSupported)
        return;

    if (g_setPreferredAppMode)
    {
        g_setPreferredAppMode(darkMode ? ForceDark : ForceLight);
    }

    if (g_refreshImmersiveColorPolicyState)
    {
        g_refreshImmersiveColorPolicyState();
    }

    if (g_flushMenuThemes)
    {
        g_flushMenuThemes();
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
}

void AllowDarkModeForWindow(HWND hwnd)
{
    if (!hwnd || !g_darkModeSupported)
        return;

    if (g_allowDarkModeForWindow)
    {
        g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
    }
}

void SetDarkExplorerTheme(HWND hwnd)
{
    if (!hwnd)
        return;

    if (g_darkModeEnabled)
    {
        // Apply dark explorer theme for scrollbars and controls
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }
    else
    {
        // Reset to default theme
        SetWindowTheme(hwnd, L"Explorer", nullptr);
    }

    // Also allow dark mode for this specific window
    if (g_allowDarkModeForWindow)
    {
        g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
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
    return g_darkModeEnabled ? RGB(60, 60, 60) : GetSysColor(COLOR_3DFACE);
}

COLORREF GetBackgroundColor()
{
    return g_darkModeEnabled ? RGB(43, 43, 43) : GetSysColor(COLOR_WINDOW);
}

COLORREF GetTextColor()
{
    return g_darkModeEnabled ? RGB(250, 250, 250) : GetSysColor(COLOR_WINDOWTEXT);
}

// Dark menu colors
static COLORREF g_menuBgColor = RGB(0x2B, 0x2B, 0x2B);
static COLORREF g_menuHotBgColor = RGB(0x40, 0x40, 0x40);
static COLORREF g_menuTextColor = RGB(0xF0, 0xF0, 0xF0);
static COLORREF g_menuDisabledTextColor = RGB(0x80, 0x80, 0x80);

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
                HBRUSH hBrush = CreateSolidBrush(g_menuBgColor);
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
            COLORREF bgColor = g_menuBgColor;
            COLORREF textColor = g_menuTextColor;

            bool isHot = (dis.itemState & ODS_HOTLIGHT) != 0;
            bool isSelected = (dis.itemState & ODS_SELECTED) != 0;
            bool isDisabled = (dis.itemState & (ODS_INACTIVE | ODS_DISABLED | ODS_GRAYED)) != 0;

            if (isHot || isSelected)
            {
                bgColor = g_menuHotBgColor;
            }
            if (isDisabled)
            {
                textColor = g_menuDisabledTextColor;
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
                    HBRUSH hBrush = CreateSolidBrush(g_menuBgColor);
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

// Callback for EnumChildWindows to apply dark theme to child controls
static BOOL CALLBACK ApplyDarkThemeToChildProc(HWND hwnd, LPARAM lParam)
{
    (void) lParam;

    if (!hwnd)
        return TRUE;

    // Get the window class name
    wchar_t className[256] = {0};
    GetClassNameW(hwnd, className, _countof(className));

    // Apply dark theme to header controls and list views
    if (wcscmp(className, L"SysHeader32") == 0 || wcscmp(className, L"SysListView32") == 0 ||
        wcscmp(className, WC_HEADERW) == 0)
    {
        if (g_allowDarkModeForWindow)
        {
            g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
        }

        if (g_darkModeEnabled)
        {
            SetWindowTheme(hwnd, L"ItemsView", nullptr);
        }
        else
        {
            SetWindowTheme(hwnd, nullptr, nullptr);
        }

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
        g_allowDarkModeForWindow(hwnd, g_darkModeEnabled);
    }

    // Apply dark explorer theme to the main control
    if (g_darkModeEnabled)
    {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }
    else
    {
        SetWindowTheme(hwnd, L"Explorer", nullptr);
    }

    // Apply dark theme to child controls (header, etc.)
    EnumChildWindows(hwnd, ApplyDarkThemeToChildProc, 0);

    // Force redraw
    InvalidateRect(hwnd, nullptr, TRUE);
}

} // namespace NppDarkMode

#endif // _WIN32
