///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ScrollBar.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <algorithm>

// DPI scaling helper functions
int ScrollBar::GetScaledMinThumbSize()
{
    return Slic3r::GUI::wxGetApp().em_unit() * 2; // 20px at 100%
}

int ScrollBar::GetScaledScrollbarWidth()
{
    return static_cast<int>(Slic3r::GUI::wxGetApp().em_unit() * 1.2); // 12px at 100%
}

int ScrollBar::GetScaledCornerRadius()
{
    return Slic3r::GUI::wxGetApp().em_unit() / 3; // 3px at 100%
}

int ScrollBar::GetScaledInset()
{
    return Slic3r::GUI::wxGetApp().em_unit() / 5; // 2px at 100%
}

wxBEGIN_EVENT_TABLE(ScrollBar, wxPanel) EVT_PAINT(ScrollBar::OnPaint) EVT_LEFT_DOWN(ScrollBar::OnMouse)
    EVT_LEFT_UP(ScrollBar::OnMouse) EVT_MOTION(ScrollBar::OnMouse) EVT_MOUSEWHEEL(ScrollBar::OnMouseWheel)
        EVT_SIZE(ScrollBar::OnSize) EVT_MOUSE_CAPTURE_LOST(ScrollBar::OnMouseCaptureLost) wxEND_EVENT_TABLE()

            ScrollBar::ScrollBar(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxPanel(parent, id, pos, size, wxFULL_REPAINT_ON_RESIZE)
    , m_position(0)
    , m_thumbSize(1)
    , m_range(1)
    , m_pageSize(1)
    , m_dragging(false)
    , m_dragStartY(0)
    , m_dragStartPos(0)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    int width = GetScaledScrollbarWidth();
    int minHeight = Slic3r::GUI::wxGetApp().em_unit() * 5; // 50px at 100%
    SetMinSize(wxSize(width, minHeight));
    SetMaxSize(wxSize(width, -1));
}

void ScrollBar::SetScrollbar(int position, int thumbSize, int range, int pageSize)
{
    m_thumbSize = std::max(1, thumbSize);
    m_range = std::max(1, range);
    m_pageSize = std::max(1, pageSize);
    m_position = std::max(0, std::min(position, m_range - m_thumbSize));
    Refresh();
}

void ScrollBar::SetThumbPosition(int position)
{
    int newPos = std::max(0, std::min(position, m_range - m_thumbSize));
    if (newPos != m_position)
    {
        m_position = newPos;
        Refresh();
    }
}

void ScrollBar::OnPaint(wxPaintEvent &event)
{
    wxAutoBufferedPaintDC dc(this);
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();

    // Background - match sidebar panel background exactly
    wxColour bgColor = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
    dc.SetBackground(wxBrush(bgColor));
    dc.Clear();

    const wxSize size = GetClientSize();

    // Don't draw if there's nothing to scroll
    if (m_range <= m_thumbSize)
        return;

    // No track - just draw the thumb directly on the background for cleaner look
    dc.SetPen(*wxTRANSPARENT_PEN);

    // Thumb colors
    wxColour thumbColor = is_dark ? wxColour(80, 75, 68)       // Warm medium gray
                                  : wxColour(180, 175, 168);   // Medium warm gray
    wxColour thumbHoverColor = is_dark ? wxColour(100, 95, 85) // Lighter on hover
                                       : wxColour(160, 155, 148);

    // Check if mouse is over thumb for hover effect
    wxPoint mousePos = ScreenToClient(wxGetMousePosition());
    wxRect thumbRect = GetThumbRect();
    bool isHovering = thumbRect.Contains(mousePos);

    dc.SetBrush(wxBrush(isHovering || m_dragging ? thumbHoverColor : thumbColor));
    dc.DrawRoundedRectangle(thumbRect, GetScaledCornerRadius());
}

