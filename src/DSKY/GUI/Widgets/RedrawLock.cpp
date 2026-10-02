///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "RedrawLock.hpp"

#include <unordered_map>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace DSKY
{

#ifdef _WIN32
// The locks held per window, so nested locks redraw once, when the outermost ends
static std::unordered_map<WXWidget, int> &lock_counts()
{
    static std::unordered_map<WXWidget, int> counts;
    return counts;
}
#endif

RedrawLock::RedrawLock(wxWindow *window) : m_window(window)
{
    if (window == nullptr)
        return;
#ifdef _WIN32
    if (!window->IsShownOnScreen())
        return;
    m_handle = window->GetHandle();
    if (lock_counts()[m_handle]++ == 0)
        ::SendMessage(HWND(m_handle), WM_SETREDRAW, FALSE, 0);
#else
    window->Freeze();
#endif
    m_locked = true;
}

RedrawLock::~RedrawLock()
{
    if (!m_locked)
        return;
#ifdef _WIN32
    auto it = lock_counts().find(m_handle);
    if (it == lock_counts().end() || --it->second > 0)
        return;
    lock_counts().erase(it);
    // A window destroyed during the lock took its handle with it
    if (!m_window)
        return;
    const HWND hwnd = HWND(m_handle);
    ::SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
    // Turning drawing back on makes the window visible: one hidden during the lock stays hidden
    if (!m_window->IsShown())
        ::ShowWindow(hwnd, SW_HIDE);
    else
        ::RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
#else
    if (m_window)
        m_window->Thaw();
#endif
}

} // namespace DSKY
