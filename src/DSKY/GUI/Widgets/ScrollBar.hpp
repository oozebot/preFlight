///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/panel.h>

// Custom scrollbar widget with preFlight warm theme colors
// Replaces native Windows scrollbars for consistent dark mode appearance
// Supports both vertical (default) and horizontal orientation
class ScrollBar : public wxPanel
{
public:
    ScrollBar(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition,
              const wxSize &size = wxDefaultSize, int orientation = wxVERTICAL);

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

    // Override the default background color (which comes from UIColors)
    void SetTrackColour(const wxColour &colour)
    {
        m_trackColour = colour;
        Refresh();
    }

    void sys_color_changed() { Refresh(); }
    void msw_rescale();

    static int GetScaledScrollbarWidth();

private:
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);
    void OnMouseWheel(wxMouseEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent &event);

    int PositionFromCoord(int coord) const;
    int CoordFromPosition() const;
    int ThumbPixelSize() const;
    wxRect GetThumbRect() const;
    wxRect GetTrackRect() const;

    void NotifyScroll(wxEventType eventType);

    // Axis helpers: return the primary/secondary axis value depending on orientation
    int PrimaryCoord(const wxPoint &p) const { return m_orientation == wxVERTICAL ? p.y : p.x; }
    int PrimarySize(const wxSize &s) const { return m_orientation == wxVERTICAL ? s.y : s.x; }

    wxColour m_trackColour; // Optional override for track background (invalid = use UIColors default)
    int m_orientation;      // wxVERTICAL or wxHORIZONTAL
    int m_position;         // Current scroll position
    int m_thumbSize;        // Size of visible area
    int m_range;            // Total scrollable range
    int m_pageSize;         // Page scroll amount

    bool m_dragging;
    int m_dragStartCoord;
    int m_dragStartPos;
    int m_sumWheelRotation{0}; // Accumulator for partial wheel events (XWayland)

    // DPI scaling - these are now methods instead of constants
    static int GetScaledMinThumbSize();
    static int GetScaledCornerRadius();
    static int GetScaledInset();

    wxDECLARE_EVENT_TABLE();
};

