///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/weakref.h>
#include <wx/window.h>

namespace DSKY
{

// Stops a window and everything in it from drawing while its content changes, and repaints it
// once when the last lock on it ends. On Windows that is one WM_SETREDRAW on the window alone,
// where wxWindow::Freeze visits every descendant and invalidates each shown one on the thaw
// (thousands of windows in the sidebar); a window that is not on screen is not locked at all.
// Elsewhere it is wx's freeze.
class RedrawLock
{
public:
    explicit RedrawLock(wxWindow *window);
    ~RedrawLock();
    RedrawLock(const RedrawLock &) = delete;
    RedrawLock &operator=(const RedrawLock &) = delete;

private:
    wxWeakRef<wxWindow> m_window;
#ifdef _WIN32
    WXWidget m_handle{nullptr};
#endif
    bool m_locked{false};
};

} // namespace DSKY
