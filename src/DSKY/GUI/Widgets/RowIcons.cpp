///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "RowIcons.hpp"

#include "../GUI_App.hpp"

#include <wx/dcclient.h>

namespace DSKY
{

RowIcons::RowIcons(wxWindow *parent, bool with_pin, bool with_lock, bool with_undo)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    m_slots[Pin].exists = with_pin;
    m_slots[Lock].exists = with_lock;
    m_slots[Undo].exists = with_undo;
    for (SlotState &slot : m_slots)
        slot.shown = slot.exists;
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &RowIcons::OnPaint, this);
    Bind(wxEVT_MOTION, &RowIcons::OnMotion, this);
    Bind(wxEVT_LEFT_DOWN, &RowIcons::OnLeftDown, this);
    Bind(wxEVT_LEAVE_WINDOW,
         [this](wxMouseEvent &evt)
         {
             m_hovered = SlotCount;
             evt.Skip();
         });
    UpdateSize();
}

void RowIcons::SetIcon(Slot slot, const wxBitmapBundle &icon)
{
    m_slots[slot].icon = icon;
    if (m_slots[slot].shown)
        Refresh();
}

void RowIcons::SetTip(Slot slot, const wxString &tip)
{
    m_slots[slot].tip = tip;
    if (m_hovered == slot)
        SetToolTip(tip);
}

void RowIcons::SetHandCursor(Slot slot, bool hand)
{
    m_slots[slot].hand = hand;
    if (m_hovered == slot)
        SetCursor(hand ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
}

void RowIcons::ShowSlot(Slot slot, bool show)
{
    show = show && m_slots[slot].exists;
    if (m_slots[slot].shown == show)
        return;
    m_slots[slot].shown = show;
    UpdateSize();
    Refresh();
}

void RowIcons::SetAccent(const wxColour &colour, int width)
{
    if (m_accent == colour && m_accent_width == width)
        return;
    const bool resized = (m_accent_width > 0) != (width > 0) || m_accent_width != width;
    m_accent = colour;
    m_accent_width = width;
    if (resized)
        UpdateSize();
    Refresh();
}

void RowIcons::SetLeadingMargin(bool leading)
{
    if (m_leading_margin == leading)
        return;
    m_leading_margin = leading;
    UpdateSize();
    Refresh();
}

void RowIcons::Rescale()
{
    UpdateSize();
    Refresh();
}

// The sizes a row's wxStaticBitmap icons had: a 1.6 em square, 0.2 em apart
int RowIcons::IconSize() const
{
    return int(1.6 * wxGetApp().em_unit());
}

int RowIcons::Margin() const
{
    return wxGetApp().em_unit() / 5;
}

int RowIcons::AccentSpace() const
{
    return m_accent_width;
}

void RowIcons::UpdateSize()
{
    int width = AccentSpace();
    bool first = true;
    for (const SlotState &slot : m_slots)
        if (slot.shown)
        {
            width += MarginBefore(first) + IconSize();
            first = false;
        }
    const wxSize size(width, IconSize());
    SetMinSize(size);
    SetSize(size);
    InvalidateBestSize();
}

RowIcons::Slot RowIcons::HitTest(const wxPoint &pt) const
{
    int x = AccentSpace();
    bool first = true;
    for (int i = 0; i < SlotCount; ++i)
    {
        if (!m_slots[i].shown)
            continue;
        x += MarginBefore(first);
        first = false;
        if (pt.x >= x && pt.x < x + IconSize())
            return Slot(i);
        x += IconSize();
    }
    return SlotCount;
}

void RowIcons::OnPaint(wxPaintEvent &)
{
    wxPaintDC dc(this);
    const wxSize size = GetClientSize();
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    if (m_accent_width > 0 && m_accent.IsOk())
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_accent));
        dc.DrawRectangle(0, 0, m_accent_width, size.GetHeight());
    }
    // Each icon where its own window stood: after the margin, centred in its square
    int x = AccentSpace();
    const int icon_size = IconSize();
    bool first = true;
    for (const SlotState &slot : m_slots)
    {
        if (!slot.shown)
            continue;
        x += MarginBefore(first);
        first = false;
        if (slot.icon.IsOk())
        {
            const wxBitmap bitmap = slot.icon.GetBitmapFor(this);
#ifdef __APPLE__
            const wxSize bmp_size = bitmap.GetLogicalSize();
#else
            const wxSize bmp_size = bitmap.GetSize();
#endif
            dc.DrawBitmap(bitmap, x + (icon_size - bmp_size.GetWidth()) / 2,
                          (size.GetHeight() - bmp_size.GetHeight()) / 2, true);
        }
        x += icon_size;
    }
}

void RowIcons::OnMotion(wxMouseEvent &evt)
{
    const Slot slot = HitTest(evt.GetPosition());
    if (slot != m_hovered)
    {
        m_hovered = slot;
        if (slot == SlotCount)
        {
            UnsetToolTip();
            SetCursor(wxNullCursor);
        }
        else
        {
            if (m_slots[slot].tip.IsEmpty())
                UnsetToolTip();
            else
                SetToolTip(m_slots[slot].tip);
            SetCursor(m_slots[slot].hand ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
        }
    }
    evt.Skip();
}

void RowIcons::OnLeftDown(wxMouseEvent &evt)
{
    const Slot slot = HitTest(evt.GetPosition());
    evt.Skip();
    if (slot == SlotCount || !m_slots[slot].on_click)
        return;
    // The click's action runs after this event, from a copy of the handler: an action that rebuilds
    // the row destroys this window only once wx is done with it, and a window destroyed first drops
    // its pending calls with it
    CallAfter([on_click = m_slots[slot].on_click]() { on_click(); });
}

} // namespace DSKY