void ScrollBar::OnMouse(wxMouseEvent &event)
{
    if (m_range <= m_thumbSize)
    {
        event.Skip();
        return;
    }

    if (event.LeftDown())
    {
        wxRect thumbRect = GetThumbRect();
        if (thumbRect.Contains(event.GetPosition()))
        {
            // Start dragging thumb
            m_dragging = true;
            m_dragStartY = event.GetY();
            m_dragStartPos = m_position;
            CaptureMouse();
        }
        else
        {
            // Click on track - page up/down
            wxRect trackRect = GetTrackRect();
            if (trackRect.Contains(event.GetPosition()))
            {
                int clickY = event.GetY();
                int thumbY = YFromPosition();

                if (clickY < thumbY)
                {
                    // Page up
                    SetThumbPosition(m_position - m_pageSize);
                }
                else
                {
                    // Page down
                    SetThumbPosition(m_position + m_pageSize);
                }
                NotifyScroll(wxEVT_SCROLL_THUMBTRACK);
            }
        }
        Refresh();
    }
    else if (event.LeftUp())
    {
        if (m_dragging)
        {
            m_dragging = false;
            if (HasCapture())
                ReleaseMouse();
            NotifyScroll(wxEVT_SCROLL_THUMBRELEASE);
        }
        Refresh();
    }
    else if (event.Dragging() && m_dragging)
    {
        int deltaY = event.GetY() - m_dragStartY;
        int trackHeight = GetTrackRect().GetHeight() - ThumbPixelSize();

        if (trackHeight > 0)
        {
            int scrollRange = m_range - m_thumbSize;
            int deltaPos = (deltaY * scrollRange) / trackHeight;
            SetThumbPosition(m_dragStartPos + deltaPos);
            NotifyScroll(wxEVT_SCROLL_THUMBTRACK);
        }
    }
    else if (event.Moving())
    {
        // Refresh for hover effect
        Refresh();
    }
}

void ScrollBar::OnMouseWheel(wxMouseEvent &event)
{
    if (m_range <= m_thumbSize)
    {
        event.Skip();
        return;
    }

    int rotation = event.GetWheelRotation();
    int delta = event.GetWheelDelta();
    int lines = rotation / delta;

    // Scroll 3 lines per wheel notch
    int scrollAmount = lines * 3;
    SetThumbPosition(m_position - scrollAmount);
    NotifyScroll(wxEVT_SCROLL_THUMBTRACK);
}

void ScrollBar::OnSize(wxSizeEvent &event)
{
    Refresh();
    event.Skip();
}

void ScrollBar::OnMouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    m_dragging = false;
    Refresh();
}

int ScrollBar::PositionFromY(int y) const
{
    wxRect trackRect = GetTrackRect();
    int thumbSize = ThumbPixelSize();
    int usableHeight = trackRect.GetHeight() - thumbSize;

    if (usableHeight <= 0)
        return 0;

    int relativeY = y - trackRect.GetTop() - thumbSize / 2;
    relativeY = std::max(0, std::min(relativeY, usableHeight));

    int scrollRange = m_range - m_thumbSize;
    return (relativeY * scrollRange) / usableHeight;
}

int ScrollBar::YFromPosition() const
{
    wxRect trackRect = GetTrackRect();
    int thumbSize = ThumbPixelSize();
    int usableHeight = trackRect.GetHeight() - thumbSize;

    if (usableHeight <= 0 || m_range <= m_thumbSize)
        return trackRect.GetTop();

    int scrollRange = m_range - m_thumbSize;
    int offset = (m_position * usableHeight) / scrollRange;
    return trackRect.GetTop() + offset;
}

int ScrollBar::ThumbPixelSize() const
{
    wxRect trackRect = GetTrackRect();
    int trackHeight = trackRect.GetHeight();

    if (m_range <= 0)
        return trackHeight;

    int thumbSize = (m_thumbSize * trackHeight) / m_range;
    return std::max(GetScaledMinThumbSize(), thumbSize);
}

wxRect ScrollBar::GetThumbRect() const
{
    wxRect trackRect = GetTrackRect();
    int thumbHeight = ThumbPixelSize();
    int thumbY = YFromPosition();

    // Inset thumb slightly from track edges (scaled for DPI)
    int inset = GetScaledInset();
    return wxRect(trackRect.GetLeft() + inset, thumbY, trackRect.GetWidth() - inset * 2, thumbHeight);
}

wxRect ScrollBar::GetTrackRect() const
{
    const wxSize size = GetClientSize();
    int margin = GetScaledInset(); // Use same scaling as inset
    return wxRect(margin, margin, size.x - margin * 2, size.y - margin * 2);
}

void ScrollBar::NotifyScroll(wxEventType eventType)
{
    wxScrollEvent event(eventType, GetId());
    event.SetEventObject(this);
    event.SetPosition(m_position);
    event.SetOrientation(wxVERTICAL);
    ProcessWindowEvent(event);
}

void ScrollBar::msw_rescale()
{
    int width = GetScaledScrollbarWidth();
    int minHeight = Slic3r::GUI::wxGetApp().em_unit() * 5; // 50px at 100%
    SetMinSize(wxSize(width, minHeight));
    SetMaxSize(wxSize(width, -1));
    Refresh();
}
