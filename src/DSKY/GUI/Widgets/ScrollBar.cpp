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
    return DSKY::wxGetApp().em_unit() * 2; // 20px at 100%
}

int ScrollBar::GetScaledScrollbarWidth()
{
    return static_cast<int>(DSKY::wxGetApp().em_unit() * 1.2); // 12px at 100%
}

int ScrollBar::GetScaledCornerRadius()
{
    return DSKY::wxGetApp().em_unit() / 3; // 3px at 100%
}

int ScrollBar::GetScaledInset()
{
    return DSKY::wxGetApp().em_unit() / 5; // 2px at 100%
}

wxBEGIN_EVENT_TABLE(ScrollBar, wxPanel) EVT_PAINT(ScrollBar::OnPaint) EVT_LEFT_DOWN(ScrollBar::OnMouse)
    EVT_LEFT_UP(ScrollBar::OnMouse) EVT_MOTION(ScrollBar::OnMouse) EVT_MOUSEWHEEL(ScrollBar::OnMouseWheel)
        EVT_SIZE(ScrollBar::OnSize) EVT_MOUSE_CAPTURE_LOST(ScrollBar::OnMouseCaptureLost) wxEND_EVENT_TABLE()

            ScrollBar::ScrollBar(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size,
                                 int orientation)
    : wxPanel(parent, id, pos, size, wxFULL_REPAINT_ON_RESIZE)
    , m_orientation(orientation)
    , m_position(0)
    , m_thumbSize(1)
    , m_range(1)
    , m_pageSize(1)
    , m_dragging(false)
    , m_dragStartCoord(0)
    , m_dragStartPos(0)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    int width = GetScaledScrollbarWidth();
    int minExtent = DSKY::wxGetApp().em_unit() * 5; // 50px at 100%
    if (m_orientation == wxVERTICAL)
    {
        SetMinSize(wxSize(width, minExtent));
        SetMaxSize(wxSize(width, -1));
    }
    else
    {
        SetMinSize(wxSize(minExtent, width));
        SetMaxSize(wxSize(-1, width));
    }
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
    bool is_dark = DSKY::wxGetApp().dark_mode();

    // Background - use per-instance override if set, otherwise default from UIColors
    wxColour bgColor = m_trackColour.IsOk() ? m_trackColour
                       : is_dark            ? UIColors::InputBackgroundDark()
                                            : UIColors::InputBackgroundLight();
    dc.SetBackground(wxBrush(bgColor));
    dc.Clear();

    const wxSize size = GetClientSize();

    // Don't draw if there's nothing to scroll
    if (m_range <= m_thumbSize)
        return;

    // No track - just draw the thumb directly on the background for cleaner look
    dc.SetPen(*wxTRANSPARENT_PEN);

    // Themed thumb: a visible accent-tinted mid-tone at rest (the accent blended ~50% over the track it is
    // drawn on, so it reads as subtly themed without being a glowing bar), full accent on hover/drag.
    const wxColour accent = UIColors::AccentPrimary();
    auto blend = [](unsigned char from, unsigned char to, double t) -> unsigned char
    {
        return (unsigned char) std::max(0, std::min(255, int(from + (to - from) * t + 0.5)));
    };
    const double rest_t = 0.5; // 0 = track bg, 1 = full accent
    wxColour thumbColor(blend(bgColor.Red(), accent.Red(), rest_t), blend(bgColor.Green(), accent.Green(), rest_t),
                        blend(bgColor.Blue(), accent.Blue(), rest_t));
    wxColour thumbHoverColor = accent;

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
            m_dragStartCoord = PrimaryCoord(event.GetPosition());
            m_dragStartPos = m_position;
            CaptureMouse();
        }
        else
        {
            // Click on track - page up/down
            wxRect trackRect = GetTrackRect();
            if (trackRect.Contains(event.GetPosition()))
            {
                int clickCoord = PrimaryCoord(event.GetPosition());
                int thumbCoord = CoordFromPosition();

                if (clickCoord < thumbCoord)
                    SetThumbPosition(m_position - m_pageSize);
                else
                    SetThumbPosition(m_position + m_pageSize);
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
        int deltaCoord = PrimaryCoord(event.GetPosition()) - m_dragStartCoord;
        int trackExtent = PrimarySize(GetTrackRect().GetSize()) - ThumbPixelSize();

        if (trackExtent > 0)
        {
            int scrollRange = m_range - m_thumbSize;
            int deltaPos = (deltaCoord * scrollRange) / trackExtent;
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

    // Accumulate partial wheel rotations for XWayland compatibility (credit: topisani)
    int delta = event.GetWheelDelta();
    if (delta == 0)
        return;
    m_sumWheelRotation += event.GetWheelRotation() * 3;
    int scrollAmount = m_sumWheelRotation / delta;
    m_sumWheelRotation -= scrollAmount * delta;
    if (scrollAmount == 0)
        return;

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

int ScrollBar::PositionFromCoord(int coord) const
{
    wxRect trackRect = GetTrackRect();
    int thumbSize = ThumbPixelSize();
    int usableExtent = PrimarySize(trackRect.GetSize()) - thumbSize;

    if (usableExtent <= 0)
        return 0;

    int origin = (m_orientation == wxVERTICAL) ? trackRect.GetTop() : trackRect.GetLeft();
    int relative = coord - origin - thumbSize / 2;
    relative = std::max(0, std::min(relative, usableExtent));

    int scrollRange = m_range - m_thumbSize;
    return (relative * scrollRange) / usableExtent;
}

int ScrollBar::CoordFromPosition() const
{
    wxRect trackRect = GetTrackRect();
    int thumbSize = ThumbPixelSize();
    int usableExtent = PrimarySize(trackRect.GetSize()) - thumbSize;

    if (usableExtent <= 0 || m_range <= m_thumbSize)
        return (m_orientation == wxVERTICAL) ? trackRect.GetTop() : trackRect.GetLeft();

    int scrollRange = m_range - m_thumbSize;
    int offset = (m_position * usableExtent) / scrollRange;
    return ((m_orientation == wxVERTICAL) ? trackRect.GetTop() : trackRect.GetLeft()) + offset;
}

int ScrollBar::ThumbPixelSize() const
{
    wxRect trackRect = GetTrackRect();
    int trackExtent = PrimarySize(trackRect.GetSize());

    if (m_range <= 0)
        return trackExtent;

    int thumbSize = (m_thumbSize * trackExtent) / m_range;
    return std::max(GetScaledMinThumbSize(), thumbSize);
}

wxRect ScrollBar::GetThumbRect() const
{
    wxRect trackRect = GetTrackRect();
    int thumbExtent = ThumbPixelSize();
    int thumbCoord = CoordFromPosition();
    int inset = GetScaledInset();

    if (m_orientation == wxVERTICAL)
        return wxRect(trackRect.GetLeft() + inset, thumbCoord, trackRect.GetWidth() - inset * 2, thumbExtent);
    else
        return wxRect(thumbCoord, trackRect.GetTop() + inset, thumbExtent, trackRect.GetHeight() - inset * 2);
}

wxRect ScrollBar::GetTrackRect() const
{
    const wxSize size = GetClientSize();
    int margin = GetScaledInset();
    return wxRect(margin, margin, size.x - margin * 2, size.y - margin * 2);
}

void ScrollBar::NotifyScroll(wxEventType eventType)
{
    wxScrollEvent event(eventType, GetId());
    event.SetEventObject(this);
    event.SetPosition(m_position);
    event.SetOrientation(m_orientation);
    ProcessWindowEvent(event);
}

void ScrollBar::msw_rescale()
{
    int width = GetScaledScrollbarWidth();
    int minExtent = DSKY::wxGetApp().em_unit() * 5; // 50px at 100%
    if (m_orientation == wxVERTICAL)
    {
        SetMinSize(wxSize(width, minExtent));
        SetMaxSize(wxSize(width, -1));
    }
    else
    {
        SetMinSize(wxSize(minExtent, width));
        SetMaxSize(wxSize(-1, width));
    }
    Refresh();
}
