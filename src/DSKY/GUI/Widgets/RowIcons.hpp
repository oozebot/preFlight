///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/bmpbndl.h>
#include <wx/window.h>

#include <array>
#include <functional>

namespace DSKY
{

// The icons that lead a settings row, painted by one window: the pin (Edit Visibility), the lock
// (the value against the system preset) and the undo mark (the value against the saved preset),
// in that order, each with its own tooltip, cursor and click. One window per row instead of one
// per icon, and no private bitmap copy per icon: each slot draws its shared bundle's bitmap.
// The override panel's rows use the lock alone, with the row's accent edge on the left.
class RowIcons : public wxWindow
{
public:
    enum Slot
    {
        Pin,
        Lock,
        Undo,
        SlotCount
    };

    // The slots the row has, each shown from the start; the sidebar hides the pin outside Edit Visibility
    RowIcons(wxWindow *parent, bool with_pin, bool with_lock, bool with_undo);

    void SetIcon(Slot slot, const wxBitmapBundle &icon);
    void SetTip(Slot slot, const wxString &tip);
    void SetHandCursor(Slot slot, bool hand);
    void ShowSlot(Slot slot, bool show);
    bool IsSlotShown(Slot slot) const { return m_slots[slot].shown; }
    void SetOnClick(Slot slot, std::function<void()> on_click) { m_slots[slot].on_click = std::move(on_click); }

    // A coloured strip at the left edge, `width` px wide; invalid colour for none
    void SetAccent(const wxColour &colour, int width);
    // Whether the first shown icon has the margin before it (on by default); off, the first icon is
    // flush with the window's left edge, for a row whose icons have their gaps after them
    void SetLeadingMargin(bool leading);

    // The icon size and spacing follow the em unit
    void Rescale();

    // Clicked, never focused: Tab goes from one field to the next, as with the bitmaps it replaced
    bool AcceptsFocus() const override { return false; }
    bool AcceptsFocusFromKeyboard() const override { return false; }

private:
    struct SlotState
    {
        bool exists{false};
        bool shown{false};
        wxBitmapBundle icon;
        wxString tip;
        bool hand{false};
        std::function<void()> on_click;
    };

    int IconSize() const;
    int Margin() const;
    // The margin before a shown slot; `first` for the first shown one
    int MarginBefore(bool first) const { return first && !m_leading_margin ? 0 : Margin(); }
    int AccentSpace() const;
    // The slot under the point, SlotCount for none
    Slot HitTest(const wxPoint &pt) const;
    void UpdateSize();

    void OnPaint(wxPaintEvent &);
    void OnMotion(wxMouseEvent &evt);
    void OnLeftDown(wxMouseEvent &evt);

    std::array<SlotState, SlotCount> m_slots;
    wxColour m_accent;
    int m_accent_width{0};
    bool m_leading_margin{true};
    Slot m_hovered{SlotCount};
};

} // namespace DSKY
