///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_DarkMode_hpp_
#define slic3r_DarkMode_hpp_

#ifdef _WIN32

#include <windows.h>

namespace NppDarkMode
{

// Initialize dark mode support. Call once at application startup.
// darkMode: whether to enable dark mode
// fixDarkScrollbar: whether to fix scrollbar theming (recommended true)
void InitDarkMode(bool darkMode, bool fixDarkScrollbar);

// Enable or disable dark mode globally
void SetDarkMode(bool darkMode);

// Check if dark mode is currently enabled
bool IsDarkModeEnabled();

// Apply dark title bar to a window
void SetDarkTitleBar(HWND hwnd);

// Allow dark mode for a specific window (call before showing the window)
void AllowDarkModeForWindow(HWND hwnd);

// Apply dark explorer theme to a window (for scrollbars, tree views, etc.)
void SetDarkExplorerTheme(HWND hwnd);

// Enable/disable dark mode for the application's system menu
void SetSystemMenuForApp(bool enabled);

// Get a softer dark background color (for disabled controls, etc.)
COLORREF GetSofterBackgroundColor();

// Get the standard dark background color
COLORREF GetBackgroundColor();

// Get the dark mode text color
COLORREF GetTextColor();

// Refresh title bar after theme change
void RefreshTitleBarThemeColor(HWND hwnd);

// Enable dark menu bar for a window (subclasses window to handle UAH messages)
void EnableDarkMenuForWindow(HWND hwnd);

// Disable dark menu bar for a window
void DisableDarkMenuForWindow(HWND hwnd);

// Allow dark mode for the application (call before creating windows)
void AllowDarkModeForApp();

// Apply dark mode theme to a DataViewCtrl and its header control
void SetDarkThemeForDataViewCtrl(HWND hwnd);

// Apply dark mode theme to a TreeCtrl with custom selection colors
void SetDarkThemeForTreeCtrl(HWND hwnd);

// Note: Common dialogs (Open/Save) on Windows 11 follow the Windows system theme,
// not the app's SetPreferredAppMode setting. These functions are no-ops but kept
// for API compatibility in case Windows adds per-dialog theme control in the future.
void PrepareForCommonDialog();
void RestoreAfterCommonDialog();

// RAII helper for common dialog theming (currently no-op, see above)
class CommonDialogScope
{
public:
    CommonDialogScope() { PrepareForCommonDialog(); }
    ~CommonDialogScope() { RestoreAfterCommonDialog(); }

    // Non-copyable
    CommonDialogScope(const CommonDialogScope &) = delete;
    CommonDialogScope &operator=(const CommonDialogScope &) = delete;
};

} // namespace NppDarkMode

#endif // _WIN32

#endif // slic3r_DarkMode_hpp_
