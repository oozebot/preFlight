///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_ScrollablePanel_hpp_
#define slic3r_GUI_ScrollablePanel_hpp_

#include <wx/panel.h>
#include <wx/sizer.h>

class ScrollBar;

// A scrollable panel that uses the custom ScrollBar widget instead of native scrollbars.
// Provides consistent dark mode appearance matching preFlight's warm theme.
//
// Architecture:
// - ScrollablePanel contains a content panel and a custom scrollbar
// - The content panel can be taller than the visible area
// - Scrolling works by repositioning the content panel (negative Y offset)
class ScrollablePanel : public wxPanel
{
public:
    ScrollablePanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition,
                    const wxSize &size = wxDefaultSize, long style = 0);

    // Get the content panel where child controls should be added
    wxPanel *GetContentPanel() { return m_content; }

    // Set the sizer for the content panel
    void SetContentSizer(wxSizer *sizer);

    // Compatibility methods for wxScrolledWindow replacement
    wxSizer *GetSizer() const { return m_content ? m_content->GetSizer() : nullptr; }
    void SetSizer(wxSizer *sizer) { SetContentSizer(sizer); }
    void FitInside() { UpdateScrollbar(); }
    void SetScrollRate(int, int) {} // No-op for compatibility

    // Scroll to a specific position
    void ScrollToPosition(int position);

    // Scroll to show a specific child window
    void ScrollToChild(wxWindow *child);

    // Update scrollbar after content changes
    void UpdateScrollbar();

    // Theme change handler
    void sys_color_changed();

    // DPI change handler
    void msw_rescale();

    // Get scroll position
    int GetScrollPosition() const;

private:
    void OnSize(wxSizeEvent &event);
    void OnScroll(wxScrollEvent &event);
    void OnMouseWheel(wxMouseEvent &event);

    wxPanel *m_content;     // The content panel (can be taller than visible)
    ScrollBar *m_scrollbar; // Custom scrollbar
    int m_scrollPosition;   // Current scroll position in pixels
    int m_contentHeight;    // Cached content height

    wxDECLARE_EVENT_TABLE();
};

#endif // !slic3r_GUI_ScrollablePanel_hpp_
