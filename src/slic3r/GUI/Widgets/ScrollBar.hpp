///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_ScrollBar_hpp_
#define slic3r_GUI_ScrollBar_hpp_

#include <wx/panel.h>

// Custom vertical scrollbar widget with preFlight warm theme colors
// Replaces native Windows scrollbars for consistent dark mode appearance
class ScrollBar : public wxPanel
{
public:
    ScrollBar(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition,
              const wxSize &size = wxDefaultSize);

    // Set scroll parameters (like wxScrollBar)
    // position: current scroll position
    // thumbSize: size of visible area (determines thumb size)
    // range: total scrollable range
    // pageSize: amount to scroll on page up/down
    void SetScrollbar(int position, int thumbSize, int range, int pageSize);

    int GetThumbPosition() const { return m_position; }
    void SetThumbPosition(int position);

    int GetThumbSize() const { return m_thumbSize; }
    int GetRange() const { return m_range; }
    int GetPageSize() const { return m_pageSize; }

    void sys_color_changed() { Refresh(); }
    void msw_rescale();

    static int GetScaledScrollbarWidth();

private:
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);
    void OnMouseWheel(wxMouseEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent &event);

    int PositionFromY(int y) const;
    int YFromPosition() const;
    int ThumbPixelSize() const;
    wxRect GetThumbRect() const;
    wxRect GetTrackRect() const;

    void NotifyScroll(wxEventType eventType);

    int m_position;  // Current scroll position
    int m_thumbSize; // Size of visible area
    int m_range;     // Total scrollable range
    int m_pageSize;  // Page scroll amount

    bool m_dragging;
    int m_dragStartY;
    int m_dragStartPos;

    // DPI scaling - these are now methods instead of constants
    static int GetScaledMinThumbSize();
    static int GetScaledCornerRadius();
    static int GetScaledInset();

    wxDECLARE_EVENT_TABLE();
};

#endif // !slic3r_GUI_ScrollBar_hpp_
