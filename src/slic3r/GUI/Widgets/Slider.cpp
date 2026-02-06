///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Slider.hpp"
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <algorithm>
#include "../GUI_App.hpp"

// Brand colors - #EAA032 (RGB: 234, 160, 50)
const wxColour Slider::BRAND_COLOR(234, 160, 50);
const wxColour Slider::BRAND_COLOR_DARK(200, 140, 40);

// DPI scaling helper functions
static int GetScaledTrackHeight()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 4) / 10; // 4px at 100%
}

static int GetScaledThumbWidth()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 12) / 10; // 12px at 100%
}

static int GetScaledThumbHeight()
{
    return Slic3r::GUI::wxGetApp().em_unit() * 2; // 20px at 100%
}

static int GetScaledCornerRadius()
{
    return Slic3r::GUI::wxGetApp().em_unit() / 5; // 2px at 100%
}

static int GetScaledPenWidth()
{
    return std::max(1, Slic3r::GUI::wxGetApp().em_unit() / 10); // 1px at 100%, min 1px
}

wxBEGIN_EVENT_TABLE(Slider, wxPanel) EVT_PAINT(Slider::OnPaint) EVT_LEFT_DOWN(Slider::OnMouse)
    EVT_LEFT_UP(Slider::OnMouse) EVT_MOTION(Slider::OnMouse) EVT_MOUSEWHEEL(Slider::OnMouseWheel)
        EVT_SIZE(Slider::OnSize) wxEND_EVENT_TABLE()

            Slider::Slider(wxWindow *parent, wxWindowID id, int value, int minValue, int maxValue, const wxPoint &pos,
                           const wxSize &size)
    : wxPanel(parent, id, pos, size, wxFULL_REPAINT_ON_RESIZE)
    , m_value(value)
    , m_min(minValue)
    , m_max(maxValue)
    , m_dragging(false)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    int em = Slic3r::GUI::wxGetApp().em_unit();
    SetMinSize(wxSize(em * 10, em * 2)); // 100x20 at 100%
}

void Slider::SetValue(int value)
{
    value = std::max(m_min, std::min(m_max, value));
    if (m_value != value)
    {
        m_value = value;
        Refresh();
    }
}

void Slider::SetRange(int minValue, int maxValue)
{
    m_min = minValue;
    m_max = maxValue;
    m_value = std::max(m_min, std::min(m_max, m_value));
    Refresh();
}

void Slider::SetToolTip(const wxString &tip)
{
    wxPanel::SetToolTip(tip);
}

void Slider::OnPaint(wxPaintEvent &event)
{
    wxAutoBufferedPaintDC dc(this);

    // Use window background color for proper dark/light theme support
    wxColour bgColor = Slic3r::GUI::wxGetApp().get_window_default_clr();
    dc.SetBackground(wxBrush(bgColor));
    dc.Clear();

    const wxSize size = GetClientSize();
    const int trackHeight = GetScaledTrackHeight();
    const int thumbWidth = GetScaledThumbWidth();
    const int thumbHeight = GetScaledThumbHeight();
    const int trackY = (size.y - trackHeight) / 2;

    // Track color - darker than background for visibility in both themes
    wxColour trackColor = bgColor.ChangeLightness(Slic3r::GUI::wxGetApp().dark_mode() ? 150 : 85);

    // Draw track
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(trackColor));
    dc.DrawRectangle(0, trackY, size.x, trackHeight);

    // Draw filled portion (from start to thumb)
    int thumbX = PositionFromValue();
    dc.SetBrush(wxBrush(BRAND_COLOR_DARK));
    dc.DrawRectangle(0, trackY, thumbX + thumbWidth / 2, trackHeight);

    // Draw thumb
    wxRect thumbRect = GetThumbRect();
    dc.SetBrush(wxBrush(BRAND_COLOR));
    dc.SetPen(wxPen(BRAND_COLOR_DARK, GetScaledPenWidth()));
    dc.DrawRoundedRectangle(thumbRect, GetScaledCornerRadius());
}

void Slider::OnMouse(wxMouseEvent &event)
{
    if (event.LeftDown())
    {
        wxRect thumbRect = GetThumbRect();
        if (thumbRect.Contains(event.GetPosition()))
        {
            m_dragging = true;
            CaptureMouse();
        }
        else
        {
            // Click on track - jump to position
            int newValue = ValueFromPosition(event.GetX());
            SetValue(newValue);
            NotifyValueChanged();
        }
    }
    else if (event.LeftUp())
    {
        if (m_dragging)
        {
            m_dragging = false;
            if (HasCapture())
                ReleaseMouse();
            NotifyValueChanged();
        }
    }
    else if (event.Dragging() && m_dragging)
    {
        int newValue = ValueFromPosition(event.GetX());
        SetValue(newValue);
        // Fire wxEVT_SLIDER during dragging so tooltip updates in real-time
        wxCommandEvent sliderEvent(wxEVT_SLIDER, GetId());
        sliderEvent.SetEventObject(this);
        sliderEvent.SetInt(m_value);
        ProcessWindowEvent(sliderEvent);
    }
}

void Slider::OnMouseWheel(wxMouseEvent &event)
{
    // Limit scroll speed - only move by 1 step regardless of wheel speed
    int delta = event.GetWheelRotation() > 0 ? 1 : -1;

    // Only process if we're not at the bounds
    if ((delta > 0 && m_value < m_max) || (delta < 0 && m_value > m_min))
    {
        int newValue = m_value + delta;
        SetValue(newValue);
        NotifyValueChanged(); // Fire THUMBRELEASE event for wheel
    }
}

void Slider::OnSize(wxSizeEvent &event)
{
    Refresh();
    event.Skip();
}

int Slider::ValueFromPosition(int x) const
{
    const int size = GetClientSize().x;
    const int thumbWidth = GetScaledThumbWidth();
    const int usableWidth = size - thumbWidth;

    if (usableWidth <= 0)
        return m_min;

    x = std::max(thumbWidth / 2, std::min(x, size - thumbWidth / 2));
    float ratio = float(x - thumbWidth / 2) / float(usableWidth);
    return m_min + int(ratio * (m_max - m_min) + 0.5f);
}

int Slider::PositionFromValue() const
{
    const int size = GetClientSize().x;
    const int thumbWidth = GetScaledThumbWidth();
    const int usableWidth = size - thumbWidth;

    if (m_max == m_min)
        return thumbWidth / 2;

    float ratio = float(m_value - m_min) / float(m_max - m_min);
    return thumbWidth / 2 + int(ratio * usableWidth);
}

wxRect Slider::GetThumbRect() const
{
    const int thumbWidth = GetScaledThumbWidth();
    const int thumbHeight = GetScaledThumbHeight();
    const int thumbX = PositionFromValue() - thumbWidth / 2;
    const int thumbY = (GetClientSize().y - thumbHeight) / 2;

    return wxRect(thumbX, thumbY, thumbWidth, thumbHeight);
}

void Slider::NotifyValueChanged()
{
    wxScrollEvent event(wxEVT_SCROLL_THUMBRELEASE, GetId());
    event.SetEventObject(this);
    event.SetPosition(m_value);
    ProcessWindowEvent(event);
}

void Slider::msw_rescale()
{
    int em = Slic3r::GUI::wxGetApp().em_unit();
    SetMinSize(wxSize(em * 10, em * 2)); // 100x20 at 100%
    Refresh();
}
